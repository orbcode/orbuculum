/*
 * SWO Splitter for Blackmagic Probe and TTL Serial Interfaces
 * ===========================================================
 *
 * Copyright (C) 2017, 2019, 2020  Dave Marples  <dave@marples.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * * Neither the names Orbtrace, Orbuculum nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#if defined OSX
    #include <sys/ioctl.h>
    #include <libusb.h>
    #include <termios.h>
#else
    #if defined LINUX
        #include <libusb-1.0/libusb.h>
        #include <asm/ioctls.h>
        #if defined TCGETS2
            #include <asm/termios.h>
            /* Manual declaration to avoid conflict. */
            extern int ioctl ( int __fd, unsigned long int __request, ... ) __THROW;
        #else
            #include <sys/ioctl.h>
            #include <termios.h>
        #endif
    #else
        #error "Unknown OS"
    #endif
#endif
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>

#include "git_version_info.h"
#include "generics.h"
#include "fileWriter.h"

#ifdef WITH_FIFOS
    #include "fifos.h"
    #define IF_WITH_FIFOS(...) __VA_ARGS__
    #define IF_NOT_WITH_FIFOS(...)
#else
    #define IF_WITH_FIFOS(...)
    #define IF_NOT_WITH_FIFOS(...) __VA_ARGS__
#endif

#ifdef WITH_NWCLIENT
    #include "nwclient.h"
    #define IF_WITH_NWCLIENT(...) __VA_ARGS__
    #define IF_NOT_WITH_NWCLIENT(...)
#else
    #define IF_WITH_NWCLIENT(...)
    #define IF_NOT_WITH_NWCLIENT(...) __VA_ARGS__
#endif

#ifdef INCLUDE_FPGA_SUPPORT
    #define FPGA_MAX_FRAMES (0x1ff)

    #include <libftdi1/ftdi.h>
    #include "ftdispi.h"
    #define IF_INCLUDE_FPGA_SUPPORT(...) __VA_ARGS__
    #define FPGA_SERIAL_INTERFACE_SPEED (12000000)

    #ifdef FPGA_UART_FEEDER
        #define EFFECTIVE_DATA_TRANSFER_SPEED FPGA_SERIAL_INTERFACE_SPEED
        #define FPGA_HS_TRANSFER_SIZE (512)
    #endif

    #ifdef FPGA_SPI_FEEDER
        #define FTDI_INTERFACE (INTERFACE_A)
        #define FTDI_INTERFACE_SPEED CLOCK_MAX_SPEEDX5
        #define EFFECTIVE_DATA_TRANSFER_SPEED (27000000)
        #define FTDI_VID  (0x0403)
        #define FTDI_PID  (0x6010)
        #define FTDI_PACKET_SIZE  (16)
        #define FTDI_NUM_FRAMES   (511)  // If you make this too large the driver drops frames
        #define FTDI_HS_TRANSFER_SIZE (FTDI_PACKET_SIZE*(FTDI_NUM_FRAMES+2))
        #define FPGA_AWAKE  (0x80)
        #define FPGA_ASLEEP (0x90)
    #endif

#else
    #define IF_INCLUDE_FPGA_SUPPORT(...)
#endif

#define SEGGER_HOST "localhost"               /* Address to connect to SEGGER */
#define SEGGER_PORT (2332)

/* Table of known devices to try opening */
static const struct deviceList
{
    uint32_t vid;
    uint32_t pid;
    uint8_t iface;
    uint8_t ep;
    char *name;
} _deviceList[] =
{
    { 0x1d50, 0x6018, 5, 0x85, "Blackmagic Probe" },
    { 0x2b3e, 0xc610, 3, 0x85, "Phywhisperer-UDT" },
    { 0, 0, 0, 0 }
};

#ifndef TRANSFER_SIZE
    #define TRANSFER_SIZE (4096)
#endif
#define TRANSFER_BUFFER (200000)

//#define DUMP_BLOCK

/* Record for options, either defaults or from command line */
struct
{
    /* Config information */
    bool segger;                                         /* Using a segger debugger */
    IF_WITH_FIFOS( bool filewriter; )                    /* Supporting filewriter functionality */
    IF_WITH_FIFOS( char *fwbasedir; )                    /* Base directory for filewriter output */
    IF_WITH_FIFOS( bool permafile; )                     /* Use permanent files rather than fifos */

    /* FPGA Information */
    IF_INCLUDE_FPGA_SUPPORT( bool orbtrace; )            /* In trace mode? */
    IF_INCLUDE_FPGA_SUPPORT( uint32_t orbtraceWidth; )   /* Trace pin width */

    /* Source information */
    char *seggerHost;                                    /* Segger host connection */
    int32_t seggerPort;                                  /* ...and port */
    char *port;                                          /* Serial host connection */
    int speed;                                           /* Speed of serial link */
    uint32_t dataSpeed;                                  /* Effective data speed (can be less than link speed!) */
    char *file;                                          /* File host connection */
    bool fileTerminate;                                  /* Terminate when file read isn't successful */

    uint32_t intervalReportTime;                         /* If we want interval reports about performance */

    /* Network link */
    IF_WITH_NWCLIENT( int listenPort );                  /* Listening port for network */
} options =
{
    IF_WITH_NWCLIENT( .listenPort = NWCLIENT_SERVER_PORT, )
    .seggerHost = SEGGER_HOST,
#ifdef INCLUDE_FPGA_SUPPORT
    .orbtraceWidth = 4
#endif
};

struct
{
    /* Link to the fifo subsystem */
    IF_WITH_FIFOS( struct fifosHandle *f );

    /* Link to the network client subsystem */
    IF_WITH_NWCLIENT( struct nwclientsHandle *n );

