/*
 * SWO Splitter for Blackmagic Probe and TTL Serial Interfaces
 * ===========================================================
 *
 * Copyright (C) 2017, 2019  Dave Marples  <dave@marples.net>
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
    #define FTDI_INTERFACE (INTERFACE_A)
    #define FTDI_UART_INTERFACE (INTERFACE_B)
    #define FTDI_INTERFACE_SPEED CLOCK_MAX_SPEEDX5
    #define FTDI_PACKET_SIZE  (17)
    #define FTDI_NUM_FRAMES   (900)  // If you make this too large the driver drops frames
    #define FTDI_HS_TRANSFER_SIZE (FTDI_PACKET_SIZE*FTDI_NUM_FRAMES)
    #define FPGA_AWAKE (0x80)
    #define FPGA_ASLEEP (0x90)
    // #define DUMP_FTDI_BYTES // Uncomment to get data dump of bytes from FTDI transfer
#else
    #define IF_INCLUDE_FPGA_SUPPORT(...)
#endif

#define SEGGER_HOST "localhost"               /* Address to connect to SEGGER */
#define SEGGER_PORT (2332)

/* Descriptor information for BMP */
#define VID       (0x1d50)
#define PID       (0x6018)
#define INTERFACE (5)
#define ENDPOINT  (0x85)

#ifndef TRANSFER_SIZE
    #define TRANSFER_SIZE (4096)
#endif

/* Record for options, either defaults or from command line */
struct
{
    /* Config information */
    bool segger;                                         /* Using a segger debugger */

    /* FPGA Information */
    IF_INCLUDE_FPGA_SUPPORT( char *fwbasedir );          /* Where the firmware is stored */
    IF_INCLUDE_FPGA_SUPPORT( bool orbtrace; )            /* In trace mode? */
    IF_INCLUDE_FPGA_SUPPORT( uint32_t orbtraceWidth; )   /* Trace pin width */

    /* Source information */
    char *seggerHost;                                    /* Segger host connection */
    int32_t seggerPort;                                  /* ...and port */
    char *port;                                          /* Serial host connection */
    int speed;                                           /* Speed of serial link */
    char *file;                                          /* File host connection */

    /* Network link */
    IF_WITH_NWCLIENT( int listenPort );                  /* Listening port for network */
} options =
{
    .speed = 115200,
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
    IF_WITH_NWCLIENT( struct nwclientHandle *n );

    /* Link to the FPGA subsystem */
    IF_INCLUDE_FPGA_SUPPORT( bool feederExit );                        /* Do we need to leave now? */
    IF_INCLUDE_FPGA_SUPPORT( struct ftdi_context *ftdi );              /* Connection materials for ftdi fpga interface */
    IF_INCLUDE_FPGA_SUPPORT( struct ftdispi_context ftdifsc );
} _r;

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Private routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
static void _intHandler( int sig, siginfo_t *si, void *unused )

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
    fprintf( stdout, "        f: <filename> Take input from specified file" EOL );
    fprintf( stdout, "        h: This help" EOL );
    IF_WITH_FIFOS( fprintf( stdout, "        i: <channel> Set ITM Channel in TPIU decode (defaults to 1)" EOL ) );
    IF_WITH_NWCLIENT( fprintf( stdout, "        l: <port> Listen port for the incoming connections (defaults to %d)" EOL, NWCLIENT_SERVER_PORT ) );
    IF_WITH_FIFOS( fprintf( stdout, "        n: Enforce sync requirement for ITM (i.e. ITM needs to issue syncs)" EOL ) );
    IF_INCLUDE_FPGA_SUPPORT( fprintf( stdout, "        o: <num> Use traceport FPGA custom interface with 1, 2 or 4 bits width" EOL ) );
    fprintf( stdout, "        p: <serialPort> to use" EOL );
    fprintf( stdout, "        s: <address>:<port> Set address for SEGGER JLink connection (default none:%d)" EOL, SEGGER_PORT );
    IF_WITH_FIFOS( fprintf( stdout, "        t: Use TPIU decoder" EOL ) );
    fprintf( stdout, "        v: <level> Verbose mode 0(errors)..3(debug)" EOL );
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

    IF_WITH_NWCLIENT( while ( ( c = getopt ( argc, argv, "a:b:c:f:hl:o:p:s:v:" ) ) != -1 ) )
        IF_NOT_WITH_NWCLIENT( while ( ( c = getopt ( argc, argv, "a:b:c:f:ho:p:s:v:" ) ) != -1 ) )
#else
    IF_WITH_NWCLIENT( while ( ( c = getopt ( argc, argv, "a:f:hi:l:no:p:s:tv:" ) ) != -1 ) )
        IF_NOT_WITH_NWCLIENT( while ( ( c = getopt ( argc, argv, "a:f:hi:no:p:s:tv:" ) ) != -1 ) )
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
                    fifoSetChannel( _r.f, chan, chanName, GenericsUnescape( chanIndex ) );
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

