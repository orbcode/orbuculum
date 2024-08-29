/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * ITM Dumper for Orbuculum
 * ========================
 *
 */

#include <unistd.h>
#include <ctype.h>
#include <stdio.h>
#include <getopt.h>
#include <signal.h>

#include "generics.h"
#include "uthash.h"
#include "git_version_info.h"
#include "generics.h"
#include "tpiuDecoder.h"
#include "oflow.h"
#include "itmDecoder.h"
#include "stream.h"

#include "nw.h"

#define MAX_STRING_LENGTH (256)              /* Maximum length that will be output from a fifo for a single event */

#define DEFAULT_OUTFILE "/dev/stdout"
#define DEFAULT_TIMELEN 10000

enum Prot { PROT_OFLOW, PROT_ITM, PROT_TPIU, PROT_UNKNOWN };
const char *protString[] = {"OFLOW", "ITM", "TPIU", NULL};

/* ---------- CONFIGURATION ----------------- */

struct                                      /* Record for options, either defaults or from command line */
{
    /* Config information */
    bool forceITMSync;
    uint32_t tag;

    /* File to output dump to */
    char *outfile;

    /* Do we need to write synchronously */
    bool writeSync;

    /* How long to dump */
    uint32_t timelen;

    /* Supress colour in output */
    bool mono;
    /* Source information */
    int port;
    char *server;
    enum Prot protocol;
} options =
{
    .forceITMSync = true,
    .tag = 1,
    .outfile = DEFAULT_OUTFILE,
    .timelen = DEFAULT_TIMELEN,
    .port = OTCLIENT_SERVER_PORT,
    .server = "localhost"
};

/* ----------- LIVE STATE ----------------- */
struct
{
    /* The decoders and the packets from them */
    struct ITMDecoder i;
    struct ITMPacket h;
    struct TPIUDecoder t;
    struct TPIUPacket p;
    struct OFLOW c;
    bool   ending;
} _r;

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Internally available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
uint64_t _timestamp( void )

{
    struct timeval te;
    gettimeofday( &te, NULL ); // get current time
    uint64_t milliseconds = te.tv_sec * 1000LL + te.tv_usec / 1000; // caculate milliseconds
    return milliseconds;
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
    if ( PROT_TPIU == options.protocol )
    {
        switch ( TPIUPump( &_r.t, c ) )
        {
            // ------------------------------------
            case TPIU_EV_NEWSYNC:
            case TPIU_EV_SYNCED:
                ITMDecoderForceSync( &_r.i, true );
                break;

            // ------------------------------------
            case TPIU_EV_RXING:
            case TPIU_EV_NONE:
                break;

            // ------------------------------------
            case TPIU_EV_UNSYNCED:
                ITMDecoderForceSync( &_r.i, false );
                break;

            // ------------------------------------
            case TPIU_EV_RXEDPACKET:
                if ( !TPIUGetPacket( &_r.t, &_r.p ) )
                {
                    genericsReport( V_WARN, "TPIUGetPacket fell over" EOL );
                }

                for ( uint32_t g = 0; g < _r.p.len; g++ )
                {
                    if ( _r.p.packet[g].s == options.tag )
                    {
                        ITMPump( &_r.i, _r.p.packet[g].d );
                        continue;
                    }

                    if ( _r.p.packet[g].s != 0 )
                    {
                        genericsReport( V_DEBUG, "Unknown TPIU channel %02x" EOL, _r.p.packet[g].s );
                    }
                }

                break;

            // ------------------------------------
            case TPIU_EV_ERROR:
                genericsReport( V_WARN, "****ERROR****" EOL );
                break;
                // ------------------------------------
        }
    }
    else
    {
        /* There's no TPIU in use, so this goes straight to the ITM layer */
        ITMPump( &_r.i, c );
    }
}
// ====================================================================================================
void _printHelp( const char *const progName )

{
    genericsPrintf( "Usage: %s [options]" EOL, progName );
    genericsPrintf( "    -h, --help:         This help" EOL );
    genericsPrintf( "    -l, --length:       <timelen> Length of time in ms to record from point of acheiving sync (defaults to %dmS)" EOL, options.timelen );
    genericsPrintf( "    -M, --no-colour:    Supress colour in output" EOL );
    genericsPrintf( "    -n, --itm-sync:     Enforce sync requirement for ITM (i.e. ITM needs to issue syncs)" EOL );
    genericsPrintf( "    -o, --output-file:  <filename> to be used for dump file (defaults to %s)" EOL, options.outfile );
    genericsPrintf( "    -p, --protocol:     Protocol to communicate. Defaults to OFLOW if -s is not set, otherwise ITM unless" EOL \
                    "                        explicitly set to TPIU to decode TPIU frames on channel set by -t" EOL );
    genericsPrintf( "    -s, --server:       <Server>:<Port> to use" EOL );
    genericsPrintf( "    -t, --tag:          <stream> Which TPIU stream or OFLOW tag to use (normally 1)" EOL );
    genericsPrintf( "    -v, --verbose:      <level> Verbose mode 0(errors)..3(debug)" EOL );
    genericsPrintf( "    -V, --version:      Print version and exit" EOL );
    genericsPrintf( "    -w, --sync-write:   Write synchronously to the output file after every packet" EOL );
}
// ====================================================================================================
void _printVersion( void )