    /* Link to the FPGA subsystem */
    IF_INCLUDE_FPGA_SUPPORT( struct ftdi_context *ftdi );              /* Connection materials for ftdi fpga interface */
    IF_INCLUDE_FPGA_SUPPORT( struct ftdispi_context ftdifsc );

    uint64_t  intervalBytes;                                           /* Number of bytes transferred in current interval */

    pthread_t bufferFeederHandle;                                      /* Handle to any established buffer feeder */

    pthread_t intervalThread;                                          /* Thread reporting on intervals */
    bool      ending;                                                  /* Flag indicating app is terminating */

    /* Transfer buffers from the receiver */
    sem_t dataWaiting;
    uint32_t wp;
    uint32_t rp;
    uint8_t buffer[TRANSFER_BUFFER];

} _r;

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Private routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
static void _intHandler( int sig )

{
    /* CTRL-C exit is not an error... */
    exit( 0 );
}
// ====================================================================================================
#if defined(LINUX) && defined (TCGETS2)
static int _setSerialConfig ( int f, speed_t speed )
{
    // Use Linux specific termios2.
    struct termios2 settings;
    int ret = ioctl( f, TCGETS2, &settings );

    if ( ret < 0 )
    {
        return ( -3 );
    }

    settings.c_iflag &= ~( ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF );
    settings.c_lflag &= ~( ICANON | ECHO | ECHOE | ISIG );
    settings.c_cflag &= ~PARENB; /* no parity */
    settings.c_cflag &= ~CSTOPB; /* 1 stop bit */
    settings.c_cflag &= ~CSIZE;
    settings.c_cflag &= ~( CBAUD | CIBAUD );
    settings.c_cflag |= CS8 | CLOCAL; /* 8 bits */
    settings.c_oflag &= ~OPOST; /* raw output */

    settings.c_cflag |= BOTHER;
    settings.c_ispeed = speed;
    settings.c_ospeed = speed;

    ret = ioctl( f, TCSETS2, &settings );

    if ( ret < 0 )
    {
        genericsReport( V_ERROR, "Unsupported baudrate" EOL );
        return ( -3 );
    }

    // Check configuration is ok.
    ret = ioctl( f, TCGETS2, &settings );

    if ( ret < 0 )
    {
        return ( -3 );
    }

    if ( ( settings.c_ispeed != speed ) || ( settings.c_ospeed != speed ) )
    {
        genericsReport( V_ERROR, "Failed to set baudrate" EOL );
        return -4;
    }

    // Flush port.
    ioctl( f, TCFLSH, TCIOFLUSH );
    return 0;
}
#else
static int _setSerialConfig ( int f, speed_t speed )
{
    struct termios settings;

    if ( tcgetattr( f, &settings ) < 0 )
    {
        perror( "tcgetattr" );
        return ( -3 );
    }

    if ( cfsetspeed( &settings, speed ) < 0 )
    {
        genericsReport( V_ERROR, "Error Setting input speed" EOL );
        return ( -3 );
    }

    settings.c_iflag &= ~( ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF );
    settings.c_lflag &= ~( ICANON | ECHO | ECHOE | ISIG );
    settings.c_cflag &= ~PARENB; /* no parity */
    settings.c_cflag &= ~CSTOPB; /* 1 stop bit */
    settings.c_cflag &= ~CSIZE;
    settings.c_cflag |= CS8 | CLOCAL; /* 8 bits */
    settings.c_oflag &= ~OPOST; /* raw output */

    if ( tcsetattr( f, TCSANOW, &settings ) < 0 )
    {
        genericsReport( V_ERROR, "Unsupported baudrate" EOL );
        return ( -3 );
    }

    tcflush( f, TCOFLUSH );
    return 0;
}
#endif

// ====================================================================================================
void _printHelp( char *progName )

{

    IF_WITH_FIFOS( fprintf( stdout, "Usage: %s <hntv> <s name:number> <b basedir> <f filename>  <i channel> <p port> <a speed>" EOL, progName ) );
    IF_NOT_WITH_FIFOS( fprintf( stdout, "Usage: %s <hv> <s name:number> <f filename>  <p port> <a speed>" EOL, progName ) );
    fprintf( stdout, "        a: <serialSpeed> to use" EOL );
    IF_WITH_FIFOS( fprintf( stdout, "        b: <basedir> for channels" EOL ) );
    IF_WITH_FIFOS( fprintf( stdout, "        c: <Number>,<Name>,<Format> of channel to populate (repeat per channel)" EOL ) );
    fprintf( stdout, "        e: When reading from file, terminate at end of file rather than waiting for further input" EOL );
    fprintf( stdout, "        f: <filename> Take input from specified file" EOL );
    fprintf( stdout, "        h: This help" EOL );
    IF_WITH_FIFOS( fprintf( stdout, "        i: <channel> Set ITM Channel in TPIU decode (defaults to 1)" EOL ) );
    IF_WITH_NWCLIENT( fprintf( stdout, "        l: <port> Listen port for the incoming connections (defaults to %d)" EOL, NWCLIENT_SERVER_PORT ) );
    fprintf( stdout, "        m: <interval> Output monitor information about the link at <interval>ms" EOL );
    IF_WITH_FIFOS( fprintf( stdout, "        n: Enforce sync requirement for ITM (i.e. ITM needs to issue syncs)" EOL ) );
    IF_INCLUDE_FPGA_SUPPORT( fprintf( stdout, "        o: <num> Use traceport FPGA custom interface with 1, 2 or 4 bits width" EOL ) );
    fprintf( stdout, "        p: <serialPort> to use" EOL );
    IF_WITH_FIFOS( fprintf( stdout, "        P: Create permanent files rather than fifos" EOL ) );
    fprintf( stdout, "        s: <address>:<port> Set address for SEGGER JLink connection (default none:%d)" EOL, SEGGER_PORT );
    IF_WITH_FIFOS( fprintf( stdout, "        t: Use TPIU decoder" EOL ) );
    fprintf( stdout, "        v: <level> Verbose mode 0(errors)..3(debug)" EOL );
    IF_WITH_FIFOS( fprintf( stdout, "        w: <path> Enable filewriter functionality using specified base path" EOL ) );
    IF_WITH_FIFOS( fprintf( stdout, "        (Built with fifo support)" EOL ) );
    IF_NOT_WITH_FIFOS( fprintf( stdout, "        (Built without fifo support)" EOL ) );
}
// ====================================================================================================
int _processOptions( int argc, char *argv[] )

