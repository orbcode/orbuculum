[![Build Linux binaries](https://github.com/orbcode/orbuculum/actions/workflows/build-linux.yml/badge.svg)](https://github.com/orbcode/orbuculum/actions/workflows/build-linux.yml)

[![Build Windows binaries](https://github.com/orbcode/orbuculum/actions/workflows/build-windows.yml/badge.svg)](https://github.com/orbcode/orbuculum/actions/workflows/build-windows.yml)

[![Discord](https://img.shields.io/discord/613131135903596547?logo=discord)](https://discord.gg/P7FYThy)

![Screenshot](https://raw.githubusercontent.com/orbcode/orbuculum/main/Docs/title.png)

This is the development branch for V2.0.0 which includes parallel trace support as well as all the SWO goodness that Orbuculum has always offered. It is pretty close to being labelled as 2.0.0 with only a few minor issues outstanding.

ORBTrace (the FPGA trace interface) has now been moved into its own separate repository as it's grown considerably and really needs its own identity. History for orbtrace until the split point is maintained here for provenance purposes, but new work is now done over in the new location.

The CHANGES file now tells you what's been done recently.

Orbuculum now has an active Discord channel at https://discord.gg/P7FYThy . Thats the place to go if you need interactive help.

An Orbuculum is a Crystal Ball, used for seeing things that would
 be otherwise invisible. A  nodding reference to (the) BlackMagic
 (debug probe), BMP.

You can find information about using this suite at [Orbcode](https://www.orbcode.org), especially the 'Data Feed' section.

** ORBTrace Mini is now available for debug and realtime tracing. Go to [Orbcode](https://www.orbcode.org) for more info. **

For the current development status you will need to use the `Devel` branch. Fixes are made on main and Devel.

The code is in daily use now and small issues are patched as they are found. The software runs on Linux, OSX and Windows and the whole suite is working OK on most workloads. Any bugs found now will be treated as high priority issues. Functional enhancements will also be folded in as time permits. Currently progress is reasonably rapid, and patches are always welcome.

What is it?
===========

Orbuculum is a set of tools for decoding and presenting output flows from
the Debug pins of a CORTEX-M CPU. Originally it only used the SWO pin but it now also
supports hardware for parallel tracing through ORBtrace. Numerous types of data can be output through these
pins, from multiple channels of text messages through to Program Counter samples. Processing
these data gives you a huge amount of insight into what is really going on inside
your CPU. The tools are all mix-and-match according to what you are trying to do. The current set is;

* orbuculum: The main program which interfaces to the trace probe and then issues a network
interface to which an arbitary number of clients can connect, by default on TCP/3443. This is
used by a base interface to the target by other programmes in the suite. Generally you configure
this for the TRACE tool you're using and then you can just leave it running and it'll grab
data from the target and make it available to clients whenever it can. Note that some
debug probes can now create an orbuculum-compatible interface on TCP/3443, and then you can
connect the rest of the suite to that directly, without using the orbuculum mux itself.

* orbfifo: The fifo pump: Turns a trace feed into a set of fifos (or permanent files).

* orbcat: A simple cat utility for ITM channel data.

* orbdump: A utility for dumping raw SWO data to a file for post-processing.

* orbmortem: A post mortem analysis tool (needs parallel trace data).

* orbtop: A top utility to see what's actually going on with your target. It can also
provide dot and gnuplot source data for perty graphics.

* orbstat: An analysis/statistics utility which can produce KCacheGrind input files. KCacheGrind
is a very powerful code performance analysis tool.

* orbtrace: The fpga configuration controller.

* orbzmq: ZeroMQ server.

A few simple use cases are documented in the last section of this
document, as are example outputs of using orbtop to report on the
activity of BMP while emitting SWO packets.

The data flowing from the SWO pin can be encoded
either using NRZ (UART) or RZ (Manchester) formats.  The pin is a
dedicated one that would be used for TDO when the debug interface is
in JTAG mode. We've demonstrated
ITM feeds of around 4.3MBytes/sec on a STM32F427 via SWO with Manchester
encoding running at 48Mbps.

The data flowing from the TRACE pins is clocked using a separate TRACECLK pin. There can
be 1-4 TRACE pins which obviously give you much higher bandwidth than the single SWO. We've demonstrated
ITM feeds of around 12.5MBytes/sec on a STM32F427 via 4 bit parallel trace.

Whatever it's source, orbuculum takes this data flow and makes it accessible to tools on the host
PC. At its core it takes the data from the source, decodes it and presents it on a network
interface. The Orbuculum suite tools don't care if the data
originates from a RZ or NRZ port, SWO or TRACE, or at what speed....that's all the job
of the interface.

At the present time Orbuculum supports eleven devices for collecting trace
from the target;

* the Black Magic Debug Probe (BMP)
* the SEGGER JLink
* generic USB TTL Serial Interfaces
* FTDI High speed serial interfaces
* OpenOCD (Add a line like `tpiu config internal :3443 uart off 32000000` to your openocd config to use it.)
* PyOCD (Add options like `enable_swv: True`, `swv_system_clock: 32000000` to your `pyocd.yml` to use it.)
* The ice40-HX8K Breakout Board for parallel trace
* The ECPIX-5 ECP5 Breakout Board for parallel trace
* Anything capable of saving the raw SWO data to a file
* Anything capable of offering SWO on a TCP port
* ORBTrace Mini

gdb setup files for each device type can be found in the `Support` directory. You'll find
example get-you-going applications in the [Orbmule](https://github.com/orbcode/orbmule) repository including
`gdbinit` scripts for OpenOCD, pyOCD and Blackmagic Probe Hosted. There are walkthroughs for lots of examples
of the use of the orbuculum suite at (Orbcode)[https://orbcode.org].

When using SWO Orbuculum can use, or bypass, the TPIU. The TPIU adds overhead
to the datastream, but provides better synchronisation if there is corruption
on the link. To include the TPIU in decode stack, provide the -t
option on the command line. If you don't provide it, and the ITM decoder sees
TPIU syncs in the datastream, it will complain and barf out. This is deliberate
after I spent two days trying to find an obscure bug 'cos I'd left the `-t` option off. You can have multiple
channels to the `-t` option, which is useful when you've got debug data in one stream and
trace data in another.

Beware that in parallel trace the TPIU is mandatory, so therefore so is the -t option.

TPIU framing can be stripped either
by individual applications or the `orbuculum` mux. When its stripped by the mux the data are made available on
consecutive TCP/IP ports...so `-t 1,2` would put stream 1 data out over TCP port 3443 and stream 2 over 3444, by default.

When in NRZ (UART) mode the SWO data rate that comes out of the chip _must_
match the rate that the debugger expects. On the BMP speeds of
2.25Mbps are normal, TTL Serial devices tend to run a little slower
but 921600 baud is normally acheivable. On BMP the baudrate is set via
the gdb session with the 'monitor traceswo xxxx' command. For a TTL
Serial device its set by the Orbuculum command line.  Segger devices
can normally work faster, but no experimentation has been done to
find their max limits, which are probably it's dependent on the specific JLink
you are using. ORBTrace Mini can operate with Manchester encoded SWO at up
to 48Mbps, which means there's no speed matching needed to use it, and it should
continue to work correctly even if the target clock speed changes (e.g. when it goes
into a low power mode). This is a good thing.

Configuring the Target
======================

Generally speaking, you will need to configure the target device to output
SWD or parallel data. You can either do that through program code, or through magic
incantations in gdb. The gdb approach is more flexible and the program code
version is grandfathered. It's in the support directory if you want it...but I would advice
you take the gdb script and turn it into code manually, that is maintained and the code
in the support directory will eventually rust...only use it for guidance.

In the support directory you will find a script `gdbtrace.init` which contains a
set of setup macros for the SWO functionality. Full details of how to set up these
various registers are available from [Arm](https://static.docs.arm.com/ddi0403/e/DDI0403E_B_armv7m_arm.pdf) and
you've got various options for the type of output generated, its frequency and it's content.

Using these macros means you do not need to change your program code to be able to use
facilities like orbtop. Obviously, if you want textual trace output, you've got to create
that in the program!

Information about the contents of this file can be found by importing it into your
gdb session with `source gdbtrace.init` and then typing `help orbuculum`. Help on the
parameters for each macro are available via the help system too (e.g. `help enableSTM32SWO`).

In general, you will configure orbuculum via your local `.gdbinit` file. Several example files are
also in the Support directory. There you will find a `gdbtrace.init` file for 'regular' gcc
use, and a `gdbtrace_withwith.init` file for use with recent versions of gdb that support
the `with` syntax. The functionality of both is identical, but the `with` syntax allows it
to be used more easily with non-C languages like Rust...unfortunately that syntax isn't
supported on older versions of gdb.

Anyway, generically, a configuration looks like this;

    source Support/gdbtrace.init            <---- Source the trace specific stuff
    target extended-remote /dev/ttyACM0     <-
    monitor swdp_scan                       <-
    file ofiles/firmware.elf                <-
    attach 1                                <---- Connect to the target
    set mem inaccessible-by-default off     <-
    set print pretty                        <-
    load                                    <---- Load the program

    start                                   <---- and get to main

    # ---------- Using Stm32F1 as debuggee---------------------------
    enableSTM32SWO                          <*--- turn on SWO output pin on CPU
    # ----------ALTERNATIVELY, for Stm32F4 as debuggee----------------
    enableSTM32SWO 4                        <*--- turn on SWO output pin on CPU
    # ----------END OF ALTERNATIVE-----------------------------------

    # ---------- EITHER, IF USING A BLUEPILL-------------------------
    monitor traceswo 2250000                <*--- wakeup tracing on the probe
    prepareSWO SystemCoreClock 2250000 1 0  <*--- Setup SWO timing (Bluepill case)

    # ----------ALTERNATIVELY, FOR GENUINE BMP-----------------------
    monitor traceswo                        <*--- Enable BMP traceswo output
    prepareSWO ConfigCoreClock 200000 0 1   <*--- Setup SWO timing (BMP case)
    # ----------END OF ALTERNATIVE-----------------------------------

    dwtSamplePC 1                           <-
    dwtSyncTap 3                            <-
    dwtPostTap 1                            <-
    dwtPostInit 1                           <-
    dwtPostReset 15                         <-
    dwtCycEna 1                             <---- Configure Data Watch/Trace

    ITMId 1                                 <-
    ITMGTSFreq 3                            <-
    ITMTSPrescale 3                         <-
    ITMTXEna 1                              <-
    ITMSYNCEna 1                            <-
    ITMEna 1                                <---- Enable Instruction Trace Macrocell

    ITMTER 0 0xFFFFFFFF                     <---- Enable Trace Ports
    ITMTPR 0xFFFFFFFF                       <---- Make them accessible to user level code

Alternatively, if you're using parallel trace via the ice40 remove the lines marked <*- above and
replace them with the following;

    enableSTM32TRACE                         <---- Switch on parallel trace on the STM32
    prepareTRACE 4                           <---- Set up the TPIU for 4 bit output (or 2 or 1)

...be careful to set the trace width to be the same as what you've configured on the FPGA. While
we're here it's worth mentioning the `startETM` command too, that outputs tracing data. That is
needed for `orbmortem`.

Building on Linux
=================

Dependencies
------------
* libusb-1.0
* libczmq-dev
* ncurses

Note that `objdump`  version at least 2.33.1 is also required. By default the suite will run `arm-none-eabi-objdump` but another binary or pathname can be
subsituted via the OBJDUMP environment variable.

Build
-----

The command line to build the Orbuculum tool suite is:

>make

You may need to change the paths to your libusb files, depending on how well your build environment is set up.

Permissions and Access
----------------------
A udev rules files is included in ```Support/60-orbcode.rules```
Install this to ```/etc/udev/rules.d``` to grant access to orbcode hardware, if required.

Building on OSX
===============

Recipie instructions courtesy of FrankTheTank;

* `brew install libusb`
* `brew install zmq`

and finally;

```
export LDFLAGS="-L/usr/local/opt/binutils/lib"
export CPPFLAGS="-I/usr/local/opt/binutils/include"
```

You can also see notes under [Issue #63](https://github.com/orbcode/orbuculum/issues/63) from Gasman2014 about building on a M1
mac. You need to watch out for Homebrew binutils...on a M1 Mac you must use the Apple binutils or you will get linker errors. All
you need to do is move the homebrew binutils out of the way while you do the build....no big deal when you know about it.


Building on Windows
===================

MinGW-w64 from MSys2 is recommended as environment for building Windows distribution.

Dependencies
------------
* mingw-w64-x86_64-libusb
* mingw-w64-x86_64-zeromq

Build
-----

The command line to build the Orbuculum tool suite is:

>make

In order to get single folder with Orbuculum and MinGW dependencies run:

>make install install-mingw-deps DESTDIR=<directory1> INSTALL_ROOT=/<directory2>

Everything will be installed into `directory1/directory2` and can be transfered to different machine or used outside of MSys2 shell.

Using
=====

The command line options for Orbuculum are available by running
orbuculum with the -h option. Orbuculum is just the multiplexer, the
fifos are now provided by `orbfifo`. *This is a change to the previous
way the suite was configured where the fifos were integrated into `orbuculum` itself*. Note that
orbfifo isn't available on Windows (there are no fifos), so use orbzmq to get similar functionality
on that platform.

Simply start orbuculum with the correct options for your trace probe and
then you can start of stop other utilities as you wish. A typical command
to run orbuculum would be;

```$ orbuculum --monitor 100```

In this case, because no source options were provided on the command line, input
will be taken from a Blackmagic probe USB SWO feed, or from an ORBTrace mini if it can find one.
It will start the daemon with a monitor reporting interval of 100mS.  Orbuculum exposes TCP port 3443 to which
network clients can connect. This port delivers raw TPIU frames to any
client that is connected (such as orbcat, orbfifo or orbtop).
The practical limit to the number of clients that can connect is set by the speed of the host machine....but there's
nothing stopping you using another one on the local network :-)

Information about command line options can be found with the -h
option.  Orbuculum itself is specifically designed to be 'hardy' to probe and
target disconnects and restarts (y'know, like you get in the real
world). In general the other programs in the suite will stay alive while
`orbuculum` itself is available.  The intention being to give you useful information whenever it can get
it.  Orbuculum does _not_ require gdb to be running, but you may need a
gdb session to start the output.  BMP needs traceswo to be turned on
at the command line before it capture data from the port, for example.

Command Line Options
====================

For `orbuculum`, the specific command line options of note are;

 `-a, --serial-speed: [serialSpeed]`: Use serial port and set device speed.

 `-E, --eof`: When reading from file, ignore eof.

 `-f, --input-file [filename]`: Take input from file rather than device.

 `-h, --help`: Brief help.

 `-m, --monitor`: Monitor interval (in mS) for reporting on state of the link. If baudrate is specified (using `-a`) and is greater than 100bps then the percentage link occupancy is also reported.

  `-o, --output-file [filename]`: Record trace data locally. This is unfettered data directly from the source device, can be useful for replay purposes or other tool testing.

  `-p, --serial-port [serialPort]`: to use. If not specified then the program defaults to Blackmagic probe.

  `-s, --server [address]:[port]`: Set address for explicit TCP Source connection, (default none:2332).

  `-t, --tpiu x,y,...`: Remove TPIU formatting and issue streams x, y etc over incrementing IP port numbers.


Orbfifo
-------

**Note:** `orbfifo` is not supported on Windows.

The easiest way to use the output from orbuculum is with one of the utilities
such as orbfifo. This creates a set of fifos or permanent files in a given
directory containing the decoded streams which apps can exploit directly. It also has
a few other tricks up it's sleeve like filewriter capability. It used to be integrated into
`orbuculum` but seperating it out splits the trace interface from the user space utilities, which
is a Good Thing(tm).

A typical command line would be;

>orbfifo -b swo/ -c 0,text,"%c" -v 1

The directory 'swo/' is expected to already exist, into which will be placed
a file 'text' which delivers the output from swo channel 0 in character
format.  Multiple -c options can be provided to set up fifos for individual channels
from the debug device. The format of the -c option is;

>-c ChannelNum,ChannelName,FormatString

ChannelNum is 0..31 and corresponds to the ITM channel. The name is the one
that will appear in the directory and the FormatString can present the data
using any printf-compatable formatting you prefer, so, the following are all
legal channel specifiers;

    -c 7,temperature,"%d \260C\n"
    -c 2,hexAddress,"%08x,"
    -c 0,volume,"\l%d\b\n"

Be aware that if you start making the formatting or screen handling too complex
its quite possible your machine might not keep up...and then you will loose data!

While you've got `orbfifo` running a further fifo `hwevent` will be found in
the output directory, which reports on events from the hardware, one event per line as follows (note that
the order of these has changed);

* `0,[Status],[TS]` : Time status and timestamp.
* `1,[EventType],[ExceptionNumber]` : Hardware exception. Event type is one of [Enter, Exit, Resume].
* `2,[PCAddr]` : Report Program Counter Sample.
* `3,[DWTEvent]` : Report on DWT event from the set [CPI,Exc,Sleep,LSU,Fold and Cyc].
* `4,[Comp],[RW],[Data]` : Report Read/Write event.
* `5,[Comp],[Addr]` : Report data access watchpoint event.
* `6,[Comp],[Ofs]` : Report data offset event.
* `7` : Currently unused.
* `8,[Status],[Address]` : ISYNC event.

The command line options are;

 `-b, --basedir [basedir]`: for channels. Note that this is actually just leading text on the channel
     name, so if you put xyz/chan then all ITM software channels will end up in a directory
     xyz, prepended with chan.  If xyz doesn't exist, then the channel creation will
     fail silently.

 `-c, --channel [Number],[Name],[Format]`: of channel to populate (repeat per channel) using printf formatting.

 `-E`: When reading from file, terminate when file exhausts, rather than waiting for more data to arrive.

 `-f, --input-file [filename]`: Take input from specified file (CTRL-C to abort from this).

 `-h, --help`: Brief help.

  `-P, --permanent`: Create permanent files rather than fifos - useful when you want to use the processed data later.

  `-s [address]:[port]`: Set address for Source connection, (default localhost:3443).

  `-t, --tpiu`: Use TPIU decoder.  This will not sync if TPIU is not configured, so you won't see
     packets in that case.

  `-v, --verbose`: Verbose mode 0==Errors only, 1=Warnings (Default) 2=Info, 3=Full Debug.

  `-W, --writer-path [path]` : Enable filewriter functionality with output in specified directory (disabled by default).

Orbzmq
------
`orbzmq` is utility that connects to orbuculum over the network and outputs data from various ITM HW and SW channels that it find. This output is sent over (ZeroMQ)[https://zeromq.org/] PUBLISH socket bound to specified URL. Each published message is composed of two parts: **topic** and **payload**. Topic can be used by consumers to filter incoming messages, payload contains actual message data - for SW channels formatted or raw data and predefined format for HW channels.

A typical command line would be like:
> orbzmq -l tcp://localhost:1234 -c 0,text,%c -c 1,raw, -c 2,formatted,"Value: 0x%08X\n" -e AWP,OFS,PC -v 1

`orbzmq` will create a ZeroMQ socket bound to address `tcp://*:3442` (which means, all interfaces, tcp port 3442) and publish messages with topics: `text`, `raw`, `formatted`, `hweventAWP`, `hweventOFS` and `hweventPC`.

A simple python client can receive messsages in the following way:
```python
import zmq


ctx = zmq.Context()
sock = ctx.socket(zmq.SUB)
sock.connect('tcp://localhost:3442')
sock.setsockopt(zmq.SUBSCRIBE, b'raw')
sock.setsockopt(zmq.SUBSCRIBE, b'formatted')
sock.setsockopt(zmq.SUBSCRIBE, b'hwevent') # subscribe to all hwevents

while True:
    [topic, msg] = sock.recv_multipart()
    if topic == b'raw':
        decoded = int.from_bytes(msg, byteorder='little')
        print(f'Raw: 0x{decoded:08X}')
    elif topic == b'formatted':
        print(msg.decode('ascii'),end="")
    elif topic.startswith(b'hwevent'):
        print(f'HWEvent: {topic} Msg: {msg}')
```

Command line options are:

 `-c, --channel [Topic],[Name],[Format]`:      of channel to populate (repeat per channel)

 `-e, --hwevent [event1],[event2]`:      Comma-separated list of published hwevents (Use `all` to include all hwevents)

 `-E, --eof`:          Terminate when the file/socket ends/is closed, or attempt to wait for more / reconnect

 `-f, --input-file [filename]`:   Take input from specified file

 `-h, --help`:         This help

 `-n, --itm-sync`:     Enforce sync requirement for ITM (i.e. ITM needsd to issue syncs)

 `-s, --server [server]:[port]`:       to connect to

 `-t, --tpiu [channel]`:         Use TPIU decoder on specified channel (normally 1)

 `-v, --verbose [level]`:      Verbose mode 0(errors)..3(debug)

 `-V, --version`:      Print version and exit

 `-z, --zbind [url]`:         ZeroMQ bind URL


Orbcat
------

orbcat is a simple utility that connects to orbuculum over the network and
outputs data from various ITM HW and SW channels that it finds.  This
output is sent to stdout so the program is very useful for providing direct
input for other utilities.  There can be any number of instances of orbcat
running at the same time, and they will all decode data independently. They all get
a seperate networked data feed.  A
typical use case for orbcat would be to act as a stdin for another program...an example
of doing this to just replicate the data delivered over ITM Channel 0 would be

`orbcat -c 0,"%c"`

...note that any number of `-c` options can be entered on the command line, which
will combine data from those individual channels into one stream. Command line
options for orbcat are;

 `-c, --channel [Number],[Format]`: of channel to populate (repeat per channel) using printf
     formatting. Note that the `Name` component is missing in this format because
     orbcat does not create fifos.

 `-E, --eof`: When reading from file, terminate when file exhausts, rather than waiting for more data to arrive.

 `-f, --input-file [filename]`: Take input from specified file (CTRL-C to abort from this).

 `-h, --help`: Brief help.

 `-n, --itm-sync`: Enforce sync requirement for ITM (i.e. ITM needsd to issue syncs)

 `-s --server [server]:[port]`: to connect to. Defaults to localhost:3443 to connect to the orbuculum daemon. Use localhost:2332 to connect to a Segger J-Link, or whatever other combination applies to your source.

 `-t, --tpiu`: Use TPIU decoder.  This will not sync if TPIU is not configured, so you won't see
     packets in that case.

 `-v, --verbose [x]`: Verbose mode level 0..3.

Orbtop
------

Orbtop connects to orbuculum over the network and
samples the Program Counter to identify where the program is spending its time. By default
it will update its statistical output once per second. For code that matches to a function
the the source file it will totalise all of the samples to tell you how much time is being
spent in that function.  Any samples that do not match to an identifiable function are
reported as 'Unknown'.

As with Orbcat there can be any number of instances of orbtop running at the same time,
which might be useful to perform sampling over different time horizons. A typical invocation
line for orbtop would be;

`orbtop -e ~/Develop/STM32F103-skel/ofiles/firmware.elf`

...the pointer to the elf file is always needed for orbtop to be able to recover symbols from.

One useful command line option for orbtop (and indeed, for the majority of the rest of the
suite) is -s localhost:2332, which will connect directly to any source you might have exporting
SWO data on its TCP its port, with no requirement for the orbuculum multiplexer in the way.

Command line options for orbtop are;

 `-c, --cut-after [num]`: Cut screen output after number of lines.

 `-d, --del-prefix [DeleteMaterial]`: to take off front of filenames (for pretty printing).

 `-D, --no-demangle`: Switch off C++ symbol demangling (on by default).

 `-e, --elf-file`: Set elf file for recovery of program symbols. This will be monitored and reloaded if it changes.

 `-E, --exceptions`: Include exception (interrupt) measurements.

 `-f, --input-file [Filename]`: Take input from specified file

 `-g, --record-file [LogFile]`: Append historic records to specified file on an ongoing basis.

 `-h, --help`: Brief help.

 `-I, --interval [Interval]`: Set integration and display interval in milliseconds (defaults to 1000 mS)

 `-j, --json-file [filename]`: Output to file in JSON format (or screen if <filename> is '-')

 `-l, --agg-lines`: Aggregate per line rather than per function

 `-n, --itm-sync`: Enforce sync requirement for ITM (i.e. ITM needs to issue syncs)

 `-o, --output-file [filename]`: Set file to be used for output history

 `-O, --objdump-opts [opts]`: Set options to pass directly to objdump

 `-r, --routines <routines>`: Number of lines to record in history file

 `-R, --report-file [filename]`: Report filenames as part of function discriminator

 `-s, --server [server]:[port]`: to connect to. Defaults to localhost:3443

 `-t, --tpiu`: Use TPIU decoder.  This will not sync if TPIU is not configured, so you won't see
     packets in that case.

 `-v, --verbose [x]`: Verbose mode 0..3.

Its worth a few notes about interrupt measurements. orbtop can provide information about the number of
times an interrupt is called, what its maximum nesting is, how many 'execution ticks' it's active for
and what the spread is of those. Here's a typical combination output for a simple system;

```
 98.25%     1911 ** SLEEPING **
  0.25%        5 uart_xmitchars
  0.20%        4 up_serialin
  0.10%        2 up_doirq
  0.10%        2 up_interrupt
  0.10%        2 up_restoreusartint
  0.10%        2 uart_pollnotify
  0.10%        2 uart_write
  0.10%        2 nxsem_post
-----------------
 99.30%     1932 of 1945 Samples


 Ex |   Count  |  MaxD | TotalTicks  |  AveTicks  |  minTicks  |  maxTicks
----+----------+-------+-------------+------------+------------+------------
 11 |        1 |     1 |        263  |        263 |       263  |       263
 15 |      100 |     1 |      10208  |        102 |       100  |       210
 53 |      210 |     1 |      44752  |        213 |        97  |       479

[V-TH] Interval = 1002mS / 7966664 (~7950 Ticks/mS)
```

The top half of this display is the typical 'top' output, the bottom half is a table of
active interrupts that have been monitored in the interval. Note that outputs are
given in terms of 'ticks', and the number of cpu cycles that correspond to a tick
is set by ```ITMTSPrescale```. You will also need to set ```dwtTraceException`` and
```ITMTSEna``` to be able to use this output mode.

Orbmortem
---------

To use orbmortem you must be using a parallel trace source such as ORBTrace Mini, and it must be
configured to stream parallel trace info (clue; the `startETM` option).

The command line options of note are;

 `-a, --alt-addr-enc`: Don't use alternate address encoding. Select this if decodes don't seem to arrive correctly. You can discover if you need this option by using the `describeETM` command inside the debugger.

 `-b, --buffer-len [Length]`: Set length of post-mortem buffer, in KBytes (Default 32 KBytes)

 `-c, --editor-cmd [command]`: Set command line for external editor (0.000000 = filename, % = line). A few examples are;

     * emacs; `-c emacs "+%l %f"`
     * codium/VSCode; `-c codium  -g "%f:%l"`
     * eclipse; `-c eclipse "%f:%l"`

 `-D, --no-demangle`: Switch off C++ symbol demangling

 `-d, --del-prefix [String]`: Material to delete off front of filenames

 `-e, --elf-file [ElfFile]`: to use for symbols and source

 `-E, --eof`: When reading from file, terminate at end of file rather than waiting for further input

 `-f, --input-file [filename]`: Take input from specified file rather than live from a probe (useful for ETB decode)

 `-h, --help`: Provide brief help

 `-p, --trace-proto [protocol]`: to use, where protocols are MTB or ETM35 (default). Note that MTB only makes sense from a file.

 `-s, --server [Server:Port]`: to use

 `-t, --tpiu [channel]`: Use TPIU to strip TPIU on specfied channel (normally best to let `orbuculum` handle this


Once it's running you will receive an indication at the lower right of the screen that it's capturing data. Hitting `H` will hold the capture and it will decode whatever is currently in the buffer. More usefully, if the capture stream is lost (e.g. because of debugger entry) then it will auto-hold and decode the buffer, showing you the last instructions executed. You can use the arrow keys to move around this buffer and dive into individual source files. Hit the `?` key for a quick overview of available commands.


Reliability
===========

A whole chunk of work has gone into making sure the dataflow over both the
SWO link and parallel Trace is reliable....but it's pretty dependent on the debug
interface itself.  The TL;DR is that if the interface _is_ reliable
then Orbuculum will be. There are factors outside of our control
(i.e. the USB bus you are connected to) that could potentially break the
reliabilty but there's not too much we can do about that since the SWO
link is unidirectional (no opportunity for re-transmits). The
following section provides evidence for the claim that the link is good;

On blackmagic a test 'mule' sends data flat out to the link at the maximum data rate
of 2.25Mbps using a loop like the one below;

    while (1)
    {
        for (uint32_t r=0; r<26; r++)
        {
            for (uint32_t g=0; g<31; g++)
            {
                ITM_SendChar('A'+r);
            }
            ITM_SendChar('\n');
        }
    }

100MB of data (more than 200MB of actual SWO packets, due to the
encoding) was sent from the mule via a BMP where the output from
swolisten chan00 was cat'ted into a file;

>cat swo/chan00 > o

....this process was interrupted once the file had grown to 100MB. The
first and last lines were removed from it (these represent previously
buffered data and an incomplete packet at the point where the capture
was interrupted)  and the resulting file analysed for consistency;

> sort o | uniq -c

The output was;

```
126462 AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
126462 BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB
126462 CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC
126462 DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD
126461 EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE
126461 FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF
126461 GGGGGGGGGGGGGGGGGGGGGGGGGGGGGGG
126461 HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH
126461 IIIIIIIIIIIIIIIIIIIIIIIIIIIIIII
126461 JJJJJJJJJJJJJJJJJJJJJJJJJJJJJJJ
126461 KKKKKKKKKKKKKKKKKKKKKKKKKKKKKKK
126461 LLLLLLLLLLLLLLLLLLLLLLLLLLLLLLL
126461 MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM
126461 NNNNNNNNNNNNNNNNNNNNNNNNNNNNNNN
126461 OOOOOOOOOOOOOOOOOOOOOOOOOOOOOOO
126461 PPPPPPPPPPPPPPPPPPPPPPPPPPPPPPP
126461 QQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQ
126461 RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR
126461 SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS
126461 TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT
126461 UUUUUUUUUUUUUUUUUUUUUUUUUUUUUUU
126461 VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV
126461 WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW
126461 XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
126461 YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY
126461 ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ
```

(On inspection, the last line of recorded data was indeed a 'D' line).

ORBTrace Mini is considerably faster than blackmagic. To test that, strings were sent out at maximum speed
over a 48Mbps SWO/Manch channel for an extended period of time. They were then collated and sorted by uniqueness, as follows;

```
$ time orbcat -c 0,"%c" | sort | uniq -c
      1 ABCDEFGHIJKLMNOPQRST
2385153869 ABCDEFGHIJKLMNOPQRSTUVWXYZ_*_abcdefghijklmnopqrstuvwxyz

real    490m37.616s
user    82m15.087s
sys    11m2.847s
```

That is 124.4GBytes of user to user transfer, 155.5GBytes of line transfer with no errors at 5.4MBytes/sec line,
4.32MBytes/sec user-to-user, assuming my math is holding up. As with blackmagic...if the wiring is reliable, the
link will be.  The equivalent test on the same chip using 4 bit parallel TRACE gives a user-to-user data rate
of around 12.5MBytes/sec...at that point you're limited by the speed the CPU can put data out onto line rather
than the capacity of the link itself.

Using SWO in Battle
===================

SWO gives you a number of powerful new capabilities in your debug
arsenal. Here are a few examples....if you have more to add please
send me an email.

Multi-channel Debug
-------------------

The easiest and most obvious use of SWO is to give you multi-channel
debug capability. By adding multiple '-c' definitions to the orbuculum
comand line you can create multiple fifos which will each emit data of
interest. So, for the simple case of two distinct serial streams,
something like the following will suffice;

`-c 0,out0,"%c" -c 1,out1,"%c"`

...this will create two fifos in your output directory, `out0` and
`out1`, each with distinct output data.  By default the CMSIS provided
`ITM_SendChar` routine only outputs to channel0, so you will need a
new routine that can output to a specified channel. Something like;

    static __INLINE uint32_t ITM_SendChar (uint32_t c, uint32_t ch)
    {
      if ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk)  &&      /* Trace enabled */
          (ITM->TCR & ITM_TCR_ITMENA_Msk)                  &&      /* ITM enabled */
          (ITM->TER & (1ul << c)        )                    )     /* ITM Port c enabled */
      {
        while (ITM->PORT[c].u32 == 0);
        ITM->PORT[c].u8 = (uint8_t) ch;
      }
      return (ch);
    }

Now, this works perfectly for chars, but you can also write longer
values into the transit buffer so if, for example, you wanted to write
32 bit values from a calculation, just update the routine to take
int32_t and change the channel definition to be something more like
`-c 4,calcResult,"%d"`. You'll find example routines in the orbmule/examples/simple repository.


Mixing Channels
---------------

It gets more complicated when you want to mix output from
individual channels together. In this circumstance you can either
write a bit of script to merge the channels together, or you can use
`orbcat` to do the same thing from the command line. So if, for
example, you wanted to merge the text from channel0 with the 32 bit values
from channel 4, an orbcat line such as this would do the job;

`orbcat -c 0,"%c" -c 4,"\nResult=%d\n"`

...its obvious that the formatting of this buffer is completely
dependent on the order in which data arrive from the target, so you
might want to put some 'tags' or differentiators into each channel to
keep them distinct - a typical mechanism might be to use commas to
seperate the flows into different columns in a CSV file.

Multiple Simulteneous Outputs
-----------------------------

Orbuculum will place fifos for any defined channels (plus the hardware event
channel) in the specified output directory. It will simulteneously
create a TCP server to which an arbitary number of clients can
connect. Those clients each decode the data flow independently of
orbuculum, so you can present the data from the target simulteneously
in multiple formats (you might log it to a file while also processing
it via a plot routine, for example).  You can also use the source code for orbcat or
orbtop as the basis for creating your own specific decoders
(and I'd really appreciate a copy to fold into this suite too please!).

Using Orbtop
------------
Orbtop is an example client to orbuculum which processes the PC
sampling information to identify what routines are running at any
point in time.  This is essential information to understand what your
target is actually doing and once you've got this data you'll find you
become addicted to it! Just running orbtop with the details of your
target binary is enough for orbtop to
do its magic (along with information about the configuration of the
incoming SWO stream, of course);

`orbtop -t -i 9 -a -e firmware.elf`

orbtop can aggregate per function or per program line. By default it aggregates
per function but to work per-line just add the `-l` option...usually that gives you
too much information though.

The amount (and indeed, presence) of sample data is set by a number of
configuration options. These can be set from program code, but it's
more flexible to set them from gdb. The main ones are;

* `dwtSamplePC` : Enable or disable Program Counter sample generation
* `dwtPostTap` : Set the count rate for the PC interval counter at
either bit 6 or bit 10 of the main CPU clock.
* `dwtPostInit` : set the initial value for the PC interval counter
(this defines when the first sample is taken....you've got to be
pretty precise if this is important to you!).
* `dwtPostReset` : Set the reload value for the PC Interval Counter
(higher values = slower counting).
* `dwtCycEna` : Enable the cycle counter input (i.e. switch the whole
thing on). You won't get far without this set!

The maximum speed at which you can generate samples is defined by the
speed of your SWO connection but, with a 72MHz CPU, the slowest
settings (`dwtPostTap 1` and `dwtPostReset 15`) still generate about
4000 samples per second, so you will get useful information at that
level of resolution. There is a risk that you could miss frequent, but
short, routines if you're running too slow, so do vary the speeds to
make sure you get consistent results...on the other hand running too
fast will lead to flooding the SWO and potentially missing other
important data such as channel output.  You do not need to restart
orbuculum or orbtop in order to change the parameters in gdb - just
CTRL-C, change, and restart. With a setting of `dwtPostReset 1` there
are no overflows when using a async interface at 2.25Mbps, which equates
to 35200 Program Counter samples per second.

Here's a typical example of orbtop output for a Skeleton application based
on FreeRTOS with USB over Serial (CDC) support. This table is updated once per second;

     97.90%     4308 ** Sleeping **
      1.25%       55 USB_LP_CAN1_RX0_IRQHandler
      0.20%        9 xTaskIncrementTick
      0.13%        6 Suspend
      0.09%        4 SysTick_Handler
      0.06%        3 Resume
      0.06%        3 __WFI
      0.04%        2 vTaskSwitchContext
      0.04%        2 TIM_Cmd
      0.02%        1 prvAddCurrentTaskToDelayedList
      0.02%        1 xTaskResumeAll
      0.02%        1 vTaskDelay
      0.02%        1 PendSV_Handler
      0.02%        1 __ISB
      0.02%        1 taskIn
      0.02%        1 statsGetRTVal
      0.02%        1 taskOut
    -----------------
                4400 Samples

orbtop can also generate graph output. You will find utilities to support this for
gnuplot in the `Support` directory.  Just start orbtop with the option `-o <filename>`
to generate the output data and then run `Support/orbtop_plot` to generate the output.
By default it generates pdf graphs once per second, but that's easily changed.

Dogfood
=======

Orbuculum was pointed at a a BMP instance (running on a 72MHz
STM32F103C8) both with and without SWO running in asynchronous mode at
2.25Mbps.

Firstly, without SWO;

     26.96%     1186 gdb_if_update_buf
     23.23%     1022 stm32f103_ep_read_packet
     21.82%      960 gdb_if_getchar_to
      6.66%      293 cdcacm_get_config
      6.54%      288 platform_timeout_is_expired
      5.61%      247 usbd_ep_read_packet
      4.86%      214 platform_time_ms
      3.90%      172 cdcacm_get_dtr
      0.13%        6 _gpio_clear
      0.06%        3 gpio_set_mode
      0.04%        2 swdptap_turnaround
      0.04%        2 swdptap_seq_out
      0.02%        1 swdptap_bit_in
      0.02%        1 swdptap_bit_in
      0.02%        1 swdptap_turnaround
      0.02%        1 platform_timeout_set
    -----------------
                4399 Samples

...and then, with SWO running (note that in this second case the
sample frequency had to be increased to be able to see the impact,
which is reflected in `dma1_channel5_isr` and to a much lesser degree
in `trace_buf_drain`). When this trace was taken the target
was emitting nearly 18000 PC samples per second, encoded in TPIU
frames.

     18.17%     3198 stm32f103_ep_read_packet
     17.35%     3054 gdb_if_getchar_to
     15.05%     2648 gdb_if_update_buf
     11.02%     1940 usbd_ep_read_packet
      9.24%     1627 platform_time_ms
      9.07%     1597 platform_timeout_is_expired
      7.93%     1396 cdcacm_get_dtr
      4.78%      842 cdcacm_get_config
      4.72%      831 dma1_channel5_isr
      1.52%      268 usb_copy_to_pm
      0.45%       80 stm32f103_poll
      0.17%       31 trace_buf_drain
      0.06%       12 usb_lp_can_rx0_isr
      0.05%        9 gpio_set_mode
      0.03%        7 swdptap_turnaround
      0.03%        7 swdptap_turnaround
      0.03%        7 _gpio_clear
      0.03%        7 usbd_poll
      0.03%        6 swdptap_seq_out_parity
      0.02%        5 swdptap_seq_out
      0.02%        4 swdptap_seq_in_parity
      0.01%        3 adiv5_swdp_low_access
      0.01%        2 swdptap_bit_in
      0.01%        2 swdptap_bit_out
      0.01%        2 _gpio_set
      (Anthing < 0.01% removed)
    -----------------
               17594 Samples

Using orbuculum with other info Sources
=======================================

As Karl Palsson pointed out in Issue #4 on github, all of the support tools just need a stream
of 'clean' trace data. Normally that is provided by the network connection that orbuculum exports, but you
can also use something like netcat to generate the stream for orbuculum or its clients.
For example, from a file that is written to via something like openocd;

    > tail -f swo.dump.log | nc -v -v -l 9999 -k

and then;

    > ./ofiles/orbuculum -g 9999 -b md/ -c 0,text,"%c"

However, that's probably over-complicated now...just use the orbuculum -s option to hook to any source that
is pumping out clean SWO data. This information is just left here to show the flexibilities you have got available.
