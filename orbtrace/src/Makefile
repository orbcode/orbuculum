# Define the line below to get SUMP2 incorporated
#INCLUDE_SUMP2=1  WARNING: Probably bitrotted a bit

# Note that SPI_FEEDER is only for ICE40, ECP5 hardware doesn't support it
USE_UART_FEEDER=1
#USE_SPI_FEEDER=1

RVAL ?= 42 # Replace with $$RANDOM to get a randomised build
           # In general, for a release, set this to a value that
           # results in the timing minima being met, to make life
           # easy for people to rebuild the bitfile

$(echo Build number is $RVAL)

ICE_HARDWARE=ICE40HX8K_B_EVN ICEBREAKER
ECP_HARDWARE=ECPIX_5_85F

SUPPORTED_HARDWARE=$(ICE_HARDWARE) $(ECP_HARDWARE)

.PHONY: all clean $(SUPPORTED_HARDWARE)

BUFFER_FLAGS=
ORB_SOURCE_FILES   = ram.v traceIF.v frameBuffer.v
ICE40_SOURCE_FILES = toplevel_ice40.v
ECP5_SOURCE_FILES  = toplevel_ecp5.v

ifdef USE_UART_FEEDER
ifdef USE_SPI_FEEDER
$(error Cannot define both feeders at the same time)
endif
ORB_SOURCE_FILES += frameToSerial.v uart.v
BUFFER_FLAGS += -DUSE_UART_TRANSPORT
endif

ifdef USE_SPI_FEEDER
ORB_SOURCE_FILES += frameToSPI.v spi.v
BUFFER_FLAGS += -DUSE_SPI_TRANSPORT
endif

SOURCE_FILES=$(ORB_SOURCE_FILES)

ifdef INCLUDE_SUMP2
BUFFER_FLAGS+=-DINCLUDE_SUMP2
SUMP_SOURCE_FILES = sump2/core.v sump2/mesa2lb.v sump2/mesa_core.v sump2/mesa_phy.v
SUMP_SOURCE_FILES+= sump2/spi_byte2bit.v sump2/time_stamp.v sump2/mesa_ascii2nibble.v
SUMP_SOURCE_FILES+= sump2/mesa_decode.v sump2/mesa_tx_uart.v sump2/spi_prom.v sump2/top_pll.v
SUMP_SOURCE_FILES+= sump2/mesa2ctrl.v sump2/mesa_byte2ascii.v sump2/mesa_id.v sump2/mesa_uart.v
SUMP_SOURCE_FILES+= sump2/sump2.v

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
	@-rm -f *.json
	@-rm -f *.asc
	@-rm -f *.bin

# ----------- ICE40 TOOLING ---------------------------------
$(ICE_HARDWARE):%:%.bin
	iceprog $(PROG_FLAGS) $<

# bitmap conversion stage:

ICE%.bin: ICE%.asc
	icepack $< $@

# place and route stage:

ICE%.asc:
	nextpnr-ice40 --seed $(RVAL) $(DEVICE) --package $(PACKAGE) --pcf $(subst .asc,.pcf,$@) --json $< --asc $@
# ----------- ICE40 TOOLING ENDS ---------------------------

# ------------ ECP5 TOOLING --------------------------------

$(ECP_HARDWARE):%:%.bin
	ecpprog $(PROG_FLAGS) $<

# bitmap conversion stage:

ECP%.bin: ECP%.asc
	ecppack --compress $< $@

ECP%.asc:
	nextpnr-ecp5 --seed $(RVAL) $(DEVICE) --package $(PACKAGE) --lpf $(subst .asc,.lpf,$@) --json $< --textcfg $@

# ------------ ECP5 TOOLING ENDS ---------------------------

ICE40HX8K_B_EVN.asc: DEVICE := --hx8k
ICE40HX8K_B_EVN.asc: PACKAGE := ct256
ICE40HX8K_B_EVN.asc: BUFFER_FLAGS += -DICE40HX8K_EVN -DICE40
ICE40HX8K_B_EVN.asc: icetraceIF_GBIO.json
ICE40HX8K_B_EVN.asc: ICE40HX8K_B_EVN.pcf
ICE40HX8K_B_EVN: PROG_FLAGS := -S

ICEBREAKER.asc: DEVICE := --up5k
ICEBREAKER.asc: PACKAGE := sg48
ICEBREAKER.asc: BUFFER_FLAGS += -DICEBREAKER -DICE40
ICEBREAKER.asc: icetraceIF_GB.json
ICEBREAKER.asc: ICEBREAKER.pcf
ICEBREAKER: PROG_FLAGS :=

ECPIX_5_85F.asc: DEVICE := --um5g-85k
ECPIX_5_85F.asc: PACKAGE := CABGA554
ECPIX_5_85F.asc: BUFFER_FLAGS += -DECPIX_5_85F -DECP5
ECPIX_5_85F.asc: ecp5traceIF_GB.json
ECPIX_5_85F.asc: ECPIX_5_85F.lpf
ECPIX_5_85F: PROG_FLAGS := -S

# verilog synthesis stage:

ecp%.json:
ifdef USE_SPI_FEEDER
	$(error Cannot build ECP5 for SPI feeder)
endif
	yosys -f "verilog $(BUFFER_FLAGS)" -p "synth_ecp5 -json $@" $^

ice%.json:
	yosys -f "verilog $(BUFFER_FLAGS)" -p "synth_ice40 -json $@" $^

ecp5traceIF_GB.json: BUFFER_FLAGS+=
ecp5traceIF_GB.json: $(SOURCE_FILES) $(ECP5_SOURCE_FILES)

icetraceIF_GBIO.json: BUFFER_FLAGS+=
icetraceIF_GBIO.json: $(SOURCE_FILES) $(ICE40_SOURCE_FILES)

icetraceIF_GB.json: BUFFER_FLAGS+=-DNO_GB_IO_AVAILABLE
icetraceIF_GB.json: $(SOURCE_FILES) $(ICE40_SOURCE_FILES)

print-%:
	@echo $* is $($*)