{
    int c;
#define DELIMITER ','

    IF_WITH_FIFOS( char *chanConfig );
    IF_WITH_FIFOS( char *chanName );
    IF_WITH_FIFOS( uint chan );
    IF_WITH_FIFOS( char *chanIndex );

#ifdef WITH_FIFOS

    IF_WITH_NWCLIENT( while ( ( c = getopt ( argc, argv, "a:b:c:ef:hi:l:m:no:p:Ps:tv:w:" ) ) != -1 ) )
        IF_NOT_WITH_NWCLIENT( while ( ( c = getopt ( argc, argv, "a:b:c:efi::hm:o:p:Ps:tv:w:" ) ) != -1 ) )
#else
    IF_WITH_NWCLIENT( while ( ( c = getopt ( argc, argv, "a:ef:hl:m:no:p:s:v:" ) ) != -1 ) )
        IF_NOT_WITH_NWCLIENT( while ( ( c = getopt ( argc, argv, "a:ef:hm:no:p:s:v:" ) ) != -1 ) )
#endif
            switch ( c )
            {
                // ------------------------------------
                case 'a':
                    options.speed = atoi( optarg );
                    options.dataSpeed = options.speed;
                    break;

                    // ------------------------------------
#ifdef WITH_FIFOS

                case 'b':
                    fifoSetChanPath( _r.f, optarg );
                    break;
#endif

                // ------------------------------------
                case 'e':
                    options.fileTerminate = true;
                    break;

                // ------------------------------------
                case 'f':
                    options.file = optarg;
                    break;

                // ------------------------------------
                case 'h':
                    _printHelp( argv[0] );
                    return false;

                    // ------------------------------------
#ifdef WITH_FIFOS

                case 'i':
                    fifoSettpiuITMChannel( _r.f, atoi( optarg ) );
                    break;
#endif
                    // ------------------------------------
#if WITH_NWCLIENT

                case 'l':
                    options.listenPort = atoi( optarg );
                    break;
#endif

                // ------------------------------------

                case 'm':
                    options.intervalReportTime = atoi( optarg );
                    break;

                    // ------------------------------------

#ifdef WITH_FIFOS

                case 'n':
                    fifoSetForceITMSync( _r.f, false );
                    break;
#endif
                    // ------------------------------------
#ifdef INCLUDE_FPGA_SUPPORT

                case 'o':
                    // Generally you need TPIU for orbtrace
                    IF_WITH_FIFOS( fifoSetUseTPIU( _r.f, true ) );
                    options.orbtrace = true;
                    options.orbtraceWidth = atoi( optarg );
                    break;
#endif

                // ------------------------------------

                case 'p':
                    options.port = optarg;
                    break;

                    // ------------------------------------

#ifdef WITH_FIFOS

                case 'P':
                    options.permafile = true;
                    break;
#endif

                // ------------------------------------
                case 's':
                    IF_WITH_FIFOS( fifoSetForceITMSync( _r.f, true ) );
                    options.seggerHost = optarg;

                    // See if we have an optional port number too
                    char *a = optarg;

                    while ( ( *a ) && ( *a != ':' ) )
                    {
                        a++;
                    }

                    if ( *a == ':' )
                    {
                        *a = 0;
                        options.seggerPort = atoi( ++a );
                    }

                    if ( !options.seggerPort )
                    {
                        options.seggerPort = SEGGER_PORT;
                    }

                    break;

                    // ------------------------------------
#ifdef WITH_FIFOS

                case 't':
                    fifoSetUseTPIU( _r.f, true );
                    break;
#endif

                // ------------------------------------
                case 'v':
                    genericsSetReportLevel( atoi( optarg ) );
                    break;

                    // ------------------------------------

#ifdef WITH_FIFOS

                case 'w':
                    options.filewriter = true;
                    options.fwbasedir = optarg;
                    break;

                // ------------------------------------

                /* Individual channel setup */
                case 'c':
                    chanIndex = chanConfig = strdup( optarg );
                    chan = atoi( optarg );

                    if ( chan >= NUM_CHANNELS )
                    {
                        genericsReport( V_ERROR, "Channel index out of range" EOL );
                        return false;
                    }

                    /* Scan for start of filename */
                    while ( ( *chanIndex ) && ( *chanIndex != DELIMITER ) )
                    {
                        chanIndex++;
                    }

                    if ( !*chanIndex )
                    {
                        genericsReport( V_ERROR, "No filename for channel %d" EOL, chan );
                        return false;
                    }

                    chanName = ++chanIndex;

                    /* Scan for format */
                    while ( ( *chanIndex ) && ( *chanIndex != DELIMITER ) )
                    {
                        chanIndex++;
                    }

                    if ( !*chanIndex )
                    {
                        genericsReport( V_WARN, "No output format for channel %d, output raw!" EOL, chan );
                        fifoSetChannel( _r.f, chan, chanName, NULL );
                        break;
                    }

                    *chanIndex++ = 0;
                    fifoSetChannel( _r.f, chan, chanName, genericsUnescape( chanIndex ) );
                    break;
#endif

                // ------------------------------------


                case '?':
                    if ( optopt == 'b' )
                    {
                        genericsReport( V_ERROR, "Option '%c' requires an argument." EOL, optopt );
                    }
                    else if ( !isprint ( optopt ) )
                    {
                        genericsReport( V_ERROR, "Unknown option character `\\x%x'." EOL, optopt );
                    }

                    return false;

                // ------------------------------------
                default:
                    genericsReport( V_ERROR, "Unrecognised option '%c'" EOL, c );
                    return false;
                    // ------------------------------------
            }

#ifdef INCLUDE_FPGA_SUPPORT

    if ( ( options.orbtrace ) && !( ( options.orbtraceWidth == 1 ) || ( options.orbtraceWidth == 2 ) || ( options.orbtraceWidth == 4 ) ) )
    {
        genericsReport( V_ERROR, "Orbtrace interface illegal port width" EOL );
        return false;
    }

    if ( ( options.orbtrace ) && ( !options.port ) )
    {
        genericsReport( V_ERROR, "Supporting serial port needs to be specified for orbtrace" EOL );
        return false;
    }

    /* Override link speed as primary capacity indicator for orbtrace case */
    if ( options.orbtrace )
    {
        options.dataSpeed = EFFECTIVE_DATA_TRANSFER_SPEED;
    }

#endif

    /* ... and dump the config if we're being verbose */
    genericsReport( V_INFO, "Orbuculum V" VERSION " (Git %08X %s, Built " BUILD_DATE ")" EOL, GIT_HASH, ( GIT_DIRTY ? "Dirty" : "Clean" ) );
    IF_WITH_FIFOS( genericsReport( V_INFO, "BasePath    : %s" EOL, fifoGetChanPath( _r.f ) ) );
    IF_WITH_FIFOS( genericsReport( V_INFO, "ForceSync   : %s" EOL, fifoGetForceITMSync( _r.f ) ? "true" : "false" ) );
    IF_WITH_FIFOS( genericsReport( V_INFO, "Permafile   : %s" EOL, options.permafile ? "true" : "false" ) );

    if ( options.intervalReportTime )
    {
        genericsReport( V_INFO, "Report Intv : %d mS" EOL, options.intervalReportTime );
    }

    if ( options.port )
    {
        genericsReport( V_INFO, "Serial Port : %s" EOL, options.port );
    }

    if ( options.speed )
    {
        genericsReport( V_INFO, "Serial Speed: %d baud" EOL, options.speed );
    }

    if ( options.dataSpeed )
    {
        genericsReport( V_INFO, "Max Data Rt : %d bps" EOL, options.dataSpeed );
    }

    if ( options.seggerPort )
    {
        genericsReport( V_INFO, "SEGGER H&P : %s:%d" EOL, options.seggerHost, options.seggerPort );
    }

#ifdef INCLUDE_FPGA_SUPPORT

    if ( options.orbtrace )
    {
        genericsReport( V_INFO, "Orbtrace    : %d bits width, ", options.orbtraceWidth );
#ifdef FPGA_SPI_FEEDER
        genericsReport( V_INFO, "SPI Feeder" EOL );
#elif defined(FPGA_UART_FEEDER)
        genericsReport( V_INFO, "UART Feeder" EOL );
#else
        genericsReport( V_INFO, "NO Feeder" EOL );
#endif
    }

#endif

#ifdef WITH_FIFOS

    if ( fifoGetUseTPIU( _r.f ) )
    {
        genericsReport( V_INFO, "Using TPIU  : true (ITM on channel %d)" EOL, fifoGettpiuITMChannel( _r.f ) );
    }
    else
    {
        genericsReport( V_INFO, "Using TPIU  : false" EOL );
    }

#endif

    if ( options.file )
    {
        genericsReport( V_INFO, "Input File  : %s", options.file );

        if ( options.fileTerminate )
        {
            genericsReport( V_INFO, " (Terminate on exhaustion)" EOL );
        }
        else
        {
            genericsReport( V_INFO, " (Ongoing read)" EOL );
        }
    }

#ifdef WITH_FIFOS
    genericsReport( V_INFO, "Channels    :" EOL );

    for ( int g = 0; g < NUM_CHANNELS; g++ )
    {
        if ( fifoGetChannelName( _r.f, g ) )
        {
            genericsReport( V_INFO, "         %02d [%s] [%s]" EOL, g, genericsEscape( fifoGetChannelFormat( _r.f, g ) ? : "RAW" ), fifoGetChannelName( _r.f, g ) );
        }
    }

    genericsReport( V_INFO, "         HW [Predefined] [" HWFIFO_NAME "]" EOL );
#endif

    if ( ( options.file ) && ( ( options.port ) || ( options.seggerPort ) ) )
    {
        genericsReport( V_ERROR, "Cannot specify file and port or Segger at same time" EOL );
        return false;
    }

    if ( ( options.port ) && ( options.seggerPort ) )
    {
        genericsReport( V_ERROR, "Cannot specify port and Segger at same time" EOL );
        return false;
    }

    return true;
}
// ====================================================================================================
void *_checkInterval( void *params )

