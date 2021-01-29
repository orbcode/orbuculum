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
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>

#include "git_version_info.h"
#include "generics.h"
#include "fileWriter.h"

#include "fifos.h"

#define TRANSFER_SIZE       (4096)
#define SERVER_PORT         (3443)           /* Server port definition */

//#define DUMP_BLOCK

/* Record for options, either defaults or from command line */
struct
{
    /* Config information */
    bool filewriter;                    /* Supporting filewriter functionality */
    char *fwbasedir;                    /* Base directory for filewriter output */
    bool permafile;                     /* Use permanent files rather than fifos */

    /* Source information */
    uint32_t dataSpeed;                 /* Effective data speed (can be less than link speed!) */
    char *file;                         /* File host connection */
    bool fileTerminate;                 /* Terminate when file read isn't successful */

    uint32_t intervalReportTime;        /* If we want interval reports about performance */
    int port;                           /* Source information */
    char *server;

} options =
{
    .port = SERVER_PORT,
    .server = "localhost"
};

struct
{
    struct fifosHandle *f;              /* Link to the fifo subsystem */
    uint64_t  intervalBytes;            /* Number of bytes transferred in current interval */
    bool      ending;                   /* Flag indicating app is terminating */
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
    genericsPrintf( "        a: <speed> of comms link in bps for stats" EOL );
    genericsPrintf( "        b: <basedir> for channels" EOL );
    genericsPrintf( "        c: <Number>,<Name>,<Format> of channel to populate (repeat per channel)" EOL );
    genericsPrintf( "        e: When reading from file, terminate at end of file rather than waiting for further input" EOL );
    genericsPrintf( "        f: <filename> Take input from specified file" EOL );
    genericsPrintf( "        h: This help" EOL );
    genericsPrintf( "        i: <channel> Set ITM Channel in TPIU decode (defaults to 1)" EOL );
    genericsPrintf( "        m: <interval> Output monitor information about the link at <interval>ms" EOL );
    genericsPrintf( "        P: Create permanent files rather than fifos" EOL );
    genericsPrintf( "        t: Use TPIU decoder" EOL );
    genericsPrintf( "        v: <level> Verbose mode 0(errors)..3(debug)" EOL );
    genericsPrintf( "        w: <path> Enable filewriter functionality using specified base path" EOL );
}
// ====================================================================================================
static int _processOptions( int argc, char *argv[] )

{
    int c;
#define DELIMITER ','

    char *chanConfig;
    char *chanName;
    uint chan;
    char *chanIndex;

    while ( ( c = getopt ( argc, argv, "a:b:c:ef:i:hm:n:Ptv:w:" ) ) != -1 )
        switch ( c )
        {
            // ------------------------------------
            case 'a':
                options.dataSpeed = atoi( optarg );
                break;

            // ------------------------------------

            case 'b':
                fifoSetChanPath( _r.f, optarg );
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

            case 'i':
                fifoSettpiuITMChannel( _r.f, atoi( optarg ) );
                break;

            // ------------------------------------

            case 'm':
                options.intervalReportTime = atoi( optarg );
                break;

            // ------------------------------------

            case 'n':
                fifoSetForceITMSync( _r.f, false );
                break;

            // ------------------------------------

            case 'P':
                options.permafile = true;
                break;

            // ------------------------------------

            case 't':
                fifoSetUseTPIU( _r.f, true );
                break;

            // ------------------------------------

            case 'v':
                genericsSetReportLevel( atoi( optarg ) );
                break;

            // ------------------------------------

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
    genericsReport( V_INFO, "BasePath    : %s" EOL, fifoGetChanPath( _r.f ) );
    genericsReport( V_INFO, "ForceSync   : %s" EOL, fifoGetForceITMSync( _r.f ) ? "true" : "false" );
    genericsReport( V_INFO, "Permafile   : %s" EOL, options.permafile ? "true" : "false" );

    if ( options.intervalReportTime )
    {
        genericsReport( V_INFO, "Report Intv : %d mS" EOL, options.intervalReportTime );
    }

    if ( options.dataSpeed )
    {
        genericsReport( V_INFO, "Max Data Rt : %d bps" EOL, options.dataSpeed );
    }

    if ( fifoGetUseTPIU( _r.f ) )
    {
        genericsReport( V_INFO, "Using TPIU  : true (ITM on channel %d)" EOL, fifoGettpiuITMChannel( _r.f ) );
    }
    else
    {
        genericsReport( V_INFO, "Using TPIU  : false" EOL );
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

    genericsReport( V_INFO, "Channels    :" EOL );

    for ( int g = 0; g < NUM_CHANNELS; g++ )
    {
        if ( fifoGetChannelName( _r.f, g ) )
        {
            genericsReport( V_INFO, "         %02d [%s] [%s]" EOL, g, genericsEscape( fifoGetChannelFormat( _r.f, g ) ? : "RAW" ), fifoGetChannelName( _r.f, g ) );
        }
    }

    genericsReport( V_INFO, "         HW [Predefined] [" HWFIFO_NAME "]" EOL );

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
            fifoProtocolPump( _r.f, *cbw++ );
        }
    }

}
// ====================================================================================================
static void _doExit( void )

{
    _r.ending = true;
    fifoShutdown( _r.f );
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

    /* Setup fifos with forced ITM sync, no TPIU and TPIU on channel 1 if its engaged later */
    _r.f = fifoInit( true, false, 1 );
    assert( _r.f );

    if ( !_processOptions( argc, argv ) )
    {
        /* processOptions generates its own error messages */
        genericsExit( -1, "" EOL );
    }

    fifoUsePermafiles( _r.f, options.permafile );

    /* Make sure the fifos get removed at the end */
    atexit( _doExit );

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

    if ( ! ( fifoCreate( _r.f ) ) )
    {
        genericsExit( -1, "Failed to make channel devices" EOL );
    }

    /* Start the filewriter */
    fifoFilewriter( _r.f, options.filewriter, options.fwbasedir );

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
            serv_addr.sin_port = htons( options.port );

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

                    if ( options.dataSpeed > 100 )
                    {
                        /* Conversion to percentage done as a division to avoid overflow */
                        uint32_t fullPercent = ( snapInterval * 100 ) / options.dataSpeed;
                        genericsPrintf( "(" C_DATA " %3d%% " C_RESET "full)", ( fullPercent > 100 ) ? 100 : fullPercent );
                    }

                    if ( fifoGetUseTPIU( _r.f ) )
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
