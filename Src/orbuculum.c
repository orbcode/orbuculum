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

#include "nwclient.h"

#ifdef INCLUDE_FPGA_SUPPORT
    #define FPGA_MAX_FRAMES (0x1ff)

    #include <libftdi1/ftdi.h>
    #include "ftdispi.h"
    #define IF_INCLUDE_FPGA_SUPPORT(...) __VA_ARGS__
    #define FPGA_SERIAL_INTERFACE_SPEED (12000000)

    #ifdef FPGA_UART_FEEDER
        #define EFFECTIVE_DATA_TRANSFER_SPEED ((FPGA_SERIAL_INTERFACE_SPEED/10)*8)
        #define FPGA_HS_TRANSFER_SIZE (512)
    #endif

    #ifdef FPGA_SPI_FEEDER
        #define FTDI_INTERFACE (INTERFACE_A)
        #define FTDI_INTERFACE_SPEED CLOCK_MAX_SPEEDX5
        #define EFFECTIVE_DATA_TRANSFER_SPEED (21600000)
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
    bool autodiscover;
    uint8_t iface;
    uint8_t ep;
    char *name;
} _deviceList[] =
{
    { 0x1209, 0x3443, true,  0, 0x81, "Orbtrace"         },
    { 0x1d50, 0x6018, false, 5, 0x85, "Blackmagic Probe" },
    { 0x2b3e, 0xc610, false, 3, 0x85, "Phywhisperer-UDT" },
    { 0, 0, 0, 0, 0 }
};

#ifndef TRANSFER_SIZE
    #define TRANSFER_SIZE (4096)
#endif
#define TRANSFER_BUFFER_COUNT (256)

//#define DUMP_BLOCK

/* Record for options, either defaults or from command line */
struct
{
    /* Config information */
    bool segger;                                         /* Using a segger debugger */

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
    int listenPort;                                      /* Listening port for network */
} options =
{
    .listenPort = NWCLIENT_SERVER_PORT,
    .seggerHost = SEGGER_HOST,
#ifdef INCLUDE_FPGA_SUPPORT
    .orbtraceWidth = 4
#endif
};

struct dataBlock
{
    uint32_t fillLevel;
    uint8_t buffer[TRANSFER_SIZE];
};

struct
{
    /* Link to the network client subsystem */
    struct nwclientsHandle *n;

    /* Link to the FPGA subsystem */
    IF_INCLUDE_FPGA_SUPPORT( struct ftdi_context *ftdi );              /* Connection materials for ftdi fpga interface */
    IF_INCLUDE_FPGA_SUPPORT( struct ftdispi_context ftdifsc );

    uint64_t  intervalBytes;                                           /* Number of bytes transferred in current interval */

    pthread_t intervalThread;                                          /* Thread reporting on intervals */
    bool      ending;                                                  /* Flag indicating app is terminating */

    /* Transfer buffers from the receiver */
    sem_t dataWaiting;
    uint32_t wp;
    uint32_t rp;
    struct dataBlock bufSet[TRANSFER_BUFFER_COUNT];
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

    genericsPrintf( "Usage: %s <hv> <s name:number> <f filename>  <p port> <a speed>" EOL, progName );
    genericsPrintf( "        a: <serialSpeed> to use" EOL );
    genericsPrintf( "        e: When reading from file, terminate at end of file rather than waiting for further input" EOL );
    genericsPrintf( "        f: <filename> Take input from specified file" EOL );
    genericsPrintf( "        h: This help" EOL );
    genericsPrintf( "        l: <port> Listen port for the incoming connections (defaults to %d)" EOL, NWCLIENT_SERVER_PORT );
    genericsPrintf( "        m: <interval> Output monitor information about the link at <interval>ms" EOL );
    IF_INCLUDE_FPGA_SUPPORT( genericsPrintf( "        o: <num> Use traceport FPGA custom interface with 1, 2 or 4 bits width" EOL ) );
    genericsPrintf( "        p: <serialPort> to use" EOL );
    genericsPrintf( "        s: <Server>:<Port> to use" EOL );
    genericsPrintf( "        v: <level> Verbose mode 0(errors)..3(debug)" EOL );
}
// ====================================================================================================
int _processOptions( int argc, char *argv[] )

{
    int c;
#define DELIMITER ','

    while ( ( c = getopt ( argc, argv, "a:ef:hl:m:no:p:s:v:" ) ) != -1 )
        switch ( c )
        {
            // ------------------------------------
            case 'a':
                options.speed = atoi( optarg );
                options.dataSpeed = options.speed;
                break;

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

            case 'l':
                options.listenPort = atoi( optarg );
                break;

            // ------------------------------------

            case 'm':
                options.intervalReportTime = atoi( optarg );
                break;

                // ------------------------------------

#ifdef INCLUDE_FPGA_SUPPORT

            case 'o':
                // Generally you need TPIU for orbtrace
                options.orbtrace = true;
                options.orbtraceWidth = atoi( optarg );
                break;
#endif

            // ------------------------------------

            case 'p':
                options.port = optarg;
                break;

            // ------------------------------------

            case 's':
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
            case 'v':
                genericsSetReportLevel( atoi( optarg ) );
                break;

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

        snapInterval *= 8;
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

        genericsPrintf( C_RESET EOL );
    }

    return NULL;
}
// ====================================================================================================
static void _processBlock( int s, unsigned char *cbw )

/* Generic block processor for received data */

