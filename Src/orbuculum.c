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
    #include <libftdi1/ftdi.h>
    #include "ftdispi.h"
    #define IF_INCLUDE_FPGA_SUPPORT(...) __VA_ARGS__
    #define FTDI_VID  (0x0403)
    #define FTDI_PID  (0x6010)
    #define FTDI_INTERFACE (INTERFACE_B)
    #define FPGA_INTERFACE_SPEED (12000000)
    #define FPGA_HS_TRANSFER_SIZE (512)
    #define DUMP_FTDI_BYTES // Uncomment to get data dump of bytes from FTDI transfer
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
    IF_INCLUDE_FPGA_SUPPORT( bool feederExit );                        /* Do we need to leave now? */
    IF_INCLUDE_FPGA_SUPPORT( struct ftdi_context *ftdi );              /* Connection materials for ftdi fpga interface */
    IF_INCLUDE_FPGA_SUPPORT( struct ftdispi_context ftdifsc );

    uint64_t  intervalBytes;                                           /* Number of bytes transferred in current interval */
    pthread_t intervalThread;                                          /* Thread reporting on intervals */
    bool      ending;                                                  /* Flag indicating app is terminating */
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

    const unsigned int speed1[] =
    {
        B115200, B230400, 0, B460800, B576000,
        0, 0, B921600, 0, B1152000
    };
    const unsigned int speed2[] =
    {
        B500000,  B1000000, B1500000, B2000000,
        B2500000, B3000000, B3500000, B4000000
    };
    int speed_ok = 0;

    if ( ( speed % 500000 ) == 0 )
    {
        // speed is multiple of 500000, use speed2 table.
        int i = speed / 500000;

        if ( i <= 8 )
        {
            speed_ok = speed2[i - 1];
        }
    }
    else if ( ( speed % 115200 ) == 0 )
    {
        int i = speed / 115200;

        if ( i <= 10 && speed1[i - 1] )
        {
            speed_ok = speed2[i - 1];
        }
    }

    if ( speed_ok )
    {
        settings.c_cflag |= speed_ok;
    }
    else
    {
        settings.c_cflag |= BOTHER;
        settings.c_ispeed = speed;
        settings.c_ospeed = speed;
    }

    // Ensure input baud is same than output.
    settings.c_cflag |= ( settings.c_cflag & CBAUD ) << IBSHIFT;
    // Now configure port.
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

    if ( speed_ok )
    {
        if ( ( settings.c_cflag & CBAUD ) != speed_ok )
        {
            genericsReport( V_WARN, "Fail to set baudrate" EOL );
        }
    }
    else
    {
        if ( ( settings.c_ispeed != speed ) || ( settings.c_ospeed != speed ) )
        {
            genericsReport( V_WARN, "Fail to set baudrate" EOL );
        }
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
                    genericsReport( V_ERROR, "%c" EOL, c );
                    return false;
                    // ------------------------------------
            }

#ifdef INCLUDE_FPGA_SUPPORT

    if ( ( options.orbtrace ) && !( ( options.orbtraceWidth == 1 ) || ( options.orbtraceWidth == 2 ) || ( options.orbtraceWidth == 4 ) ) )
    {
        genericsReport( V_ERROR, "Orbtrace interface illegal port width" EOL );
        return false;
    }

#endif

    /* ... and dump the config if we're being verbose */
    genericsReport( V_INFO, "Orbuculum V" VERSION " (Git %08X %s, Built " BUILD_DATE ")" EOL, GIT_HASH, ( GIT_DIRTY ? "Dirty" : "Clean" ) );
    IF_WITH_FIFOS( genericsReport( V_INFO, "BasePath   : %s" EOL, fifoGetChanPath( _r.f ) ) );
    IF_WITH_FIFOS( genericsReport( V_INFO, "ForceSync  : %s" EOL, fifoGetForceITMSync( _r.f ) ? "true" : "false" ) );
    IF_WITH_FIFOS( genericsReport( V_INFO, "Permafile  : %s" EOL, options.permafile ? "true" : "false" ) );

    if ( options.intervalReportTime )
    {
        genericsReport( V_INFO, "Report Intv : %dmS" EOL, options.intervalReportTime );
    }

    if ( options.port )
    {
        genericsReport( V_INFO, "Serial Port : %s" EOL "Serial Speed: %d" EOL, options.port, options.speed );
    }

    if ( options.seggerPort )
    {
        genericsReport( V_INFO, "SEGGER H&P : %s:%d" EOL, options.seggerHost, options.seggerPort );
    }

