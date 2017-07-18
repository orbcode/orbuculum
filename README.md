Orbuculum - ARM Cortex SWO Output Processing Tools
==================================================

An Orbuculum is a Crystal Ball, used for seeing things that would 
 be otherwise invisible. A  nodding reference to (the) BlackMagic
 (debug probe), BMP.

*This program is in heavy development. Check back frequently for new versions 
with additional functionality. The current status (18th July) is that orbstat
has just been added, which, initially, provides KCacheGrind compatible output
for program visualisation. The software runs on both Linux and OSX.  Orbtop has recently
received even more special love and it seems robust on all tested workloads,
and now can produce graphs too.*

I would not say the whole suite is throughly tested yet...that will
come when full functionality has been completed. For now the 'happy
flows' are working OK but the stuff might not look too pretty.

What is it?
===========
Orbuculum is a set of tools for decoding and presenting output flows from
the SWO pin of a CORTEX-M CPU. Numerous types of data can be output through this
pin, from multiple channels of text messages through to Program Counter samples. Processing
these data gives you a huge amount of insight into what is really going on inside
your CPU. The current tools are;

* orbuculum: The main program and multiplexer...used as a base interface to the target by
other programmes in the suite. Generally you can just leave this running and it'll grab
data from the target and make it available to clients whenever it can.

* orbcat: A simple cat utility for ITM channel data.

* orbdump: A utility for dumping raw SWO data to a sfile for post-processing.

* orbtop: A top utility to see what's actually going on with your target. It can also
provide dot and gnuplot source data for perty graphics.

* orbstat: An analysis/statistics utility which can produce KCacheGrind input files. KCacheGrind
is a very powerful code performance analysis tool.

A few simple use cases are documented in the last section of this
document, as are example outputs of using orbtop to report on the
activity of BMP while emitting SWO packets. More will be added.

The data flowing from the SWO pin can be encoded
either using NRZ (UART) or RZ (Manchester) formats.  The pin is a
dedicated one that would be used for TDO when the debug interface is
in JTAG mode. 

Orbuculum takes this output and makes it accessible to tools on the host
PC. At its core it takes the data from the SWO, decodes it and presents
it to a set of unix fifos which can then be used as the input to other
programs (e.g. cat, or something more sophisticated like gnuplot,
octave or whatever). Orbuculum itself doesn't care if the data
originates from a RZ or NRZ port, or at what speed....that's the job
of the interface.

At the present time Orbuculum supports three devices for collecting SWO
from the target;
 
* the Black Magic Debug Probe (BMP)
* the SEGGER JLink
* generic USB TTL Serial Interfaces 

Information about using each individual interface can be found in the
docs directory. gdb setup files for each device type can be found in the `Support` directory.

Orbuculum can use, or bypass, the TPIU. The TPIU adds (a small amount of) overhead
to the datastream, but provides better synchronisation if there is corruption
on the link. To include the TPIU in decode stack, provide the -t 
option on the command line. If you don't provide it, and the ITM decoder sees
TPIU syncs in the datastream, it will complain and barf out. This is deliberate
after I spent two days trying to find an obscure bug 'cos I'd left the `-t` option off...

When in NRZ mode the SWO data rate that comes out of the chip _must_
match the rate that the debugger expects. On the BMP speeds of
2.25Mbps are normal, TTL Serial devices tend to run a little slower
but 921600 baud is normally acheivable. On BMP the baudrate is set via
the gdb session with the 'monitor traceswo xxxx' command. For a TTL
Serial device its set by the Orbuculum command line.  Segger devices
can normally work faster, but no experimentation has yet been done to
find their max limits, which are probably dependent on the specific JLink
you are using.


Configuring the Target
======================

Generally speaking, you will need to configure the target device to output
SWD data. You can either do that through program code, or through magic
incantations in gdb. The gdb approach is more flexible and the program code
version is grandfathered. It's in the support directory if you want it.

