ORBTrace Development
====================

This is the development status for the ORBTrace parallel TRACE hardware. Its very unlikely you want to be here but, just in case you do, this is built using Clifford Wolfs' icestorm toolchain and currently targets a either a lattice iCE40HX-8K board or the lattice icestick.

It is work in progress, but progress is now quite good (10th Jan 2021). In theory, on a HX1 or HX8 part, it will work with any trace port operating up to 120MHz. I've never seen one faster.  Early work on UP5K suggests it will run there up to around 50MHz, but work on that is incomplete at the moment.

Outstanding development actions;

 * Parse TPIU packets to split out ETM data and buffer it on-chip (will allow for post-mortem dumps)
 * Needs width setting from `orbuculum` down to the FPGA. At the moment it's set in the FPGA verilog `toplevel.v` file and the setting on the orbuculum command line is ignored.
 * Complete UP5K (Icebreaker) support
 
Current testing status;

 * Tested on ICE40HX8K at 1, 2 & 4 bit depths
 * Tested with STM32F427 CPU running at 16MHz & 160MHz.

 * Needs testing at higher speeds and on more CPUs
 * Needs testing on HX1K

To build it perform;

```
cd src
make ICE40HX8K_B_EVN

```
or;

```
cd src
make ICE40HX1K_STICK_EVN

```

..You can also do `make ICEBREAKER` but that will just get you a broken result at the moment.

The system can be built using either an SPI or a UART transport layer. SPI gives better performance (30MHz data rate) but is even more experimental than the UART version. By default UART is built. Change the defines at the head of both the `orbuculum` and `orbtrace` makefiles to use the SPI.

Using UART data is presented at 12Mbaud over the serial port of the HX8 board. You can simply copy the frames of data to somewhere with something like this;

```
cat /dev/ttyUSB1 > myfile
```
Note that all syncs are removed from the data flow, which allows you to convey a lot more _real_ data. By default a sync is inserted every 16 data frames...you can shorten or lengthen that internal through a parameter in `packToSerial.v` if you wish.

Information on how to integrate it with orbuculum (hint, the `-o` option) is in the main README. Basically, you need to tell it that you're using the TRACE hardware and which serial port it appears on, like this;

```
ofiles/orbuculum -o 4 -p /dev/ttyUSB1
```

If you include the `-m 1000` kind of option it'll tell you how full the link is too.  Once it's up and running you can treat it just like any other orbuculum data source. Note that you do need to set parallel trace, as opposed to SWO mode, in your setup. If you're using the gdbtrace.init file provided as part of orbuculum then this works fine for a STM32F427;

```
source ~/Develop/orbuculum/Support/gdbtrace.init

target remote localhost:2331
file ofiles/tracemule.elf
set mem inaccessible-by-default off
set print pretty
monitor reset
load
monitor reset

# This line for parallel trace output
enableSTM32TRACE 4

dwtSamplePC 0
dwtSyncTap 1
dwtPostTap 0
dwtPostInit 1
dwtPostReset 0
dwtCycEna 1
dwtTraceException 1

ITMId 1
ITMGTSFreq 0
ITMTSPrescale 3
ITMTXEna 1
ITMSYNCEna 1
ITMEna 1
ITMTSEna 0
ITMTER 0 0xFFFFFFFF
ITMTPR 0xFFFFFFFF
```

Note that signal integrity is a _huge_ issue. the `enableSTM32TRACE` command takes a second parameter which is drive strength, value 0-3. It defaults to 1. You may have to increase this.  If you have unexpected data corruption rest a finger on the HX8 TRACE input pins. If that fixes the issue, you know where it lies.  This will be addressed on 'production' hardware.

LEDs
====

 - D2: Heartbeat. Solid red when the bitfile is loaded. Flashes while the trace collector is running.
 - D4: Overflow. Illuminated when there are too many data arriving over the trace link to be stored in local RAM prior to transmission (i.e. the off-board transmission link can't keep up).
 - D8: Tx. Flashes while data is being sent over the serial link.
 - D9: Data. A 'sticky' version of D8 which will stay illuminated for about 0.7Secs after any data.