/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Orbuculum main receiver and TPIU/OFLOW demux
 * ============================================
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <assert.h>
#ifdef WIN32
    #include <winsock2.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netdb.h>
    #include <arpa/inet.h>
    #include <libgen.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <strings.h>
#include <string.h>
#include <getopt.h>
#if defined OSX
    #include <sys/ioctl.h>
    #include <libusb.h>
    #include <termios.h>
#elif defined LINUX
    #include <libusb-1.0/libusb.h>
    #include <asm/ioctls.h>
    #if defined TCGETS2
        #include <asm/termios.h>
        /* Manual declaration to avoid conflict. */
        extern int ioctl ( int __fd, unsigned long int __request, ... ) ;
    #else
        #include <sys/ioctl.h>
        #include <termios.h>
    #endif
#elif defined FREEBSD
    #include <libusb.h>
    #include <sys/ioctl.h>
    #include <termios.h>
#elif defined WIN32
    #include <libusb.h>
#else
    #error "Unknown OS"
#endif
#include <signal.h>

#include "git_version_info.h"
#include "generics.h"
#include "tpiuDecoder.h"
#include "oflow.h"
#include "nwclient.h"
#include "orbtraceIf.h"
#include "stream.h"

#ifndef O_BINARY
    #define O_BINARY 0
#endif

#define MAX_LINE_LEN (1024)
#define ORBTRACE "orbtrace"
#define ORBTRACEENVNAME "ORBTRACE"

/* Multiple blocks are used for USB, otherwise just the one */
#define NUM_RAW_BLOCKS (32)

/* File header for OFLOW formatted file */
#define OFLOW_SIG (const char*)"%%ORBFLOW1.0.0%%"
#define OFLOW_SIG_LEN (strlen(OFLOW_SIG))

/* Number of potential tags */
#define NUM_TAGS (256)
#define LAST_TAG_SEEN_TIME_NS (2L*1000*1000*1000)

/* Record of transferred data per tag */
struct TagDataCount
{
    bool     hasHandler;
    uint64_t ts;
    uint64_t totalData;
    uint64_t intervalData;
};

/* Record for options, either defaults or from command line */
struct Options
{
    /* Config information */
    bool nwserver;                                       /* Using a nw server source */

    /* Source information */
    char *nwserverHost;                                  /* NW Server host connection */
    int32_t nwserverPort;                                /* ...and port */
    char *port;                                          /* Serial host connection */
    int speed;                                           /* Speed of serial link */
    bool useTPIU;                                        /* Are we using TPIU, and stripping TPIU frames? */
    uint32_t dataSpeed;                                  /* Effective data speed (can be less than link speed!) */
    char *file;                                          /* File host connection */
    bool fileTerminate;                                  /* Terminate when file read isn't successful */
    char *outfile;                                       /* Output file for raw data dumping */
    char *otcl;                                          /* Orbtrace command line options */
    uint32_t intervalReportTime;                         /* If we want interval reports about performance */
    bool mono;                                           /* Supress colour in output */
    int paceDelay;                                       /* Delay between blocks of data transmission in file readout */
    char *channelList;                                   /* List of channels to be exported over legacy connection */
    bool hiresTime;                                      /* Use hiresolution time (shorter timeouts...more accurate but higher load */
    char *sn;                                            /* Any part serial number for identifying a specific device */
    int listenPort;                                      /* Listening port for network */
};

struct handlers
{
    int channel;                                         /* Channel number for this handler */
    struct dataBlock *strippedBlock;                     /* Processed buffers for output to clients */
    struct nwclientsHandle *n;                           /* Link to the network client subsystem */
};

struct RunTime
{
    struct TPIUDecoder t;                                /* TPIU decoder instance, in case we need it */
    struct OFLOW oflow;                                  /* OFLOW instance, in case we need it */

    struct OrbtraceIf  *o;                               /* For accessing ORBTrace devices + BMPs */

    uint64_t  intervalRawBytes;                          /* Number of bytes transferred in current interval */
    uint64_t lastInterval;                               /* Timestamp of previous interval */

    bool      ending;                                    /* Flag indicating app is terminating */
    bool      errored;                                   /* Flag indicating problem in reception process */
    bool      conn;                                      /* Flag indicating that we have a good connection */

    int f;                                               /* File handle to data source */

    int opFileHandle;                                    /* Handle if we're writing orb output locally */
    struct Options *options;                             /* Command line options (reference to above) */

    struct dataBlock rawBlock[NUM_RAW_BLOCKS];           /* Transfer buffers from the receiver */

    struct nwclientsHandle *oflowHandler;                /* Handle to OFLOW output handler */
    bool usingOFLOW;                                     /* Flag that OFLOW protocol is in use from the source */

    struct TagDataCount tagCount[NUM_TAGS];              /* Data carried per tag/TPIU channel */
    int numHandlers;                                     /* Number of TPIU channel handlers in use */
    struct handlers *handler;
    char *sn;                                            /* Serial number for any device we've established contact with */
};

#ifdef WIN32
    // https://stackoverflow.com/a/14388707/995351
    #define SO_REUSEPORT SO_REUSEADDR
#endif

#define NWSERVER_HOST "localhost"                        /* Address to connect to NW Server */
#define NWSERVER_PORT (2332)

#define NUM_OFLOW_CHANNELS 0x7F

#define INTERVAL_100US (100U)
#define INTERVAL_1MS   (10*INTERVAL_100US)
#define INTERVAL_100MS (100*INTERVAL_1MS)
#define INTERVAL_1S    (10*INTERVAL_100MS)

struct Options _options =
{
    .listenPort   = OTCLIENT_SERVER_PORT,
    .nwserverHost = NWSERVER_HOST,
    .channelList  = "1",
};

struct RunTime _r;

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Private routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================


#if defined(LINUX) && defined (TCGETS2)
// ====================================================================================================
// Linux Specific Drivers
// ====================================================================================================
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

