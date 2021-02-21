from nmigen                  import *

DAP_CONNECT_DEFAULT      = 1                # Default connect is SWD
DAP_VERSION_STRING       = Cat(C(0x31,8),C(0x2e,8),C(0x30,8),C(0x30,8))
DAP_CAPABILITIES         = 0x01             # SWD Debug only (for now)
DAP_TD_TIMER_FREQ        = 0x3B9ACA00       # 1uS resolution timer
DAP_TB_SIZE              = 1000             # 1000 bytes in trace buffer
DAP_MAX_PACKET_COUNT     = 64               # 64 max packet count
DAP_MAX_PACKET_SIZE      = 64               # 64 Bytes max packet size

DAP_Info                 = 0x00
DAP_HostStatus           = 0x01
DAP_Connect              = 0x02
DAP_Disconnect           = 0x03
DAP_TransferConfigure    = 0x04
DAP_Transfer             = 0x05
DAP_TransferBlock        = 0x06
DAP_TransferAbort        = 0x07
DAP_WriteABORT           = 0x08
DAP_Delay                = 0x09
DAP_ResetTarget          = 0x0a
DAP_SWJ_Pins             = 0x10
DAP_SWJ_Clock            = 0x11
DAP_SWJ_Sequence         = 0x12
DAP_SWD_Configure        = 0x13
DAP_JTAG_Sequence        = 0x14
DAP_JTAG_Configure       = 0x15
DAP_JTAG_IDCODE          = 0x16
DAP_SWO_Transport        = 0x17
DAP_SWO_Mode             = 0x18
DAP_SWO_Baudrate         = 0x19
DAP_SWO_Control          = 0x1a
DAP_SWO_Status           = 0x1b
DAP_SWO_Data             = 0x1c
DAP_SWD_Sequence         = 0x1d
DAP_SWO_ExtendedStatus   = 0x1e
DAP_ExecuteCommands      = 0x7f

DAP_QueueCommands        = 0x7e
DAP_Invalid              = 0xff

class WideRam(Elaboratable):
    def __init__(self):
        self.adr   = Signal(8)
        self.dat_r = Signal(32)
        self.dat_w = Signal(32)
        self.we    = Signal()
        self.mem   = Memory(width=32, depth=8, init=[0xdeadbeef])

    def elaborate(self, platform):
        m = Module()
        m.submodules.rdport = rdport = self.mem.read_port()
        m.submodules.wrport = wrport = self.mem.write_port()
        m.d.comb += [
            rdport.addr.eq(self.adr),
            self.dat_r.eq(rdport.data),
            wrport.addr.eq(self.adr),
            wrport.data.eq(self.dat_w),
            wrport.en.eq(self.we),
        ]
        return m