/* Perform any interval reporting that may be needed */

{
    uint64_t snapInterval;

    while ( !_r.ending )
    {
        usleep( options.intervalReportTime * 1000 );

        /* Grab the interval and scale to 1 second */
        snapInterval = _r.intervalBytes * 1000 / options.intervalReportTime;
        _r.intervalBytes = 0;

        /* Async channel, so each byte is 10 bits */
        snapInterval *= 10;
        genericsPrintf( C_PREV_LN C_CLR_LN C_DATA );

        if ( snapInterval / 1000000 )
        {
            genericsPrintf( "%4d.%d " C_RESET "MBits/sec ", snapInterval / 1000000, ( snapInterval * 1 / 100000 ) % 10 );
        }
        else if ( snapInterval / 1000 )
        {
            genericsPrintf( "%4d.%d " C_RESET "KBits/sec ", snapInterval / 1000, ( snapInterval / 100 ) % 10 );
        }
        else
        {
            genericsPrintf( "  %4d " C_RESET " Bits/sec ", snapInterval );
        }

        if ( options.dataSpeed > 100 )
        {
            /* Conversion to percentage done as a division to avoid overflow */
            uint32_t fullPercent = ( snapInterval * 100 ) / options.dataSpeed;
            genericsPrintf( "(" C_DATA " %3d%% " C_RESET "full)", ( fullPercent > 100 ) ? 100 : fullPercent );
        }

#ifdef WITH_FIFOS
#ifdef INCLUDE_FPGA_SUPPORT

        if ( options.orbtrace )
        {
            struct TPIUCommsStats *c = fifoGetCommsStats( _r.f );
            genericsPrintf( C_RESET " LEDS: %s%s%s%s" C_RESET " Frames: "C_DATA "%u" C_RESET,
                            c->leds & 1 ? C_DATA_IND "d" : C_RESET "-",
                            c->leds & 2 ? C_TX_IND "t" : C_RESET "-",
                            c->leds & 0x20 ? C_OVF_IND "O" : C_RESET "-",
                            c->leds & 0x80 ? C_HB_IND "h" : C_RESET "-",
                            c->totalFrames );

            genericsReport( V_INFO, " Pending:%5d Lost:%5d",
                            c->pendingCount,
                            c->lostFrames );
        }

#endif
#endif
        genericsPrintf( C_RESET EOL );
    }

    return NULL;
}
// ====================================================================================================
static void _processBlock( int s, unsigned char *cbw )