In the support directory you will find a script `gdbtrace.init` which contains a
set of setup macros for the SWO functionality. Full details of how to set up these
various registers are available from https://static.docs.arm.com/ddi0403/e/DDI0403E_B_armv7m_arm.pdf and
you've got various options for the type of output generated, its frequency and it's content.

Using these macros means you do not need to change your program code to be able to use
facilities like orbtop. Obviously, if you want textual trace output, you've got to create
that in the program!

Information about the contets of this file can be found by importing it into your
gdb session with `source gdbtrace.init` and then typing `help orbuculum`. Help on the
parameters for each macro are available via the help system too.

In general, you will configure orbuculum via your local `.gdbinit` file. Several example files are 
also in the Support directory. Generically, it looks like this; 

    source config/gdbtrace.init             <---- Source the trace specific stuff
    target extended-remote /dev/ttyACM1     <-
    monitor swdp_scan                       <-
    file ofiles/firmware.elf                <- 
    attach 1                                <---- Connect to the target
    set mem inaccessible-by-default off     <-
    set print pretty                        <-
    load                                    <---- Load the program
    
    monitor traceswo 2250000                <---- wakeup tracing on the probe
    
    start                                   <---- and get to main

    enableSTM32F1SWD                        <---- turn on SWO output pin on CPU

    prepareSWD SystemCoreClock 2250000 1 0  <---- Setup SWO timing

    dwtSamplePC 1                           <-
    dwtSyncTAP 3                            <-
    dwtPostTAP 1                            <-
    dwtPostInit 1                           <-
    dwtPostReset 15                         <-
    dwtCycEna 1                             <---- Configure Data Watch/Trace

    ITMId 9                                 <-
    ITMGSTFreq 3                            <-
    ITMTSPrescale 3                         <-
    ITMTXEna 1                              <-
    ITMSYNCEna 1                            <-
    ITMEna 1                                <---- Enable Instruction Trace Macrocell

    ITMTER 0 0xFFFFFFFF                     <---- Enable Trace Ports
    ITMTPR 0xFFFFFFFF                       <---- Make them accessible to user level code

Building
========

The command line to build the Orbuculum tool is;

make

...you may need to change the paths to your libusb files, depending on
how well your build environment is set up.

Using
=====

The command line options for Orbuculum are available by running
orbuculum with the -h option.

A typical command line would be;

>orbuculum -b swo/ -c 0,text,"%c" -vt

The directory 'swo/' is expected to already exist, into which will be placed
a file 'text' which delivers the output from swo channel 0 in character
format.  Because no source options were provided on the command line, input
will be taken from a Blackmagic probe USB SWO feed.

Multiple -c options can be provided to set up fifos for individual channels
from the debug device. The format of the -c option is;

 ChannelNum,ChannelName,FormatString

ChannelNum is 0..31 and corresponds to the ITM channel. The name is the one
that will appear in the directory and the FormatString can present the data
using any printf-compatable formatting you prefer, so, the following are all 
legal channel specifiers;

    -c 7,temperature,"%d \260C\n"
    -c 2,hexAddress,"%08x,"
    -c 0,volume,"\l%d\b\n"

Be aware that if you start making the formatting or screen handling too complex
its quite possible your machine might not keep up...and then you will loose data!

Information about command line options can be found with the -h
option.  Orbuculum is specifically designed to be 'hardy' to probe and
target disconnects and restarts (y'know, like you get in the real
world). The intention being to give you streams whenever it can get
them.  It does _not_ require gdb to be running, but you may need a 
gdb session to start the output.  BMP needs traceswo to be turned on
at the command line before it capture data from the port, for example.

A further fifo `hwevent` will be found in the output directory, which reports on 
events from the hardware, one event per line as follows;

* `0,[Status],[TS]` : Time status and timestamp.
* `1,[PCAddr]` : Report Program Counter Sample.
* `2,[DWTEvent]` : Report on DWT event from the set [CPI,Exc,Sleep,LSU,Fold and Cyc].
* `3,[EventType],[ExceptionNumber]` : Hardware exception. Event type is one of [Enter, Exit, Resume].
* `4,[Comp],[RW],[Data]` : Report Read/Write event.
* `5,[Comp],[Addr]` : Report data access watchpoint event.
* `6,[Comp],[Ofs]` : Report data offset event.

