from nmigen                  import *
from nmigen.hdl.xfrm         import DomainRenamer
from nmigen.lib.fifo         import SyncFIFOBuffered

class DBGIF(Elaboratable):
    def __init__(self, dbgpins):
        self.dbgpins      = dbgpins;
        self.divisor      = Signal(11,reset=32);
        self.countdown    = Signal(23);
        self.turnaround   = Signal(4, reset=1);
        self.addr32       = Signal(2);
        self.rnw          = Signal();
        self.apndp        = Signal();
        self.dwrite       = Signal(32);
        self.dread        = Signal(32);
        self.perr         = Signal();
        self.go           = Signal();
        self.pins_write   = Signal();
        self.done         = Signal();
        self.ack          = Signal(3);
        
    def elaborate(self, platform):
        
        m = Module()
        swin = Signal();
        swout = Signal();

        m.submodules.dbgif = Instance(
            "dbgIF",
	    i_rst = ResetSignal("sync"),
            i_clk = ClockSignal("sync"),

            # Gross control - mode, power etc
            i_mode      = 1,
            i_tgt_reset = 0,
            i_vsen      = 0,
            i_vdrive    = 1,

            # Downwards interface to the pins
            i_swdi = self.dbgpins.tms_swdio.i,
            o_tms_swdo = self.dbgpins.tms_swdio.o,
            o_swwr = self.dbgpins.swdwr,
            o_tck_swclk = self.dbgpins.tck_swclk,
            o_tdi = self.dbgpins.tdi,
            i_tdo_swo = self.dbgpins.tdo_swo,
            i_tgt_reset_state = self.dbgpins.nreset_sense,
            
            o_tgt_reset_pin = self.dbgpins.reseten,
            o_nvsen_pin = self.dbgpins.nvsen,
            o_nvdrive_pin = self.dbgpins.nvdriveen,

            # Configuration
            i_clkDiv = self.divisor,
            i_countdown = self.countdown,
            i_turnaround = self.turnaround,

            # Upwards interface to command controller
	    i_addr32 = self.addr32, 
            i_rnw = self.rnw, 
            i_apndp = self.apndp,
            i_dwrite = self.dwrite,
            o_ack = self.ack,
            o_dread = self.dread,
            o_perr = self.perr,

            i_command = self.command,
            i_go = self.go
            )

        m.d.comb += self.dbgpins.tms_swdio.oe.eq(self.dbgpins.swdwr)
        return m
