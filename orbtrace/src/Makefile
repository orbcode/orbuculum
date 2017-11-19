
SUPPORTED_HARDWARE:=ICE40HX8K_B_EVN ICE40HX1K_STICK_EVN
.PHONY: help $(SUPPORTED_HARDWARE) build flash clean

help:
	@echo "please specify hardware platform as make target."
	@echo "supported are:"
	@echo -e "\t$(SUPPORTED_HARDWARE)"
	@exit 1

ICE40HX8K_B_EVN: BOARD = ice40hx8k_b_evn
ICE40HX8K_B_EVN: DEVICE = hx8k
ICE40HX8K_B_EVN: PACKAGE = ct256
ICE40HX8K_B_EVN: build_traceIF_GBIO flash

ICE40HX1K_STICK_EVN: BOARD = ice40hx1k_stick_evn
ICE40HX1K_STICK_EVN: DEVICE = hx1k
ICE40HX1K_STICK_EVN: PACKAGE = tq144
ICE40HX1K_STICK_EVN: build_traceIF_GB flash

%.blif:
	yosys -f "verilog $(BUFFER_FLAGS)" -p "synth_ice40 -blif $@" $^

traceIF_GBIO.blif: BUFFER_FLAGS=
traceIF_GBIO.blif: toplevel.v uart.v traceIF.v packSend.v

traceIF_GB.blif: BUFFER_FLAGS=-DNO_GB_IO_AVAILABLE
traceIF_GB.blif: toplevel.v uart.v traceIF.v packSend.v

build_traceIF_%: traceIF_%.blif
	arachne-pnr -m 800 -d $(subst hx,,$(subst lp,,$(DEVICE))) -P $(PACKAGE) -p toplevel_$(BOARD).pcf $^ -o traceIF_$(BOARD).txt
	icepack traceIF_$(BOARD).txt traceIF_$(BOARD).bin

flash:
	iceprog -S traceIF_$(BOARD).bin

clean:
	-rm -f *.blif
	-rm -f *.txt
	-rm -f *.bin