{
    genericsPrintf( "orbdump version " GIT_DESCRIBE EOL );
}
// ====================================================================================================
static struct option _longOptions[] =
{
    {"help", no_argument, NULL, 'h'},
    {"length", required_argument, NULL, 'l'},
    {"itm-sync", no_argument, NULL, 'n'},
    {"no-colour", no_argument, NULL, 'M'},
    {"no-color", no_argument, NULL, 'M'},
    {"output-file", required_argument, NULL, 'o'},
    {"protocol", required_argument, NULL, 'p'},
    {"server", required_argument, NULL, 's'},
    {"tag", required_argument, NULL, 't'},
    {"verbose", required_argument, NULL, 'v'},
    {"version", no_argument, NULL, 'V'},
    {"sync-write", no_argument, NULL, 'w'},
    {NULL, no_argument, NULL, 0}
};
// ====================================================================================================
bool _processOptions( int argc, char *argv[] )

{
    int c, optionIndex = 0;
    bool protExplicit = false;
    bool serverExplicit = false;
    bool portExplicit = false;

    while ( ( c = getopt_long ( argc, argv, "hVl:Mno:p:s:t:v:w", _longOptions, &optionIndex ) ) != -1 )
        switch ( c )
        {
            case 'o':
                options.outfile = optarg;
                break;

            case 'l':
                options.timelen = atoi( optarg );
                break;

            case 'M':
                options.mono = true;
                break;

            case 'n':
                options.forceITMSync = false;
                break;

            case 'w':
                options.writeSync = true;
                break;

            case 'v':
                if ( !isdigit( *optarg ) )
                {
                    genericsReport( V_ERROR, "-v requires a numeric argument." EOL );
                    return false;
                }

                genericsSetReportLevel( atoi( optarg ) );
                break;

            case 't':
                options.tag = atoi( optarg );
                break;

            // ------------------------------------

            case 'p':
                options.protocol = PROT_UNKNOWN;
                protExplicit = true;

                for ( int i = 0; protString[i]; i++ )
                {
                    if ( !strcmp( protString[i], optarg ) )
                    {
                        options.protocol = i;
                        break;
                    }
                }

                if ( options.protocol == PROT_UNKNOWN )
                {
                    genericsReport( V_ERROR, "Unrecognised protocol type" EOL );
                    return false;
                }

                break;

            // ------------------------------------
            case 's':
                options.server = optarg;
                serverExplicit = true;

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
                    options.port = NWCLIENT_SERVER_PORT;
                }
                else
                {
                    portExplicit = true;
                }

                break;

            // ------------------------------------
            case 'h':
                _printHelp( argv[0] );
                return false;

            // ------------------------------------
            case 'V':
                _printVersion();
                return false;

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

            default:
                genericsReport( V_ERROR, "Unknown option %c" EOL, optopt );
                return false;
        }

    /* If we set an explicit server and port and didn't set a protocol chances are we want ITM, not OFLOW */
    if ( serverExplicit && !protExplicit )
    {
        options.protocol = PROT_ITM;
    }

    if ( ( options.protocol == PROT_TPIU ) && !portExplicit )
    {
        options.port = NWCLIENT_SERVER_PORT;
    }

    genericsReport( V_INFO, "orbdump version " GIT_DESCRIBE EOL );
    genericsReport( V_INFO, "Server    : %s:%d" EOL, options.server, options.port );
    genericsReport( V_INFO, "ForceSync : %s" EOL, options.forceITMSync ? "true" : "false" );


    if ( options.timelen )
    {
        genericsReport( V_INFO, "Rec Length: %dmS" EOL, options.timelen );
    }
    else
    {
        genericsReport( V_INFO, "Rec Length: Unlimited" EOL );
    }

    genericsReport( V_INFO, "Sync Write: %s" EOL, options.writeSync ? "true" : "false" );

    switch ( options.protocol )
    {
        case PROT_OFLOW:
            genericsReport( V_INFO, "Decoding OFLOW (Orbuculum) with ITM in stream %d" EOL, options.tag );
            break;

        case PROT_ITM:
            genericsReport( V_INFO, "Decoding ITM" EOL );
            break;

        case  PROT_TPIU:
            genericsReport( V_INFO, "Using TPIU with ITM in stream %d" EOL, options.tag );
            break;

        default:
            genericsReport( V_INFO, "Decoding unknown" EOL );
            break;
    }

    return true;
}

