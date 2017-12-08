/*
 * SWO Splitter for Blackmagic Probe and TTL Serial Interfaces
 * ===========================================================
 *
 * Copyright (C) 2017  Dave Marples  <dave@marples.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#if defined OSX
    #include <libusb.h>
#else
    #if defined LINUX
        #include <libusb-1.0/libusb.h>
    #else
        #error "Unknown OS"
    #endif
#endif
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <termios.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#include "git_version_info.h"
#include "generics.h"
#include "tpiuDecoder.h"
#include "itmDecoder.h"
#include "fileWriter.h"

#ifdef INCLUDE_FPGA_SUPPORT
    #include <libftdi1/ftdi.h>
    #define FTDI_VID  (0x0403)
    #define FTDI_PID  (0x6010)
    #define FTDI_INTERFACE (INTERFACE_B)
#endif

#define SERVER_PORT 3443                      /* Server port definition */
#define SEGGER_HOST "localhost"               /* Address to connect to SEGGER */
#define SEGGER_PORT (2332)

/* Descriptor information for BMP */
#define VID       (0x1d50)
#define PID       (0x6018)
#define INTERFACE (5)
#define ENDPOINT  (0x85)

#define TRANSFER_SIZE (4096)
#define NUM_CHANNELS  32
#define HW_CHANNEL    (NUM_CHANNELS)         /* Make the hardware fifo on the end of the software ones */

#define MAX_STRING_LENGTH (100)              /* Maximum length that will be output from a fifo for a single event */

/* Information for an individual channel */
struct Channel
{
    char *chanName;                          /* Filename to be used for the fifo */
    char *presFormat;                        /* Format of data presentation to be used */
};

#define HWFIFO_NAME "hwevent"

/* Record for options, either defaults or from command line */
struct
{
    /* Config information */
    bool verbose;
    bool useTPIU;
    bool segger;
    bool forceITMSync;
    char *fwbasedir;

#ifdef INCLUDE_FPGA_SUPPORT
    bool orbtrace;
#endif
    char *seggerHost;
    int32_t seggerPort;

    uint32_t tpiuITMChannel;

    /* Sink information */
    struct Channel channel[NUM_CHANNELS + 1];
    char *chanPath;

    /* Source information */
    char *port;
    char *file;
    int speed;
} options =
{
    .forceITMSync = true,
    .chanPath = "",
    .speed = 115200,
    .useTPIU = false,
    .tpiuITMChannel = 1,
    .seggerHost = SEGGER_HOST
};


/* Runtime state */
struct channelRuntime
{
    int handle;
    pthread_t thread;
    char *fifoName;
};

/* List of any connected network clients */
struct nwClient

{
    int handle;
    pthread_t thread;
    struct nwClient *nextClient;
    struct nwClient *prevClient;
};

/* Informtation about each individual network client */
struct nwClientParams

{
    struct nwClient *client;
    int portNo;
    int listenHandle;
};

struct
{
    struct channelRuntime c[NUM_CHANNELS + 1];   /* Output for each channel */
    struct nwClient *firstClient;                /* Head of linked list of network clients */
    pthread_t ipThread;                          /* The listening thread for n/w clients */

    /* Timestamp info */
    uint64_t lastHWExceptionTS;

    /* The decoders and the packets from them */
    struct ITMDecoder i;
    struct ITMPacket h;
    struct TPIUDecoder t;
    struct TPIUPacket p;

#ifdef INCLUDE_FPGA_SUPPORT
    struct ftdi_context *ftdi;
#endif
} _r;

/* Structure for parameters passed to a software task thread */
struct _runThreadParams

{
    int portNo;
    int listenHandle;
};
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Mechanism for handling the set of Fifos
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
static void *_runFifo( void *arg )

/* This is the control loop for the channel fifos (for each software port) */

{
    struct _runThreadParams *params = ( struct _runThreadParams * )arg;
    struct ITMPacket p;
    uint32_t w;

    char constructString[MAX_STRING_LENGTH];
    int fifo;
    int readDataLen, writeDataLen;

    if ( mkfifo( _r.c[params->portNo].fifoName, 0666 ) < 0 )
    {
        return NULL;
    }

    /* Don't kill this sub-process when any reader or writer evaporates */
    signal( SIGPIPE, SIG_IGN );

    while ( 1 )
    {
        /* This is the child */
        fifo = open( _r.c[params->portNo].fifoName, O_WRONLY );

        while ( 1 )
        {
            /* ....get the packet */
            readDataLen = read( params->listenHandle, &p, sizeof( struct ITMPacket ) );

            if ( readDataLen != sizeof( struct ITMPacket ) )
            {
                return NULL;
            }

            /* Build 32 value the long way around to avoid type-punning issues */
            w = ( p.d[3] << 24 ) | ( p.d[2] << 16 ) | ( p.d[1] << 8 ) | ( p.d[0] );
            writeDataLen = snprintf( constructString, MAX_STRING_LENGTH, options.channel[params->portNo].presFormat, w );

            if ( write( fifo, constructString, ( writeDataLen < MAX_STRING_LENGTH ) ? writeDataLen : MAX_STRING_LENGTH ) <= 0 )
            {
                break;
            }
        }

        close( fifo );
    }

    return NULL;
}
// ====================================================================================================
static void *_runHWFifo( void *arg )

