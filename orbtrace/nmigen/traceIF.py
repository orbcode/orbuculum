from nmigen                  import *
from nmigen.hdl.xfrm         import DomainRenamer
from nmigen.lib.fifo         import SyncFIFOBuffered

class TRACE(Elaboratable):
    def __init__(self, tracepins, width, phaseinv, fravail, frame):
        self.tracepins      = tracepins
        self.widthSet       = width
        self.phaseinv       = phaseinv
        self.fravail        = fravail
        self.frame          = frame

    def elaborate(self, platform):
        m = Module()
        
        m.submodules.trace = Instance(
            "traceIF",
            i_traceDina=self.tracepins.dat.i0,
            i_traceDinb=self.tracepins.dat.i1,
            i_traceClkin=self.tracepins.clk,
            i_width=self.widthSet,
            i_rst = 0,
            o_edgeOutput=self.phaseinv,
            o_FrAvail=self.fravail,
            o_Frame=self.frame
            )
        m.d.comb +=  self.tracepins.dat.i_clk.eq(self.tracepins.clk.i)

        return m

class TRACE_TO_USB(Elaboratable):
    def __init__(self, tracepins, usbTraceDataEP, leds_out):
        self.tracepins      = tracepins
        self.leds_out       = leds_out
        self.usbTraceDataEP = usbTraceDataEP

        # Indicators for conditions on the link
        self.rx_ind       = Signal()
        self.tx_ind       = Signal()
        self.inv_ind      = Signal()
        self.overflow_ind = Signal()
        self.phaseinv     = Signal()


    def elaborate(self, platform):
        m = Module()

        m.submodules.afifo = afifo = DomainRenamer("usb")(SyncFIFOBuffered( width=128, depth=512 ))
        self.fravail     = Signal()
        self.fravailtrak = Signal(3)
        self.width       = Signal(2,reset=3)
        self.totalFrames = Signal(32)
        self.lostFrames  = Signal(32)
        self.txFrame     = Signal(128)
        self.ticks       = Signal(22)
        self.sendInt     = Signal(reset=1)

        m.submodules.cortex_trace_stream = TRACE(self.tracepins, self.width, self.inv_ind, self.fravail, afifo.w_data)


        # FIFO IN SIDE
        # ============
        #
        # Move the fravail check along with each clock tick
        m.d.usb  += self.fravailtrak.eq( Cat(self.fravail,self.fravailtrak[0:2]) )
        m.d.comb += afifo.w_en.eq( ((self.fravailtrak==0b100) | (self.fravailtrak==0b011)) )

        m.d.usb  += [ self.rx_ind.eq(0), self.overflow_ind.eq(0) ]

        # Increment total counters when we receive traffic, and set indicators if needed
        with m.If ((self.fravailtrak==0b100) | (self.fravailtrak==0b011)):
            m.d.usb += [
                self.totalFrames.eq(self.totalFrames+1),
                self.rx_ind.eq(1) ]
            with m.If (afifo.w_rdy==0):
                m.d.usb += [
                    self.lostFrames.eq(self.lostFrames+1),
                    self.overflow_ind.eq(1) ]


        # FIFO OUT SIDE
        # =============
        #
        # If its time to send an interval frame then prepare it and snapshot the frame
        m.d.usb += self.ticks.eq(self.ticks-1)
        with m.If(self.ticks==0):
            m.d.usb += self.sendInt.eq(1)

        # See what is to be transmitted
        m.d.usb += [ afifo.r_en.eq(0),
                     self.usbTraceDataEP.stream.last.eq(0),
                     self.usbTraceDataEP.stream.valid.eq(0),
                     self.tx_ind.eq(0) ]

        # Action a 'keepalive' status transmission
        stretchedLevel = Signal(16)
        m.d.comb += stretchedLevel.eq(afifo.r_level)

        with m.If ((self.usbTraceDataEP.stream.ready==1) & (self.sendInt==1)):
            m.d.usb += [
                self.sendInt.eq(0),
                self.usbTraceDataEP.stream.valid.eq(1),
                self.usbTraceDataEP.stream.payload.eq(self.txFrame),
                self.txFrame.eq(Cat( C(0xA6,8),
                                     stretchedLevel[8:16],stretchedLevel[0:8],
                                     C(0,16),
                                     self.leds_out,
                                     Cat(self.lostFrames[8:16],self.lostFrames[0:8]),
                                     Cat(self.totalFrames[24:32],self.totalFrames[16:24],self.totalFrames[8:16],self.totalFrames[0:8]),
                                     C(0x7FFFFFFF,32))),
                self.usbTraceDataEP.stream.last.eq(1)
            ]

        # ...or a real data transmission
        with m.If ((self.usbTraceDataEP.stream.ready==1) & (self.sendInt==0) & (afifo.r_rdy==1)):
            m.d.usb += [
                self.usbTraceDataEP.stream.valid.eq(1),
                self.usbTraceDataEP.stream.payload.eq( Cat( afifo.r_data[120:128], afifo.r_data[112:120], afifo.r_data[104:112], afifo.r_data[ 96:104],
                                                            afifo.r_data[ 88: 96], afifo.r_data[ 80: 88], afifo.r_data[ 72: 80], afifo.r_data[ 56: 64],
                                                            afifo.r_data[ 56: 64], afifo.r_data[ 48: 56], afifo.r_data[ 40: 48], afifo.r_data[ 32: 40],
                                                            afifo.r_data[ 24: 32], afifo.r_data[ 16: 24], afifo.r_data[  8: 16], afifo.r_data[  0:  8] ) ),
                self.tx_ind.eq(1),
                afifo.r_en.eq(1)
            ]

        return m
