/*
 * SWO Catter for Blackmagic Probe and TTL Serial Interfaces
 * =========================================================
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
#include <netinet/in.h>
#include <netdb.h>

#include "git_version_info.h"
#include "generics.h"
#include "tpiuDecoder.h"
#include "itmDecoder.h"

#define SERVER_PORT 3443                  /* Server port definition */


#define TRANSFER_SIZE (64)
#define NUM_CHANNELS  32
#define HW_CHANNEL    (NUM_CHANNELS)      /* Make the hardware fifo on the end of the software ones */

#define MAX_STRING_LENGTH (100)           /* Maximum length that will be output from a fifo for a single event */

// Record for options, either defaults or from command line
struct
{
    /* Config information */
    BOOL verbose;
    BOOL useTPIU;
    uint32_t tpiuITMChannel;

    uint32_t hwOutputs;

    /* Sink information */
    char *presFormat[NUM_CHANNELS + 1];

    /* Source information */
    int port;
    char *server;

} options = {.tpiuITMChannel = 1, .port = SERVER_PORT, .server = "localhost"};


#define MAX_IP_PACKET_LEN (1500)

struct
{
    /* The decoders and the packets from them */
    struct ITMDecoder i;
    struct ITMPacket h;
    struct TPIUDecoder t;
    struct TPIUPacket p;
} _r;
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Handler for individual message types from SWO
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
void _handleException( struct ITMDecoder *i, struct ITMPacket *p )

{
    if ( !( options.hwOutputs & ( 1 << HWEVENT_EXCEPTION ) ) )
    {
        return;
    }

    uint32_t exceptionNumber = ( ( p->d[1] & 0x01 ) << 8 ) | p->d[0];
    uint32_t eventType = p->d[1] >> 4;

    const char *exNames[] = {"Thread", "Reset", "NMI", "HardFault", "MemManage", "BusFault", "UsageFault", "UNKNOWN_7",
                             "UNKNOWN_8", "UNKNOWN_9", "UNKNOWN_10", "SVCall", "Debug Monitor", "UNKNOWN_13", "PendSV", "SysTick"
                            };
    const char *exEvent[] = {"Enter", "Exit", "Resume"};
    fprintf( stdout, "%d,%s,%s\n", HWEVENT_EXCEPTION, exEvent[eventType], exNames[exceptionNumber] );
}
// ====================================================================================================
void _handleDWTEvent( struct ITMDecoder *i, struct ITMPacket *p )

{
    if ( !( options.hwOutputs & ( 1 << HWEVENT_DWT ) ) )
    {
        return;
    }

    uint32_t event = p->d[1] & 0x2F;
    const char *evName[] = {"CPI", "Exc", "Sleep", "LSU", "Fold", "Cyc"};

    for ( uint32_t i = 0; i < 6; i++ )
    {
        if ( event & ( 1 << i ) )
        {
            fprintf( stdout, "%d,%s\n", HWEVENT_DWT, evName[event] );
        }
    }
}
// ====================================================================================================
void _handlePCSample( struct ITMDecoder *i, struct ITMPacket *p )

{
    if ( !( options.hwOutputs & ( 1 << HWEVENT_PCSample ) ) )
    {
        return;
    }

    uint32_t pc = ( p->d[3] << 24 ) | ( p->d[2] << 16 ) | ( p->d[1] << 8 ) | ( p->d[0] );
    fprintf( stdout, "%d,0x%08x\n", HWEVENT_PCSample, pc );
}
// ====================================================================================================
void _handleDataRWWP( struct ITMDecoder *i, struct ITMPacket *p )

{
    if ( !( options.hwOutputs & ( 1 << HWEVENT_RWWT ) ) )
    {
        return;
    }

    uint32_t comp = ( p->d[0] & 0x30 ) >> 4;
    BOOL isWrite = ( ( p->d[0] & 0x08 ) != 0 );
    uint32_t data;

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

    fprintf( stdout, "%d,%d,%s,0x%x\n", HWEVENT_RWWT, comp, isWrite ? "Write" : "Read", data );
}
// ====================================================================================================
void _handleDataAccessWP( struct ITMDecoder *i, struct ITMPacket *p )

{
    if ( !( options.hwOutputs & ( 1 << HWEVENT_AWP ) ) )
    {
        return;
    }

    uint32_t comp = ( p->d[0] & 0x30 ) >> 4;
    uint32_t data = ( p->d[1] ) | ( ( p->d[2] ) << 8 ) | ( ( p->d[3] ) << 16 ) | ( ( p->d[4] ) << 24 );

    fprintf( stdout, "%d,%d,0x%08x\n", HWEVENT_AWP, comp, data );
}
// ====================================================================================================
void _handleDataOffsetWP( struct ITMDecoder *i, struct ITMPacket *p )