/* This is the control loop for the hardware fifo */

{
    struct _runThreadParams *params = ( struct _runThreadParams * )arg;
    int fifo;
    int readDataLen;
    uint8_t p[MAX_STRING_LENGTH];

    if ( mkfifo( _r.c[params->portNo].fifoName, 0666 ) < 0 )
    {
        return NULL;
    }

    /* Don't kill this sub-process when any reader or writer evaporates */
    signal( SIGPIPE, SIG_IGN );

    while ( 1 )
    {
        /* This is the child */
        fifo = open( _r.c[params->portNo].fifoName, O_WRONLY );

        while ( 1 )
        {
            /* ....get the packet */
            readDataLen = read( params->listenHandle, p, MAX_STRING_LENGTH );

            if ( write( fifo, p, readDataLen ) <= 0 )
            {
                break;
            }
        }

        close( fifo );
    }

    return NULL;
}
// ====================================================================================================
static bool _makeFifoTasks( void )

/* Create each sub-process that will handle a port */

{
    struct _runThreadParams *params;
    int f[2];

    /* Cycle through channels and create a fifo for each one that is enabled */
    for ( int t = 0; t < ( NUM_CHANNELS + 1 ); t++ )
    {
        if ( t < NUM_CHANNELS )
        {
            if ( options.channel[t].chanName )
            {
                /* This is a live software channel fifo */
                if ( pipe( f ) < 0 )
                {
                    return false;
                }

                fcntl( f[1], F_SETFL, O_NONBLOCK );
                _r.c[t].handle = f[1];

                params = ( struct _runThreadParams * )malloc( sizeof( struct _runThreadParams ) );
                params->listenHandle = f[0];
                params->portNo = t;

                _r.c[t].fifoName = ( char * )malloc( strlen( options.channel[t].chanName ) + strlen( options.chanPath ) + 2 );
                strcpy( _r.c[t].fifoName, options.chanPath );
                strcat( _r.c[t].fifoName, options.channel[t].chanName );

                if ( pthread_create( &( _r.c[t].thread ), NULL, &_runFifo, params ) )
                {
                    return false;
                }
            }
        }
        else
        {
            /* This is the hardware fifo channel */
            if ( pipe( f ) < 0 )
            {
                return false;
            }

            fcntl( f[1], F_SETFL, O_NONBLOCK );
            _r.c[t].handle = f[1];

            params = ( struct _runThreadParams * )malloc( sizeof( struct _runThreadParams ) );
            params->listenHandle = f[0];
            params->portNo = t;

            _r.c[t].fifoName = ( char * )malloc( strlen( HWFIFO_NAME ) + strlen( options.chanPath ) + 2 );
            strcpy( _r.c[t].fifoName, options.chanPath );
            strcat( _r.c[t].fifoName, HWFIFO_NAME );

            if ( pthread_create( &( _r.c[t].thread ), NULL, &_runHWFifo, params ) )
            {
                return false;
            }
        }
    }

    return true;
}
// ====================================================================================================
static void _removeFifoTasks( void )

/* Destroy the per-port sub-processes */

{
    for ( int t = 0; t < NUM_CHANNELS + 1; t++ )
    {
        if ( _r.c[t].handle > 0 )
        {
            close( _r.c[t].handle );
            unlink( _r.c[t].fifoName );
        }
    }
}
// ====================================================================================================
uint64_t _timestampuS( void )