#ifdef INCLUDE_FPGA_SUPPORT

    if ( options.orbtrace )
    {
        genericsReport( V_INFO, "Orbtrace   : %d bits width" EOL, options.orbtraceWidth );
    }

#endif

#ifdef WITH_FIFOS

    if ( fifoGetUseTPIU( _r.f ) )
    {
        genericsReport( V_INFO, "Using TPIU : true (ITM on channel %d)" EOL, fifoGettpiuITMChannel( _r.f ) );
    }
    else
    {
        genericsReport( V_INFO, "Using TPIU : false" EOL );
    }

#endif

    if ( options.file )
    {
        genericsReport( V_INFO, "Input File : %s", options.file );

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
    genericsReport( V_INFO, "Channels   :" EOL );

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
        genericsPrintf( C_PREV_LN C_CLR_LN C_YELLOW );

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

        if ( options.speed > 100 )
        {
            /* Conversion to percentage done as a division to avoid overflow */
            uint32_t fullPercent = ( snapInterval * 100 ) / options.speed;
            genericsPrintf( "(" C_YELLOW " %3d%% " C_RESET "full)", ( fullPercent > 100 ) ? 100 : fullPercent );
        }

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
        IF_WITH_NWCLIENT( nwclientSend( _r.n, s, cbw ) );
#ifdef WITH_FIFOS
        unsigned char *c = cbw;

        while ( s-- )
        {
            fifoProtocolPump( _r.f, *c++ );
        }

#endif
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

        while ( true )
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

    while ( 1 )
    {
        sockfd = socket( AF_INET, SOCK_STREAM, 0 );
        setsockopt( sockfd, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof( flag ) );

        if ( sockfd < 0 )
        {
            genericsReport( V_ERROR, "Error creating socket" EOL );
            return -1;
        }

        while ( connect( sockfd, ( struct sockaddr * ) &serv_addr, sizeof( serv_addr ) ) < 0 )
        {
            usleep( 500000 );
        }

        genericsReport( V_INFO, "Established Segger Link" EOL );

        IF_WITH_FIFOS( fifoForceSync( _r.f, true ) );

        while ( ( t = read( sockfd, cbw, TRANSFER_SIZE ) ) > 0 )
        {
            _processBlock( t, cbw );
        }

        close( sockfd );

        genericsReport( V_INFO, "Lost Segger Link" EOL );
    }

    return -2;
}
// ====================================================================================================
int serialFeeder( void )
{
    int f, ret;
    unsigned char cbw[TRANSFER_SIZE];
    ssize_t t;

    while ( 1 )
    {
#ifdef OSX
        int flags;

        while ( ( f = open( options.port, O_RDONLY | O_NONBLOCK ) ) < 0 )
#else
        while ( ( f = open( options.port, O_RDONLY ) ) < 0 )
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

        while ( ( t = read( f, cbw, TRANSFER_SIZE ) ) > 0 )
        {
            _processBlock( t, cbw );
        }

        genericsReport( V_INFO, "Read failed" EOL );

        close( f );
    }
}
// ====================================================================================================
#ifdef INCLUDE_FPGA_SUPPORT

int fpgaFeeder( void )
{
    int f, ret;

    unsigned char cbw[FPGA_HS_TRANSFER_SIZE];
    ssize_t t;

    while ( 1 )
    {
#ifdef OSX
        int flags;

        while ( ( f = open( options.port, O_RDONLY | O_NONBLOCK ) ) < 0 )
#else
        while ( ( f = open( options.port, O_RDONLY ) ) < 0 )
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

        if ( ( ret = _setSerialConfig ( f, FPGA_INTERFACE_SPEED ) ) < 0 )
        {
            genericsExit( ret, "fpga setSerialConfig failed" EOL );
        }

        while ( ( t = read( f, cbw, FPGA_HS_TRANSFER_SIZE ) ) > 0 )
        {
#ifdef DUMP_FTDI_BYTES
            printf( "*** %d ***" EOL, t );
            uint8_t *c = cbw;
            uint32_t y = t;

            while ( y-- )
            {
                printf( "%02X ", *c++ );

                if ( !( y % 20 ) )
                {
                    printf( EOL );
                }
            }

            _processBlock( t, cbw );
#endif
        }

        genericsReport( V_INFO, "fpga Read failed" EOL );

        close( f );
    }
}
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
    usleep( 200000 );
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