#elif defined WIN32
// ====================================================================================================
// WIN32 Specific Driver
// ====================================================================================================

static bool _setSerialSpeed( HANDLE handle, int speed )
{
    DCB dcb;
    SecureZeroMemory( &dcb, sizeof( DCB ) );
    dcb.DCBlength = sizeof( DCB );
    BOOL ok = GetCommState( handle, &dcb );

    if ( !ok )
    {
        return false;
    }

    dcb.BaudRate = speed;
    dcb.ByteSize = 8;
    dcb.Parity   = NOPARITY;
    dcb.StopBits = ONESTOPBIT;

    ok = SetCommState( handle, &dcb );

    if ( !ok )
    {
        return false;
    }

    // Set timeouts
    COMMTIMEOUTS timeouts;
    ok = GetCommTimeouts( handle, &timeouts );

    if ( !ok )
    {
        return false;
    }

    timeouts.ReadIntervalTimeout         = 0;
    timeouts.ReadTotalTimeoutConstant    = 0;
    timeouts.ReadTotalTimeoutMultiplier  = 0;
    ok = SetCommTimeouts( handle, &timeouts );

    if ( !ok )
    {
        return false;
    }

    return true;
}

#else
// =========================================================================================================
// Default Drivers ( OSX and Linux without TCGETS2 )
// =========================================================================================================

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
static void _doExit( void )

{
    _r.ending = true;

    if ( _r.opFileHandle )
    {
        close( _r.opFileHandle );
        _r.opFileHandle = 0;
    }

    /* Need to nudge our own process in case it's stuck in a read or similar */
    _exit( 0 );
}
// ====================================================================================================
static void _intHandler( int sig )

{
    /* CTRL-C exit is not an error... */
    _doExit();
}
// ====================================================================================================
void _printHelp( const char *const progName, struct RunTime *r )

{
    genericsPrintf( "Usage: %s [options]" EOL, progName );
    genericsPrintf( "    -a, --serial-speed:  <serialSpeed> to use" EOL );
    genericsPrintf( "    -E, --eof:           When reading from file, terminate at end of file" EOL );
    genericsPrintf( "    -f, --input-file:    <filename> Take input from specified file" EOL );
    genericsPrintf( "    -h, --help:          This help" EOL );
    genericsPrintf( "    -H, --hires:         High resolution time (much higher CPU load though!)" EOL );
    genericsPrintf( "    -l, --listen-port:   <port> Listen port for incoming ORBFLOW connections (defaults to %d)" EOL, r->options->listenPort );
    genericsPrintf( "    -m, --monitor:       <interval> Output monitor information about the link at <interval>ms, min 500ms" EOL );
    genericsPrintf( "    -M, --no-colour:     Supress colour in output" EOL );
    genericsPrintf( "    -n, --serial-number: <Serial> any part of serial number to differentiate specific device" EOL );
    genericsPrintf( "    -o, --output-file:   <filename> to be used for dump file" EOL );
    genericsPrintf( "    -O, --orbtrace:      \"<options>\" run orbtrace with specified options on device connect" EOL );
    genericsPrintf( "    -p, --serial-port:   <serialPort> to use" EOL );
    genericsPrintf( "    -P, --pace:          <microseconds> delay in block of data transmission to clients" EOL );
    genericsPrintf( "    -s, --server:        <Server>:<Port> to use" EOL );
    genericsPrintf( "    -T, --tpiu:          Strip TPIU framing from input flows (mostly not relevant)" EOL );
    genericsPrintf( "    -t, --tag:           <stream,stream....> TPIU streams to decode and onward route (Default %s)" EOL, r->options->channelList );
    genericsPrintf( "    -v, --verbose:       <level> Verbose mode 0(errors)..3(debug)" EOL );
    genericsPrintf( "    -V, --version:       Print version, connected usb devices, and exit" EOL );
}

// ====================================================================================================
void _printVersion( struct RunTime *r )

{
    genericsPrintf( "orbuculum version " GIT_DESCRIBE EOL );
    r->o = OrbtraceIfCreateContext();
    int ndevices = OrbtraceIfGetDeviceList( r->o, NULL, DEVTYPE( DEVICE_ORBTRACE_MINI ) | DEVTYPE( DEVICE_BMP ) );

    if ( !ndevices )
    {
        genericsPrintf( "No devices found" EOL );
    }
    else
    {
        genericsPrintf( "Device%s Found;" EOL, ( ndevices > 1 ) ? "s" : "" );
        OrbtraceIfListDevices( r->o );
    }

    OrbtraceIfDestroyContext( r->o );
    r->o = NULL;
}
// ====================================================================================================
static struct option _longOptions[] =
{
    {"serial-speed", required_argument, NULL, 'a'},
    {"eof", no_argument, NULL, 'E'},
    {"input-file", required_argument, NULL, 'f'},
    {"help", no_argument, NULL, 'h'},
    {"hires", no_argument, NULL, 'H'},
    {"listen-port", required_argument, NULL, 'l'},
    {"monitor", required_argument, NULL, 'm'},
    {"no-colour", no_argument, NULL, 'M'},
    {"no-color", no_argument, NULL, 'M'},
    {"serial-number", required_argument, NULL, 'n'},
    {"output-file", required_argument, NULL, 'o'},
    {"orbtrace", required_argument, NULL, 'O'},
    {"serial-port", required_argument, NULL, 'p'},
    {"pace", required_argument, NULL, 'P'},
    {"server", required_argument, NULL, 's'},
    {"tpiu", required_argument, NULL, 'T'},
    {"tag", required_argument, NULL, 't'},
    {"verbose", required_argument, NULL, 'v'},
    {"version", no_argument, NULL, 'V'},
    {NULL, no_argument, NULL, 0}
};
// ====================================================================================================
bool _processOptions( int argc, char *argv[], struct RunTime *r )