// ====================================================================================================

static void _OFLOWpacketRxed ( struct OFLOWFrame *p, void *param )

{
    if ( !p->good )
    {
        genericsReport( V_WARN, "Bad packet received" EOL );
    }
    else
    {
        if ( p->tag == options.tag )
        {
            for ( int i = 0; i < p->len; i++ )
            {
                ITMPump( &_r.i, p->d[i] );
            }
        }
    }
}

// ====================================================================================================

static struct Stream *_tryOpenStream( void )
{
    return streamCreateSocket( options.server, options.port );
}
// ====================================================================================================
static void _intHandler( int sig )

{
    /* CTRL-C exit is not an error... */
    _r.ending = true;
}
// ====================================================================================================
int main( int argc, char *argv[] )
{
    uint8_t cbw[TRANSFER_SIZE];
    uint64_t firstTime = 0;
    size_t octetsRxed = 0;
    FILE *opFile;

    ssize_t t;
    size_t receivedSize;

    bool haveSynced = false;
    bool alreadyReported = false;
    struct Stream *stream;

    if ( !_processOptions( argc, argv ) )
    {
        exit( -1 );
    }

    genericsScreenHandling( !options.mono );

    /* Reset the TPIU handler before we start */
    TPIUDecoderInit( &_r.t );
    ITMDecoderInit( &_r.i, options.forceITMSync );
    OFLOWInit( &_r.c );
    stream = _tryOpenStream();

    /* This ensures the signal handler gets called */
    if ( SIG_ERR == signal( SIGINT, _intHandler ) )
    {
        genericsExit( -1, "Failed to establish Int handler" EOL );
    }

    if ( stream == NULL )
    {

        stream = _tryOpenStream();

        if ( stream != NULL )
        {
            if ( alreadyReported )
            {
                genericsReport( V_INFO, "Connected" EOL );
                alreadyReported = false;
            }
        }

        if ( !alreadyReported )
        {
            genericsReport( V_INFO, EOL "No connection" EOL );
            alreadyReported = true;
        }

        /* Checking every 100ms for a connection is quite often enough */
        usleep( 10000 );
    }

    /* .... and the file to dump it into */
    opFile = fopen( options.outfile, "wb" );

    if ( !opFile )
    {
        genericsReport( V_ERROR, "Could not open output file for writing" EOL );
        return -2;
    }

    genericsReport( V_INFO, "Waiting for sync" EOL );

    /* Start the process of collecting the data */
    while ( !_r.ending )
    {
        enum ReceiveResult result = stream->receive( stream, cbw, TRANSFER_SIZE, NULL, &receivedSize );

        if ( result != RECEIVE_RESULT_OK )
        {
            if ( result == RECEIVE_RESULT_EOF )
            {
                break;
            }

            if ( result == RECEIVE_RESULT_ERROR )
            {
                genericsReport( V_ERROR, "Reading from connection failed" EOL );
                return -2;
            }
        }

        if ( ( options.timelen ) && ( ( firstTime != 0 ) && ( ( _timestamp() - firstTime ) > options.timelen ) ) )
        {
            /* This packet arrived at the end of the window...finish the write process */
            break;
        }


        if ( PROT_OFLOW == options.protocol )
        {
            OFLOWPump( &_r.c, cbw, receivedSize, _OFLOWpacketRxed, &_r );
        }
        else
        {

            uint8_t *c = cbw;

            t = receivedSize;

            while ( t-- )
            {
                _protocolPump( *c++ );
            }
        }

        /* Check to make sure there's not an unexpected TPIU in here */
        if ( ITMDecoderGetStats( &_r.i )->tpiuSyncCount )
        {
            genericsReport( V_WARN, "Got a TPIU sync while decoding ITM...did you miss a -t option?" EOL );
            break;
        }

        /* ... now check if we've acheived sync so can write frames */
        if ( !haveSynced )
        {
            if ( !ITMDecoderIsSynced( &_r.i ) )
            {
                continue;
            }

            haveSynced = true;
            /* Fill in the time to start from */
            firstTime = _timestamp();

            genericsReport( V_INFO, "Started recording" EOL );
        }

        octetsRxed += fwrite( cbw, 1, receivedSize, opFile );

        if ( !ITMDecoderIsSynced( &_r.i ) )
        {
            genericsReport( V_WARN, "Warning:Sync lost while writing output" EOL );
        }

        if ( options.writeSync )
        {
#if defined(WIN32)
            _flushall();
#else
            sync();
#endif
        }
    }

    stream->close( stream );
    free( stream );
    fclose( opFile );

    if ( receivedSize <= 0 )
    {
        genericsReport( V_ERROR, "Network Read failed" EOL );
        return -2;
    }

    genericsReport( V_INFO, "Wrote %ld bytes of data" EOL, octetsRxed );

    return 0;
}
// ====================================================================================================