/* Generic block processor for received data */

{
    genericsReport( V_DEBUG, "RXED Packet of %d bytes" EOL, s );

    /* Account for this reception */
    _r.intervalBytes += s;

    if ( s )
    {
#ifdef DUMP_BLOCK
        uint8_t *c = cbw;
        uint32_t y = s;

        fprintf( stderr, EOL );

        while ( y-- )
        {
            fprintf( stderr, "%02X ", *c++ );

            if ( !( y % 16 ) )
            {
                fprintf( stderr, EOL );
            }
        }

#endif
        IF_WITH_NWCLIENT( nwclientSend( _r.n, s, cbw ) );

#ifdef WITH_FIFOS

        while ( s-- )
        {
            fifoProtocolPump( _r.f, *cbw++ );
        }

#endif

    }

}
// ====================================================================================================
static void *_bufferFeeder( void *arg )

{
    /* Pump a block of data from the buffer into the various handlers. */
    uint32_t span;

    while ( !_r.ending )
    {
        sem_wait( &_r.dataWaiting );

        while ( _r.wp != _r.rp )
        {
            if ( _r.wp > _r.rp )
            {
                span = _r.wp - _r.rp;
            }
            else
            {
                span = TRANSFER_BUFFER - _r.rp;
            }

            IF_WITH_NWCLIENT( nwclientSend( _r.n, span, &_r.buffer[_r.rp] ) );

#ifdef WITH_FIFOS

            for ( uint32_t r = 0; r < span; r++ )
            {
                fifoProtocolPump( _r.f, _r.buffer[_r.rp + r] );
            }

#endif
            _r.rp = ( _r.rp + span ) % TRANSFER_BUFFER;
            _r.intervalBytes += span;
        }
    }

    return 0;
}
// ====================================================================================================
static void _submitBlock( uint32_t t, uint8_t *p )

{
    uint32_t writeLen, space;

    while ( t )
    {
        /* Spin until there is room in the buffer for the data */
        do
        {
            /* Amount of space in the buffer is size of the buffer take away what is used */
            space = TRANSFER_BUFFER - ( ( TRANSFER_BUFFER + _r.wp - _r.rp ) % TRANSFER_BUFFER );

            if ( space >= t )
            {
                break;
            }
            else
            {
                usleep( 100 );
            }
        }
        while ( true );

        /* Write just the bit we've got room for before the end of the circular buffer */
        if ( _r.wp + t > TRANSFER_BUFFER )
        {
            writeLen = TRANSFER_BUFFER - _r.wp;
        }
        else
        {
            writeLen = t;
        }

        memcpy( &_r.buffer[_r.wp], p, writeLen );
        t -= writeLen;
        p += writeLen;

        _r.wp = ( _r.wp + writeLen ) % TRANSFER_BUFFER;
    }

    /* Make sure the reader knows there are data here */
    sem_post( &_r.dataWaiting );
}
// ====================================================================================================
static void _createBufferFeeder( void )

{
    sem_init( &_r.dataWaiting, 0, 0 );

    if ( pthread_create( &( _r.bufferFeederHandle ), NULL, &_bufferFeeder, NULL ) )
    {
        genericsExit( -1, "Failed to create buffer feeder" EOL );
    }
}
// ====================================================================================================
static void _destroyBufferFeeder( void )

{
    if ( _r.bufferFeederHandle )
    {
        pthread_cancel( _r.bufferFeederHandle );
        _r.bufferFeederHandle = 0;
    }
}
// ====================================================================================================
int usbFeeder( void )