{
    struct timeval te;
    gettimeofday( &te, NULL ); // get current time
    return ( te.tv_sec * 1000000LL + te.tv_usec ); // caculate microseconds
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Network server implementation for raw SWO feed
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
static void *_client( void *args )

/* Handle an individual network client account */

{
    struct nwClientParams *params = ( struct nwClientParams * )args;
    int readDataLen;
    uint8_t maxTransitPacket[TRANSFER_SIZE];

    while ( 1 )
    {
        readDataLen = read( params->listenHandle, maxTransitPacket, TRANSFER_SIZE );

        if ( ( readDataLen <= 0 ) || ( write( params->portNo, maxTransitPacket, readDataLen ) < 0 ) )
        {
            /* This port went away, so remove it */
            if ( options.verbose )
            {
                fprintf( stdout, "Connection dropped" EOL );
            }

            close( params->portNo );
            close( params->listenHandle );

            if ( params->client->prevClient )
            {
                params->client->prevClient->nextClient = params->client->nextClient;
            }
            else
            {
                _r.firstClient = params->client->nextClient;
            }

            if ( params->client->nextClient )
            {
                params->client->nextClient->prevClient = params->client->prevClient;
            }

            return NULL;
        }
    }
}
// ====================================================================================================
static void *_listenTask( void *arg )

{
    int sockfd = *( ( int * )arg );
    int newsockfd;
    socklen_t clilen;
    struct sockaddr_in cli_addr;
    int f[2];                               /* File descriptor set for pipe */
    struct nwClientParams *params;
    char s[100];

    while ( 1 )
    {
        listen( sockfd, 5 );
        clilen = sizeof( cli_addr );
        newsockfd = accept( sockfd, ( struct sockaddr * ) &cli_addr, &clilen );

        if ( options.verbose )
        {
            inet_ntop( AF_INET, &cli_addr.sin_addr, s, 99 );
            fprintf( stdout, "New connection from %s" EOL, s );
        }

        /* We got a new connection - spawn a thread to handle it */
        if ( !pipe( f ) )
        {
            params = ( struct nwClientParams * )malloc( sizeof( struct nwClientParams ) );

            params->client = ( struct nwClient * )malloc( sizeof( struct nwClient ) );
            params->client->handle = f[1];
            params->listenHandle = f[0];
            params->portNo = newsockfd;

            if ( !pthread_create( &( params->client->thread ), NULL, &_client, params ) )
            {
                /* Auto-cleanup for this thread */
                pthread_detach( params->client->thread );

                /* Hook into linked list */
                params->client->nextClient = _r.firstClient;
                params->client->prevClient = NULL;

                if ( params->client->nextClient )
                {
                    params->client->nextClient->prevClient = params->client;
                }

                _r.firstClient = params->client;
            }
        }
    }

    return NULL;
}
// ====================================================================================================
static bool _makeServerTask( void )

/* Creating the listening server thread */

{
    static int sockfd;  /* This needs to be static to keep it available for the inferior */
    struct sockaddr_in serv_addr;
    int flag = 1;

    sockfd = socket( AF_INET, SOCK_STREAM, 0 );
    setsockopt( sockfd, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof( flag ) );

    if ( sockfd < 0 )
    {
        fprintf( stderr, "Error opening socket" EOL );
        return false;
    }

    bzero( ( char * ) &serv_addr, sizeof( serv_addr ) );
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons( SERVER_PORT );

    if ( bind( sockfd, ( struct sockaddr * ) &serv_addr, sizeof( serv_addr ) ) < 0 )
    {
        fprintf( stderr, "Error on binding" EOL );
        return false;
    }

    /* We have the listening socket - spawn a thread to handle it */
    if ( pthread_create( &( _r.ipThread ), NULL, &_listenTask, &sockfd ) )
    {
        fprintf( stderr, "Failed to create listening thread" EOL );
        return false;
    }

    return true;
}
// ====================================================================================================
static void _sendToClients( uint32_t len, uint8_t *buffer )

{
    struct nwClient *n = _r.firstClient;

    while ( n )
    {
        write( n->handle, buffer, len );
        n = n->nextClient;
    }
}
// ====================================================================================================
void _handleException( struct ITMDecoder *i, struct ITMPacket *p )

{
    uint64_t ts = _timestampuS(); /* Stamp as early as possible */
    char outputString[MAX_STRING_LENGTH];
    int opLen;
    uint32_t exceptionNumber = ( ( p->d[1] & 0x01 ) << 8 ) | p->d[0];
    uint32_t eventType = p->d[1] >> 4;
    uint64_t eventdifftS = ts - _r.lastHWExceptionTS;


    const char *exNames[] = {"Thread", "Reset", "NMI", "HardFault", "MemManage", "BusFault", "UsageFault", "UNKNOWN_7",
                             "UNKNOWN_8", "UNKNOWN_9", "UNKNOWN_10", "SVCall", "Debug Monitor", "UNKNOWN_13", "PendSV", "SysTick"
                            };
    const char *exEvent[] = {"Unknown", "Enter", "Exit", "Resume"};

    _r.lastHWExceptionTS = ts;

    if ( exceptionNumber < 16 )
    {
        /* This is a system based exception */
        opLen = snprintf( outputString, MAX_STRING_LENGTH, "%d,%ld,%s,%s" EOL, HWEVENT_EXCEPTION, eventdifftS, exEvent[eventType & 0x03], exNames[exceptionNumber & 0x0F] );
    }
    else
    {
        /* This is a CPU defined exception */
        opLen = snprintf( outputString, MAX_STRING_LENGTH, "%d,%ld,%s,External,%d" EOL, HWEVENT_EXCEPTION, eventdifftS, exEvent[eventType & 0x03], exceptionNumber - 16 );
    }

    write( _r.c[HW_CHANNEL].handle, outputString, opLen );
}
// ====================================================================================================
void _handleDWTEvent( struct ITMDecoder *i, struct ITMPacket *p )

{
    uint64_t ts = _timestampuS(); /* Stamp as early as possible */
    char outputString[MAX_STRING_LENGTH];
    int opLen;
    uint32_t event = p->d[1] & 0x2F;
    const char *evName[] = {"CPI", "Exc", "Sleep", "LSU", "Fold", "Cyc"};
    uint64_t eventdifftS = ts - _r.lastHWExceptionTS;

    _r.lastHWExceptionTS = ts;

    for ( uint32_t i = 0; i < 6; i++ )
    {
        if ( event & ( 1 << i ) )
        {
            opLen = snprintf( outputString, MAX_STRING_LENGTH, "%d,%ld,%s" EOL, HWEVENT_DWT, eventdifftS, evName[event] );
            write( _r.c[HW_CHANNEL].handle, outputString, opLen );
        }
    }
}
// ====================================================================================================
void _handlePCSample( struct ITMDecoder *i, struct ITMPacket *p )

/* We got a sample of the PC */

{
    uint64_t ts = _timestampuS(); /* Stamp as early as possible */
    char outputString[MAX_STRING_LENGTH];
    int opLen;
    uint64_t eventdifftS = ts - _r.lastHWExceptionTS;

    _r.lastHWExceptionTS = ts;

    if ( p->len == 1 )
    {
        /* This is a sleep packet */
        opLen = snprintf( outputString, ( MAX_STRING_LENGTH - 1 ), "%d,%ld,**SLEEP**" EOL, HWEVENT_PCSample, eventdifftS );
    }
    else
    {
        uint32_t pc = ( p->d[3] << 24 ) | ( p->d[2] << 16 ) | ( p->d[1] << 8 ) | ( p->d[0] );
        opLen = snprintf( outputString, ( MAX_STRING_LENGTH - 1 ), "%d,%ld,0x%08x" EOL, HWEVENT_PCSample, eventdifftS, pc );
    }

    write( _r.c[HW_CHANNEL].handle, outputString, opLen );
}
// ====================================================================================================
void _handleDataRWWP( struct ITMDecoder *i, struct ITMPacket *p )

/* We got an alert due to a watch pointer */

{
    uint64_t ts = _timestampuS(); /* Stamp as early as possible */
    uint32_t comp = ( p->d[0] & 0x30 ) >> 4;
    bool isWrite = ( ( p->d[0] & 0x08 ) != 0 );
    uint32_t data;
    char outputString[MAX_STRING_LENGTH];
    int opLen;
    uint64_t eventdifftS = ts - _r.lastHWExceptionTS;

    _r.lastHWExceptionTS = ts;

    switch ( p->len )
    {
        case 1:
            data = p->d[1];
            break;

        case 2:
            data = ( p->d[1] ) | ( ( p->d[2] ) << 8 );
            break;

        default:
            data = ( p->d[1] ) | ( ( p->d[2] ) << 8 ) | ( ( p->d[3] ) << 16 ) | ( ( p->d[4] ) << 24 );
            break;
    }

    opLen = snprintf( outputString, MAX_STRING_LENGTH, "%d,%ld,%d,%s,0x%x" EOL, HWEVENT_RWWT, eventdifftS, comp, isWrite ? "Write" : "Read", data );
    write( _r.c[HW_CHANNEL].handle, outputString, opLen );
}
// ====================================================================================================
void _handleDataAccessWP( struct ITMDecoder *i, struct ITMPacket *p )

/* We got an alert due to a watchpoint */

{
    uint64_t ts = _timestampuS(); /* Stamp as early as possible */
    uint32_t comp = ( p->d[0] & 0x30 ) >> 4;
    uint32_t data = ( p->d[1] ) | ( ( p->d[2] ) << 8 ) | ( ( p->d[3] ) << 16 ) | ( ( p->d[4] ) << 24 );
    char outputString[MAX_STRING_LENGTH];
    int opLen;

    uint64_t eventdifftS = ts - _r.lastHWExceptionTS;

    _r.lastHWExceptionTS = ts;
    opLen = snprintf( outputString, MAX_STRING_LENGTH, "%d,%ld,%d,0x%08x" EOL, HWEVENT_AWP, eventdifftS, comp, data );
    write( _r.c[HW_CHANNEL].handle, outputString, opLen );
}
// ====================================================================================================
void _handleDataOffsetWP( struct ITMDecoder *i, struct ITMPacket *p )

/* We got an alert due to an offset write event */

{
    uint64_t ts = _timestampuS(); /* Stamp as early as possible */
    uint32_t comp = ( p->d[0] & 0x30 ) >> 4;
    uint32_t offset = ( p->d[1] ) | ( ( p->d[2] ) << 8 );
    char outputString[MAX_STRING_LENGTH];
    int opLen;
    uint64_t eventdifftS = ts - _r.lastHWExceptionTS;

    _r.lastHWExceptionTS = ts;
    opLen = snprintf( outputString, MAX_STRING_LENGTH, "%d,%ld,%d,0x%04x" EOL, HWEVENT_OFS, eventdifftS, comp, offset );
    write( _r.c[HW_CHANNEL].handle, outputString, opLen );
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Handlers for each message type
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
void _handleSW( struct ITMDecoder *i )

{
    struct ITMPacket p;

    if ( ITMGetPacket( i, &p ) )
    {
        /* Filter off filewriter packets and let the filewriter module deal with those */
        if ( p.srcAddr == FW_CHANNEL )
        {
            filewriterProcess( &p );
        }
        else
        {
            if ( ( p.srcAddr < NUM_CHANNELS ) && ( _r.c[p.srcAddr].handle ) )
            {
                write( _r.c[p.srcAddr].handle, &p, sizeof( struct ITMPacket ) );
            }
        }
    }
}
// ====================================================================================================
void _handleHW( struct ITMDecoder *i )

/* ... a hardware event has been received, dispatch it */

{
    struct ITMPacket p;
    ITMGetPacket( i, &p );

    switch ( p.srcAddr )
    {
        // --------------
        case 0: /* DWT Event */
            break;

        // --------------
        case 1: /* Exception */
            _handleException( i, &p );
            break;

        // --------------
        case 2: /* PC Counter Sample */
            _handlePCSample( i, &p );
            break;

        // --------------
        default:
            if ( ( p.d[0] & 0xC4 ) == 0x84 )
            {
                _handleDataRWWP( i, &p );
            }
            else if ( ( p.d[0] & 0xCF ) == 0x47 )
            {
                _handleDataAccessWP( i, &p );
            }
            else if ( ( p.d[0] & 0xCF ) == 0x4E )
            {
                _handleDataOffsetWP( i, &p );
            }

            break;
            // --------------
    }
}
// ====================================================================================================
void _handleXTN( struct ITMDecoder *i )

/* ... an extension packet has been received : for now this is not used */

{
    struct ITMPacket p;

    if ( ITMGetPacket( i, &p ) )
    {
        printf( "XTN len=%d (%02x)" EOL, p.len, p.d[0] );
    }
    else
    {
        printf( "GET FAILED" EOL );
    }
}
// ====================================================================================================
void _handleTS( struct ITMDecoder *i )

/* ... a timestamp */

{
    char outputString[MAX_STRING_LENGTH];
    int opLen;
    struct ITMPacket p;
    uint32_t stamp = 0;

    if ( ITMGetPacket( i, &p ) )
    {
        if ( !( p.d[0] & 0x80 ) )
        {
            /* This is packet format 2 ... just a simple increment */
            stamp = p.d[0] >> 4;
        }
        else
        {
            /* This is packet format 1 ... full decode needed */
            i->timeStatus = ( p.d[0] & 0x30 ) >> 4;
            stamp = ( p.d[1] ) & 0x7f;

            if ( p.len > 2 )
            {
                stamp |= ( p.d[2] ) << 7;

                if ( p.len > 3 )
                {
                    stamp |= ( p.d[3] & 0x7F ) << 14;

                    if ( p.len > 4 )
                    {
                        stamp |= ( p.d[4] & 0x7f ) << 21;
                    }
                }
            }
        }

        i->timeStamp += stamp;
    }

    opLen = snprintf( outputString, MAX_STRING_LENGTH, "%d,%d,%" PRIu64 EOL, HWEVENT_TS, i->timeStatus, i->timeStamp );
    write( _r.c[HW_CHANNEL].handle, outputString, opLen );
}
// ====================================================================================================
void _itmPumpProcess( char c )

/* Handle individual characters into the itm decoder */

{
    switch ( ITMPump( &_r.i, c ) )
    {
        // ------------------------------------
        case ITM_EV_NONE:
            break;

        // ------------------------------------
        case ITM_EV_UNSYNCED:
            if ( options.verbose )
            {
                fprintf( stdout, "ITM Lost Sync (%d)" EOL, ITMDecoderGetStats( &_r.i )->lostSyncCount );
            }

            break;

        // ------------------------------------
        case ITM_EV_SYNCED:
            if ( options.verbose )
            {
                fprintf( stdout, "ITM In Sync (%d)" EOL, ITMDecoderGetStats( &_r.i )->syncCount );
            }

            break;

        // ------------------------------------
        case ITM_EV_OVERFLOW:
            if ( options.verbose )
            {
                fprintf( stdout, "ITM Overflow (%d)" EOL, ITMDecoderGetStats( &_r.i )->overflow );
            }

            break;

        // ------------------------------------
        case ITM_EV_ERROR:
            if ( options.verbose )
            {
                fprintf( stdout, "ITM Error" EOL );
            }

            break;

        // ------------------------------------
        case ITM_EV_TS_PACKET_RXED:
            _handleTS( &_r.i );
            break;

        // ------------------------------------
        case ITM_EV_SW_PACKET_RXED:
            _handleSW( &_r.i );
            break;

        // ------------------------------------
        case ITM_EV_HW_PACKET_RXED:
            _handleHW( &_r.i );
            break;

        // ------------------------------------
        case ITM_EV_XTN_PACKET_RXED:
            _handleXTN( &_r.i );
            break;
            // ------------------------------------
    }
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Protocol pump for decoding messages
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
void _protocolPump( uint8_t c )

/* Top level protocol pump */

{
    if ( options.useTPIU )
    {
        switch ( TPIUPump( &_r.t, c ) )
        {
            // ------------------------------------
            case TPIU_EV_NEWSYNC:
                if ( options.verbose )
                {
                    fprintf( stdout, "TPIU In Sync (%d)" EOL, TPIUDecoderGetStats( &_r.t )->syncCount );
                }

            // This fall-through is deliberate
            case TPIU_EV_SYNCED:

                ITMDecoderForceSync( &_r.i, true );
                break;

            // ------------------------------------
            case TPIU_EV_RXING:
            case TPIU_EV_NONE:
                break;

            // ------------------------------------
            case TPIU_EV_UNSYNCED:
                fprintf( stdout, "TPIU Lost Sync (%d)" EOL, TPIUDecoderGetStats( &_r.t )->lostSync );
                ITMDecoderForceSync( &_r.i, false );
                break;

            // ------------------------------------
            case TPIU_EV_RXEDPACKET:
                if ( !TPIUGetPacket( &_r.t, &_r.p ) )
                {
                    fprintf( stderr, "TPIUGetPacket fell over" EOL );
                }

                for ( uint32_t g = 0; g < _r.p.len; g++ )
                {
                    if ( _r.p.packet[g].s == options.tpiuITMChannel )
                    {
                        _itmPumpProcess( _r.p.packet[g].d );
                        continue;
                    }

                    if ( ( _r.p.packet[g].s != 0 ) && ( _r.p.packet[g].s != 0x7f ) && ( options.verbose ) )
                    {
                        if ( options.verbose )
                        {
                            fprintf( stdout, "Unknown TPIU channel %02x" EOL, _r.p.packet[g].s );
                        }
                    }
                }

                break;

            // ------------------------------------
            case TPIU_EV_ERROR:
                fprintf( stderr, "****ERROR****" EOL );
                break;
                // ------------------------------------
        }
    }
    else
    {
        /* There's no TPIU in use, so this goes straight to the ITM layer */
        _itmPumpProcess( c );
    }
}
// ====================================================================================================
void intHandler( int dummy )

{
    exit( 0 );
}
// ====================================================================================================
void _printHelp( char *progName )

{
    fprintf( stdout, "Useage: %s <hntv> <a name:number> <b basedir> <f filename>  <i channel> <p port> <s speed>" EOL, progName );
    fprintf( stdout, "        a: <serialSpeed> to use" EOL );
    fprintf( stdout, "        b: <basedir> for channels" EOL );
    fprintf( stdout, "        c: <Number>,<Name>,<Format> of channel to populate (repeat per channel)" EOL );
    fprintf( stdout, "        f: <filename> Take input from specified file" EOL );
    fprintf( stdout, "        h: This help" EOL );
    fprintf( stdout, "        i: <channel> Set ITM Channel in TPIU decode (defaults to 1)" EOL );
    fprintf( stdout, "        n: Enforce sync requirement for ITM (i.e. ITM needs to issue syncs)" EOL );
#ifdef INCLUDE_FPGA_SUPPORT
    fprintf( stdout, "        o: Use orbuculum custom interface" EOL );
#endif
    fprintf( stdout, "        p: <serialPort> to use" EOL );
    fprintf( stdout, "        s: <address>:<port> Set address for SEGGER JLink connection (default none:%d)" EOL, SEGGER_PORT );
    fprintf( stdout, "        t: Use TPIU decoder" EOL );
    fprintf( stdout, "        v: Verbose mode" EOL );
}
// ====================================================================================================
int _processOptions( int argc, char *argv[] )

{
    int c;
    char *chanConfig;
    uint chan;
    char *chanIndex;
#define DELIMITER ','

    while ( ( c = getopt ( argc, argv, "a:b:c:f:hi:nop:s:tv" ) ) != -1 )
        switch ( c )
        {
            // ------------------------------------
            case 'a':
                options.speed = atoi( optarg );
                break;

            // ------------------------------------
            case 'b':
                options.chanPath = optarg;
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
            case 'i':
                options.tpiuITMChannel = atoi( optarg );
                break;

            // ------------------------------------
            case 'n':
                options.forceITMSync = false;
                break;
                // ------------------------------------
#ifdef INCLUDE_FPGA_SUPPORT

            case 'o':
                // Generally you need TPIU for orbtrace
                options.useTPIU = true;
                options.orbtrace = true;
                break;
#endif

            // ------------------------------------
            case 'p':
                options.port = optarg;
                break;

            // ------------------------------------
            case 's':
                options.forceITMSync = true;
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
            case 't':
                options.useTPIU = true;
                break;

            // ------------------------------------
            case 'v':
                options.verbose = 1;
                break;

            // ------------------------------------
            /* Individual channel setup */
            case 'c':
                chanIndex = chanConfig = strdup( optarg );
                chan = atoi( optarg );

                if ( chan >= NUM_CHANNELS )
                {
                    fprintf( stderr, "Channel index out of range" EOL );
                    return false;
                }

                /* Scan for start of filename */
                while ( ( *chanIndex ) && ( *chanIndex != DELIMITER ) )
                {
                    chanIndex++;
                }

                if ( !*chanIndex )
                {
                    fprintf( stderr, "No filename for channel %d" EOL, chan );
                    return false;
                }

                options.channel[chan].chanName = ++chanIndex;

                /* Scan for format */
                while ( ( *chanIndex ) && ( *chanIndex != DELIMITER ) )
                {
                    chanIndex++;
                }

                if ( !*chanIndex )
                {
                    fprintf( stderr, "No output format for channel %d" EOL, chan );
                    return false;
                }

                *chanIndex++ = 0;
                options.channel[chan].presFormat = strdup( GenericsUnescape( chanIndex ) );
                break;

            // ------------------------------------


            case '?':
                if ( optopt == 'b' )
                {
                    fprintf ( stderr, "Option '%c' requires an argument." EOL, optopt );
                }
                else if ( !isprint ( optopt ) )
                {
                    fprintf ( stderr, "Unknown option character `\\x%x'." EOL, optopt );
                }

                return false;

            // ------------------------------------
            default:
                printf( "%c" EOL, c );
                return false;
                // ------------------------------------
        }

    /* Now perform sanity checks.... */
    if ( ( options.useTPIU ) && ( !options.tpiuITMChannel ) )
    {
        fprintf( stderr, "TPIU set for use but no channel set for ITM output" EOL );
        return false;
    }

    /* ... and dump the config if we're being verbose */
    if ( options.verbose )
    {
        fprintf( stdout, "Orbuculum V" VERSION " (Git %08X %s, Built " BUILD_DATE ")" EOL, GIT_HASH, ( GIT_DIRTY ? "Dirty" : "Clean" ) );

        fprintf( stdout, "Verbose    : true" EOL );
        fprintf( stdout, "BasePath   : %s" EOL, options.chanPath );
        fprintf( stdout, "ForceSync  : %s" EOL, options.forceITMSync ? "true" : "false" );

        if ( options.port )
        {
            fprintf( stdout, "Serial Port : %s" EOL "Serial Speed: %d" EOL, options.port, options.speed );
        }

        if ( options.seggerPort )
        {
            fprintf( stdout, "SEGGER H&P : %s:%d" EOL, options.seggerHost, options.seggerPort );
        }

#ifdef INCLUDE_FPGA_SUPPORT

        if ( options.orbtrace )
        {
            fprintf( stdout, "Orbtrace : true" EOL );
        }

#endif

        if ( options.useTPIU )
        {
            fprintf( stdout, "Using TPIU : true (ITM on channel %d)" EOL, options.tpiuITMChannel );
        }

        if ( options.file )
        {
            fprintf( stdout, "Input File : %s" EOL, options.file );
        }

        fprintf( stdout, "Channels   :" EOL );

        for ( int g = 0; g < NUM_CHANNELS; g++ )
        {
            if ( options.channel[g].chanName )
            {
                fprintf( stdout, "         %02d [%s] [%s]" EOL, g, GenericsEscape( options.channel[g].presFormat ), options.channel[g].chanName );
            }
        }

        fprintf( stdout, "         HW [Predefined] [" HWFIFO_NAME "]" EOL );
    }

    if ( ( options.file ) && ( ( options.port ) || ( options.seggerPort ) ) )
    {
        fprintf( stdout, "Cannot specify file and port or Segger at same time" EOL );
        return false;
    }

    if ( ( options.port ) && ( options.seggerPort ) )
    {
        fprintf( stdout, "Cannot specify port and Segger at same time" EOL );
        return false;
    }

    return true;
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
            fprintf( stderr, "Failed to initalise USB interface" EOL );
            return ( -1 );
        }

        /* Snooze waiting for the device to appear .... this is useful for when they come and go */
        while ( !( handle = libusb_open_device_with_vid_pid( NULL, VID, PID ) ) )
        {
            usleep( 500000 );
        }

        if ( !( dev = libusb_get_device( handle ) ) )
        {
            /* We didn't get the device, so try again in a while */
            continue;
        }

        if ( ( err = libusb_claim_interface ( handle, INTERFACE ) ) < 0 )
        {
            fprintf( stderr, "Failed to claim interface (%d)" EOL, err );
            return 0;
        }

        int32_t r;

        while ( true )
        {
            r = libusb_bulk_transfer( handle, ENDPOINT, cbw, TRANSFER_SIZE, &size, 10 );

            if ( ( r < 0 ) && ( r != LIBUSB_ERROR_TIMEOUT ) )
            {
                break;
            }

            _sendToClients( size, cbw );
            unsigned char *c = cbw;

            while ( size-- )
            {
                _protocolPump( *c++ );
            }
        }

        libusb_close( handle );
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
        fprintf( stderr, "Cannot find host" EOL );
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
            fprintf( stderr, "Error creating socket" EOL );
            return -1;
        }

        while ( connect( sockfd, ( struct sockaddr * ) &serv_addr, sizeof( serv_addr ) ) < 0 )
        {
            usleep( 500000 );
        }

        if ( options.verbose )
        {
            fprintf( stdout, "Established Segger Link" EOL );
        }

        TPIUDecoderForceSync( &_r.t, 0 );
        ITMDecoderForceSync( &_r.i, true );

        while ( ( t = read( sockfd, cbw, TRANSFER_SIZE ) ) > 0 )
        {
            _sendToClients( t, cbw );
            uint8_t *c = cbw;

            while ( t-- )
            {
                _protocolPump( *c++ );
            }
        }

        close( sockfd );

        if ( options.verbose )
        {
            fprintf( stdout, "Lost Segger Link" EOL );
        }
    }

    return -2;
}
// ====================================================================================================
int serialFeeder( void )