#ifdef WITH_FIFOS

    /* Now perform sanity checks.... */
    if ( fifoGetUseTPIU( _r.f ) && ( !fifoGettpiuITMChannel( _r.f ) ) )
    {
        genericsReport( V_ERROR, "TPIU set for use but no channel set for ITM output" EOL );
        return false;
    }

#endif

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

#endif

    if ( options.file )
    {
        genericsReport( V_INFO, "Input File : %s" EOL, options.file );
    }

#ifdef WITH_FIFOS
    genericsReport( V_INFO, "Channels   :" EOL );

    for ( int g = 0; g < NUM_CHANNELS; g++ )
    {
        if ( fifoGetChannelName( _r.f, g ) )
        {
            genericsReport( V_INFO, "         %02d [%s] [%s]" EOL, g, GenericsEscape( fifoGetChannelFormat( _r.f, g ) ? : "RAW" ), fifoGetChannelName( _r.f, g ) );
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
static void _processBlock( int s, unsigned char *cbw )

/* Generic block processor for received data */

{
    genericsReport( V_DEBUG, "RXED Packet of %d bytes" EOL, s );

    IF_WITH_NWCLIENT( nwclientSend( _r.n, s, cbw ) );

#ifdef WITH_FIFOS
    unsigned char *c = cbw;

    while ( s-- )
    {
        fifoProtocolPump( _r.f, *c++ );
    }

#endif

}
// ====================================================================================================
int usbFeeder( void )

{

    unsigned char cbw[TRANSFER_SIZE];
    libusb_device_handle *handle;
    libusb_device *dev;
    int size;
    int32_t err;

    while ( 1 )
    {
        if ( libusb_init( NULL ) < 0 )
        {
            genericsReport( V_ERROR, "Failed to initalise USB interface" EOL );
            return ( -1 );
        }

        genericsReport( V_INFO, "Opening USB Device" EOL );

        /* Snooze waiting for the device to appear .... this is useful for when they come and go */
        while ( !( handle = libusb_open_device_with_vid_pid( NULL, VID, PID ) ) )
        {
            usleep( 500000 );
        }

        genericsReport( V_INFO, "USB Device opened" EOL );

        if ( !( dev = libusb_get_device( handle ) ) )
        {
            /* We didn't get the device, so try again in a while */
            continue;
        }

        if ( ( err = libusb_claim_interface ( handle, INTERFACE ) ) < 0 )
        {
            genericsReport( V_ERROR, "Failed to claim interface (%d)" EOL, err );
            return 0;
        }

        int32_t r;

        genericsReport( V_INFO, "USB Interface claimed, ready for data" EOL );

        while ( true )
        {
            r = libusb_bulk_transfer( handle, ENDPOINT, cbw, TRANSFER_SIZE, &size, 10 );

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
    int f;
    int t = 0;
    uint8_t cbw[FTDI_HS_TRANSFER_SIZE];
    uint8_t scratchBuffer[FTDI_HS_TRANSFER_SIZE - FTDI_NUM_FRAMES];
    uint8_t *c;
    uint8_t *d;
    uint8_t initSequence[] = {0xA5, 0xAC};

    // Insert appropriate control byte to set port width for trace
    initSequence[1] = ( char[] )
    {
        0xA0, 0xA4, 0xAC, 0xAC
    }[options.orbtraceWidth - 1];

    // FTDI Chip takes a little while to reset itself
    // usleep( 400000 );

    _r.feederExit = false;

    while ( !_r.feederExit )
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
        while ( ( f < 0 ) && ( !_r.feederExit ) );

        genericsReport( V_INFO, "Port opened" EOL );
        f = ftdispi_open( &_r.ftdifsc, _r.ftdi, FTDI_INTERFACE );

        if ( f < 0 )
        {
            genericsReport( V_ERROR, "Cannot open spi %d (%s)" EOL, f, ftdi_get_error_string( _r.ftdi ) );
            return -2;
        }

        ftdispi_setmode( &_r.ftdifsc, 1, 0, 1, 0, 0, FPGA_AWAKE );

        ftdispi_setloopback( &_r.ftdifsc, 0 );

        f = ftdispi_setclock( &_r.ftdifsc, FTDI_INTERFACE_SPEED );

        if ( f < 0 )
        {
            genericsReport( V_ERROR, "Cannot set clockrate %d %d (%s)" EOL, f, FTDI_INTERFACE_SPEED, ftdi_get_error_string( _r.ftdi ) );
            return -2;
        }

        genericsReport( V_INFO, "All parameters configured" EOL );

        IF_WITH_FIFOS( fifoForceSync( _r.f, true ) );

        while ( ( !_r.feederExit ) && ( ( t = ftdispi_write_read( &_r.ftdifsc, initSequence, 2, cbw, FTDI_HS_TRANSFER_SIZE, FPGA_AWAKE ) ) >= 0 ) )
        {
            c = cbw;
            d = scratchBuffer;

            for ( f = 0; f < FTDI_NUM_FRAMES; f++ )
            {
                if ( ( *c ) & 0x80 )
                {
                    // This frame has no useful data
                    c += FTDI_PACKET_SIZE;
                    continue;
                }
                else
                {
                    // This frame contains something - copy and feed it, excluding header byte
                    memcpy( d, &c[1], FTDI_PACKET_SIZE - 1 );
                    d += FTDI_PACKET_SIZE - 1;

                    for ( uint32_t e = 1; e < FTDI_PACKET_SIZE; e++ )
                    {
#ifdef DUMP_FTDI_BYTES
                        printf( "%02X ", c[e] );
#endif
                        IF_WITH_FIFOS( fifoProtocolPump( _r.f, c[e] ) );
                    }

                    c += FTDI_PACKET_SIZE;
#ifdef DUMP_FTDI_BYTES
                    printf( "\n" );
#endif
                }
            }

            genericsReport( V_WARN, "RXED frame of %d/%d full packets (%3d%%)    \r",
                            ( d - scratchBuffer ) / ( FTDI_PACKET_SIZE - 1 ), FTDI_NUM_FRAMES, ( ( d - scratchBuffer ) * 100 ) / ( FTDI_HS_TRANSFER_SIZE - FTDI_NUM_FRAMES ) );

            IF_WITH_NWCLIENT( nwclientSend( _r.n, ( d - scratchBuffer ), scratchBuffer ) );
        }

        genericsReport( V_WARN, "Exit Requested (%d, %s)" EOL, t, ftdi_get_error_string( _r.ftdi ) );

        ftdispi_setgpo( &_r.ftdifsc, FPGA_ASLEEP );
        ftdispi_close( &_r.ftdifsc, 1 );
    }

    return 0;
}

// ====================================================================================================
void fpgaFeederClose( int dummy )

{
    ( void )dummy;
    _r.feederExit = true;
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

    genericsReport( V_INFO, "Reading from file" EOL );

    while ( ( t = read( f, cbw, TRANSFER_SIZE ) ) >= 0 )
    {

        if ( !t )
        {
            // Just spin for a while to avoid clogging the CPU
            usleep( 100000 );
            continue;
        }

        _processBlock( t, cbw );
    }

    genericsReport( V_INFO, "File read error" EOL );

    close( f );
    return true;
}
// ====================================================================================================
static void _doExit( void )

{
#ifdef WITH_FIFOS

    if ( _r.f )
    {
        fifoRemove( _r.f );
    }

#endif
}
// ====================================================================================================
int main( int argc, char *argv[] )

{
    sigset_t set;
    struct sigaction sa;

    IF_WITH_FIFOS( _r.f = fifoInit( ) );
    IF_WITH_FIFOS( assert( _r.f ) );

    IF_WITH_NWCLIENT( _r.n = nwclientInit( ) );
    IF_WITH_NWCLIENT( assert( _r.n ) );

    if ( !_processOptions( argc, argv ) )
    {
        /* processOptions generates its own error messages */
        genericsExit( -1, "" EOL );
    }

    /* Make sure the fifos get removed at the end */
    atexit( _doExit );

    /* This ensures the atexit gets called */
    sa.sa_flags = SA_SIGINFO;
    sigemptyset( &sa.sa_mask );
    sa.sa_sigaction = _intHandler;

    if ( sigaction( SIGINT, &sa, NULL ) == -1 )
    {
        genericsExit( -1, "Failed to establish Int handler" EOL );
    }

    /* Don't kill a sub-process when any reader or writer evaporates */
    sigemptyset( &set );
    sigaddset( &set, SIGPIPE );

    if ( pthread_sigmask( SIG_BLOCK, &set, NULL ) == -1 )
    {
        genericsExit( -1, "Failed to establish Int handler" EOL );
    }

#ifdef WITH_FIFOS

    if ( ! ( fifoCreate( _r.f ) ) )
    {
        genericsExit( -1, "Failed to make channel devices" EOL );
    }

#endif

#ifdef WITH_NWCLIENT

    if ( !nwclientStart( _r.n, options.listenPort ) )
    {
        genericsExit( -1, "Failed to make network server" EOL );
    }

#endif

#ifdef INCLUDE_FPGA_SUPPORT
    /* Start the filewriter */
    filewriterInit( options.fwbasedir );

    if ( options.orbtrace )
    {
        signal( SIGINT, fpgaFeederClose );
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
