RVAL ?= $$RANDOM
SUPPORTED_HARDWARE=ICE40HX8K_B_EVN ICE40HX1K_STICK_EVN

.PHONY: all clean $(SUPPORTED_HARDWARE)

# supporting rules:

help:
	@echo ""
	@echo "As target please give either:"
	@echo ""
	@echo "    all"
	@echo ""
	@echo "to build all supported platforms,"
	@echo "or one of:"
	@echo ""
	@echo "    $(SUPPORTED_HARDWARE)"
	@echo ""
	@echo "to build and flash this specific platform."
	@echo ""

all:	$(patsubst %,%.bin,$(SUPPORTED_HARDWARE))

clean:
	-rm -f *.blif
	-rm -f *.asc
	-rm -f *.bin

$(SUPPORTED_HARDWARE):%:%.bin
	iceprog -S $<

# bitmap conversion stage:

%.bin: %.asc
	icepack $< $@

# place and route stage:

%.asc:
	arachne-pnr -s $(RVAL) -m 800 -d $(subst hx,,$(subst lp,,$(DEVICE))) -P $(PACKAGE) -p $(subst .asc,.pcf,$@) $< -o $@

ICE40HX1K_STICK_EVN.asc: DEVICE := hx1k
ICE40HX1K_STICK_EVN.asc: PACKAGE := tq144
ICE40HX1K_STICK_EVN.asc: traceIF_GB.blif

ICE40HX8K_B_EVN.asc: DEVICE := hx8k
ICE40HX8K_B_EVN.asc: PACKAGE := ct256
ICE40HX8K_B_EVN.asc: traceIF_GBIO.blif

# verilog synthesis stage:

%.blif:
	yosys -f "verilog $(BUFFER_FLAGS)" -p "synth_ice40 -blif $@" $^

traceIF_GBIO.blif: BUFFER_FLAGS=
traceIF_GBIO.blif: toplevel.v spi.v traceIF.v packSend.v flagSync.v

#uart.v 
traceIF_GB.blif: BUFFER_FLAGS=-DNO_GB_IO_AVAILABLE
traceIF_GB.blif: toplevel.v spi.v traceIF.v packSend.v flagSync.v

# uart.v

