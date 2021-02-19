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

        self.txBlock      = Signal( 6*8 )  # Response to be returned
        self.txLen        = Signal(3)      # Length of response to be returned
        self.txedLen      = Signal(3)      # Length of response that has been returned so far
        self.busy         = Signal()       # Indicator that we can't receive USB traffic at the moment
        
    # -------------------------------------------------------------------------------------
    def RESP_Invalid(self, m):
        # Simply transmit an 'invalid' packet back
        m.d.usb += [ self.txBlock.word_select(0,8).eq(C(DAP_Invalid,8)), self.txLen.eq(1), self.busy.eq(1) ]        
        m.next = 'RESPOND'
    # -------------------------------------------------------------------------------------
    def RESP_I(self, m,rv=0xff,rv2=0xff):
        # Simply transmit an 'invalid' packet back
        #m.d.usb += [ self.txBlock.word_select(0,8).eq(DAP_Invalid), self.txBlock.word_select(1,8).eq(0x77), self.txLen.eq(1) ]
        m.d.usb += [ self.txBlock.word_select(0,8).eq(rv), self.txBlock.word_select(1,8).eq(rv2), self.txLen.eq(2), self.busy.eq(1) ] 
        m.next = 'RESPOND'
    # -------------------------------------------------------------------------------------
    def RESP_Info(self, m):
        # Transmit requested information packet back
        m.d.usb += self.txBlock.word_select(0,8).eq(C(DAP_Info,8))
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
        m.d.usb += [
            self.txBlock.word_select(0,16).eq(Cat(C(DAP_HostStatus,8),C(0,8))),
            self.txLen.eq(2)
            ]
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
        self.RESP_Invalid(m)  # Set an invalid response unless its overriden below
        
        if (DAP_CAPABILITIES&(1<<0)):
            # SWD mode is permitted
            with m.If ((((self.rxBlock.word_select(1,8))==0) & (DAP_CONNECT_DEFAULT==1)) | ((self.rxBlock.word_select(1,8))==1)):
                m.d.usb += [
                    self.txBlock.word_select(0,16).eq(Cat(C(DAP_Connect,8),C(1,8))),
                    self.txLen.eq(2)
                ]
                m.next = 'RESPOND'
        if (DAP_CAPABILITIES&(1<<1)):
            with m.If ((((self.rxBlock.word_select(1,8))==0) & (DAP_CONNECT_DEFAULT==2)) | ((self.rxBlock.word_select(1,8))==2)):
                m.d.usb += [
                    self.txBlock.word_select(0,16).eq(Cat(C(DAP_Connect,8),C(2,8))),
                    self.txLen.eq(2)
                ]
                m.next = 'RESPOND'
    # -------------------------------------------------------------------------------------
    def RESP_Disconnect(self, m):
        m.d.usb += [
            self.txBlock.word_select(0,16).eq(Cat(C(DAP_Disconnect,8),C(0,8))),
            self.txLen.eq(2)
        ]
        m.next = 'RESPOND'
    # -------------------------------------------------------------------------------------    
    def RESP_WriteABORT(self, m):
        m.d.usb += [
            self.txBlock.word_select(0,16).eq(Cat(C(DAP_WriteABORT,8),C(0,8))),
            self.txLen.eq(2)
        ]
        m.next = 'RESPOND'
    # -------------------------------------------------------------------------------------
    def RESP_Delay(self, m):
        m.d.usb += [
            self.txBlock.word_select(0,16).eq(Cat(C(DAP_Delay,8),C(0,8))),
            self.txLen.eq(2)
        ]
        m.next = 'RESPOND'
    # -------------------------------------------------------------------------------------
    def RESP_ResetTarget(self, m):
        m.d.usb += [
            self.txBlock.word_select(0,24).eq(Cat(C(DAP_ResetTarget,8),C(0,8),C(0,8))),
            self.txLen.eq(3)
        ]
        m.next = 'RESPOND'
    # -------------------------------------------------------------------------------------
    def RESP_SWJ_Pins(self, m):
        m.d.usb += [
            self.txBlock.word_select(0,16).eq(Cat(C(DAP_SWJ_Pins,8),C(0x99,8))),
            self.txLen.eq(2)
        ]
        m.next = 'RESPOND'
    # -------------------------------------------------------------------------------------
    def RESP_SWJ_Clock(self, m):
        m.d.usb += [
            self.txBlock.word_select(0,16).eq(Cat(C(DAP_SWJ_Clock,8),C(0x0,8))),
            self.txLen.eq(2)
        ]
        m.next = 'RESPOND'
    # -------------------------------------------------------------------------------------
    # -------------------------------------------------------------------------------------
    def RESP_SWJ_Sequence_Setup(self, m):
        # Triggered at the start of a RESP_SWJ Sequence
        m.d.usb += [
            self.txBlock.word_select(0,8).eq(C(DAP_SWJ_Sequence,8)),
            self.txLen.eq(2),
            self.swjbits.eq(self.rxBlock.word_select(1,8))
        ]
        m.next = 'DAP_SWJ_Sequence_PROCESS'

    def RESP_SWJ_Sequence_Process(self, m):
        with m.If(self.streamOut.valid):
            # Triggered for each octet of a RESP_SWJ Sequence
            m.d.usb += self.swjbits.eq(self.swjbits-8)

            with m.If(self.swjbits<9):
                # We think it should be the end of the command
                m.d.usb+=self.txBlock.word_select(1,8).eq(~self.streamOut.last)
                m.next = 'RESPOND'
            
        with m.If(self.streamOut.last):
            # End of the command, if we like it or not!
            m.d.usb+=self.txBlock.word_select(1,8).eq(self.swjbits!=0)
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
        
        m.d.usb += self.can.eq(0)

        with m.FSM(domain="usb") as decoder:
            with m.State('IDLE'):
                m.d.usb += [ self.txedLen.eq(0), self.busy.eq(0) ]

                # Only process if this is the start of a packet (i.e. it's not overrrun or similar)
                with m.If((self.streamOut.valid) & (self.streamOut.first==1)):
                    m.next = 'ProtocolError'
                    m.d.usb+=self.rxedLen.eq(1)
                    m.d.usb += self.rxBlock.word_select(0,8).eq(self.streamOut.payload)
                    
                    with m.Switch(self.streamOut.payload):
                        with m.Case(DAP_Disconnect, DAP_ResetTarget, DAP_SWO_Status, DAP_TransferAbort):
                            m.d.usb+=self.rxLen.eq(1)
                            m.next='RxParams'

                        with m.Case(DAP_Info, DAP_Connect, DAP_SWD_Configure, DAP_SWO_Transport,
                                    DAP_SWO_Mode, DAP_SWO_Control, DAP_SWO_ExtendedStatus, DAP_JTAG_IDCODE):
                            m.d.usb+=self.rxLen.eq(2)
                            with m.If(~self.streamOut.last):
                                m.next = 'RxParams'

                        with m.Case(DAP_HostStatus, DAP_SWO_Data, DAP_JTAG_Configure):
                            m.d.usb+=self.rxLen.eq(3)                            
                            with m.If(~self.streamOut.last):
                                m.next = 'RxParams'

