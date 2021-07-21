/*
 * Trace Decoder for parallel trace
 * ================================
 *
 * Copyright (C) 2017, 2019, 2020, 2021  Dave Marples  <dave@marples.net>
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
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>

#include "git_version_info.h"
#include "generics.h"

#include "tpiuDecoder.h"
#include "etmdec.h"

#define TRANSFER_SIZE       (4096)
#define REMOTE_ETM_PORT     (3443)            /* Server port definition */
#define REMOTE_SERVER       "localhost"

//#define DUMP_BLOCK

/* Record for options, either defaults or from command line */
struct
{
    /* Config information */

    /* Source information */
    char *file;                         /* File host connection */
    bool fileTerminate;                 /* Terminate when file read isn't successful */


    bool useTPIU;                       /* Are we using TPIU, and stripping TPIU frames? */
    uint8_t channel;                    /* When TPIU is in use, which channel to decode? */
    uint32_t intervalReportTime;        /* If we want interval reports about performance */
    int port;                           /* Source information */
    char *server;

} options =
{
    .port = REMOTE_ETM_PORT,
    .server = REMOTE_SERVER,
    .channel = 2
};

struct
{
    struct etmdecHandle f;              /* Link to the etmdecoder subsystem */
    bool      ending;                   /* Flag indicating app is terminating */
    uint64_t intervalBytes;             /* Number of bytes transferred in current interval */
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
static void _printHelp( char *progName )

{
    genericsPrintf( "Usage: %s <ehntv> <b basedir> <f filename> <i channel>" EOL, progName );
    genericsPrintf( "       -e: When reading from file, terminate at end of file rather than waiting for further input" EOL );
    genericsPrintf( "       -f <filename>: Take input from specified file" EOL );
    genericsPrintf( "       -h: This help" EOL );
    genericsPrintf( "       -s: <Server>:<Port> to use" EOL );
    genericsPrintf( "       -t <channel>: Use TPIU to strip TPIU on specfied channel (defaults to 2)" EOL );
    genericsPrintf( "       -v: <level> Verbose mode 0(errors)..3(debug)" EOL );
    genericsPrintf( EOL "(Will connect one port higher than that set in -s when TPIU is not used)" EOL );
}
// ====================================================================================================
static int _processOptions( int argc, char *argv[] )

{
    int c;

    while ( ( c = getopt ( argc, argv, "ef:hs:t:v:" ) ) != -1 )
        switch ( c )
        {
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

            case 's':
                options.server = optarg;

                // See if we have an optional port number too
                char *a = optarg;

                while ( ( *a ) && ( *a != ':' ) )
                {
                    a++;
                }

                if ( *a == ':' )
                {
                    *a = 0;
                    options.port = atoi( ++a );
                }

                if ( !options.port )
                {
                    options.port = REMOTE_ETM_PORT;
                }

                break;

            // ------------------------------------

            case 't':
                options.useTPIU = true;
                options.channel = atoi( optarg );
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

    /* ... and dump the config if we're being verbose */
    genericsReport( V_INFO, "%s V" VERSION " (Git %08X %s, Built " BUILD_DATE ")" EOL, argv[0], GIT_HASH, ( GIT_DIRTY ? "Dirty" : "Clean" ) );

    if ( options.intervalReportTime )
    {
        genericsReport( V_INFO, "Report Intv : %d mS" EOL, options.intervalReportTime );
    }

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

    return true;
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

        while ( s-- )
        {
            etmdecProtocolPump( &_r.f, *cbw++ );
        }
    }

}
// ====================================================================================================
static void _doExit( void )

{
    _r.ending = true;
    /* Give them a bit of time, then we're leaving anyway */
    usleep( 200 );
}
// ====================================================================================================
int main( int argc, char *argv[] )

{
    int sourcefd;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    uint8_t cbw[TRANSFER_SIZE];
    int flag = 1;

    ssize_t t;
    int64_t lastTime;
    int64_t snapInterval;
    int r;
    struct timeval tv;
    fd_set readfds;
    int32_t remainTime;


    if ( !_processOptions( argc, argv ) )
    {
        /* processOptions generates its own error messages */
        //  genericsExit( -1, "" EOL );
    }

    /* Make sure the fifos get removed at the end */
    atexit( _doExit );

    /* Setup etmdecode with ETM on channel 2 */
    etmdecInit( &_r.f, options.useTPIU, options.channel );

    /* Fill in a time to start from */
    lastTime = genericsTimestampmS();

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

    while ( !_r.ending )
    {
        if ( !options.file )
        {
            /* Get the socket open */
            sourcefd = socket( AF_INET, SOCK_STREAM, 0 );
            setsockopt( sourcefd, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof( flag ) );

            if ( sourcefd < 0 )
            {
                perror( "Error creating socket\n" );
                return -EIO;
            }

            if ( setsockopt( sourcefd, SOL_SOCKET, SO_REUSEADDR, &( int )
        {
            1
        }, sizeof( int ) ) < 0 )
            {
                perror( "setsockopt(SO_REUSEADDR) failed" );
                return -EIO;
            }

            /* Now open the network connection */
            bzero( ( char * ) &serv_addr, sizeof( serv_addr ) );
            server = gethostbyname( options.server );

            if ( !server )
            {
                perror( "Cannot find host" );
                return -EIO;
            }

            serv_addr.sin_family = AF_INET;
            bcopy( ( char * )server->h_addr,
                   ( char * )&serv_addr.sin_addr.s_addr,
                   server->h_length );
            serv_addr.sin_port = htons( options.port + ( options.useTPIU ? 0 : 1 ) );

            if ( connect( sourcefd, ( struct sockaddr * ) &serv_addr, sizeof( serv_addr ) ) < 0 )
            {
                genericsPrintf( CLEAR_SCREEN EOL );

                perror( "Could not connect" );
                close( sourcefd );
                usleep( 1000000 );
                continue;
            }
        }
        else
        {
            if ( ( sourcefd = open( options.file, O_RDONLY ) ) < 0 )
            {
                genericsExit( sourcefd, "Can't open file %s" EOL, options.file );
            }
        }

        while ( !_r.ending )
        {
            if ( options.intervalReportTime )
            {
                remainTime = ( ( lastTime + options.intervalReportTime - genericsTimestampmS() ) * 1000 ) - 500;
            }
            else
            {
                remainTime = ( ( lastTime + 1000 - genericsTimestampmS() ) * 1000 ) - 500;
            }

            r = t = 0;

            if ( remainTime > 0 )
            {
                tv.tv_sec = remainTime / 1000000;
                tv.tv_usec  = remainTime % 1000000;

                FD_ZERO( &readfds );
                FD_SET( sourcefd, &readfds );
                r = select( sourcefd + 1, &readfds, NULL, NULL, &tv );
            }

            if ( r < 0 )
            {
                /* Something went wrong in the select */
                break;
            }

            if ( r > 0 )
            {
                t = read( sourcefd, cbw, TRANSFER_SIZE );

                if ( t <= 0 )
                {
                    /* We are at EOF (Probably the descriptor closed) */
                    break;
                }

                /* Pump all of the data through the protocol handler */
                _processBlock( t, cbw );
            }

            /* See if its time to report on the past seconds stats */
            if ( r == 0 )
            {
                lastTime = genericsTimestampmS();

                if ( options.intervalReportTime )
                {

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


                    {
                        struct TPIUCommsStats *c = etmdecCommsStats( &_r.f );

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

                    genericsPrintf( C_RESET EOL );
                }
            }
        }

        close( sourcefd );

        if ( options.fileTerminate )
        {
            _r.ending = true;
        }
    }

    return -ESRCH;
}
// ====================================================================================================