{
    genericsReport( V_DEBUG, "RXED Packet of %d bytes" EOL, s );


    if ( s )
    {
        /* Account for this reception */
        _r.intervalBytes += s;

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
        nwclientSend( _r.n, s, cbw );
    }

}
// ====================================================================================================
int usbFeeder( void )

{
    uint8_t cbw[TRANSFER_SIZE];
    libusb_device_handle *handle = NULL;
    libusb_device *dev;
    int32_t size;
    const struct deviceList *p;
    uint8_t iface;
    uint8_t ep;
    uint8_t altsetting = 0;
    uint8_t num_altsetting = 0;
    int32_t err;

    while ( !_r.ending )
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

        iface = p->iface;
        ep = p->ep;

        if ( p->autodiscover )
        {
            genericsReport( V_DEBUG, "Searching for trace interface" EOL );

            struct libusb_config_descriptor *config;

            if ( ( err = libusb_get_active_config_descriptor( dev, &config ) ) < 0 )
            {
                genericsReport( V_WARN, "Failed to get config descriptor (%d)" EOL, err );
                continue;
            }

            bool interface_found = false;

            for ( int if_num = 0; if_num < config->bNumInterfaces && !interface_found; if_num++ )
            {
                for ( int alt_num = 0; alt_num < config->interface[if_num].num_altsetting && !interface_found; alt_num++ )
                {
                    const struct libusb_interface_descriptor *i = &config->interface[if_num].altsetting[alt_num];

                    if (
                                i->bInterfaceClass != 0xff ||
                                i->bInterfaceSubClass != 0x54 ||
                                ( i->bInterfaceProtocol != 0x00 && i->bInterfaceProtocol != 0x01 ) ||
                                i->bNumEndpoints != 0x01 )
                    {
                        continue;
                    }

                    iface = i->bInterfaceNumber;
                    altsetting = i->bAlternateSetting;
                    num_altsetting = config->interface[if_num].num_altsetting;
                    ep = i->endpoint[0].bEndpointAddress;

                    genericsReport( V_DEBUG, "Found interface %#x with altsetting %#x and ep %#x" EOL, iface, altsetting, ep );

                    interface_found = true;
                }
            }

            if ( !interface_found )
            {
                genericsReport( V_DEBUG, "No supported interfaces found, falling back to hardcoded values" EOL );
            }

            libusb_free_config_descriptor( config );
        }

        if ( ( err = libusb_claim_interface ( handle, iface ) ) < 0 )
        {
            genericsReport( V_WARN, "Failed to claim interface (%d)" EOL, err );
            continue;
        }

        if ( num_altsetting > 1 && ( err = libusb_set_interface_alt_setting ( handle, iface, altsetting ) ) < 0 )
        {
            genericsReport( V_WARN, "Failed to set altsetting (%d)" EOL, err );
        }

        genericsReport( V_DEBUG, "USB Interface claimed, ready for data" EOL );

        while ( !_r.ending )
        {
            int32_t r = libusb_bulk_transfer( handle, ep, cbw, TRANSFER_SIZE, ( int * )&size, 10 );

            if ( ( r < 0 ) && ( r != LIBUSB_ERROR_TIMEOUT ) )
            {
                genericsReport( V_INFO, "USB data collection failed with error %d" EOL, r );
                break;
            }

            _processBlock( size, cbw );
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

            _processBlock( t, cbw );
        }

        if ( !_r.ending )
        {
            genericsReport( V_INFO, "fpga Read failed" EOL );
        }

        close( f );
    }

    return 0;
}
#endif
// ====================================================================================================
#ifdef FPGA_SPI_FEEDER

int fpgaFeeder( void )

{
    int f;
    uint8_t cbw[FTDI_HS_TRANSFER_SIZE];
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
                                    cbw,
                                    ( readableFrames + 2 ) * FTDI_PACKET_SIZE,
                                    FPGA_AWAKE );

            if ( t < 0 )
            {
                break;
            }

            genericsReport( V_DEBUG, "RXED frame of %d packets" EOL, readableFrames );

            /* We deliberately include the first element in the transfer so there's a frame sync (0xfffffff7) in the flow */
            _processBlock( ( readableFrames + 1 )*FTDI_PACKET_SIZE, cbw );

            /* Get pointer to after last valid frame */
            uint8_t *s = &cbw[ ( readableFrames + 1 ) * FTDI_PACKET_SIZE ];

            /* Final protocol frame should contain number of frames available in next run */
            if ( ( *s != 0xA6 ) || ( *( s + 12 ) != 0xff ) || ( *( s + 13 ) != 0xff ) ||
                    ( *( s + 14 ) != 0xff ) || ( *( s + 15 ) != 0x7f ) )
            {
                genericsReport( V_INFO, "Protocol error" EOL );

                /* Resetting readable frames will allow us to restart the protocol */
                readableFrames = 0;
            }
            else
            {
                readableFrames = ( *( s + 2 ) ) + 256 * ( *( s + 1 ) );

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

    nwclientShutdown( _r.n );
    /* Give them a bit of time, then we're leaving anyway */
    usleep( 200 );
}
// ====================================================================================================
int main( int argc, char *argv[] )

{
    /* Setup fifos with forced ITM sync, no TPIU and TPIU on channel 1 if its engaged later */

    if ( !_processOptions( argc, argv ) )
    {
        /* processOptions generates its own error messages */
        genericsExit( -1, "" EOL );
    }

    /* Make sure the network clients get removed at the end */
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

    if ( !( _r.n = nwclientStart( options.listenPort ) ) )
    {
        genericsExit( -1, "Failed to make network server" EOL );
    }

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