{
    int f;
    unsigned char cbw[TRANSFER_SIZE];
    ssize_t t;
    struct termios settings;

    while ( 1 )
    {
        while ( ( f = open( options.port, O_RDONLY ) ) < 0 )
        {
            if ( options.verbose )
            {
                fprintf( stderr, "Can't open serial port" EOL );
            }

            usleep( 500000 );
        }

        if ( options.verbose )
        {
            fprintf( stderr, "Port opened" EOL );
        }

        if ( tcgetattr( f, &settings ) < 0 )
        {
            perror( "tcgetattr" );
            return ( -3 );
        }

        if ( cfsetspeed( &settings, options.speed ) < 0 )
        {
            perror( "Setting input speed" );
            return -3;
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
            fprintf( stderr, "Unsupported baudrate" EOL );
            exit( -3 );
        }

        tcflush( f, TCOFLUSH );

        while ( ( t = read( f, cbw, TRANSFER_SIZE ) ) > 0 )
        {
            _sendToClients( t, cbw );
            unsigned char *c = cbw;

            while ( t-- )
            {
                _protocolPump( *c++ );
            }
        }

        if ( options.verbose )
        {
            fprintf( stderr, "Read failed" EOL );
        }

        close( f );
    }
}
// ====================================================================================================
#ifdef INCLUDE_FPGA_SUPPORT
int fpgaFeeder( void )

