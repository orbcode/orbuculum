from nmigen                  import *

DAP_VERSION_STRING       = Cat(C(0x31,8),C(0x2e,8),C(0x30,8),C(0x30,8))
DAP_CAPABILITIES         = C(0x01,8)           # SWD Debug only (for now)
DAP_TD_TIMER_FREQ        = C(0x3B9ACA00,32)    # 1uS resolution timer
DAP_TB_SIZE              = C(1000,32)          # 1000 bytes in trace buffer
DAP_MAX_PACKET_COUNT     = C(64,8)             # 64 max packet count
DAP_MAX_PACKET_SIZE      = C(64,16)            # 64 Bytes max packet size

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
    def __init__(self, usbCMSISDAPIn, usbCMSISDAPOut):
        self.usbIn        = usbCMSISDAPIn
        self.usbOut       = usbCMSISDAPOut

        self.rxBlock      = Signal( 7*8 )  # Longest message we pickup is 6 bytes + command
        self.rxLen        = Signal(3)      # Rxlen to pick up
        self.rxedLen      = Signal(3)      # Rxlen picked up so far

        self.txBlock      = Signal( 6*8 )  # Response to be returned
        self.txLen        = Signal(3)      # Length of response to be returned
        self.txedLen      = Signal(3)      # Length of response that has been returned so far
        self.busy         = Signal()       # Indicator that we can't receive USB traffic at the moment
        
    # -------------------------------------------------------------------------------------
    def RESP_Invalid(self, m):
        # Simply transmit an 'invalid' packet back
        m.d.usb += [ self.txBlock.word_select(0,8).eq(DAP_Invalid), self.txLen.eq(1), self.busy.eq(1) ]        
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
        m.d.usb += self.txBlock.word_select(0,8).eq(DAP_Info)
        
        with m.Switch(self.rxBlock.word_select(1,8)):
            # These cases are not implemented in this firmware
            # Get the Vendor ID, Product ID, Serial Number, Target Device Vendor, Target Device Name
            with m.Case(0x01, 0x02, 0x03, 0x05, 0x06): 
                m.d.usb += [ self.txLen.eq(2), self.txBlock[8:16].eq(Cat(C(0,8))) ]

            with m.Case(0x04): # Get the CMSIS-DAP Firmware Version (string)
                m.d.usb += [ self.txLen.eq(6), self.txBlock[8:48].eq(Cat(C(4,8),DAP_VERSION_STRING))]
            with m.Case(0xF0): # Get information about the Capabilities (BYTE) of the Debug Unit
                m.d.usb+=[self.txLen.eq(3), self.txBlock[8:24].eq(Cat(C(1,8),DAP_CAPABILITIES))]
            with m.Case(0xF1): # Get the Test Domain Timer parameter information
                m.d.usb+=[self.txLen.eq(6), self.txBlock[8:56].eq(Cat(C(8,8),DAP_TD_TIMER_FREQ))]
            with m.Case(0xFD): # Get the SWO Trace Buffer Size (WORD)
                m.d.usb+=[self.txLen.eq(6), self.txBlock[8:48].eq(Cat(C(4,8),DAP_TB_SIZE))]
            with m.Case(0xFE): # Get the maximum Packet Count (BYTE)
                m.d.usb+=[self.txLen.eq(6), self.txBlock[8:24].eq(Cat(C(1,8),DAP_MAX_PACKET_COUNT))]
            with m.Case(0xFF): # Get the maximum Packet Size (SHORT).
                m.d.usb+=[self.txLen.eq(6), self.txBlock[8:32].eq(Cat(C(2,8),DAP_MAX_PACKET_SIZE))]                    
            with m.Default():
                self.RESP_Invalid(m)

        m.next = 'RESPOND'
    # -------------------------------------------------------------------------------------

    def elaborate(self,platform):
        self.can = platform.request("canary")

        m = Module()
        # Reset everything before we start

        m.d.usb += [  self.usbIn.stream.valid.eq(0) ]

        m.d.comb += [
            self.usbOut.stream.ready.eq(~self.busy),
        ]
        
        m.d.usb += self.can.eq(0)
        m.d.usb += self.rxBlock.bit_select(self.rxedLen<<3,8).eq(self.usbOut.stream.payload)

        with m.FSM(domain="usb") as decoder:
            with m.State('IDLE'):
                m.d.usb += [ self.txedLen.eq(0), self.busy.eq(0) ]

                # Only process if this is the start of a packet (i.e. it's not overrrun or similar)
                with m.If((self.usbOut.stream.valid) & (self.usbOut.stream.first==1)):
                    m.next = 'ProtocolError'
                    m.d.usb+=self.rxedLen.eq(1)
                    
                    with m.Switch(self.rxBlock.word_select(0,8)):
                        with m.Case(DAP_Disconnect, DAP_ResetTarget, DAP_SWO_Status, DAP_TransferAbort):
                            m.d.usb+=self.rxLen.eq(1)
                            m.next='RxParams'

                        with m.Case(DAP_Info, DAP_Connect, DAP_SWJ_Clock, DAP_SWD_Configure, DAP_SWO_Transport,
                                    DAP_SWO_Mode, DAP_SWO_Control, DAP_SWO_ExtendedStatus, DAP_JTAG_IDCODE):
                            m.d.usb+=self.rxLen.eq(2)
                            with m.If(~self.usbOut.stream.last):
                                m.next = 'RxParams'

                        with m.Case(DAP_HostStatus, DAP_SWO_Data, DAP_JTAG_Configure):
                            m.d.usb+=self.rxLen.eq(3)                            
                            with m.If(~self.usbOut.stream.last):
                                m.next = 'RxParams'

                        with m.Case(DAP_Delay):
                            m.d.usb+=self.rxLen.eq(4)
                            with m.If(~self.usbOut.stream.last):
                                m.next = 'RxParams'
                            
                        with m.Case(DAP_SWO_Baudrate):
                            m.d.usb+=self.rxLen.eq(5)
                            with m.If(~self.usbOut.stream.last):
                                m.next = 'RxParams'

                        with m.Case(DAP_WriteABORT, DAP_TransferConfigure):
                            m.d.usb+=self.rxLen.eq(6)
                            with m.If(~self.usbOut.stream.last):
                                m.next = 'RxParams'

                        with m.Case(DAP_SWJ_Pins):
                            m.d.usb+=self.rxLen.eq(7)
                            with m.If(~self.usbOut.stream.last):
                                m.next = 'RxParams'

                        with m.Case(DAP_SWJ_Sequence):
                            with m.If(~self.usbOut.stream.last):
                                m.next = 'DAP_SWJ_Sequence_GetLen'

                        with m.Case(DAP_SWD_Sequence):
                            with m.If(~self.usbOut.stream.last):
                                m.next = 'DAP_SWD_Sequence_GetCount'

                        with m.Case(DAP_JTAG_Sequence):
                            with m.If(~self.usbOut.stream.last):
                                m.next = 'DAP_JTAG_Sequence_GetCount'
                            
                        with m.Case(DAP_Transfer):
                            with m.If(~self.usbOut.stream.last):
                                m.next = 'DAP_Transfer'
                            
                        with m.Case(DAP_TransferBlock):
                            with m.If(~self.usbOut.stream.last):
                                m.next = 'DAP_TransferBlock'

                        with m.Case(DAP_ExecuteCommands):
                            with m.If(~self.usbOut.stream.last):
                                m.next = 'DAP_ExecuteCommands_GetNum'
                            
                        with m.Case(DAP_QueueCommands):
                            with m.If(~self.usbOut.stream.last):
                                m.next = 'DAP_QueueCommands_GetNum'

                        with m.Default():
                            self.RESP_Invalid(m)
                            
    #########################################################################################
    
            with m.State('RESPOND'):
                with m.If(self.txedLen<self.txLen):
                    with m.If(self.usbIn.stream.ready):
                        m.d.usb += [
                            self.usbIn.stream.payload.eq(self.txBlock.word_select(self.txedLen,8)),                            
                            self.usbIn.stream.valid.eq(1),
                            self.txedLen.eq(self.txedLen+1),
                            self.usbIn.stream.last.eq(self.txedLen+1==self.txLen)
                        ]
                with m.Else():
                    # Everything is transmitted, return to idle condition
                    m.d.usb += [
                        self.usbIn.stream.valid.eq(0),
                        self.usbIn.stream.last.eq(0),
                        self.rxedLen.eq(0),
                        self.busy.eq(0)
                    ]
                    m.next = 'IDLE'
                        
    #########################################################################################
    
            with m.State('RxParams'):
                # If we've got everything for this packet then let's process it
                with m.If(self.rxedLen==self.rxLen):
                    m.d.usb+=self.busy.eq(1)
                    with m.Switch(self.rxBlock.word_select(0,8)):
                        with m.Case(DAP_Info):
                            self.RESP_Info(m)

                        with m.Default():
                            self.RESP_Invalid(m)

                # Grab next byte in this packet
                with m.Elif(self.usbOut.stream.valid):
                    m.d.usb += self.rxedLen.eq(self.rxedLen+1)

                    # Check to make sure this packet isn't foreshortened
                    with m.If(self.usbOut.stream.last):
                        with m.If(self.rxedLen+1!=self.rxLen):
                            self.RESP_Invalid(m)


    #########################################################################################
    
            with m.State('DAP_SWJ_Sequence_GetLen'):
                self.RESP_Invalid(m)
                            
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
    