{
    unsigned char cbw[TRANSFER_SIZE];
    libusb_device_handle *handle = NULL;
    libusb_device *dev;
    int size;
    const struct deviceList *p;
    int32_t err;

    while ( 1 )
    {
        if ( libusb_init( NULL ) < 0 )
        {
            genericsReport( V_ERROR, "Failed to initalise USB interface" EOL );
            return ( -1 );
        }

        /* Snooze waiting for the device to appear .... this is useful for when they come and go */
        while ( 1 )
        {
            p = _deviceList;

            while ( p->vid != 0 )
            {
                genericsReport( V_DEBUG, "Looking for %s (%04x:%04x)" EOL, p->name, p->vid, p->pid );

                if ( ( handle = libusb_open_device_with_vid_pid( NULL, p->vid, p->pid ) ) )
                {
                    break;
                }

                p++;
            }

            if ( handle )
            {
                break;
            }

            /* Take a pause before looking again */
            usleep( 500000 );
        }

        genericsReport( V_INFO, "Found %s" EOL, p->name );

        if ( !( dev = libusb_get_device( handle ) ) )
        {
            /* We didn't get the device, so try again in a while */
            continue;
        }

        if ( ( err = libusb_claim_interface ( handle, p->iface ) ) < 0 )
        {
            genericsReport( V_WARN, "Failed to claim interface (%d)" EOL, err );
            continue;
        }

        int32_t r;

        genericsReport( V_DEBUG, "USB Interface claimed, ready for data" EOL );

        while ( !_r.ending )
        {
            r = libusb_bulk_transfer( handle, p->ep, cbw, TRANSFER_SIZE, &size, 10 );

            if ( ( r < 0 ) && ( r != LIBUSB_ERROR_TIMEOUT ) )
            {
                genericsReport( V_INFO, "USB data collection failed with error %d" EOL, r );
                break;
            }

            if ( size )
            {
                _processBlock( size, cbw );
            }
        }

        libusb_close( handle );
        genericsReport( V_INFO, "USB Interface closed" EOL );
    }

    return 0;
}
// ====================================================================================================
int seggerFeeder( void )