class CMSIS_DAP(Elaboratable):
    def __init__(self, streamIn, streamOut):
        # External interface
        self.running      = Signal()       # Flag for if target is running
        self.connected    = Signal()       # Flag for if target is connected

        self.streamIn     = streamIn
        self.streamOut    = streamOut
        self.rxBlock      = Signal( 7*8 )  # Longest message we pickup is 6 bytes + command
        self.rxLen        = Signal(3)      # Rxlen to pick up
        self.rxedLen      = Signal(3)      # Rxlen picked up so far
        self.swjbits      = Signal(8)      # Number of bits of SWJ remaining outstanding

        self.txBlock      = Signal( 14*8 ) # Response to be returned
        self.txLen        = Signal(16)     # Length of response to be returned
        self.txedLen      = Signal(16)     # Length of response that has been returned so far
        self.busy         = Signal()       # Indicator that we can't receive USB traffic at the moment

        self.maxSWObytes  = Signal(16)     # Maximum number of receivable SWO bytes from protocol packet
        self.txb          = Signal(4)      # State of various orthogonal state machines

        # Support for JTAG_Sequence
        self.tmsValue     = Signal()       # TMS value while performing JTAG sequence
        self.tdoCapture   = Signal()       # Are we capturing TDO when performing JTAG sequence
        self.tdiData      = Signal(8)      # TDI being sent out
        self.tdoCount     = Signal(7)      # Count of tdi bits being sent (in txBlock)
        self.seqCount     = Signal(8)      # Number of sequences that follow
        self.tckCycles    = Signal(6)      # Number of tckCycles in this sequence
        self.bytebits     = Signal(4)      # Number of bits in this byte that have been transmitted

        # Support for DAP_Transfer
        self.dapIndex     = Signal(8)      # Index of selected JTAG device
        self.transferCount= Signal(8)      # Number of transfers 1..255
        self.ramwpos      = Signal(8)      # Current position in ram for write
        self.lastack      = Signal(3)      # Last ack returned by target
        self.memaddr      = Signal(8)
        
        self.tfrReq       = Signal(8)      # Transfer request from controller
        self.tfrData      = Signal(32)     # Transfer data from controller
        
        # CMSIS-DAP Configuration info
        self.ndev         = Signal(8)      # Number of devices in signal chain
        self.irlength     = Signal(8)      # JTAG IR register length for each device

        self.idleCycles   = Signal(8)      # Number of extra idle cycles after each transfer
        self.waitRetry    = Signal(16)     # Number of transfer retries after WAIT response
        self.matchRetry   = Signal(16)     # Number of retries on reads with Value Match in DAP_Transfer
    # -------------------------------------------------------------------------------------
    def RESP_Invalid(self, m):
        # Simply transmit an 'invalid' packet back
        m.d.usb += [ self.txBlock.word_select(0,8).eq(C(DAP_Invalid,8)), self.txLen.eq(1), self.busy.eq(1) ]        
        m.next = 'RESPOND'
    # -------------------------------------------------------------------------------------
    def RESP_I(self, m,rv=0xff,rv2=0xff):
        # Simply transmit an 'invalid' packet back
        m.d.usb += [ self.txBlock.word_select(0,8).eq(rv), self.txBlock.word_select(1,8).eq(rv2), self.txLen.eq(2), self.busy.eq(1) ] 
        m.next = 'RESPOND'
    # -------------------------------------------------------------------------------------
    def RESP_Info(self, m):
        # Transmit requested information packet back
        m.next = 'RESPOND'
        
        with m.Switch(self.rxBlock.word_select(1,8)):
            # These cases are not implemented in this firmware
            # Get the Vendor ID, Product ID, Serial Number, Target Device Vendor, Target Device Name
            with m.Case(0x01, 0x02, 0x03, 0x05, 0x06): 
                m.d.usb += [ self.txLen.eq(2), self.txBlock[8:16].eq(Cat(C(0,8))) ]

            with m.Case(0x04): # Get the CMSIS-DAP Firmware Version (string)
                m.d.usb += [ self.txLen.eq(6), self.txBlock[8:48].eq(Cat(C(4,8),DAP_VERSION_STRING))]
            with m.Case(0xF0): # Get information about the Capabilities (BYTE) of the Debug Unit
                m.d.usb+=[self.txLen.eq(3), self.txBlock[8:24].eq(Cat(C(1,8),C(DAP_CAPABILITIES,8)))]
            with m.Case(0xF1): # Get the Test Domain Timer parameter information
                m.d.usb+=[self.txLen.eq(6), self.txBlock[8:56].eq(Cat(C(8,8),C(DAP_TD_TIMER_FREQ,32)))]
            with m.Case(0xFD): # Get the SWO Trace Buffer Size (WORD)
                m.d.usb+=[self.txLen.eq(6), self.txBlock[8:48].eq(Cat(C(4,8),C(DAP_TB_SIZE,32)))]
            with m.Case(0xFE): # Get the maximum Packet Count (BYTE)
                m.d.usb+=[self.txLen.eq(6), self.txBlock[8:24].eq(Cat(C(1,8),C(DAP_MAX_PACKET_COUNT,8)))]
            with m.Case(0xFF): # Get the maximum Packet Size (SHORT).
                m.d.usb+=[self.txLen.eq(6), self.txBlock[8:32].eq(Cat(C(2,8),C(DAP_MAX_PACKET_SIZE,16)))]                    
            with m.Default():
                self.RESP_Invalid(m)
    # -------------------------------------------------------------------------------------
    def RESP_HostStatus(self, m):
        m.next = 'RESPOND'

        with m.Switch(self.rxBlock.word_select(1,8)):
            with m.Case(0x00): # Connect LED
                m.d.usb+=self.connected.eq(self.rxBlock.word_select(2,8)==C(1,8))
            with m.Case(0x01): # Running LED
                m.d.usb+=self.running.eq(self.rxBlock.word_select(2,8)==C(1,8))
            with m.Default():
                self.RESP_Invalid(m)
    # -------------------------------------------------------------------------------------    
    def RESP_Connect(self, m):
        self.RESP_Invalid(m)
        
        if (DAP_CAPABILITIES&(1<<0)):
            # SWD mode is permitted
            with m.If ((((self.rxBlock.word_select(1,8))==0) & (DAP_CONNECT_DEFAULT==1)) | ((self.rxBlock.word_select(1,8))==1)):
                m.d.usb += self.txBlock.word_select(0,16).eq(Cat(self.rxBlock.word_select(0,8),C(1,8)))
                m.next = 'RESPOND'
        if (DAP_CAPABILITIES&(1<<1)):
            with m.If ((((self.rxBlock.word_select(1,8))==0) & (DAP_CONNECT_DEFAULT==2)) | ((self.rxBlock.word_select(1,8))==2)):
                m.d.usb += self.txBlock.word_select(0,16).eq(Cat(self.rxBlock.word_select(0,8),C(2,8)))
                m.next = 'RESPOND'
    # -------------------------------------------------------------------------------------
    def RESP_Disconnect(self, m):
        m.next = 'RESPOND'
    # -------------------------------------------------------------------------------------    
    def RESP_WriteABORT(self, m):
        m.next = 'RESPOND'
    # -------------------------------------------------------------------------------------
    def RESP_Delay(self, m):
        m.next = 'RESPOND'
    # -------------------------------------------------------------------------------------
    def RESP_ResetTarget(self, m):
        m.d.usb += [
            self.txBlock.bit_select(8,16).eq(Cat(C(0,8),C(0,8))),
            self.txLen.eq(3)
        ]
        m.next = 'RESPOND'
    # -------------------------------------------------------------------------------------
    def RESP_SWJ_Pins(self, m):
        m.d.usb += [
            self.txBlock.word_select(1,8).eq(C(0x99,8)),
            self.txLen.eq(2)
        ]
        m.next = 'RESPOND'
    # -------------------------------------------------------------------------------------
    def RESP_SWJ_Clock(self, m):
        m.next = 'RESPOND'
    # -------------------------------------------------------------------------------------
    def RESP_SWO_Transport(self, m):
        with m.If(self.rxBlock.word_select(1,8)>2):
            m.d.usb += self.txBlock.word_select(1,8).eq(C(0xff,8))
        m.next = 'RESPOND'
    # -------------------------------------------------------------------------------------
    def RESP_SWO_Mode(self, m):
        with m.If(self.rxBlock.word_select(1,8)>2):
            m.d.usb += self.txBlock.word_select(1,8).eq(C(0xff,8))
        m.next = 'RESPOND'
    # -------------------------------------------------------------------------------------    
    def RESP_SWO_Baudrate(self, m):
        m.d.usb += self.txBlock.eq(self.rxBlock)
        m.d.usb += self.txLen.eq(5)
        m.next = 'RESPOND'
    # -------------------------------------------------------------------------------------    
    def RESP_SWO_Control(self, m):
        with m.If(self.rxBlock.word_select(1,8)>1):
            m.d.usb += self.txBlock.word_select(1,8).eq(C(0xff,8))
        m.next = 'RESPOND'
    # -------------------------------------------------------------------------------------
    def RESP_SWO_Status(self, m):
        m.d.usb += self.txBlock.bit_select(8,40).eq( Cat(C(0,8),C(0x11223344,32) ))
        m.d.usb += self.txLen.eq(6)
        m.next = 'RESPOND'
    # -------------------------------------------------------------------------------------
    def RESP_JTAG_Configure(self, m):
        m.d.usb += self.irlength.eq(self.rxBlock.word_select(2,8))
        m.d.usb += self.ndev.eq(self.rxBlock.word_select(1,8))                               
        m.next = 'RESPOND'
    # -------------------------------------------------------------------------------------
    def RESP_JTAG_IDCODE(self, m):
        m.d.usb += self.txBlock.bit_select(16,32).eq(0x44332211)
        m.d.usb += self.txLen.eq(6)
        m.next = 'RESPOND'
    # -------------------------------------------------------------------------------------    
    def RESP_SWO_ExtendedStatus(self, m):
        with m.If((self.rxBlock.word_select(1,8)&0xf8)!=0):
            self.RESP_Invalid(m)
        with m.Else():
            m.d.usb += self.txBlock.bit_select(8,104).eq( Cat(C(0,8),C(0x11223344,32),C(0x55667788,32),C(0x99aabbcc,32) ))
            m.d.usb += self.txLen.eq(14)
            m.next = 'RESPOND'
    # -------------------------------------------------------------------------------------
    def RESP_TransferConfigure(self, m):
        m.d.usb += [
            self.idleCycles.eq(self.rxBlock.bit_select(8,8)),
            self.waitRetry.eq(self.rxBlock.bit_select(16,16)),
            self.matchRetry.eq(self.rxBlock.bit_select(32,16))
        ]
        m.next = 'RESPOND'
    # -------------------------------------------------------------------------------------
    # -------------------------------------------------------------------------------------
    def RESP_Transfer_Setup(self, m):
        # Triggered at start of a Transfer data sequence
        # We have the command, index and transfer count, need to set up to get the transfers
        m.d.usb += [
            self.dapIndex.eq(self.rxBlock.word_select(1,8)),
            self.transferCount.eq(self.rxBlock.word_select(2,8)),
            self.memaddr.eq(0),
            self.ramwpos.eq(0),
            self.busy.eq(0),
            self.txb.eq(0)
        ]

        # Filter for case someone tries to send us no transfers to perform
        with m.If(self.rxBlock.word_select(2,8)):
            m.next = 'DAP_Transfer_PROCESS'
        with m.Else():
            m.d.usb += [
                self.txBlock.word_select(2,8).eq(self.lastack),
                self.txLen.eq(3)
                ]
            m.next = 'RESPOND'

    def RESP_Transfer_Process(self, m):
        m.d.comb += self.tfrram.dat_w.eq(self.tfrData)
        m.d.usb += [
            self.busy.eq(1),
            self.tfrram.we.eq(0)
            ]

        with m.Switch(self.txb):
            with m.Case(0): # Get transfer request
                m.d.usb += self.busy.eq(0)                
                with m.If(self.streamOut.ready):
                    m.d.usb += self.tfrReq.eq(self.streamOut.payload)
                    with m.If ((~self.streamOut.payload[1]) | (self.streamOut.payload[4]) | (self.streamOut.payload[5] )):
                        # Need to collect the value
                        m.d.usb += self.txb.eq(1)
                    with m.Else():
                        m.d.usb += [
                            self.txb.eq(5),
                            self.busy.eq(1)
                            ]
    
            with m.Case(1,2,3,4): # Collect the 32 bit tfrData to go with the command
                m.d.usb +=self.busy.eq(self.txb==4)
                with m.If(self.streamOut.ready):
                    m.d.usb+=[
                        self.tfrData.word_select(self.txb-1,8).eq(self.streamOut.payload),
                        self.tfrram.adr.eq(self.memaddr),
                        self.txb.eq(self.txb+1)
                        ]
                    
            with m.Case(5): # We have the command and any needed data, action it
                m.d.usb += [                    
                    self.memaddr.eq(self.memaddr+1),
                    self.tfrram.we.eq(1),
                    self.transferCount.eq(self.transferCount-1),
                    self.txBlock.word_select(2,8).eq(self.lastack),
                    self.txb.eq(Mux((self.transferCount==1),6,1)),
                    self.ramwpos.eq(self.memaddr+1),
                    ]

            with m.Case(6,7,8): # Transfer completed, start sending data back
                with m.If(self.streamIn.ready):
                    m.d.usb += [
                        self.memaddr.eq(0),
                        self.tfrram.adr.eq(0),
                        self.streamIn.payload.eq(self.rxBlock.word_select(self.txb-6,8)),
                        self.streamIn.valid.eq(1),
                        self.txb.eq(self.txb+1)
                    ]

            with m.Case(9): # Collect transfer value from RAM store
                m.d.usb += [
                    self.tfrData.eq(self.tfrram.dat_r),
                    self.memaddr.eq(self.memaddr+1),
                    self.tfrram.adr.eq(self.memaddr+1),
                    self.txb.eq(10)
                ]

            with m.Case(10,11,12,13): # Send 32 bit value to usb
                with m.If(self.streamIn.ready):
                    m.d.usb += [
                        self.streamIn.payload.eq(self.tfrData[0:8]),
                        self.tfrData.eq(self.tfrData>>8),
                        self.streamIn.valid.eq(1),
                        self.txb.eq(self.txb+1)
                        ]
                    with m.If(self.txb==13):
                        with m.If(self.memaddr==self.ramwpos):
                            m.d.usb += self.streamIn.last.eq(1)
                            m.next = 'IDLE'
                        with m.Else():
                            m.d.usb += self.txb.eq(9)
                             
    # -------------------------------------------------------------------------------------
    # -------------------------------------------------------------------------------------        
    def RESP_SWO_Data_Setup(self, m):
        # Triggered at the start of a RESP SWO Data Sequence
        # No more data to receive, but a variable amount to send back...
        m.d.comb += self.maxSWObytes.eq(self.rxBlock.bit_select(8,16))

        m.d.usb += [
            self.txLen.eq(Mux(self.maxSWObytes<100,self.maxSWObytes,100)),
            self.txb.eq(0)
            ]
        m.next = 'DAP_SWO_Data_PROCESS'
        
    def RESP_SWO_Data_Process(self, m):
        with m.If(self.streamIn.ready):
            # Triggered for each octet of a data stream
            m.d.usb += self.streamIn.last.eq(0)
        
            with m.Switch(self.txb):
                with m.Case(0): # Response header
                    m.d.usb += self.streamIn.payload.eq(DAP_SWO_Data)
                    m.d.usb += self.txb.eq(1)

                with m.Case(1): # Trace status
                    m.d.usb += self.streamIn.payload.eq(0)
                    m.d.usb += self.txb.eq(2)

                with m.Case(2): # Count H
                    m.d.usb += self.streamIn.payload.eq(self.txLen.word_select(0,8))
                    m.d.usb += self.txb.eq(3)

                with m.Case(3): # Count L
                    m.d.usb += self.streamIn.payload.eq(self.txLen.word_select(1,8))
                    m.d.usb += self.txb.eq(Mux(self.txLen,4,5))
                    with m.If(self.txLen==0):
                        m.d.usb += self.streamIn.last.eq(1)

                with m.Case(4): # Data to be sent
                    m.d.usb += self.streamIn.payload.eq(42)
                    m.d.usb += self.txLen.eq(self.txLen-1)
                    with m.If(self.txLen==1):
                        m.d.usb += [
                            self.streamIn.last.eq(1),
                            self.txb.eq(5)
                        ]

                with m.Case(5):
                    m.next = 'IDLE'

            with m.If(self.txb!=5):
                m.d.usb += self.streamIn.valid.eq(1)
    # -------------------------------------------------------------------------------------
    # -------------------------------------------------------------------------------------
    def RESP_JTAG_Sequence_Setup(self,m):
        # Triggered at the start of a RESP JTAG Sequence
        # There are data to receive at this point, and potentially bytes to transmit

        # Collect how many sequences we'll be processing, then move to get the first one
        m.d.usb += [
            self.seqCount.eq(self.rxBlock.word_select(1,8)),
            self.tdoCount.eq(16),  # TDI Data goes after packet ID and status
            self.txBlock[16:].eq(0), # Blank out any TDI storage
            self.txb.eq(0)
            ]
        m.next = 'DAP_JTAG_Sequence_PROCESS'


    def RESP_JTAG_Sequence_PROCESS(self,m):
        m.d.usb += self.busy.eq(1)

        # If we're done then go home
        with m.If(self.seqCount == 0):
            # Packet response has been built in this state machine, so send it
            m.d.usb += [
                self.txLen.eq((self.tdoCount+7)>>3),
                self.busy.eq(0)
                ]
            m.next = 'RESPOND'
        with m.Else():
            # Otherwise...
            with m.Switch(self.txb):
                # --------------
                with m.Case(0): # Get info for this sequence
                    m.d.usb += self.busy.eq(0)
                    with m.If(self.streamOut.ready):
                        m.d.usb += [
                            self.tckCycles.eq(self.streamOut.payload[0:6]),
                            self.tmsValue.eq(self.streamOut.payload[6]),
                            self.tdoCapture.eq(self.streamOut.payload[7]),
                            self.txb.eq(1)
                        ]
                    # Just in case a previous sequence had tdoCapture on, round up the bits
                    m.d.usb += self.tdoCount.eq((self.tdoCount+7)&0x78)
                        
                # --------------
                with m.Case(1): # Waiting for TDI byte to arrive
                    m.d.usb += self.busy.eq(0)
                    with m.If(self.streamOut.ready):
                        m.d.usb += [
                            self.tdiData.eq(self.streamOut.payload),
                            self.txb.eq(2),
                            self.busy.eq(1),
                            self.bytebits.eq(Mux(((self.tckCycles==0) | (self.tckCycles>7)),8,self.tckCycles))
                            ]
                # --------------
                with m.Case(2): # Clocking out TDI and, perhaps, receiving TDO
                    # We still need to deal with this bit...
                    m.d.usb += self.tckCycles.eq(self.tckCycles-1)
                    m.d.usb += self.bytebits.eq(self.bytebits-1)

                    # If this is the last bit of this byte, best go get another one
                    with m.If(self.bytebits==1):
                        m.d.usb += self.txb.eq(1)

                    # ...and if we've got anything to record
                    with m.If(self.tdoCapture):
                        m.d.usb += [
                            self.tdoCount.eq(self.tdoCount+1),
                            self.txBlock.bit_select(self.tdoCount,1).eq(self.bytebits==1)
                        ]
                        # Add code here for I/O
                    m.d.usb += self.tdiData.eq(self.tdiData>>1)

                    with m.If(self.tckCycles==1):
                        # This sequence is done, so move to next transmission
                        m.d.usb += self.seqCount.eq(self.seqCount-1)
                        m.d.usb += self.txb.eq(0)
                # --------------
                
    # -------------------------------------------------------------------------------------
    # -------------------------------------------------------------------------------------
    def RESP_SWJ_Sequence_Setup(self, m):
        # Triggered at the start of a RESP_SWJ Sequence
        # Need to release the busy so next USB payload arrives
        m.d.usb += [
            self.txLen.eq(2),
            self.busy.eq(0),
            self.swjbits.eq(self.rxBlock.word_select(1,8))
        ]
        m.next = 'DAP_SWJ_Sequence_PROCESS'

    def RESP_SWJ_Sequence_Process(self, m):
        with m.If(self.streamOut.valid):
            # Triggered for each octet of a RESP_SWJ Sequence
            m.d.usb += self.swjbits.eq(self.swjbits-8)

            # The >0 is there to accomodate the need to carry 256 bits (from the spec)
            with m.If((self.swjbits>0) & (self.swjbits<9)):
                # We think it should be the end of the command
                m.d.usb+=self.txBlock.word_select(1,8).eq((~self.streamOut.last)<<6)
                m.next = 'RESPOND'
            
            with m.Elif(self.streamOut.last):
                # End of the command, if we like it or not!
                m.d.usb+=self.txBlock.word_select(1,8).eq(self.swjbits)
                m.next = 'RESPOND'            
    # -------------------------------------------------------------------------------------
    # -------------------------------------------------------------------------------------

    def elaborate(self,platform):
        self.can = platform.request("canary")

        m = Module()
        # Reset everything before we start

        m.d.usb += [  self.streamIn.valid.eq(0) ]

        m.d.comb += [
            self.streamOut.ready.eq(~self.busy),
        ]

        m.submodules.tfrram = self.tfrram = WideRam()
        m.d.usb += self.can.eq(0)

        with m.FSM(domain="usb") as decoder:
            with m.State('IDLE'):
                m.d.usb += [ self.txedLen.eq(0), self.busy.eq(0) ]

                # Only process if this is the start of a packet (i.e. it's not overrrun or similar)
                with m.If((self.streamOut.valid) & (self.streamOut.first==1)):
                    m.next = 'ProtocolError'
                    m.d.usb += self.rxedLen.eq(1)
                    m.d.usb += self.rxBlock.word_select(0,8).eq(self.streamOut.payload)

                    # Default return is packet name followed by 0 (no error)
                    m.d.usb += self.txBlock.word_select(0,16).eq(Cat(self.streamOut.payload,C(0,8)))
                    m.d.usb += self.txLen.eq(2)
                    
                    with m.Switch(self.streamOut.payload):
                        with m.Case(DAP_Disconnect, DAP_ResetTarget, DAP_SWO_Status, DAP_TransferAbort):
                            m.d.usb+=self.rxLen.eq(1)
                            m.next='RxParams'

                        with m.Case(DAP_Info, DAP_Connect, DAP_SWD_Configure, DAP_SWO_Transport, DAP_SWJ_Sequence,
                                    DAP_SWO_Mode, DAP_SWO_Control, DAP_SWO_ExtendedStatus, DAP_JTAG_IDCODE, DAP_JTAG_Sequence):
                            m.d.usb+=self.rxLen.eq(2)
                            with m.If(~self.streamOut.last):
                                m.next = 'RxParams'

                        with m.Case(DAP_HostStatus, DAP_SWO_Data, DAP_JTAG_Configure,DAP_Transfer):
                            m.d.usb+=self.rxLen.eq(3)                            
                            with m.If(~self.streamOut.last):
                                m.next = 'RxParams'

                        with m.Case(DAP_SWO_Baudrate, DAP_SWJ_Clock, DAP_Delay):
                            m.d.usb+=self.rxLen.eq(5)
                            with m.If(~self.streamOut.last):
                                m.next = 'RxParams'

                        with m.Case(DAP_WriteABORT, DAP_TransferConfigure):
                            m.d.usb+=self.rxLen.eq(6)
                            with m.If(~self.streamOut.last):
                                m.next = 'RxParams'

                        with m.Case(DAP_SWJ_Pins):
                            m.d.usb+=self.rxLen.eq(7)
                            with m.If(~self.streamOut.last):
                                m.next = 'RxParams'

                        with m.Case(DAP_SWD_Sequence):
                            with m.If(~self.streamOut.last):
                                m.next = 'DAP_SWD_Sequence_GetCount'

                        with m.Case(DAP_TransferBlock):
                            with m.If(~self.streamOut.last):
                                m.next = 'DAP_TransferBlock'

                        with m.Case(DAP_ExecuteCommands):
                            with m.If(~self.streamOut.last):
                                m.next = 'DAP_ExecuteCommands_GetNum'
                            
                        with m.Case(DAP_QueueCommands):
                            with m.If(~self.streamOut.last):
                                m.next = 'DAP_QueueCommands_GetNum'

                        with m.Default():
                            self.RESP_Invalid(m)
                            
    #########################################################################################
    
            with m.State('RESPOND'):
                with m.If(self.txedLen<self.txLen):
                    with m.If(self.streamIn.ready):
                        m.d.usb += [
                            self.streamIn.payload.eq(self.txBlock.word_select(self.txedLen,8)),                            
                            self.streamIn.valid.eq(1),
                            self.txedLen.eq(self.txedLen+1),
                            self.streamIn.last.eq(self.txedLen+1==self.txLen)
                        ]
                with m.Else():
                    # Everything is transmitted, return to idle condition
                    m.d.usb += [
                        self.streamIn.valid.eq(0),
                        self.streamIn.last.eq(0),
                        self.rxedLen.eq(0),
                        self.busy.eq(0)
                    ]
                    m.next = 'IDLE'
                        
    #########################################################################################
    
            with m.State('RxParams'):
                # ---- Action dispatcher --------------------------------------
                # If we've got everything for this packet then let's process it
                with m.If(self.rxedLen==self.rxLen):
                    with m.Switch(self.rxBlock.word_select(0,8)):

                        # General Commands
                        # ================
                        with m.Case(DAP_Info):
                            self.RESP_Info(m)

                        with m.Case(DAP_HostStatus):
                            self.RESP_HostStatus(m)

                        with m.Case(DAP_Connect):
                            self.RESP_Connect(m)

                        with m.Case(DAP_Disconnect):
                            self.RESP_Disconnect(m)         

                        with m.Case(DAP_WriteABORT):
                            self.RESP_WriteABORT(m)         

                        with m.Case(DAP_Delay):
                            self.RESP_Delay(m)         

                        with m.Case(DAP_ResetTarget):
                            self.RESP_ResetTarget(m)

                        # Common SWD/JTAG Commands
                        # ========================
                        with m.Case(DAP_SWJ_Pins):
                            self.RESP_SWJ_Pins(m)
                            
                        with m.Case(DAP_SWJ_Clock):
                            self.RESP_SWJ_Clock(m)

                        with m.Case(DAP_SWJ_Sequence):
                            self.RESP_SWJ_Sequence_Setup(m)

                        # SWO Commands
                        # ============
                        with m.Case(DAP_SWO_Transport):
                            self.RESP_SWO_Transport(m)

                        with m.Case(DAP_SWO_Mode):
                            self.RESP_SWO_Mode(m)

                        with m.Case(DAP_SWO_Baudrate):
                            self.RESP_SWO_Baudrate(m)

                        with m.Case(DAP_SWO_Control):
                            self.RESP_SWO_Control(m)

                        with m.Case(DAP_SWO_Status):
                            self.RESP_SWO_Status(m)

                        with m.Case(DAP_SWO_ExtendedStatus):
                            self.RESP_SWO_ExtendedStatus(m)

                        with m.Case(DAP_SWO_Data):
                            self.RESP_SWO_Data_Setup(m)

                        # JTAG Commands
                        # =============
                        with m.Case(DAP_JTAG_Sequence):
                            self.RESP_JTAG_Sequence_Setup(m)

                        with m.Case(DAP_JTAG_Configure):
                            self.RESP_JTAG_Configure(m)

                        with m.Case(DAP_JTAG_IDCODE):
                            self.RESP_JTAG_IDCODE(m)

                        # Transfer Commands
                        # =================
                        with m.Case(DAP_TransferConfigure):
                            self.RESP_TransferConfigure(m)

                        with m.Case(DAP_Transfer):
                            self.RESP_Transfer_Setup(m)
                            
                        with m.Default():
                            self.RESP_Invalid(m)

                # Grab next byte in this packet
                with m.Elif(self.streamOut.valid):
                    m.d.usb += [
                        self.rxBlock.word_select(self.rxedLen,8).eq(self.streamOut.payload),
                        self.rxedLen.eq(self.rxedLen+1)
                    ]
                    # Don't grab more data if we've got what we were commanded for
                    with m.If(self.rxedLen+1==self.rxLen):
                        m.d.usb += self.busy.eq(1)

                    # Check to make sure this packet isn't foreshortened
                    with m.If(self.streamOut.last):
                        with m.If(self.rxedLen+1!=self.rxLen):
                            self.RESP_Invalid(m)


    #########################################################################################
    
            with m.State('DAP_SWJ_Sequence_PROCESS'):
                self.RESP_SWJ_Sequence_Process(m)

            with m.State('DAP_SWO_Data_PROCESS'):
                self.RESP_SWO_Data_Process(m)

            with m.State('DAP_JTAG_Sequence_PROCESS'):
                self.RESP_JTAG_Sequence_PROCESS(m)

            with m.State('DAP_Transfer_PROCESS'):
                self.RESP_Transfer_Process(m)
                            
            with m.State('DAP_SWD_Sequence_GetCount'):
                self.RESP_Invalid(m)

            with m.State('DAP_TransferBlock'):
                self.RESP_Invalid(m)
                
            with m.State('DAP_ExecuteCommands_GetNum'):
                self.RESP_Invalid(m)
                
            with m.State('DAP_QueueCommands_GetNum'):
                self.RESP_Invalid(m)
                
            with m.State('Error'):
                self.RESP_Invalid(m)

            with m.State('ProtocolError'):
                self.RESP_Invalid(m)
                             
        return m
    