{
    if ( !( options.hwOutputs & ( 1 << HWEVENT_OFS ) ) )
    {
        return;
    }

    uint32_t comp = ( p->d[0] & 0x30 ) >> 4;
    uint32_t offset = ( p->d[1] ) | ( ( p->d[2] ) << 8 );

    fprintf( stdout, "%d,%d,0x%04x\n", HWEVENT_OFS, comp, offset );
}
// ====================================================================================================
void _handleSW( struct ITMDecoder *i )

{
    struct ITMPacket p;
    uint32_t w;

    if ( ITMGetPacket( i, &p ) )
    {
        if ( ( p.srcAddr < NUM_CHANNELS ) && ( options.presFormat[p.srcAddr] ) )
        {
            /* Build 32 value the long way around to avoid type-punning issues */
            w = ( p.d[3] << 24 ) | ( p.d[2] << 16 ) | ( p.d[1] << 8 ) | ( p.d[0] );
            fprintf( stdout, options.presFormat[p.srcAddr], w );
        }
    }
}
// ====================================================================================================
void _handleHW( struct ITMDecoder *i )

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

{
    struct ITMPacket p;

    if ( ITMGetPacket( i, &p ) )
    {
        printf( "XTN len=%d (%02x)\n", p.len, p.d[0] );
    }
    else
    {
        printf( "GET FAILED\n" );
    }
}
// ====================================================================================================
void _handleTS( struct ITMDecoder *i )

{
    struct ITMPacket p;
    uint32_t stamp = 0;

    if ( !( options.hwOutputs & ( 1 << HWEVENT_TIMESTAMP ) ) )
    {
        return;
    }

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

    fprintf( stdout, "%d,%d,%" PRIu64 "\n", HWEVENT_TS, i->timeStatus, i->timeStamp );
}
// ====================================================================================================
void _itmPumpProcess( char c )

