/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * ITM Splitter for Orbuculum
 * ==========================
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include <getopt.h>

#include "git_version_info.h"
#include "generics.h"
#include "fileWriter.h"
#include "stream.h"
#include "nw.h"

#include "itmfifos.h"


//#define DUMP_BLOCK

/* Record for options, either defaults or from command line */
struct
{
    /* Config information */
    bool filewriter;                    /* Supporting filewriter functionality */
    char *fwbasedir;                    /* Base directory for filewriter output */
    bool permafile;                     /* Use permanent files rather than fifos */

    /* Source information */
    char *file;                         /* File host connection */
    bool fileTerminate;                 /* Terminate when file read isn't successful */

    int port;                           /* Source information */
    char *server;

} options =
{
    .port = NWCLIENT_SERVER_PORT,
    .server = "localhost"
};

struct
{
    struct itmfifosHandle *f;           /* Link to the itmfifo subsystem */
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
static void _printHelp( const char *const progName )

{
    genericsPrintf( "Usage: %s [options]" EOL, progName );
    genericsPrintf( "    -b, --basedir:      <basedir> for channels" EOL );
    genericsPrintf( "    -c, --channel:      <Number>,<Name>,<Format> of channel to populate (repeat per channel)" EOL );
    genericsPrintf( "    -e, --eof:          When reading from file, terminate at end of file rather than waiting for further input" EOL );
    genericsPrintf( "    -f, --input-file:   <filename> Take input from specified file" EOL );
    genericsPrintf( "    -h, --help:         This help" EOL );
    genericsPrintf( "    -P, --permanent:    Create permanent files rather than fifos" EOL );
    genericsPrintf( "    -t, --tpiu:         <channel> Use TPIU decoder on specified channel, normally 1" EOL );
    genericsPrintf( "    -v, --verbose:      <level> Verbose mode 0(errors)..3(debug)" EOL );
    genericsPrintf( "    -V, --version:      Print version and exit" EOL );
    genericsPrintf( "    -W, --writer-path:  <path> Enable filewriter functionality using specified base path" EOL );
}
// ====================================================================================================
void _printVersion( void )

{
    genericsPrintf( "orbfifo version " GIT_DESCRIBE );
}
// ====================================================================================================
struct option _longOptions[] =
{
    {"basedir", required_argument, NULL, 'b'},
    {"channel", required_argument, NULL, 'c'},
    {"eof", no_argument, NULL, 'e'},
    {"input-file", required_argument, NULL, 'f'},
    {"help", no_argument, NULL, 'h'},
    {"permanent", no_argument, NULL, 'P'},
    {"tpiu", required_argument, NULL, 't'},
    {"verbose", required_argument, NULL, 'v'},
    {"version", no_argument, NULL, 'V'},
    {"writer-path", required_argument, NULL, 'W'},
    {NULL, no_argument, NULL, 0}
};
// ====================================================================================================
static bool _processOptions( int argc, char *argv[] )

{
    int c, optionIndex = 0;
#define DELIMITER ','

    char *chanConfig;
    char *chanName;
    uint chan;
    char *chanIndex;

    while ( ( c = getopt_long ( argc, argv, "b:c:ef:hVn:Pt:v:w:", _longOptions, &optionIndex ) ) != -1 )
        switch ( c )
        {
            // ------------------------------------

            case 'b':
                itmfifoSetChanPath( _r.f, optarg );
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
            case 'V':
                _printVersion();
                return false;

            // ------------------------------------

            case 'n':
                itmfifoSetForceITMSync( _r.f, false );
                break;

            // ------------------------------------

            case 'P':
                options.permafile = true;
                break;

            // ------------------------------------

            case 't':
                itmfifoSetUseTPIU( _r.f, true );
                itmfifoSettpiuITMChannel( _r.f, atoi( optarg ) );
                break;

            // ------------------------------------

            case 'v':
                genericsSetReportLevel( atoi( optarg ) );
                break;

            // ------------------------------------

            case 'W':
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
                    itmfifoSetChannel( _r.f, chan, chanName, NULL );
                    break;
                }

                *chanIndex++ = 0;
                itmfifoSetChannel( _r.f, chan, chanName, genericsUnescape( chanIndex ) );
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
    genericsReport( V_INFO, "orbfifo version " GIT_DESCRIBE EOL );    
    if ( itmfifoGetUseTPIU( _r.f ) )
    {
        genericsReport( V_INFO, "Using TPIU  : true (ITM on channel %d)" EOL, itmfifoGettpiuITMChannel( _r.f ) );
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
        if ( itmfifoGetChannelName( _r.f, g ) )
        {
            genericsReport( V_INFO, "         %02d [%s] [%s]" EOL, g, genericsEscape( itmfifoGetChannelFormat( _r.f, g ) ? : "RAW" ), itmfifoGetChannelName( _r.f, g ) );
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
            itmfifoProtocolPump( _r.f, *cbw++ );
        }
    }

}
// ====================================================================================================
static void _doExit( void )

{
    _r.ending = true;
    itmfifoShutdown( _r.f );
    /* Give them a bit of time, then we're leaving anyway */
    usleep( 200 );
}
// ====================================================================================================
int main( int argc, char *argv[] )

{
    uint8_t cbw[TRANSFER_SIZE];
    size_t t;
    struct Stream *stream = NULL;
    int64_t lastTime;
    struct timeval tv;
    int32_t remainTime;

    /* Setup fifos with forced ITM sync, no TPIU and TPIU on channel 1 if its engaged later */
    _r.f = itmfifoInit( true, false, 1 );
    assert( _r.f );

    if ( !_processOptions( argc, argv ) )
    {
        /* processOptions generates its own error messages */
        genericsExit( -1, "" EOL );
    }

    itmfifoUsePermafiles( _r.f, options.permafile );

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

    if ( ! ( itmfifoCreate( _r.f ) ) )
    {
        genericsExit( -1, "Failed to make channel devices" EOL );
    }

    /* Start the filewriter */
    itmfifoFilewriter( _r.f, options.filewriter, options.fwbasedir );

    while ( !_r.ending )
    {
        if ( options.file != NULL )
        {
            stream = streamCreateFile( options.file );
        }
        else
        {
            while ( 1 )
            {
                stream = streamCreateSocket( options.server, options.port );

                if ( !stream )
                {
                    break;
                }

                perror( "Could not connect" );
                usleep( 1000000 );
            }
        }

        while ( !_r.ending )
        {
            remainTime = ( ( lastTime + 1000 - genericsTimestampmS() ) * 1000 ) - 500;

            if ( remainTime > 0 )
            {
                tv.tv_sec = remainTime / 1000000;
                tv.tv_usec  = remainTime % 1000000;
            }

            enum ReceiveResult result = stream->receive( stream, cbw, TRANSFER_SIZE, &tv, ( size_t * )&t );

            if ( ( result == RECEIVE_RESULT_EOF ) || ( result == RECEIVE_RESULT_ERROR ) )
            {
                break;
            }

            _processBlock( t, cbw );
        }

        stream->close( stream );
        free( stream );

        if ( options.fileTerminate )
        {
            _r.ending = true;
        }
    }

    return -ESRCH;
}
// ====================================================================================================