#                        with m.Case():
#                            m.d.usb+=self.rxLen.eq(4)
#                            with m.If(~self.streamOut.last):
#                                m.next = 'RxParams'
                            
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

                        with m.Case(DAP_SWJ_Sequence):
                            m.d.usb+=self.rxLen.eq(2)
                            with m.If(~self.streamOut.last):
                                m.next = 'RxParams'

                        with m.Case(DAP_SWD_Sequence):
                            with m.If(~self.streamOut.last):
                                m.next = 'DAP_SWD_Sequence_GetCount'

                        with m.Case(DAP_JTAG_Sequence):
                            with m.If(~self.streamOut.last):
                                m.next = 'DAP_JTAG_Sequence_GetCount'
                            
                        with m.Case(DAP_Transfer):
                            with m.If(~self.streamOut.last):
                                m.next = 'DAP_Transfer'
                            
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
                    m.d.usb+=self.busy.eq(1)
                    with m.Switch(self.rxBlock.word_select(0,8)):
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

                        with m.Case(DAP_SWJ_Pins):
                            self.RESP_SWJ_Pins(m)
                            
                        with m.Case(DAP_SWJ_Clock):
                            self.RESP_SWJ_Clock(m)

                        with m.Case(DAP_SWJ_Sequence):
                            self.RESP_SWJ_Sequence_Setup(m)
                            
                        with m.Default():
                            self.RESP_Invalid(m)

                # Grab next byte in this packet
                with m.Elif(self.streamOut.valid):
                    m.d.usb += [
                        self.rxBlock.word_select(self.rxedLen,8).eq(self.streamOut.payload),
                        self.rxedLen.eq(self.rxedLen+1)
                    ]

                # Check to make sure this packet isn't foreshortened
                with m.If(self.streamOut.last):
                    with m.If(self.rxedLen+1!=self.rxLen):
                        self.RESP_Invalid(m)


    #########################################################################################
    
            with m.State('DAP_SWJ_Sequence_PROCESS'):
                self.RESP_SWJ_Sequence_Process(m)
                            
            with m.State('DAP_SWD_Sequence_GetCount'):
                self.RESP_Invalid(m)

            with m.State('DAP_JTAG_Sequence_GetCount'):
                self.RESP_Invalid(m)
                            
            with m.State('DAP_Transfer'):
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
    