{
    int c, optionIndex = 0;
#define DELIMITER ','

    while ( ( c = getopt_long ( argc, argv, "a:Ef:hHVl:m:Mn:o:O:p:P:s:Tt:v:", _longOptions, &optionIndex ) ) != -1 )
        switch ( c )
        {
            // ------------------------------------
            case 'a':
                r->options->speed = atoi( optarg );
                r->options->dataSpeed = r->options->speed;

                if ( r->options->speed <= 0 )
                {
                    genericsReport( V_ERROR, "Speed out of range" EOL );
                    return false;
                }

                break;

            // ------------------------------------

            case 'E':
                r->options->fileTerminate = true;
                break;

            // ------------------------------------
            case 'f':
                r->options->file = optarg;
                break;

            // ------------------------------------
            case 'h':
                _printHelp( argv[0], r );
                return false;

            // ------------------------------------
            case 'H':
                r->options->hiresTime = true;
                break;

            // ------------------------------------

            case 'V':
                _printVersion( r );
                return false;

            // ------------------------------------

            case 'l':
                r->options->listenPort = atoi( optarg );

                if ( ( r->options->listenPort <= 0 ) || ( r->options->listenPort > 0xffff ) )
                {
                    genericsReport( V_ERROR, "Port to listen on is out of range" EOL );
                    return false;
                }

                break;

            // ------------------------------------

            case 'm':
                r->options->intervalReportTime = atoi( optarg );

                if ( r->options->intervalReportTime<500 )
                {
                    genericsReport( V_ERROR, "intervalReportTime is out of range" EOL );
                    return false;
                }

                break;

            // ------------------------------------

            case 'M':
                r->options->mono = true;
                break;

            // ------------------------------------

            case 'n':
                r->options->sn = optarg;
                break;

            // ------------------------------------

            case 'o':
                r->options->outfile = optarg;
                break;

            // ------------------------------------

            case 'O':
                r->options->otcl = optarg;
                break;

            // ------------------------------------

            case 'p':
                r->options->port = optarg;
                break;

            // ------------------------------------

            case 'P':
                r->options->paceDelay = atoi( optarg );

                if ( r->options->paceDelay <= 0 )
                {
                    genericsReport( V_ERROR, "paceDelay is out of range" EOL );
                    return false;
                }

                break;

            // ------------------------------------

            case 's':
                r->options->nwserverHost = optarg;

                // See if we have an optional port number too
                char *a = optarg;

                while ( ( *a ) && ( *a != ':' ) )
                {
                    a++;
                }

                if ( *a == ':' )
                {
                    r->options->nwserverPort = atoi( ++a );
                }

                if ( !r->options->nwserverPort )
                {
                    r->options->nwserverPort = NWSERVER_PORT;
                }

                break;

            // ------------------------------------
            case 'T':
                r->options->useTPIU = true;
                break;

            // ------------------------------------
            case 't':
                r->options->channelList = optarg;
                break;

            // ------------------------------------
            case 'v':
                if ( !isdigit( *optarg ) )
                {
                    genericsReport( V_ERROR, "-v requires a numeric argument." EOL );
                    return false;
                }

                if ( !genericsSetReportLevel( atoi( optarg ) ) )
                {
                    genericsReport( V_ERROR, "Verbosity out of range" EOL );
                    return false;
                }

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

    /* ... and dump the config if we're being verbose */
    genericsReport( V_INFO, "orbuculum version " GIT_DESCRIBE EOL );

    if ( r->options->port )
    {
        /* For the base of a UART only 8 of 10 bits contain useful data */
        r->options->dataSpeed = ( r->options->dataSpeed * 8 ) / 10;
    }

    if ( r->options->intervalReportTime )
    {
        genericsReport( V_INFO, "Report Intv    : %d mS" EOL, r->options->intervalReportTime );
    }

    if ( r->options->port )
    {
        genericsReport( V_INFO, "Serial Port    : %s" EOL, r->options->port );
    }

    if ( r->options->speed )
    {
        genericsReport( V_INFO, "Serial Speed   : %d baud" EOL, r->options->speed );
    }

    if ( r->options->sn )
    {
        genericsReport( V_INFO, "Serial Number  : %s" EOL, r->options->sn );
    }

    if ( r->options->dataSpeed )
    {
        genericsReport( V_INFO, "Max Data Rt    : %d bps" EOL, r->options->dataSpeed );
    }

    if ( r->options->outfile )
    {
        genericsReport( V_INFO, "Raw Output file: %s" EOL, r->options->outfile );
    }

    if ( r->options->nwserverPort )
    {
        genericsReport( V_INFO, "NW Server      : %s:%d" EOL, r->options->nwserverHost, r->options->nwserverPort );
    }

    genericsReport( V_INFO, "Use/Strip TPIU : %s" EOL, r->options->useTPIU ? "True" : "False" );
    genericsReport( V_INFO, "Decode/Forward : %s" EOL, r->options->channelList ? r->options->channelList : "None" );

    if ( r->options->otcl )
    {
        genericsReport( V_INFO, "Orbtrace CL    : %s" EOL, r->options->otcl );
    }

    genericsReport( V_INFO, "OFLOW Port     : %d" EOL, r->options->listenPort );

    if ( r->options->file )
    {
        genericsReport( V_INFO, "Pace Delay     : %dus" EOL, r->options->paceDelay );
        genericsReport( V_INFO, "Input File  : %s", r->options->file );

        if ( r->options->fileTerminate )
        {
            genericsReport( V_INFO, " (Terminate on exhaustion)" EOL );
        }
        else
        {
            genericsReport( V_INFO, " (Ongoing read)" EOL );
        }
    }

    if ( r->options->hiresTime )
    {
        genericsReport( V_INFO, "High Res Time" EOL );
    }

    if ( ( r->options->file ) && ( ( r->options->port ) || ( r->options->nwserverPort ) ) )
    {
        genericsReport( V_ERROR, "Cannot specify file and port or NW Server at same time" EOL );
        return false;
    }

    if ( (     r->options->paceDelay ) && ( !r->options->file ) )
    {
        genericsReport( V_ERROR, "Pace Delay only makes sense when input is from a file" EOL );
        return false;
    }

    if ( ( r->options->useCOBS ) && ( r->options->useTPIU ) )
    {
        genericsReport( V_ERROR, "Cannot specify COBS and TPIU at the same time" EOL );
        return false;
    }

    if ( ( r->options->port ) && ( r->options->nwserverPort ) )
    {
        genericsReport( V_ERROR, "Cannot specify port and NW Server at same time" EOL );
        return false;
    }

    return true;
}
// ====================================================================================================
void _checkInterval( void *params )

/* Perform any interval reporting that may be needed */

{
    struct RunTime *r = ( struct RunTime * )params;
    struct timespec ts;
    uint64_t tnow;
    uint64_t snapInterval;
    int w;

    if ( r->options->intervalReportTime )
    {
        clock_gettime( CLOCK_REALTIME, &ts );
        tnow = ts.tv_sec * 1000000000L + ts.tv_nsec;

        if ( tnow - r->lastInterval >= r->options->intervalReportTime * 1000000L )
        {
            r->lastInterval = tnow;

            /* Grab the interval and scale to bits per 1 second */
            snapInterval = r->intervalRawBytes * 8000L / r->options->intervalReportTime;

            if ( r->conn )
            {
                genericsPrintf( C_PREV_LN C_DATA );

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

                uint64_t totalPct = 0;

                if ( r->intervalRawBytes )
                {
                    for ( int i = 0; i < NUM_TAGS; i++ )
                    {
                        w = 0;

                        if ( r->tagCount[i].intervalData )
                        {
                            w = ( r->tagCount[i].intervalData * 1000 ) / r->intervalRawBytes;
                            r->tagCount[i].ts = tnow;
                            totalPct += w;
                        }

                        if ( tnow - r->tagCount[i].ts < LAST_TAG_SEEN_TIME_NS )
                        {
                            if ( ( !r->tagCount[i].hasHandler ) && r->options->useTPIU )
                            {
                                genericsPrintf( C_NOCHAN" [%d:" "%3d%%] " C_RESET,  i, w / 10 );
                            }
                            else
                            {
                                genericsPrintf( " %d:" C_DATA"%3d%% " C_RESET,  i, w / 10 );
                            }
                        }

                        r->tagCount[i].intervalData = 0;
                    }

                    w = ( totalPct < 1000 ) ? 1000 - totalPct : 0;
                    genericsPrintf( " Waste:" C_DATA "%2d.%01d%% " C_RESET,  w / 10, w % 10 );
                }

                if ( r->options->dataSpeed > 100 )
                {
                    /* Conversion to percentage done as a division to avoid overflow */
                    uint32_t fullPercent = ( snapInterval * 100 ) / r->options->dataSpeed;
                    genericsPrintf( "(" C_DATA " %3d%% " C_RESET "full)", ( fullPercent > 100 ) ? 100 : fullPercent );
                }

                genericsPrintf( "   " C_RESET C_CLR_LN EOL );
            }

            r->intervalRawBytes = 0;
        }
    }
}
// ====================================================================================================
// Block decoders and handlers for the various line formats
// ====================================================================================================
static void _purgeBlock( struct RunTime *r, bool createOFLOW )

/* Send any packets to clients who want it, no matter where they originate from */

{
    struct Frame oflowOtg;
    struct handlers *h = r->handler;
    int i = r->numHandlers;

    while ( i-- )
    {
        if ( h->strippedBlock->fillLevel )
        {
            nwclientSend( h->n, h->strippedBlock->fillLevel, h->strippedBlock->buffer );

            if ( createOFLOW )
            {
                /* The OFLOW encoded version goes out on the combined OFLOW channel, with a specific channel header */
                int j = h->strippedBlock->fillLevel;
                const uint8_t *b = h->strippedBlock->buffer;

                while ( j )
                {
                    OFLOWEncode( h->channel, 0, b, ( j < OFLOW_MAX_PACKET_LEN ) ? j : OFLOW_MAX_PACKET_LEN, &oflowOtg );
                    nwclientSend( _r.oflowHandler, oflowOtg.len, oflowOtg.d );
                    b += ( j < OFLOW_MAX_PACKET_LEN ) ? j : OFLOW_MAX_PACKET_LEN;
                    j -= ( j < OFLOW_MAX_PACKET_LEN ) ? j : OFLOW_MAX_PACKET_LEN;
                }
            }

            h->strippedBlock->fillLevel = 0;
        }

        h++;
    }
}
// ====================================================================================================
static void _TPIUpacketRxed( enum TPIUPumpEvent e, struct TPIUPacket *p, void *param )

/* Callback for when a TPIU frame has been assembled */

{
    struct RunTime *r = ( struct RunTime * )param;

    struct handlers *h = NULL;
    int cachedChannel = -1;
    int chIndex = 0;

    switch ( e )
    {
        case TPIU_EV_RXEDPACKET:

            /* Iterate through the packet, putting it into the correct output buffers */
            for ( uint32_t g = 0; g < p->len; g++ )
            {
                if ( cachedChannel != p->packet[g].s )
                {
                    /* Whatever happens, cache this result */
                    cachedChannel = p->packet[g].s;

                    /* Search for channel */
                    h = r->handler;

                    for ( chIndex = 0; chIndex < r->numHandlers; chIndex++ )
                    {
                        if ( h->channel == p->packet[g].s )
                        {
                            break;
                        }

                        h++;
                    }
                }

                r->tagCount[p->packet[g].s].totalData++;
                r->tagCount[p->packet[g].s].intervalData++;

                if ( ( chIndex != r->numHandlers ) && ( h ) )
                {
                    /* We must have found a match for this at some point, so add it to the queue */
                    h->strippedBlock->buffer[h->strippedBlock->fillLevel++] = p->packet[g].d;
                }
                else
                {
                    genericsReport( V_DEBUG, "No handler for tag %d" EOL, p->packet[g].s );
                }
            }

            break;

        case TPIU_EV_ERROR:
            genericsReport( V_WARN, "****ERROR****%s" EOL, ( r->options->intervalReportTime ) ? EOL : "" );
            break;

        case TPIU_EV_NEWSYNC:
        case TPIU_EV_SYNCED:
        case TPIU_EV_RXING:
        case TPIU_EV_NONE:
        case TPIU_EV_UNSYNCED:
        default:
            break;
    }
}
// ====================================================================================================

static void _OFLOWpacketRxed( struct OFLOWFrame *p, void *param )

/* OFLOW packet received, account for it and reflect it to legacy buffers if needed */

{
    int chIndex;
    struct RunTime *r = ( struct RunTime * )param;
    struct handlers *h = _r.handler;

    if ( !p->good )
    {
        genericsReport( V_WARN, "Bad packet received" EOL );
    }
    else if ( ( r->options->useTPIU ) && ( h->channel == DEFAULT_ITM_STREAM ) )
    {
        /* Deal with the bizzare combination of OFLOW and TPIU in channel 1 */
        /* Accounting will be done in TPIUPump2 */
        TPIUPump2( &r->t, p->d, p->len, _TPIUpacketRxed, r );
    }
    else
    {
        /* Account for this reception */
        r->tagCount[p->tag].totalData += p->len;
        r->tagCount[p->tag].intervalData += p->len;

        /* Search for channel */
        for ( chIndex = 0; chIndex < r->numHandlers; chIndex++ )
        {
            if ( h->channel == p->tag )
            {
                break;
            }

            h++;
        }

        if ( ( chIndex != r->numHandlers ) && ( h ) )
        {
            /* We must have found a match for this at some point, so add it to the queue */
            for ( int i = 0; i < p->len; i++ )
            {
                h->strippedBlock->buffer[h->strippedBlock->fillLevel++] = p->d[i];

                if ( h->strippedBlock->fillLevel == sizeof( h->strippedBlock->buffer ) )
                {
                    /* We filled this block...better send it right now */
                    nwclientSend( h->n, h->strippedBlock->fillLevel, h->strippedBlock->buffer );
                    h->strippedBlock->fillLevel = 0;
                }
            }
        }
    }
}

// ====================================================================================================

static void _processNonOFLOWBlock( struct RunTime *r, ssize_t fillLevel, uint8_t *buffer )

/* Not an OFLOW block, so might be TPIU or clean ITM...deal with both */

{
    struct Frame oflowOtg;

    if ( fillLevel )
    {
        if ( r-> options->useTPIU )
        {
            /* Strip the TPIU framing from this input */
            TPIUPump2( &r->t, buffer, fillLevel, _TPIUpacketRxed, r );
        }
        else
        {
            /* Not TPIU ... need to assume this is ITM on the first channel..and assume it's present */
            r->tagCount[DEFAULT_ITM_STREAM].totalData += fillLevel;
            r->tagCount[DEFAULT_ITM_STREAM].intervalData += fillLevel;

            if ( r->handler )
            {
                nwclientSend( r->handler->n, fillLevel, buffer );
            }

            /* The OFLOW encoded version goes out on the default OFLOW channel */
            uint8_t *b = buffer;

            while ( fillLevel )
            {
                OFLOWEncode( DEFAULT_ITM_STREAM, 0, b,
                             ( fillLevel < OFLOW_MAX_PACKET_LEN ) ? fillLevel : OFLOW_MAX_PACKET_LEN,
                             &oflowOtg );
                nwclientSend( r->oflowHandler, oflowOtg.len, oflowOtg.d );
                b += ( fillLevel < OFLOW_MAX_PACKET_LEN ) ? fillLevel : OFLOW_MAX_PACKET_LEN;
                fillLevel -= ( fillLevel < OFLOW_MAX_PACKET_LEN ) ? fillLevel : OFLOW_MAX_PACKET_LEN;
            }
        }
    }
}
// ====================================================================================================
static void _handleBlock( struct RunTime *r, ssize_t fillLevel, uint8_t *buffer )

/* Handle an incoming block from any source in either 'conventional' or orbflow format */

{
    if ( fillLevel )
    {
        genericsReport( V_DEBUG, "RXED Packet of %d bytes%s" EOL, fillLevel, ( r->options->intervalReportTime ) ? EOL : "" );

        if ( r->opFileHandle )
        {
            if ( write( r->opFileHandle, buffer, fillLevel ) <= 0 )
            {
                genericsExit( -3, "Writing to file failed" EOL );
            }
        }

        if ( r->usingOFLOW )
        {
            if ( r->options->intervalReportTime )
            {
                /* We need to decode this so we can get the stats out of it .. we don't bother if we don't need stats */
                OFLOWPump( &r->oflow, buffer, fillLevel, _OFLOWpacketRxed, r );
            }

            /* ...and reflect this packet to the outgoing OFLOW channels, if we don't need to reconstruct them */
            if ( !r->options->useTPIU )
            {
                nwclientSend( r->oflowHandler, fillLevel, buffer );
            }
        }
        else
        {
            _processNonOFLOWBlock( r, fillLevel, buffer );
        }

        r->intervalRawBytes += fillLevel;

        /* Send the block to clients, but only send OFLOW if it wasn't OFLOW already */
        /* or if we're decoding TPIU in the default tag */
        _purgeBlock( r, ( !r->usingOFLOW ) || r->options->useTPIU );
    }

    _checkInterval( r );
}

// ====================================================================================================
// Generic handlers for each of the source types. These all call _handleBlock above to process.
// ====================================================================================================
static void _usb_callback( struct libusb_transfer *t )

/* For the USB case the ringbuffer isn't used .. packets are sent directly from this callback */

{
    /* Whatever the status that comes back, there may be data... */
    _handleBlock( &_r, t->actual_length, t->buffer );

    if ( ( t->status != LIBUSB_TRANSFER_COMPLETED ) &&
            ( t->status != LIBUSB_TRANSFER_TIMED_OUT ) &&
            ( t->status != LIBUSB_TRANSFER_CANCELLED )
       )
    {
        if ( !_r.errored )
        {
            genericsReport( V_WARN, "Errored out with status %d (%s)" EOL, t->status, libusb_error_name( t->status ) );
        }

        _r.errored = true;
    }
    else
    {
        if ( t->status != LIBUSB_TRANSFER_CANCELLED )
        {
            libusb_submit_transfer( t );
        }
    }
}

// ====================================================================================================

void _actionOrbtraceCommand( struct RunTime *r, char *sn, enum ORBTraceDevice d )

/* There is an orbtrace command line to be executed as part of the probe connect process */

{
    char commandLine[MAX_LINE_LEN];

    /* If we have any configuration to do on this device, go ahead */
    if ( r->options->otcl )
    {
        if ( getenv( ORBTRACEENVNAME ) )
        {
            snprintf( commandLine, MAX_LINE_LEN, "%s %s %s %s", getenv( ORBTRACEENVNAME ), r->options->otcl, sn ? ( ( d == DEVICE_ORBTRACE_MINI ) ? "-n " : "-s " ) : "", sn ? sn : "" );
        }
        else
        {
            char *baseDirectory = genericsGetBaseDirectory( );

            if ( !baseDirectory )
            {
                genericsExit( -1, "Failed to establish base directory" EOL );
            }

            snprintf( commandLine, MAX_LINE_LEN, "%s" ORBTRACE " %s %s %s", baseDirectory, r->options->otcl, sn ? ( ( d == DEVICE_ORBTRACE_MINI ) ? "-n " : "-s " ) : "", sn ? sn : "" );
            free( baseDirectory );
        }

        genericsReport( V_INFO, "%s" EOL, commandLine );

        if (  system( commandLine ) )
        {
            genericsReport( V_ERROR, "Invoking orbtrace failed" EOL );
        }
    }
}

// ====================================================================================================
static int _usbFeeder( struct RunTime *r )

/* Setup USB transfers from an ORBTrace or BMP */

{
    bool firstRunThrough = true;
    int workingDev;

    /* Copy any part serial number across */
    if ( r->options->sn )
    {
        r->sn = strdup( r->options->sn );
    }

    while ( !r->ending )
    {
        r->errored = false;

        /* ...just in case we had a context */
        OrbtraceIfDestroyContext( r->o );
        r->o = OrbtraceIfCreateContext();
        assert( r->o );

        while ( 0 == OrbtraceIfGetDeviceList( r->o, r->sn, DEVTYPE_ALL ) )
        {
            usleep( INTERVAL_1S );
        }

        genericsReport( V_INFO, "Found device" EOL );
        workingDev = OrbtraceIfSelectDevice( r->o );

        if ( !OrbtraceIfOpenDevice( r->o, workingDev ) )
        {
            genericsReport( V_INFO, "Couldn't open device" EOL );
            break;
        }

        /* Take a record of what device we're using...we'll use that next time around to re-connect */
        if ( r->sn )
        {
            free( r->sn );
        }

        r->sn = strdup( OrbtraceIfGetSN( r->o, workingDev ) );

        /* Before we open, perform any orbtrace configuration that is needed */
        _actionOrbtraceCommand( r, r->sn, OrbtraceIfGetDevtype( r->o, workingDev ) );

        if ( !OrbtraceGetIfandEP( r->o ) )
        {
            genericsReport( V_INFO, "Couldn't get IF and EP" EOL );
            break;
        }

        r->usingOFLOW = OrbtraceSupportsOFLOW( r->o );

        if ( r->usingOFLOW )
        {
            genericsReport( V_INFO, "Orbtrace supports ORBFLOW protocol" EOL );

            if ( r->options->useTPIU )
            {
                genericsReport( V_WARN, "TPIU decoding specified, but ORBTrace supports ORBFLOW, are you sure?" EOL );
            }

            if ( firstRunThrough && _r.opFileHandle )
            {
                if ( write( _r.opFileHandle, OFLOW_SIG, OFLOW_SIG_LEN ) < 0 )
                {
                    genericsExit( -4, "Could not write OFLOW signature to file (%s)" EOL, strerror( errno ) );
                }
            }

            /* We only attempt to write the file header on the first run through */
            firstRunThrough = false;
        }
        else
        {
            genericsReport( V_INFO, "Orbtrace supports legacy protocol" EOL );
        }

        genericsReport( V_DEBUG, "USB Interface claimed, ready for data" EOL );

        /* Create the USB transfer blocks .. if we are connected depends on if there was an error submitting the requests */
        r->errored = !( r->conn = OrbtraceIfSetupTransfers( r->o, r->options->hiresTime, r->rawBlock, NUM_RAW_BLOCKS, _usb_callback ) );

        /* =========================== The main dispatch loop ======================================= */
        while ( ( !r->ending )  && ( !r->errored ) )
        {
            int ret =   OrbtraceIfHandleEvents( r->o );

            if ( ( ret ) && ( ret != LIBUSB_ERROR_INTERRUPTED ) )
            {
                genericsReport( V_ERROR, "Error waiting for USB requests to complete %d" EOL, ret );
            }

        }

        /* ========================================================================================= */

        r->conn = false;

        /* Remove transfers from list and release the memory */
        OrbtraceIfCloseTransfers( r->o );

        if ( !r->ending )
        {
            genericsReport( V_INFO, "USB connection lost" EOL );
        }

        genericsReport( V_INFO, "USB Interface closed" EOL );
    }

    return 0;
}
// ====================================================================================================

static int _nwserverFeeder( struct RunTime *r )

/* Setup network based transfers (typically used for things like J-Link but can also be a legacy orbuculum session */

{
    struct dataBlock *rxBlock = &r->rawBlock[0];

    while ( true )
    {
        struct Stream *stream = streamCreateSocket( r->options->nwserverHost, r->options->nwserverPort );

        if ( stream == NULL )
        {
            continue;
        }

        genericsReport( V_INFO, "Established NW Server Link" EOL );

        r->conn = true;

        while ( !r->ending )
        {
            size_t fl;
            enum ReceiveResult result = stream->receive( stream, rxBlock->buffer, USB_TRANSFER_SIZE, NULL, &fl );
            rxBlock->fillLevel = fl;

            if ( result != RECEIVE_RESULT_OK )
            {
                break;
            }

            _handleBlock( r, rxBlock->fillLevel, rxBlock->buffer );
        }

        if ( !r->ending )
        {
            genericsReport( V_INFO, "Lost NW Server Link" EOL );
        }

        r->conn = false;
        free( stream );
    }

    return 0;
}
// ====================================================================================================


#ifdef WIN32
// ====================================================================================================
// WIN32 Specific Driver
// ====================================================================================================

static int _serialFeeder( struct RunTime *r )
{
    char portPath[MAX_PATH] = { 0 };
    snprintf( portPath, sizeof( portPath ), "\\\\.\\%s", r->options->port );

    while ( !r->ending )
    {
        HANDLE portHandle = CreateFile( portPath,
                                        GENERIC_READ,
                                        0,      //  must be opened with exclusive-access
                                        NULL,   //  default security attributes
                                        OPEN_EXISTING, //  must use OPEN_EXISTING
                                        0,    //  not overlapped I/O
                                        NULL ); //  hTemplate must be NULL for comm devices

        if ( portHandle == INVALID_HANDLE_VALUE )
        {
            genericsExit( 1, "Can't open serial port" EOL );
        }

        genericsReport( V_INFO, "Port opened" EOL );

        if ( !_setSerialSpeed( portHandle, r->options->speed ) )
        {
            genericsExit( 2, "setSerialConfig failed" EOL );
        }

        SetCommMask( portHandle, EV_RXCHAR );

        genericsReport( V_INFO, "Port configured" EOL );

        r->conn = true;

        while ( !r->ending )
        {
            DWORD eventMask = 0;
            WaitCommEvent( portHandle, &eventMask, NULL );
            DWORD unused;
            COMSTAT stats;
            ClearCommError( portHandle, &unused, &stats );

            if ( stats.cbInQue == 0 )
            {
                continue;
            }

            struct dataBlock *rxBlock = &r->rawBlock[0];

            DWORD transferSize = stats.cbInQue;

            if ( transferSize > USB_TRANSFER_SIZE )
            {
                transferSize = USB_TRANSFER_SIZE;
            }

            ReadFile( portHandle, rxBlock->buffer, transferSize, &rxBlock->fillLevel, NULL );

            if ( rxBlock->fillLevel <= 0 )
            {
                break;
            }

            _handleBlock( r, rxBlock->fillLevel, rxBlock->buffer );
        }

        r->conn = false;

        if ( ! r->ending )
        {
            genericsReport( V_INFO, "Read failed" EOL );
        }

        CloseHandle( portHandle );
    }

    return 0;
}

#else
// =========================================================================================================
// Default Driver ( OSX and Linux )
// =========================================================================================================

static int _serialFeeder( struct RunTime *r )

/* Setup incoming feed from a serial port */

{
    int ret;
    struct dataBlock *rxBlock = &r->rawBlock[0];

    while ( !r->ending )
    {
#ifdef OSX
        int flags;

        while ( !r->ending && ( r->f = open( r->options->port, O_RDONLY | O_NONBLOCK ) ) < 0 )
#else
        while ( !r->ending && ( r->f = open( r->options->port, O_RDONLY ) ) < 0 )
#endif
        {
            genericsReport( V_WARN, "Can't open serial port" EOL );
            usleep( INTERVAL_100MS );
        }

        genericsReport( V_INFO, "Port opened" EOL );

#ifdef OSX
        /* Remove the O_NONBLOCK flag now the port is open (OSX Only) */

        if ( ( flags = fcntl( r->f, F_GETFL, NULL ) ) < 0 )
        {
            genericsExit( -3, "F_GETFL failed" EOL );
        }

        flags &= ~O_NONBLOCK;

        if ( ( flags = fcntl( r->f, F_SETFL, flags ) ) < 0 )
        {
            genericsExit( -3, "F_SETFL failed" EOL );
        }

#endif

        if ( ( ret = _setSerialConfig ( r->f, r->options->speed ) ) < 0 )
        {
            genericsExit( ret, "setSerialConfig failed" EOL );
        }

        r->conn = true;

        while ( !r->ending )
        {
            if ( ( rxBlock->fillLevel = read( r->f, rxBlock->buffer, USB_TRANSFER_SIZE ) ) <= 0 )
            {
                break;
            }

            _handleBlock( r, rxBlock->fillLevel, rxBlock->buffer );
        }

        r->conn = false;

        if ( ! r->ending )
        {
            genericsReport( V_INFO, "Read failed" EOL );
        }

        close( r->f );
    }

    return 0;
}
#endif

// ====================================================================================================
static int _fileFeeder( struct RunTime *r )

/* Setup incoming data stream from a file in either legacy or OFLOW format */

{
    struct dataBlock *rxBlock = &r->rawBlock[0];


    if ( ( r->f = open( r->options->file, O_RDONLY ) ) < 0 )
    {
        genericsExit( -4, "Can't open file %s" EOL, r->options->file );
    }

    r->conn = true;

    /* Start off by checking if this is OFLOW formatted */
    rxBlock->fillLevel = read( r->f, rxBlock->buffer, OFLOW_SIG_LEN );
    r->usingOFLOW = ( ( OFLOW_SIG_LEN == rxBlock->fillLevel ) && ( !strncmp( OFLOW_SIG, ( char * )rxBlock->buffer, OFLOW_SIG_LEN ) ) );
    genericsReport( V_INFO, "File is %sin OFLOW format" EOL, ( r->usingOFLOW ) ? "" : "not " );

    if ( r->usingOFLOW )
    {
        /* This is OFLOW, so we need to read the first data after the header */
        rxBlock->fillLevel = read( r->f, rxBlock->buffer, USB_TRANSFER_SIZE );
    }

    while ( !r->ending )
    {
        if ( !rxBlock->fillLevel )
        {
            if ( r->options->fileTerminate )
            {
                break;
            }
            else
            {
                // Just spin for a while to avoid clogging the CPU
                usleep( INTERVAL_100MS );
                continue;
            }
        }

        _handleBlock( r, rxBlock->fillLevel, rxBlock->buffer );

        if ( r->options->paceDelay )
        {
            usleep( r->options->paceDelay );
        }

        rxBlock->fillLevel = read( r->f, rxBlock->buffer, USB_TRANSFER_SIZE );
    }

    r->conn = false;

    if ( !r->options->fileTerminate )
    {
        genericsReport( V_INFO, "File read error" EOL );
    }

    usleep( INTERVAL_1S );
    close( r->f );
    return true;
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Publicly available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================

int main( int argc, char *argv[] )

{
    struct timespec ts;

    /* This is set here to avoid huge .data section in startup image */
    _r.options = &_options;

#ifdef WIN32
    WSADATA wsaData;
    WSAStartup( MAKEWORD( 2, 2 ), &wsaData );
#endif

    if ( !_processOptions( argc, argv, &_r ) )
    {
        /* processOptions generates its own error messages */
        genericsExit( -1, "" EOL );
    }

    if ( _r.options->useTPIU )
    {
        TPIUDecoderInit( &_r.t );
    }

    OFLOWInit( &_r.oflow );

    genericsScreenHandling( !_r.options->mono );

    /* Make sure the network clients get removed at the end */
    atexit( _doExit );

    /* This ensures the atexit gets called */
    if ( SIG_ERR == signal( SIGINT, _intHandler ) )
    {
        genericsExit( -1, "Failed to establish Int handler" EOL );
    }

    /* Don't kill a sub-process when any reader or writer evaporates */
#if !defined WIN32

    if ( SIG_ERR == signal( SIGPIPE, SIG_IGN ) )
    {
        genericsExit( -1, "Failed to ignore SIGPIPEs" EOL );
    }

#endif

    if ( _r.options->channelList )
    {
        /* Channel list is only needed for legacy ports that we are re-exporting (i.e. clean unencapsulated flows) */
        char *c = _r.options->channelList;
        int x = 0;

        while ( *c )
        {
            while ( *c == ',' )
            {
                c++;
            }

            while ( isdigit( *c ) )
            {
                x = x * 10 + ( *c++ -'0' );
            }

            if ( ( *c ) && ( *c != ',' ) )
            {
                genericsExit( -1, "Illegal character in channel list (%c)" EOL, *c );
            }

            if ( x )
            {
                /* This is a good number, so open */
                if ( ( x < 0 ) || ( x >= NUM_OFLOW_CHANNELS ) )
                {
                    genericsExit( -1, "Channel number out of range" EOL );
                }

                _r.handler = ( struct handlers * )realloc( _r.handler, sizeof( struct handlers ) * ( _r.numHandlers + 1 ) );

                _r.handler[_r.numHandlers].channel = x;
                _r.handler[_r.numHandlers].strippedBlock = ( struct dataBlock * )calloc( 1, sizeof( struct dataBlock ) );
                _r.tagCount[x].hasHandler = true;
                _r.handler[_r.numHandlers].n = nwclientStart(  _r.options->listenPort + LEGACY_SERVER_PORT_OFS + _r.numHandlers );
                genericsReport( V_INFO, "Will decode tag %d, exported Legacy interface on port %d" EOL, x, _r.options->listenPort + LEGACY_SERVER_PORT_OFS + _r.numHandlers );

                _r.numHandlers++;
                x = 0;
            }
        }
    }

    /* The OFLOW handler doesn't need a channel list ... it works on all channels */
    _r.oflowHandler = nwclientStart( _r.options->listenPort );
    genericsReport( V_INFO, "Started Network interface for OFLOW on port %d" EOL, _r.options->listenPort );

    /* Don't do anything with interval times for at least the first interval time */
    clock_gettime( CLOCK_REALTIME, &ts );
    _r.lastInterval = ts.tv_sec * 1000000000L + ts.tv_nsec;

    if ( _r.options->outfile )
    {
        _r.opFileHandle = open( _r.options->outfile, O_CREAT | O_TRUNC | O_WRONLY | O_BINARY, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH );

        if ( _r.opFileHandle < 0 )
        {
            genericsReport( V_ERROR, "Could not open output file for writing" EOL );
            return -2;
        }
    }

    /* Blank line for tidyness' sake */
    genericsPrintf( EOL );

    if ( ( _r.options->nwserverPort ) || ( _r.options->port ) || ( _r.options->file ) )
    {
        if ( _r.options->nwserverPort )
        {
            exit( _nwserverFeeder( &_r ) );
        }

        if ( _r.options->port )
        {
            exit( _serialFeeder( &_r ) );
        }

        if ( _r.options->file )
        {
            exit( _fileFeeder( &_r ) );
        }
    }

    /* ...nothing else left, it must be usb (either ORBTrace or BMP) */
    exit( _usbFeeder( &_r ) );
}
// ====================================================================================================