{
    int f, t;
    uint8_t cbw[TRANSFER_SIZE];
    uint8_t *c;

    _r.ftdi = ftdi_new();

    while ( 1 )
    {
        do
        {
            ftdi_set_interface( _r.ftdi, FTDI_INTERFACE );
            f = ftdi_usb_open( _r.ftdi, FTDI_VID, FTDI_PID );

            if ( f < 0 )
            {
                if ( options.verbose )
                {
                    fprintf( stderr, "Cannot open device (%s)" EOL, ftdi_get_error_string( _r.ftdi ) );
                }

                usleep( 50000 );
            }
        }
        while ( f < 0 );

        if ( options.verbose )
        {
            fprintf( stderr, "Port opened" EOL );
        }

        f = ftdi_set_baudrate( _r.ftdi, options.speed );

        if ( f < 0 )
        {
            fprintf( stderr, "Cannot set baudate %d %d (%s)" EOL, f, options.speed, ftdi_get_error_string( _r.ftdi ) );
            return -2;
        }

        ftdi_set_line_property( _r.ftdi, 8, STOP_BIT_1, NONE );

        ftdi_read_data_set_chunksize( _r.ftdi, TRANSFER_SIZE );
        ftdi_setdtr( _r.ftdi, true );

        while ( ( t = ftdi_read_data( _r.ftdi, cbw, TRANSFER_SIZE ) ) >= 0 )
        {
            if ( !t )
            {
                continue;
            }

            _sendToClients( t, cbw );
            c = cbw;

            while ( t-- )
            {
                _protocolPump( *c++ );
            }
        }

        ftdi_setdtr( _r.ftdi, false );

        if ( options.verbose )
        {
            fprintf( stderr, "Read failed" EOL );
        }

        ftdi_usb_close( _r.ftdi );
        _r.ftdi = NULL;
    }
}
// ====================================================================================================
void fpgaFeederClose( void )

