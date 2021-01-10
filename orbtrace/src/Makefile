# Define the line below to get SUMP2 incorporated
#INCLUDE_SUMP2=1  WARNING: Probably bitrotted a bit
USE_UART_FEEDER=1
#USE_SPI_FEEDER=1

RVAL ?= 42 # Replace with $$RANDOM to get a randomised build
           # In general, for a release, set this to a value that
           # results in the timing minima being met, to make life
           # easy for people to rebuild the bitfile

SUPPORTED_HARDWARE=ICE40HX8K_B_EVN ICE40HX1K_STICK_EVN

.PHONY: all clean $(SUPPORTED_HARDWARE)

BUFFER_FLAGS=
ORB_SOURCE_FILES  = toplevel.v ram.v traceIF.v packBuffer.v

ifdef USE_UART_FEEDER
ifdef USE_SPI_FEEDER
$(error Cannot define both feeders at the same time)
endif
ORB_SOURCE_FILES += packToSerial.v uart.v
BUFFER_FLAGS += -DUSE_UART_TRANSPORT
endif

ifdef USE_SPI_FEEDER
ORB_SOURCE_FILES += packToSPI.v spi.v
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

$(SUPPORTED_HARDWARE):%:%.bin
	iceprog -S $<

# bitmap conversion stage:

%.bin: %.asc
	icepack $< $@

# place and route stage:

%.asc:
	nextpnr-ice40 --seed $(RVAL) $(DEVICE) --package $(PACKAGE) --pcf $(subst .asc,.pcf,$@) --json $< --asc $@

ICE40HX1K_STICK_EVN.asc: DEVICE := --hx1k
ICE40HX1K_STICK_EVN.asc: PACKAGE := tq144
ICE40HX1K_STICK_EVN.asc: traceIF_GB.json

ICE40HX8K_B_EVN.asc: DEVICE := --hx8k
ICE40HX8K_B_EVN.asc: PACKAGE := ct256
ICE40HX8K_B_EVN.asc: traceIF_GBIO.json

# verilog synthesis stage:

%.json:
	yosys -f "verilog $(BUFFER_FLAGS)" -p "synth_ice40 -json $@" $^

traceIF_GBIO.json: BUFFER_FLAGS+=
traceIF_GBIO.json: $(SOURCE_FILES)

traceIF_GB.json: BUFFER_FLAGS+=-DNO_GB_IO_AVAILABLE
traceIF_GB.json: $(SOURCE_FILES)


