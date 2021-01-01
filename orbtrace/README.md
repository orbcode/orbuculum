ORBTrace Development
====================

This is the development status for the ORBTrace parallel TRACE hardware. Its very unlikely you want to be here but, just in case you do, this is built using Clifford Wolfs' icestorm toolchain and currently targets a either a lattice iCE40HX-8K board or the lattice icestick.

It is work in progress, but progress is now quite good (1st Jan 2021). In theory it will work with any trace port operating up to 120MHz. I've never seen one faster.

Outstanding development actions;

 * Move back to SPI interface away from UART one (will allow 30MHz data transfer as opposed to 12MHz)
 * Parse TPIU packets to split out ETM data and buffer it on-chip (will allow for post-mortem dumps)
 
Current testing status;

 * Tested on ICE40HX8K at 1, 2 & 4 bit depths
 * Tested with STM32F427 CPU running at 16MHz

 * Needs testing at higher speeds
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

Information on how to integrate it with orbuculum (hint, the `-o` option) is in the main README. Basically, you need to tell it that you're using the TRACE hardware and which serial port it appears on, like this;

```
ofiles/orbuculum -o 4 -p /dev/ttyUSB1
```

Once it's up and running you can treat it just like any other orbuculum data source. Note that you do need to set parallel trace, as opposed to SWO mode, in your setup. If you're using the gdbtrace.init file provided as part of orbuculum then this works fine for a STM32F427;

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
