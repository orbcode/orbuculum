ORBTrace Development
====================

This is the development status for the ORBTrace parallel TRACE hardware. You're only here 'cos you're brave. This is built using Clifford Wolfs' icestorm toolchain and currently targets a either a lattice iCE40HX-8K board or an Lambda Concept ECPIX-5 board (by default a -85F, but that's a trivial change in the makefile).

This is now mostly complete (24th Jan 2021). In theory, on an ECP5  or ICE40 HX8 part, it will work with any trace port operating up to at least 106MHz. Some stroking of the logic may make it a bit faster but there's no point doing that until everything else is done.   Early work on UP5K suggests it will run there up to around 50MHz, but work on that is incomplete at the moment.

This should all be viewed as experimental. There remains work to be done.

Note that ICE40HX1 support has been removed as it was just too snug to fit in the chip. The last version with (experimental) support for that chip can be found at https://github.com/orbcode/orbuculum/commit/58e7c03581231e0a37549060603aed7831c37533

Outstanding development actions;

 * Parse TPIU packets to split out ETM data and buffer it on-chip (will allow for post-mortem dumps)
 * Complete UP5K (Icebreaker) support
 
Current testing status;

 * Tested on ICE40HX8K at 1, 2 & 4 bit depths
 * Tested against Rising Edge and Falling edge synced flows
 * Tested with STM32F427 CPU running at 16MHz & 160MHz.
 * Tested with NRF5340 running at 32MHz

 * Needs testing at higher speeds and on more CPUs

To build it perform;

```
cd src
make ICE40HX8K_B_EVN

```
or;

```
cd src
make ECPIX_5_85F

```

..You can also do `make ICEBREAKER` but that will just get you a broken result at the moment.

For ICE40 the system can be built using either an SPI or a UART transport layer. SPI is potentially capable of better performance (30MHz line rate). Testing shows that around 26Mbps is realistically acheivable. Both the serial and SPI interfaces are fully tested on ICE40. For ECP5 only UART is supported at the moment since the hardware doesn't support the SPI.

Using UART data is presented at 12Mbaud over the serial port of the HX8 board. You can simply copy the frames of data to somewhere with something like this;

```
cat /dev/ttyUSB1 > myfile
```
Note that all syncs are removed from the data flow, which allows you to convey a lot more _real_ data. By default a sync is inserted every now and again, which keeps the UI sync'ed with the board.

Information on how to integrate it with orbuculum (hint, the `-o` option) is in the main README. Basically, you need to tell it that you're using the TRACE hardware and which serial port it appears on, like this;

```
ofiles/orbuculum -o 4 -p /dev/ttyUSB1
```

The value of the `-o` parameter sets the 'width' (i.e. number of bits) on the trace port hardware. If you include the `-m 100` kind of option it'll tell you how full the link is too. We sneak some other data into those sync packets from the board, so you will also see how many 16 byte frames of data have been received and the board LED status too, something like this;

```
1.5 MBits/sec (  51% full) LEDS: d--h Frames: 1903
```

The leds are `d`ata, `t`ransmit, `O`verflow and `h`eartbeat. Higher verbosity levels will report more link information, but that's mostly for debug and diagnostic purposes.

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

# For NRF53 use 'enableNRF53TRACE 4' instead

dwtSamplePC 1
dwtSyncTap 1
dwtPostTap 1
dwtPostInit 1
dwtPostReset 10
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

Note that signal integrity is a _huge_ issue. the `enableSTM32TRACE` command takes a second parameter which is drive strength, value 0-3. It defaults to 1. You may have to increase this.  If you have unexpected data corruption rest a finger on the TRACE input pins. If that fixes the issue, you know where it lies.  This will be addressed on 'production' hardware.

LEDs
====

 - ICE40:D2, ECPIX-5:LD5-Green: Heartbeat. Solid red when the bitfile is loaded. Flashes while the trace collector is running. (`h` in the `orbuculum` monitor)
 - ICE40:D4, ECPIX-5:LD6-Red: Overflow. Illuminated when there are too many data arriving over the trace link to be stored in local RAM prior to transmission (i.e. the off-board transmission link can't keep up), `O` in the monitor.
 - ICE40:D5, ECPIX-5:LD7-Blue: FallingEdge. (Diagnostic). Used to indicate that the link is synced to the falling edge rather than the rising one. Completely unimportant to end users, but very important to me...I will ask you for the state of this LED if you need support!
 - ICE40:D8, ECPIX-5:LD6-Green: Tx. Flashes while data is being sent over the serial link, `t` in the monitor.
 - ICE40:D9, ECPIX-5:LD8-Blue: Data. A 'sticky' version of D8 which will stay illuminated for about 0.7Secs after any data, `d` in the monitor.
 