{
    switch ( ITMPump( &_r.i, c ) )
    {
        case ITM_EV_NONE:
            break;

        case ITM_EV_UNSYNCED:
            if ( options.verbose )
            {
                fprintf( stdout, "ITM Unsynced\n" );
            }

            break;

        case ITM_EV_SYNCED:
            if ( options.verbose )
            {
                fprintf( stdout, "ITM Synced\n" );
            }

            break;

        case ITM_EV_OVERFLOW:
            if ( options.verbose )
            {
                fprintf( stdout, "ITM Overflow\n" );
            }

            break;

        case ITM_EV_ERROR:
            if ( options.verbose )
            {
                fprintf( stdout, "ITM Error\n" );
            }

            break;

        case ITM_EV_TS_PACKET_RXED:
            _handleTS( &_r.i );
            break;

        case ITM_EV_SW_PACKET_RXED:
            _handleSW( &_r.i );
            break;

        case ITM_EV_HW_PACKET_RXED:
            _handleHW( &_r.i );
            break;

        case ITM_EV_XTN_PACKET_RXED:
            _handleXTN( &_r.i );
            break;
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

{
    if ( options.useTPIU )
    {
        switch ( TPIUPump( &_r.t, c ) )
        {
            case TPIU_EV_NEWSYNC:
	    case TPIU_EV_SYNCED:
                ITMDecoderForceSync( &_r.i, TRUE );
                break;

            case TPIU_EV_RXING:
            case TPIU_EV_NONE:
                break;

            case TPIU_EV_UNSYNCED:
                ITMDecoderForceSync( &_r.i, FALSE );
                break;

            case TPIU_EV_RXEDPACKET:
                if ( !TPIUGetPacket( &_r.t, &_r.p ) )
                {
                    fprintf( stderr, "TPIUGetPacket fell over\n" );
                }

                for ( uint32_t g = 0; g < _r.p.len; g++ )
                {
                    if ( _r.p.packet[g].s == options.tpiuITMChannel )
                    {
                        _itmPumpProcess( _r.p.packet[g].d );
                        continue;
                    }

                    if ( ( _r.p.packet[g].s != 0 ) && ( options.verbose ) )
                    {
                        if ( options.verbose )
                        {
                            printf( "Unknown TPIU channel %02x\n", _r.p.packet[g].s );
                        }
                    }
                }

                break;

            case TPIU_EV_ERROR:
                fprintf( stderr, "****ERROR****\n" );
                break;
        }
    }
    else
    {
        _itmPumpProcess( c );
    }
}
// ====================================================================================================
void _printHelp( char *progName )

{
    printf( "Useage: %s <htv> <-i channel> <-p port> <-s server>\n", progName );
    printf( "        c: <Number>,<Format> of channel to add into output stream (repeat per channel)\n" );
    printf( "        h: This help\n" );
    printf( "        i: <channel> Set ITM Channel in TPIU decode (defaults to 1)\n" );
    printf( "        p: <Port> to use\n" );
    printf( "        s: <Server> to use\n" );
    printf( "        t: Use TPIU decoder\n" );
    printf( "        v: Verbose mode (this will intermingle state info with the output flow)\n" );
}
// ====================================================================================================
int _processOptions( int argc, char *argv[] )

{
    int c;
    char *chanConfig;
    uint chan;
    char *chanIndex;
#define DELIMITER ','

    while ( ( c = getopt ( argc, argv, "c:hti:p:s:v" ) ) != -1 )
        switch ( c )
        {
            /* Config Information */
            case 'v':
                options.verbose = 1;
                break;

            case 't':
                options.useTPIU = TRUE;
                break;

            case 'i':
                options.tpiuITMChannel = atoi( optarg );
                break;

            /* Source information */
            case 'p':
                options.port = atoi( optarg );
                break;

            case 's':
                options.server = optarg;
                break;

            case 'h':
                _printHelp( argv[0] );
                return FALSE;

            /* Individual channel setup */
            case 'c':
                chanIndex = chanConfig = strdup( optarg );
                chan = atoi( optarg );

                if ( chan >= NUM_CHANNELS )
                {
                    fprintf( stderr, "Channel index out of range\n" );
                    return FALSE;
                }

                /* Scan for format */
                while ( ( *chanIndex ) && ( *chanIndex != DELIMITER ) )
                {
                    chanIndex++;
                }

                if ( !*chanIndex )
                {
                    fprintf( stderr, "No output format for channel %d\n", chan );
                    return FALSE;
                }

                *chanIndex++ = 0;
                options.presFormat[chan] = strdup( GenericsUnescape( chanIndex ) );
                break;

            case '?':
                if ( optopt == 'b' )
                {
                    fprintf ( stderr, "Option '%c' requires an argument.\n", optopt );
                }
                else if ( !isprint ( optopt ) )
                {
                    fprintf ( stderr, "Unknown option character `\\x%x'.\n", optopt );
                }

                return FALSE;

            default:
                return FALSE;
        }

    if ( ( options.useTPIU ) && ( !options.tpiuITMChannel ) )
    {
        fprintf( stderr, "TPIU set for use but no channel set for ITM output\n" );
        return FALSE;
    }

    if ( options.verbose )
    {
        fprintf( stdout, "orbcat V" VERSION " (Git %08X %s, Built " BUILD_DATE ")\n", GIT_HASH, ( GIT_DIRTY ? "Dirty" : "Clean" ) );

        fprintf( stdout, "Verbose   : TRUE\n" );
        fprintf( stdout, "Server    : %s:%d", options.server, options.port );

        if ( options.useTPIU )
        {
            fprintf( stdout, "Using TPIU: TRUE (ITM on channel %d)\n", options.tpiuITMChannel );
        }

        fprintf( stdout, "Channels  :\n" );

        for ( int g = 0; g < NUM_CHANNELS; g++ )
        {
            if ( options.presFormat[g] )
            {
                fprintf( stdout, "        %02d [%s]\n", g, GenericsEscape( options.presFormat[g] ) );
            }
        }
    }

    return TRUE;
}
// ====================================================================================================
int main( int argc, char *argv[] )

{
    int sockfd;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    unsigned char cbw[TRANSFER_SIZE];
    ssize_t t;
    int flag = 1;

    if ( !_processOptions( argc, argv ) )
    {
        exit( -1 );
    }

    /* Reset the TPIU handler before we start */
    TPIUDecoderInit( &_r.t );
    ITMDecoderInit( &_r.i );

    sockfd = socket( AF_INET, SOCK_STREAM, 0 );
    setsockopt( sockfd, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof( flag ) );

    if ( sockfd < 0 )
    {
        fprintf( stderr, "Error creating socket\n" );
        return -1;
    }

    /* Now open the network connection */
    bzero( ( char * ) &serv_addr, sizeof( serv_addr ) );
    server = gethostbyname( options.server );

    if ( !server )
    {
        fprintf( stderr, "Cannot find host\n" );
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    bcopy( ( char * )server->h_addr,
           ( char * )&serv_addr.sin_addr.s_addr,
           server->h_length );
    serv_addr.sin_port = htons( options.port );

    if ( connect( sockfd, ( struct sockaddr * ) &serv_addr, sizeof( serv_addr ) ) < 0 )
    {
        fprintf( stderr, "Could not connect\n" );
        return -1;
    }

    while ( ( t = read( sockfd, cbw, TRANSFER_SIZE ) ) > 0 )
    {
        unsigned char *c = cbw;

        while ( t-- )
        {
            _protocolPump( *c++ );
        }
	fflush(stdout);
    }

    if ( options.verbose )
    {
        fprintf( stderr, "Read failed\n" );
    }

    close( sockfd );
    return -2;
}
// ====================================================================================================
