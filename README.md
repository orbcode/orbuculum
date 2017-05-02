Orbuculum - ARM Cortex SWO Output Processing Tools
==================================================

An Orbuculum is a Crystal Ball, used for seeing things that would 
 be otherwise invisible. A  nodding reference to (the) BlackMagic (debug probe).

*This program is in heavy development. Check back frequently for new versions 
with additional functionality. The current status (2nd May) is that ITM
SW logging is working, and HW tracing (Watchpoints, Timestamps, PC Sampling
etc.) is implemented and reported, but largely untested.*

On a CORTEX M Series device SWO is a datastream that comes out of a
single pin when the debug interface is in SWD mode. It can be encoded
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

At the present time Orbuculum supports two devices for collecting SWO
from the target;
 
* the Black Magic Debug Probe (BMP) and 
* generic USB TTL Serial Interfaces 

Information about using each individual interface can be found in the
docs directory.

It can use, or bypass, the TPIU. The TPIU adds (a small amount of) overhead
to the datastream, but provides better syncronisation if there is corruption
on the link. To include the TPIU in decode stack, provide the -t 
option on the command line.

When in NRZ mode the SWO data rate that comes out of the chip _must_
match the rate that the debugger expects. On the BMP speeds of
2.25Mbps are normal, TTL Serial devices tend to run a little slower
but 921600 baud is normally acheivable. On BMP the baudrate is set via
the gdb session with the 'monitor traceswo xxxx' command. For a TTL
Serial device its set by the Orbuculum command line.

Depending on what you're using to wake up SWO on the target, you
may need code to get it into the correct mode and emitting data. You
can do that via gdb direct memory accesses, or from program code.
 
An example for a STM32F103 (it'll be pretty much the same for any
other M3, but the dividers will be different);

    void GenericsConfigureTracing(uint32_t itmChannel, uint32_t sampleInterval, uint32_t tsPrescale)
    {
    /* STM32 specific configuration to enable the TRACESWO IO pin */
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;
    AFIO->MAPR |= (2 << 24); // Disable JTAG to release TRACESWO
    DBGMCU->CR |= DBGMCU_CR_TRACE_IOEN; // Enable IO trace pins for Async trace
    /* End of STM32 Specific instructions */

    *((volatile unsigned *)(0xE0040010)) = 31;  // Output bits at 72000000/(31+1)=2.250MHz.
    *((volatile unsigned *)(0xE00400F0)) = 2;  // Use Async mode pin protocol

    if (!itmChannel)
        {
            *((volatile unsigned *)(0xE0040304)) = 0;  // Bypass the TPIU and send output directly
        }
    else
        {
            *((volatile unsigned *)(0xE0040304)) = 0x102; // Use TPIU formatter and flush
        }

    /* Configure Trace Port Interface Unit */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; // Enable access to registers

    /* Configure PC sampling and exception trace  */
    DWT->CTRL = /* CYCEVTEN */ (1<<22) |
                /* Sleep event overflow */ (1<<19)  |
                /* Enable Exception Trace */ (1 << 16) |
                /* PC Sample event */  (1<<12) |
                /* Sync Packet Interval */ ((3&0x03) << 10) | /* 0 = Off, 1 = 2^23, 2 = Every 2^25, 3 = 2^27 */
                /* CYCTap position */ (1 << 9) |  /* 0 = x32, 1=x512 */
                /* Postscaler for PC sampling */ ((sampleInterval&0x0f) << 1) | /* Divider = value + 1 */
                /* CYCCnt Enable */ (1 << 0);

    /* Configure instrumentation trace macroblock */
    ITM->LAR = ETM_LAR_KEY;
    ITM->TCR = /* DWT Stimulus */ (1<<3)|
               /* ITM Enable */   (1<<0)|
               /* SYNC Enable */  (1<<2)|
               /* TS Enable */    (1<<1)|
               /* TC Prescale */  ((tsPrescale&0x03)<<8) |
                ((itmChannel&0x7f)<<16);  /* Set trace bus ID and enable ITM */
    ITM->TER = 0xFFFFFFFF; // Enable all stimulus ports

    /* Configure embedded trace macroblock */
    ETM->LAR = ETM_LAR_KEY;
    ETM_SetupMode();
    ETM->CR = ETM_CR_ETMEN // Enable ETM output port
            | ETM_CR_STALL_PROCESSOR // Stall processor when fifo is full
            | ETM_CR_BRANCH_OUTPUT; // Report all branches
    ETM->TRACEIDR = 2; // Trace bus ID for TPIU
    ETM->TECR1 = ETM_TECR1_EXCLUDE; // Trace always enabled
    ETM->FFRR = ETM_FFRR_EXCLUDE; // Stalling always enabled
    ETM->FFLR = 24; // Stall when less than N bytes free in FIFO (range 1..24)
                    // Larger values mean less latency in trace, but more stalls.
    }

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
client that is connected (and will shortly be used for other utilities in the
suite like OrbCat and OrbTop). The practical limit to the number of clients that can connect is set by the speed of the host machine.

Command Line Options
====================

Specific command line options of note are;

 `-b [basedir]`: for channels. Note that this is actually just leading text on the channel
     name, so if you put xyz/chan then all ITM software channels will end up in a directory
     xyz, prepended with chan.  If xyz doesn't exist, then the channel creation will 
     fail silently.

 `-c [Number],[Name],[Format]`: of channel to populate (repeat per channel) using printf
     formatting.

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

