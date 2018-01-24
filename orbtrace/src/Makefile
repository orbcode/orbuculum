# Define the line below to get SUMP2 incorporated
INCLUDE_SUMP2=1

RVAL ?= 1 #  Replace with $$RANDOM to get a randomised build

SUPPORTED_HARDWARE=ICE40HX8K_B_EVN ICE40HX1K_STICK_EVN

.PHONY: all clean $(SUPPORTED_HARDWARE)

BUFFER_FLAGS=

ifdef INCLUDE_SUMP2
BUFFER_FLAGS+=-DINCLUDE_SUMP2
endif

ORB_SOURCE_FILES=toplevel.v spi.v traceIF.v packSend.v

SUMP_SOURCE_FILES = sump2/core.v sump2/mesa2lb.v sump2/mesa_core.v sump2/mesa_phy.v
SUMP_SOURCE_FILES+= sump2/spi_byte2bit.v sump2/time_stamp.v sump2/mesa_ascii2nibble.v
SUMP_SOURCE_FILES+= sump2/mesa_decode.v sump2/mesa_tx_uart.v sump2/spi_prom.v sump2/top_pll.v
SUMP_SOURCE_FILES+= sump2/mesa2ctrl.v sump2/mesa_byte2ascii.v sump2/mesa_id.v sump2/mesa_uart.v
SUMP_SOURCE_FILES+= sump2/sump2.v

SOURCE_FILES=$(ORB_SOURCE_FILES)

ifdef INCLUDE_SUMP2
SOURCE_FILES+=$(SUMP_SOURCE_FILES)
endif

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

print-%:
	@echo $* is $($*)

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


traceIF_GBIO.blif: BUFFER_FLAGS+=
traceIF_GBIO.blif: $(SOURCE_FILES)

traceIF_GB.blif: BUFFER_FLAGS+=-DNO_GB_IO_AVAILABLE
traceIF_GB.blif: $(SOURCE_FILES)