{
    /* OK, this is a bit odd.  Because there may have been a transfer in progress we need to close down */
    /* and re-open the driver in order to perform the DTR drop. Since we're in the process of leaving */
    /* anyway there's not too much point in error checking! */

    ftdi_usb_close( _r.ftdi );
    ftdi_deinit( _r.ftdi );
    _r.ftdi = ftdi_new();
    ftdi_set_interface( _r.ftdi, FTDI_INTERFACE );
    ftdi_usb_open( _r.ftdi, FTDI_VID, FTDI_PID );
    ftdi_setdtr_rts( _r.ftdi, false, false );
    ftdi_usb_close( _r.ftdi );
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
        if ( options.verbose )
        {
            fprintf( stderr, "Can't open file %s" EOL, options.file );
        }

        exit( -4 );
    }

    if ( options.verbose )
    {
        fprintf( stdout, "Reading from file" EOL );
    }

    while ( ( t = read( f, cbw, TRANSFER_SIZE ) ) >= 0 )
    {

        if ( !t )
        {
            // Just spin for a while to avoid clogging the CPU
            usleep( 100000 );
            continue;
        }

        _sendToClients( t, cbw );
        unsigned char *c = cbw;

        while ( t-- )
        {
            _protocolPump( *c++ );
        }
    }

    if ( options.verbose )
    {
        fprintf( stderr, "File read error" EOL );
    }

    close( f );
    return true;
}
// ====================================================================================================
int main( int argc, char *argv[] )

{
    if ( !_processOptions( argc, argv ) )
    {
        exit( -1 );
    }

    atexit( _removeFifoTasks );

    /* Reset the TPIU handler before we start */
    TPIUDecoderInit( &_r.t );
    ITMDecoderInit( &_r.i, options.forceITMSync );

    /* This ensures the atexit gets called */
    signal( SIGINT, intHandler );

    if ( !_makeFifoTasks() )
    {
        fprintf( stderr, "Failed to make channel devices" EOL );
        exit( -1 );
    }

    if ( !_makeServerTask() )
    {
        fprintf( stderr, "Failed to make network server" EOL );
        exit( -1 );
    }

    /* Make sure there's an initial timestamp to work with */

    _r.lastHWExceptionTS = _timestampuS();

    /* Start the filewriter */
    filewriterInit( options.fwbasedir, FW_V_INFO );

    /* Using the exit construct rather than return ensures the atexit gets called */
#ifdef INCLUDE_FPGA_SUPPORT

    if ( options.orbtrace )
    {
        atexit( fpgaFeederClose );
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
