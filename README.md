ACSOPT - ARM Cortex SWO Output Processing Tools
===============================================

On a CORTEX M Series device SWO is a datastream that comes out of a
single pin when the debug interface is in SWD mode. It can be encoded
either using NRZ (UART) or RZ (Manchester) formats.  The pin is a
dedicated one that would be used for TDO when the debug interface is
in JTAG mode. 

ACSOPT takes this output and makes it accessible to tools on the host
PC. At its core ACSOPT takes the data from the SWO, decodes it and presents
it to a set of unix fifos which can then be used as the input to other
programs (e.g. cat, or something more sophisticated like gnuplot,
octave or whatever). ACSOPT itself doesn't care if the data
originates from a RZ or NRZ port, or at what speed....that's the job
of it interface.

At the present time ACSOPT supports two devices for collecting SWO
from the target;
 
* the Black Magic Debug Probe (BMP) and 
* generic USB TTL Serial Interfaces 

Information about using each individual interface can be found in the
docs directory.

When in NRZ mode the SWO data rate that comes out of the chip _must_
match the rate that the debugger expects. On the BMP speeds of
2.25Mbps are normal, TTL Serial devices tend to run a little slower
but 921600 baud is normally acheivable. On BMP the baudrate is set via
the gdb session with the 'monitor traceswo xxxx' command. For a TTL
Serial device its set by the ACSOPT command line.

Depending on what you're using to wake up SWO on the target, you
may need code to get it into the correct mode and emitting data. You
can do that via gdb direct memory accesses, or from program code.
 
An example for a STM32F103 (it'll be pretty much the same for any
other M3, but the dividers will be different);

    /* STM32 specific configuration to enable the TRACESWO IO pin */
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;
    AFIO->MAPR |= (2 << 24); // Disable JTAG to release TRACESWO
    DBGMCU->CR |= DBGMCU_CR_TRACE_IOEN; // Enable IO trace pins

    *((volatile unsigned *)(0xE0040010)) = 31;  // Output bits at 72000000/(31+1)=2.25MHz.
    *((volatile unsigned *)(0xE00400F0)) = 2;   // Use Async mode (1 for RZ/Manchester)
    *((volatile unsigned *)(0xE0040304)) = 0;   // Disable formatter

    /* Configure instrumentation trace macroblock */
    ITM->LAR = 0xC5ACCE55;
    ITM->TCR = 0x00010005;
    ITM->TER = 0xFFFFFFFF; // Enable all stimulus ports

Building
========

The command line to build the ACSOPT tool is;

gcc -I /usr/local/include/libusb-1.0 -L /usr/local/lib  -lusb-1.0 swolisten.c -o swolisten

...you will obviously need to change the paths to your libusb files.

Using
=====

By default the tool will create fifos for the first 32 channels in a
directory swo (which you will need to create) as follows; 

>ls swo/
chan00	chan02	chan04	chan06	chan08	chan0A	chan0C	chan0E	chan10	chan12	chan14	chan16	chan18	chan1A	chan1C	chan1E
chan01	chan03	chan05	chan07	chan09	chan0B	chan0D	chan0F	chan11	chan13	chan15	chan17	chan19	chan1B	chan1D	chan1F

>cat swo/channel0
<<OUTPUT FROM ITM Channel 0>>

Information about command line options can be found with the -h
option.  ACSOPT is specifically designed to be 'hardy' to probe and
target disconnects and restarts (y'know, like you get in the real
world). The intention being to give you streams whenever it can get
them.  It does _not_ require gdb to be running, but you may need a 
gdb session to start the output.  BMP needs traceswo to be turned on
at the command line before it capture data from the port, for example.

Reliability
===========

A whole chunk of work has gone into making sure the dataflow over the
SWO link is reliable....but it's pretty dependent on the debug
interface itself.  The TL;DR is that if the interface _is_ reliable
then ACSOPT will be. There are factors outside of our control
(i.e. the USB bus you connect to) that could potentially break the
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

(On inspection, the last line of recorded data was indeed a 'D' line).