{
    int sockfd;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    uint8_t cbw[TRANSFER_SIZE];

    ssize_t t;
    int flag = 1;

    bzero( ( char * ) &serv_addr, sizeof( serv_addr ) );
    server = gethostbyname( options.seggerHost );

    if ( !server )
    {
        genericsReport( V_ERROR, "Cannot find host" EOL );
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    bcopy( ( char * )server->h_addr,
           ( char * )&serv_addr.sin_addr.s_addr,
           server->h_length );
    serv_addr.sin_port = htons( options.seggerPort );

    while ( !_r.ending )
    {
        sockfd = socket( AF_INET, SOCK_STREAM, 0 );
        setsockopt( sockfd, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof( flag ) );

        if ( sockfd < 0 )
        {
            genericsReport( V_ERROR, "Error creating socket" EOL );
            return -1;
        }

        while ( ( !_r.ending ) && ( connect( sockfd, ( struct sockaddr * ) &serv_addr, sizeof( serv_addr ) ) < 0 ) )
        {
            usleep( 500000 );
        }

        if ( _r.ending )
        {
            break;
        }

        genericsReport( V_INFO, "Established Segger Link" EOL );

        IF_WITH_FIFOS( fifoForceSync( _r.f, true ) );

        while ( !_r.ending && ( ( t = read( sockfd, cbw, TRANSFER_SIZE ) ) > 0 ) )
        {
            _processBlock( t, cbw );
        }

        close( sockfd );

        if ( ! _r.ending )
        {
            genericsReport( V_INFO, "Lost Segger Link" EOL );
        }
    }

    return -2;
}
// ====================================================================================================
int serialFeeder( void )
{
    int f, ret;
    unsigned char cbw[TRANSFER_SIZE];
    ssize_t t;

    while ( !_r.ending )
    {
#ifdef OSX
        int flags;

        while ( !_r.ending && ( f = open( options.port, O_RDONLY | O_NONBLOCK ) ) < 0 )
#else
        while ( !_r.ending && ( f = open( options.port, O_RDONLY ) ) < 0 )
#endif
        {
            genericsReport( V_WARN, "Can't open serial port" EOL );
            usleep( 500000 );
        }

        genericsReport( V_INFO, "Port opened" EOL );

#ifdef OSX
        /* Remove the O_NONBLOCK flag now the port is open (OSX Only) */

        if ( ( flags = fcntl( f, F_GETFL, NULL ) ) < 0 )
        {
            genericsExit( -3, "F_GETFL failed" EOL );
        }

        flags &= ~O_NONBLOCK;

        if ( ( flags = fcntl( f, F_SETFL, flags ) ) < 0 )
        {
            genericsExit( -3, "F_SETFL failed" EOL );
        }

#endif

        if ( ( ret = _setSerialConfig ( f, options.speed ) ) < 0 )
        {
            genericsExit( ret, "setSerialConfig failed" EOL );
        }

        while ( ( !_r.ending ) && ( t = read( f, cbw, TRANSFER_SIZE ) ) > 0 )
        {
            _processBlock( t, cbw );
        }

        if ( ! _r.ending )
        {
            genericsReport( V_INFO, "Read failed" EOL );
        }

        close( f );
    }

    return 0;
}
// ====================================================================================================
#ifdef INCLUDE_FPGA_SUPPORT

#ifdef FPGA_UART_FEEDER
int fpgaFeeder( void )
{
    int f, ret;

    unsigned char cbw[FPGA_HS_TRANSFER_SIZE];
    ssize_t t;

    assert( ( options.orbtraceWidth == 1 ) || ( options.orbtraceWidth == 2 ) || ( options.orbtraceWidth == 4 ) );
    uint8_t wwString[] = { 'w', 0xA0 | ( ( options.orbtraceWidth == 4 ) ? 3 : options.orbtraceWidth ) };

    _createBufferFeeder();

    while ( !_r.ending )
    {
#ifdef OSX
        int flags;

        while ( ( !_r.ending ) && ( f = open( options.port, O_RDWR | O_NONBLOCK ) ) < 0 )
#else
        while ( ( !_r.ending ) && ( f = open( options.port, O_RDWR ) ) < 0 )
#endif
        {
            genericsReport( V_WARN, "Can't open fpga serial port" EOL );
            usleep( 500000 );
        }

        genericsReport( V_INFO, "Port opened" EOL );

#ifdef OSX
        /* Remove the O_NONBLOCK flag now the port is open (OSX Only) */

        if ( ( flags = fcntl( f, F_GETFL, NULL ) ) < 0 )
        {
            genericsExit( -3, "F_GETFL failed" EOL );
        }

        flags &= ~O_NONBLOCK;

        if ( ( flags = fcntl( f, F_SETFL, flags ) ) < 0 )
        {
            genericsExit( -3, "F_SETFL failed" EOL );
        }

#endif


        if ( ( ret = _setSerialConfig ( f, FPGA_SERIAL_INTERFACE_SPEED ) ) < 0 )
        {
            genericsExit( ret, "fpga setSerialConfig failed" EOL );
        }

        if ( write ( f, wwString, sizeof( wwString ) ) < 0 )
        {
            genericsExit( ret, "Failed to set orbtrace width" EOL );
        }

        while ( !_r.ending )
        {
            t = read( f, cbw, FPGA_HS_TRANSFER_SIZE );

            if ( t < 0 )
            {
                break;
            }

            _submitBlock( t, cbw );
        }

        if ( !_r.ending )
        {
            genericsReport( V_INFO, "fpga Read failed" EOL );
        }

        close( f );
    }

    _destroyBufferFeeder();
    return 0;
}
#endif
// ====================================================================================================
#ifdef FPGA_SPI_FEEDER

int fpgaFeeder( void )

{
    int f;
    uint8_t transferBlock[FTDI_HS_TRANSFER_SIZE];
    int t = 0;

    /* Init sequence is <INIT> <0xA0|BITS> <TFR-H> <TFR-L> */
    assert( ( options.orbtraceWidth == 1 ) || ( options.orbtraceWidth == 2 ) || ( options.orbtraceWidth == 4 ) );
    uint8_t initSequence[] = { 0xA5, 0xA0 | ( ( options.orbtraceWidth == 4 ) ? 3 : options.orbtraceWidth ), 0, 0 };
    uint32_t readableFrames = 0;

    // FTDI Chip takes a little while to reset itself
    usleep( 400000 );

#ifdef OSX
    int flags;

    while ( ( !_r.ending ) && ( f = open( options.port, O_RDONLY | O_NONBLOCK ) ) < 0 )
#else
    while ( !( _r.ending ) && ( f = open( options.port, O_RDONLY ) ) < 0 )
#endif
    {
        genericsReport( V_WARN, "Can't open fpga supporting serial port" EOL );
        usleep( 500000 );
    }


    while ( !_r.ending )
    {
        _r.ftdi = ftdi_new();
        ftdi_set_interface( _r.ftdi, FTDI_INTERFACE );

        do
        {
            f = ftdi_usb_open( _r.ftdi, FTDI_VID, FTDI_PID );

            if ( f < 0 )
            {
                genericsReport( V_WARN, "Cannot open device (%s)" EOL, ftdi_get_error_string( _r.ftdi ) );
                usleep( 50000 );
            }
        }
        while ( ( f < 0 ) && ( !_r.ending ) );

        genericsReport( V_INFO, "Port opened" EOL );
        f = ftdispi_open( &_r.ftdifsc, _r.ftdi, FTDI_INTERFACE );

        if ( f < 0 )
        {
            genericsReport( V_ERROR, "Cannot open spi %d (%s)" EOL, f, ftdi_get_error_string( _r.ftdi ) );
            return -2;
        }

        ftdispi_setmode( &_r.ftdifsc, /*CSH=*/1, /*CPOL =*/0, /*CPHA=*/0, /*LSB=*/0, /*BITMODE=*/0, FPGA_ASLEEP );

        ftdispi_setloopback( &_r.ftdifsc, 0 );

        f = ftdispi_setclock( &_r.ftdifsc, FTDI_INTERFACE_SPEED );

        if ( f < 0 )
        {
            genericsReport( V_ERROR, "Cannot set clockrate %d %d (%s)" EOL, f, FTDI_INTERFACE_SPEED, ftdi_get_error_string( _r.ftdi ) );
            return -2;
        }

        genericsReport( V_INFO, "All parameters configured" EOL );

        IF_WITH_FIFOS( fifoForceSync( _r.f, true ) );

        _createBufferFeeder();

        /* First time through, we need to read one frame. The way this protocol works is;    */
        /* Each frame that comes from the FPGA is 16 bytes long. A frame sync is an explicit */
        /* value that cannot appear in a frame (fffffff7) that is used to 'reset' the frame  */
        /* counter inside the TPIU Decoder. We can 'hide' data in incomplete packets in the  */
        /* flow...so we send a packet that goes;                                             */
        /*           A6 HH LL .. .. .. .. .. .. .. .. .. FF FF FF F7                         */
        /* at the end of a cluster of frames. That says (in HH LL) how many frames are *now* */
        /* in the buffer. In the next round we then collect that many frames. This minimises */
        /* the overhead of using the spi since we're only ever requesting frames that we     */
        /* know contain valid data.  We also prepend that same footer to the start of a      */
        /* cluster of frames - that way we know, independently of what has gone before, what */
        /* is in this cluster.                                                               */
        /* If you request too many frames (i.e. by overrunning the SPI) then you will just   */
        /* see repeats of the footer, and those will be ignored by the protocol decoder.     */
        /* If you _underrun_ then you might get the wrong number of frames next time around  */
        /* again, but it'll correct.                                                         */
        /* For the startup or reset case, we request zero frames, then we just get the header*/
        /* and footer, and the contents of the footer allow us to get into sync.             */
        /* (as a fringe benefit, we encode other data in the fake header too, but that is all*/
        /* decoded in the TPIU decoder, not here.                                            */

        /* SPI will wake up for duration of read, then back to sleep again */
        while ( !_r.ending )
        {
            /* Try and read some data */
            t = ftdispi_write_read( &_r.ftdifsc, initSequence, 4,
                                    transferBlock,
                                    ( readableFrames + 2 ) * FTDI_PACKET_SIZE,
                                    FPGA_AWAKE );

            if ( t < 0 )
            {
                break;
            }

            genericsReport( V_DEBUG, "RXED frame of %d packets" EOL, readableFrames );

            /* We deliberately include the first element in the transfer so there's a frame sync (0xfffffff7) in the flow */
            _submitBlock( ( readableFrames + 1 )*FTDI_PACKET_SIZE, transferBlock );

            /* Get pointer to after last valid frame */
            uint8_t *cbw = &transferBlock[ ( readableFrames + 1 ) * FTDI_PACKET_SIZE ];

            /* Final protocol frame should contain number of frames available in next run */
            if ( ( *cbw != 0xA6 ) || ( *( cbw + 12 ) != 0xff ) || ( *( cbw + 13 ) != 0xff ) ||
                    ( *( cbw + 14 ) != 0xff ) || ( *( cbw + 15 ) != 0x7f ) )
            {
                genericsReport( V_INFO, "Protocol error" EOL );

                /* Resetting readable frames will allow us to restart the protocol */
                readableFrames = 0;
            }
            else
            {
                readableFrames = ( *( cbw + 2 ) ) + 256 * ( *( cbw + 1 ) );

                /* Max out the readable frames in case it exceeds our buffers */
                readableFrames = ( readableFrames > FTDI_NUM_FRAMES ) ? FTDI_NUM_FRAMES : readableFrames;
                initSequence[2] = readableFrames / 256;
                initSequence[3] = readableFrames & 255;
            }
        }

        if ( !_r.ending )
        {
            genericsReport( V_WARN, "Exit Requested (%d, %s)" EOL, t, ftdi_get_error_string( _r.ftdi ) );
        }

        ftdispi_close( &_r.ftdifsc, 1 );
    }

    _destroyBufferFeeder();
    return 0;
}
#endif

#endif
// ====================================================================================================
int fileFeeder( void )

{
    int f;
    unsigned char cbw[TRANSFER_SIZE];
    ssize_t t;

    if ( ( f = open( options.file, O_RDONLY ) ) < 0 )
    {
        genericsExit( -4, "Can't open file %s" EOL, options.file );
    }

    while ( ( t = read( f, cbw, TRANSFER_SIZE ) ) >= 0 )
    {

        if ( !t )
        {
            if ( options.fileTerminate )
            {
                break;
            }
            else
            {
                // Just spin for a while to avoid clogging the CPU
                usleep( 100000 );
                continue;
            }
        }

        _processBlock( t, cbw );
    }

    if ( !options.fileTerminate )
    {
        genericsReport( V_INFO, "File read error" EOL );
    }

    close( f );
    return true;
}
// ====================================================================================================
static void _doExit( void )

{
    _r.ending = true;

    IF_WITH_FIFOS( fifoShutdown( _r.f ) );
    IF_WITH_NWCLIENT( nwclientShutdown( _r.n ) );
    /* Give them a bit of time, then we're leaving anyway */
    usleep( 200 );
}
// ====================================================================================================
int main( int argc, char *argv[] )

{
    /* Setup fifos with forced ITM sync, no TPIU and TPIU on channel 1 if its engaged later */
    IF_WITH_FIFOS( _r.f = fifoInit( true, false, 1 ) );
    IF_WITH_FIFOS( assert( _r.f ) );

    if ( !_processOptions( argc, argv ) )
    {
        /* processOptions generates its own error messages */
        genericsExit( -1, "" EOL );
    }

    IF_WITH_FIFOS( fifoUsePermafiles( _r.f, options.permafile ) );

    /* Make sure the fifos get removed at the end */
    atexit( _doExit );

    /* This ensures the atexit gets called */
    if ( SIG_ERR == signal( SIGINT, _intHandler ) )
    {
        genericsExit( -1, "Failed to establish Int handler" EOL );
    }

    /* Don't kill a sub-process when any reader or writer evaporates */
    if ( SIG_ERR == signal( SIGPIPE, SIG_IGN ) )
    {
        genericsExit( -1, "Failed to ignore SIGPIPEs" EOL );
    }

#ifdef WITH_FIFOS

    if ( ! ( fifoCreate( _r.f ) ) )
    {
        genericsExit( -1, "Failed to make channel devices" EOL );
    }

#endif

#ifdef WITH_NWCLIENT

    if ( !( _r.n = nwclientStart( options.listenPort ) ) )
    {
        genericsExit( -1, "Failed to make network server" EOL );
    }

#endif

    /* Start the filewriter */
    IF_WITH_FIFOS( fifoFilewriter( _r.f, options.filewriter, options.fwbasedir ) );

    if ( options.intervalReportTime )
    {
        pthread_create( &_r.intervalThread, NULL, &_checkInterval, NULL );
    }

#ifdef INCLUDE_FPGA_SUPPORT

    if ( options.orbtrace )
    {
        exit( fpgaFeeder() );
    }

#endif

    if ( options.seggerPort )
    {
        exit( seggerFeeder() );
    }

    if ( options.port )
    {
        exit( serialFeeder() );
    }

    if ( options.file )
    {
        exit( fileFeeder() );
    }

    exit( usbFeeder() );
}
// ====================================================================================================