In addition to the direct fifos, Orbuculum exposes TCP port 3443 to which 
network clients can connect. This port delivers raw TPIU frames to any
client that is connected (such as orbcat, and shortly by orbtop). 
The practical limit to the number of clients that can connect is set by the speed of the host machine.

Command Line Options
====================

Specific command line options of note are;

 `-a [name]`: Set hostname for JLinkGDB SWO source

`-b [basedir]`: for channels. Note that this is actually just leading text on the channel
     name, so if you put xyz/chan then all ITM software channels will end up in a directory
     xyz, prepended with chan.  If xyz doesn't exist, then the channel creation will 
     fail silently.

 `-c [Number],[Name],[Format]`: of channel to populate (repeat per channel) using printf formatting.

`-g [Port]`: Specify JLink port to connect to (Normally 2332)

`-h`: Brief help.

`-f [filename]`: Take input from specified file.

 `-i [channel]`: Set Channel for ITM in TPIU decode (defaults to 1). Note that the TPIU must
     be in use for this to make sense.  If you call the GenericsConfigureTracing
     routine above with the ITM Channel set to 0 then the TPIU will be bypassed.

 `-p [serialPort]`: to use. If not specified then the program defaults to Blackmagic probe.

 `-s [serialSpeed]`: to use. Only relevant when using the serial interface.

 `-t`: Use TPIU decoder.  This will not sync if TPIU is not configured, so you won't see
     packets in that case.

 `-v`: Verbose mode.

Orbcat
======

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

 `-c [Number],[Format]`: of channel to populate (repeat per channel) using printf
     formatting. Note that the `Name` component is missing in this format because 
     orbcat does not create fifos.

 `-h`: Brief help.

 `-i [channel]`: Set Channel for ITM in TPIU decode (defaults to 1). Note that the TPIU must
     be in use for this to make sense.  If you call the GenericsConfigureTracing
     routine above with the ITM Channel set to 0 then the TPIU will be bypassed.

 `-p [port]`: to use. Defaults to 3443, the standard orbuculum port.

 `-s [server]`: to connect to. Defaults to localhost.

 `-t`: Use TPIU decoder.  This will not sync if TPIU is not configured, so you won't see
     packets in that case.

 `-v`: Verbose mode.


Orbtop
======

orbtop is a simple utility that connects to orbuculum over the network and 
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

Command line options for orbtop are;

 `-e`: Set elf file for recovery of program symbols.

 `-h`: Brief help.

 `-i [channel]`: Set Channel for ITM in TPIU decode (defaults to 1). Note that the TPIU must
     be in use for this to make sense.  If you call the GenericsConfigureTracing
     routine above with the ITM Channel set to 0 then the TPIU will be bypassed.

 `-m <MaxHistory>`: Number of intervals to record in history file

 `-o <filename>`: Set file to be used for output history 
 
 `-p [port]`: to use. Defaults to 3443, the standard orbuculum port.

 `-r <routines>`: Number of lines to record in history file 

 `-s [server]`: to connect to. Defaults to localhost.

 `-t`: Use TPIU decoder.  This will not sync if TPIU is not configured, so you won't see
     packets in that case.

 `-v`: Verbose mode.

Reliability
===========

A whole chunk of work has gone into making sure the dataflow over the
SWO link is reliable....but it's pretty dependent on the debug
interface itself.  The TL;DR is that if the interface _is_ reliable
then Orbuculum will be. There are factors outside of our control
(i.e. the USB bus you are connected to) that could potentially break the
reliabilty but there's not too much we can do about that since the SWO
link is unidirectional (no opportunity for re-transmits). The
following section provides evidence for the claim that the link is good; 

A test 'mule' sends data flat out to the link at the maximum data rate
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
`-c 4,calcResult,"%d"`.

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
