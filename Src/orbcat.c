/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * ITM Catter for Orbuculum
 * ========================
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include <getopt.h>
#include <time.h>
#include <sys/time.h>

#include "nw.h"
#include "git_version_info.h"
#include "generics.h"
#include "tpiuDecoder.h"
#include "itmDecoder.h"
#include "msgDecoder.h"
#include "msgSeq.h"
#include "stream.h"

#define NUM_CHANNELS  32
#define HW_CHANNEL    (NUM_CHANNELS)      /* Make the hardware fifo on the end of the software ones */

#define MAX_STRING_LENGTH (100)           /* Maximum length that will be output from a fifo for a single event */
#define DEFAULT_TS_TRIGGER '\n'           /* Default trigger character for timestamp output */

#define MSG_REORDER_BUFLEN  (10)          /* Maximum number of samples to re-order for timekeeping */
#define ONE_SEC_IN_USEC     (1000000)     /* Used for time conversions...usec in one sec */

/* Formats for timestamping */
#define REL_FORMAT            "%6" PRIu64 ".%03" PRIu64 "|"
#define REL_FORMAT_INIT       "   Initial|"
#define DEL_FORMAT            "%3" PRIu64 ".%03" PRIu64 "|"
#define DEL_FORMAT_CTD           "      +|"
#define DEL_FORMAT_INIT          "Initial|"
#define ABS_FORMAT_TM   "%d/%b/%y %H:%M:%S"
#define ABS_FORMAT              "%s.%03" PRIu64" |"
#define STAMP_FORMAT          "%12" PRIu64 "|"
#define STAMP_FORMAT_MS        "%8" PRIu64 ".%03" PRIu64 "_%03" PRIu64 "|"
#define STAMP_FORMAT_MS_DELTA  "%5" PRIu64 ".%03" PRIu64 "_%03" PRIu64 "|"

enum TSType { TSNone, TSAbsolute, TSRelative, TSDelta, TSStamp, TSStampDelta, TSNumTypes };
const char *tsTypeString[TSNumTypes] = { "None", "Absolute", "Relative", "Delta", "System Timestamp", "System Timestamp Delta" };

// Record for options, either defaults or from command line
struct
{
    /* Config information */
    bool useTPIU;
    uint32_t tpiuChannel;
    bool forceITMSync;
    uint64_t cps;                            /* Cycles per second for target CPU */

    enum TSType tsType;
    char *tsLineFormat;
    char tsTrigger;

    /* Sink information */
    char *presFormat[NUM_CHANNELS + 1];

    /* Source information */
    int port;
    char *server;

    char *file;                              /* File host connection */
    bool endTerminate;                       /* Terminate when file/socket "ends" */

} options =
{
    .forceITMSync = true,
    .tpiuChannel = 1,
    .port = NWCLIENT_SERVER_PORT,
    .server = "localhost",
    .tsTrigger = DEFAULT_TS_TRIGGER
};

struct
{
    /* The decoders and the packets from them */
    struct ITMDecoder i;
    struct MSGSeq    d;
    struct ITMPacket h;
    struct TPIUDecoder t;
    struct TPIUPacket p;
    enum timeDelay timeStatus;           /* Indicator of if this time is exact */
    uint64_t timeStamp;                  /* Latest received time */
    uint64_t lastTimeStamp;              /* Last received time */
    uint64_t te;                         /* Time on host side for line stamping */
    bool gotte;                          /* Flag that we have the initial time */
    bool inLine;                         /* We are in progress with a line that has been timestamped already */
    uint64_t oldte;                      /* Old time for interval calculation */
} _r;

// ====================================================================================================
uint64_t _timestamp( void )

{
    struct timeval te;
    gettimeofday( &te, NULL ); // get current time
    return te.tv_sec * ONE_SEC_IN_USEC + te.tv_usec;
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Handler for individual message types from SWO
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
static void _outputTimestamp( void )

{
    /* Lets output a timestamp */
    char opConstruct[MAX_STRING_LENGTH];
    uint64_t res;
    struct tm tm;
    time_t td;

    switch ( options.tsType )
    {
        case TSNone: // -----------------------------------------------------------------------
            break;

        case TSRelative: // -------------------------------------------------------------------
            if ( !_r.gotte )
            {
                /* Get the starting time */
                _r.oldte = _timestamp();
                _r.gotte = true;
                fprintf( stdout, REL_FORMAT_INIT );
            }
            else
            {
                uint64_t now = _timestamp();
                res = now - _r.oldte;
                fprintf( stdout, REL_FORMAT, res / ONE_SEC_IN_USEC, ( res / ( ONE_SEC_IN_USEC / 1000 ) ) % 1000 );
            }

            break;

        case TSAbsolute: // -------------------------------------------------------------------
            res = _timestamp();
            td = ( time_t )res / ONE_SEC_IN_USEC;
            tm = *localtime( &td );
            strftime( opConstruct, MAX_STRING_LENGTH, ABS_FORMAT_TM, &tm );
            fprintf( stdout, ABS_FORMAT, opConstruct, ( res / ( ONE_SEC_IN_USEC / 1000 ) ) % 1000 );
            break;

        case TSDelta: // ----------------------------------------------------------------------
            if ( !_r.gotte )
            {
                /* Get the starting time */
                _r.oldte = _timestamp();
                _r.gotte = true;
                fprintf( stdout, DEL_FORMAT_INIT );
            }
            else
            {
                uint64_t t = _timestamp();
                res = t - _r.oldte;
                _r.oldte = t;

                if ( res / 1000 )
                {
                    fprintf( stdout, DEL_FORMAT, res / ONE_SEC_IN_USEC, ( res / 1000 ) % 1000 );
                }
                else
                {
                    fprintf( stdout, DEL_FORMAT_CTD );
                }
            }

            break;

        case TSStamp: // -----------------------------------------------------------------------
            if ( options.cps )
            {
                uint64_t tms = ( _r.timeStamp * 1000000 ) / options.cps;
                fprintf( stdout, STAMP_FORMAT_MS, tms / 1000000, ( tms / 1000 ) % 1000, tms % 1000 );
            }
            else
            {
                fprintf( stdout, STAMP_FORMAT, _r.timeStamp );
            }

            break;

        case TSStampDelta: // ------------------------------------------------------------------
            if ( !_r.gotte )
            {
                _r.lastTimeStamp = _r.timeStamp;
                _r.gotte = true;
            }

            uint64_t delta = _r.timeStamp - _r.lastTimeStamp;
            _r.lastTimeStamp = _r.timeStamp;

            if ( options.cps )
            {
                uint64_t tms = ( delta * 1000000 ) / options.cps;
                fprintf( stdout, STAMP_FORMAT_MS_DELTA, tms / 1000000, ( tms / 1000 ) % 1000, tms % 1000 );
            }
            else
            {
                fprintf( stdout, STAMP_FORMAT, delta );
            }

            break;

        default: // ----------------------------------------------------------------------------
            assert( false );
    }
}
// ====================================================================================================
static void _outputText( char *p )

{
    /* Process the buffer and make sure it gets timestamped correctly as it's output */

    char *q;

    while ( *p )
    {
        /* If this is the first character in a new line, then we need to generate a timestamp */
        if ( !_r.inLine )
        {
            _outputTimestamp();
            _r.inLine = true;
        }

        /* See if there is a trigger in these data...if so then output everything prior to it */
        q = strchr( p, options.tsTrigger );

        if ( q )
        {
            *q = 0;
            fprintf( stdout, "%s" EOL, p );
            /* Once we've output these data then we're not in a line any more */
            _r.inLine = false;
        }
        else
        {
            /* Just output the whole of the data we've got, then we're done */
            fputs( p, stdout );
            break;
        }

        /* Move past this trigger in case there are more data to output ... this will be \0 if not */
        p = q + 1;
    }
}
// ====================================================================================================
static void _handleSW( struct swMsg *m, struct ITMDecoder *i )

{
    assert( m->msgtype == MSG_SOFTWARE );
    char opConstruct[MAX_STRING_LENGTH];

    char *p = opConstruct;

    /* Make sure line is empty by default */
    *p = 0;

    /* Print anything we want to output into the buffer */
    if ( ( m->srcAddr < NUM_CHANNELS ) && ( options.presFormat[m->srcAddr] ) )
    {
        // formatted output....start with specials
        if ( strstr( options.presFormat[m->srcAddr], "%f" ) )
        {
            /* type punning on same host, after correctly building 32bit val
             * only unsafe on systems where u32/float have diff byte order */
            float *nastycast = ( float * )&m->value;
            p += snprintf( p, MAX_STRING_LENGTH - ( p - opConstruct ), options.presFormat[m->srcAddr], *nastycast, *nastycast, *nastycast, *nastycast );
        }
        else if ( strstr( options.presFormat[m->srcAddr], "%c" ) )
        {
            /* Format contains %c, so execute repeatedly for all characters in sent data */
            uint8_t op[4] = {m->value & 0xff, ( m->value >> 8 ) & 0xff, ( m->value >> 16 ) & 0xff, ( m->value >> 24 ) & 0xff};
            uint32_t l = 0;

            do
            {
                p += snprintf( p, MAX_STRING_LENGTH - ( p - opConstruct ), options.presFormat[m->srcAddr], op[l], op[l], op[l] );
            }
            while ( ++l < m->len );
        }
        else
        {
            p += snprintf( p, MAX_STRING_LENGTH - ( p - opConstruct ), options.presFormat[m->srcAddr], m->value, m->value, m->value, m->value );
        }
    }

    /* Whatever we have, it can be sent for output */
    _outputText( opConstruct );
}
// ====================================================================================================
static void _handleTS( struct TSMsg *m, struct ITMDecoder *i )

{
    assert( m->msgtype == MSG_TS );
    _r.timeStamp += m->timeInc;
}
// ====================================================================================================
static void _itmPumpProcess( char c )

{
    struct msg p;
    struct msg *pp;

    typedef void ( *handlers )( void *decoded, struct ITMDecoder * i );

    /* Handlers for each complete message received */
    static const handlers h[MSG_NUM_MSGS] =
    {
        /* MSG_UNKNOWN */         NULL,
        /* MSG_RESERVED */        NULL,
        /* MSG_ERROR */           NULL,
        /* MSG_NONE */            NULL,
        /* MSG_SOFTWARE */        ( handlers )_handleSW,
        /* MSG_NISYNC */          NULL,
        /* MSG_OSW */             NULL,
        /* MSG_DATA_ACCESS_WP */  NULL,
        /* MSG_DATA_RWWP */       NULL,
        /* MSG_PC_SAMPLE */       NULL,
        /* MSG_DWT_EVENT */       NULL,
        /* MSG_EXCEPTION */       NULL,
        /* MSG_TS */              ( handlers )_handleTS
    };

    /* For any mode except the ones where we collect timestamps from the target we need to send */
    /* the samples out directly to give the host a chance of having accurate timing info. For   */
    /* target-based timestamps we need to re-sequence the messages so that the timestamps are   */
    /* issued _before_ the data they apply to.  These are the two cases.                        */

    if ( ( options.tsType != TSStamp ) && ( options.tsType != TSStampDelta ) )
    {
        if ( ITM_EV_PACKET_RXED == ITMPump( &_r.i, c ) )
        {
            if ( ITMGetDecodedPacket( &_r.i, &p )  )
            {
                assert( p.genericMsg.msgtype < MSG_NUM_MSGS );

                if ( h[p.genericMsg.msgtype] )
                {
                    ( h[p.genericMsg.msgtype] )( &p, &_r.i );
                }
            }
        }
    }
    else
    {

        /* Pump messages into the store until we get a time message, then we can read them out */
        if ( !MSGSeqPump( &_r.d, c ) )
        {
            return;
        }

        /* We are synced timewise, so empty anything that has been waiting */
        while ( ( pp = MSGSeqGetPacket( &_r.d ) ) )
        {
            assert( pp->genericMsg.msgtype < MSG_NUM_MSGS );

            if ( h[pp->genericMsg.msgtype] )
            {
                ( h[pp->genericMsg.msgtype] )( pp, &_r.i );
            }
        }
    }
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Protocol pump for decoding messages
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
static void _protocolPump( uint8_t c )

{
    if ( options.useTPIU )
    {
        switch ( TPIUPump( &_r.t, c ) )
        {
            case TPIU_EV_NEWSYNC:
            case TPIU_EV_SYNCED:
                ITMDecoderForceSync( &_r.i, true );
                break;

            case TPIU_EV_RXING:
            case TPIU_EV_NONE:
                break;

            case TPIU_EV_UNSYNCED:
                ITMDecoderForceSync( &_r.i, false );
                break;

            case TPIU_EV_RXEDPACKET:
                if ( !TPIUGetPacket( &_r.t, &_r.p ) )
                {
                    genericsReport( V_WARN, "TPIUGetPacket fell over" EOL );
                }

                for ( uint32_t g = 0; g < _r.p.len; g++ )
                {
                    if ( _r.p.packet[g].s == options.tpiuChannel )
                    {
                        _itmPumpProcess( _r.p.packet[g].d );
                        continue;
                    }

                    if  ( _r.p.packet[g].s != 0 )
                    {
                        genericsReport( V_DEBUG, "Unknown TPIU channel %02x" EOL, _r.p.packet[g].s );
                    }
                }

                break;

            case TPIU_EV_ERROR:
                genericsReport( V_WARN, "****ERROR****" EOL );
                break;
        }
    }
    else
    {
        _itmPumpProcess( c );
    }
}
// ====================================================================================================
static void _printHelp( const char *const progName )

{
    fprintf( stdout, "Usage: %s [options]" EOL, progName );
    fprintf( stdout, "    -c, --channel:      <Number>,<Format> of channel to add into output stream (repeat per channel)" EOL );
    fprintf( stdout, "    -C, --cpufreq:      <Frequency in KHz> (Scaled) speed of the CPU" EOL
             "                        generally /1, /4, /16 or /64 of the real CPU speed," EOL );
    fprintf( stdout, "    -E, --eof:          Terminate when the file/socket ends/is closed, or wait for more/reconnect" EOL );
    fprintf( stdout, "    -f, --input-file:   <filename> Take input from specified file" EOL );
    fprintf( stdout, "    -g, --trigger:      <char> to use to trigger timestamp (default is newline)" EOL );
    fprintf( stdout, "    -h, --help:         This help" EOL );
    fprintf( stdout, "    -n, --itm-sync:     Enforce sync requirement for ITM (i.e. ITM needs to issue syncs)" EOL );
    fprintf( stdout, "    -s, --server:       <Server>:<Port> to use" EOL );
    fprintf( stdout, "    -t, --tpiu:         <channel>: Use TPIU decoder on specified channel (normally 1)" EOL );
    fprintf( stdout, "    -T, --timestamp:    <a|r|d|s|t>: Add absolute, relative (to session start)," EOL
             "                        delta, system timestamp or system timestamp delta to output. Note" EOL
             "                        a,r & d are host dependent and you may need to run orbuculum with -H." EOL );
    fprintf( stdout, "    -v, --verbose:      <level> Verbose mode 0(errors)..3(debug)" EOL );
    fprintf( stdout, "    -V, --version:      Print version and exit" EOL );
}
// ====================================================================================================
static void _printVersion( void )

{
    genericsPrintf( "orbcat version " GIT_DESCRIBE EOL );
}
// ====================================================================================================
static struct option _longOptions[] =
{
    {"channel", required_argument, NULL, 'c'},
    {"cpufreq", required_argument, NULL, 'C'},
    {"eof", no_argument, NULL, 'E'},
    {"input-file", required_argument, NULL, 'f'},
    {"help", no_argument, NULL, 'h'},
    {"trigger", required_argument, NULL, 'g' },
    {"itm-sync", no_argument, NULL, 'n'},
    {"server", required_argument, NULL, 's'},
    {"tpiu", required_argument, NULL, 't'},
    {"timestamp", required_argument, NULL, 'T'},
    {"verbose", required_argument, NULL, 'v'},
    {"version", no_argument, NULL, 'V'},
    {NULL, no_argument, NULL, 0}
};
// ====================================================================================================
bool _processOptions( int argc, char *argv[] )

{
    int c, optionIndex = 0;
    unsigned int chan;
    char *chanIndex;
#define DELIMITER ','

    while ( ( c = getopt_long ( argc, argv, "c:C:Ef:g:hVns:t:T:v:", _longOptions, &optionIndex ) ) != -1 )
        switch ( c )
        {
            // ------------------------------------
            case 'C':
                options.cps = atoi( optarg ) * 1000;

                if ( options.cps <= 0 )
                {
                    genericsReport( V_ERROR, "cps out of range" EOL );
                    return false;
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

            // ------------------------------------
            case 'E':
                options.endTerminate = true;
                break;

            // ------------------------------------
            case 'f':
                options.file = optarg;
                break;

            // ------------------------------------
            case 'g':
                options.tsTrigger = genericsUnescape( optarg )[0];
                break;

            // ------------------------------------
            case 'n':
                options.forceITMSync = false;
                break;

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
                    options.port = NWCLIENT_SERVER_PORT;
                }

                break;

            // ------------------------------------
            case 't':
                options.useTPIU = true;
                options.tpiuChannel = atoi( optarg );

                if ( options.tpiuChannel <= 0 )
                {
                    genericsReport( V_ERROR, "tpiuChannel out of range" EOL );
                    return false;
                }

                break;

            // ------------------------------------
            case 'T':
                switch ( *optarg )
                {
                    case 'a':
                        options.tsType = TSAbsolute;
                        break;

                    case 'r':
                        options.tsType = TSRelative;
                        break;

                    case 'd':
                        options.tsType = TSDelta;
                        break;

                    case 's':
                        options.tsType = TSStamp;
                        break;

                    case 't':
                        options.tsType = TSStampDelta;
                        break;

                    default:
                        genericsReport( V_ERROR, "Unrecognised Timestamp type" EOL );
                        return false;
                }

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
                    genericsReport( V_ERROR, "Report level out of range" EOL );
                    return false;
                }

                break;

            // ------------------------------------
            /* Individual channel setup */
            case 'c':
                chanIndex = optarg;

                chan = atoi( optarg );

                if ( chan >= NUM_CHANNELS )
                {
                    genericsReport( V_ERROR, "Channel index out of range" EOL );
                    return false;
                }

                /* Scan for format */
                while ( ( *chanIndex ) && ( *chanIndex != DELIMITER ) )
                {
                    chanIndex++;
                }

                if ( !*chanIndex )
                {
                    genericsReport( V_ERROR, "Channel output spec missing" EOL );
                    return false;
                }

                /* Step over delimiter */
                chanIndex++;

                /* Scan past any whitespace */
                while ( ( *chanIndex ) && ( isspace( *chanIndex ) ) )
                {
                    chanIndex++;
                }

                if ( !*chanIndex )
                {
                    genericsReport( V_ERROR, "No output format for channel %d (avoid spaces before the output spec)" EOL, chan );
                    return false;
                }

                options.presFormat[chan] = strdup( genericsUnescape( chanIndex ) );
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
                return false;
                // ------------------------------------
        }

    if ( ( options.useTPIU ) && ( !options.tpiuChannel ) )
    {
        genericsReport( V_ERROR, "TPIU set for use but no channel set for ITM output" EOL );
        return false;
    }

    genericsReport( V_INFO, "orbcat version " GIT_DESCRIBE EOL );
    genericsReport( V_INFO, "Server     : %s:%d" EOL, options.server, options.port );
    genericsReport( V_INFO, "ForceSync  : %s" EOL, options.forceITMSync ? "true" : "false" );
    genericsReport( V_INFO, "Timestamp  : %s" EOL, tsTypeString[options.tsType] );

    if ( options.cps )
    {
        genericsReport( V_INFO, "S-CPU Speed: %d KHz" EOL, options.cps );
    }

    if ( options.tsType != TSNone )
    {
        char unesc[2] = {options.tsTrigger, 0};
        genericsReport( V_INFO, "TriggerChr : '%s'" EOL, genericsEscape( unesc ) );
    }

    if ( options.file )
    {

        genericsReport( V_INFO, "Input File : %s", options.file );

        if ( options.endTerminate )
        {
            genericsReport( V_INFO, " (Terminate on exhaustion)" EOL );
        }
        else
        {
            genericsReport( V_INFO, " (Ongoing read)" EOL );
        }
    }

    if ( options.useTPIU )
    {
        genericsReport( V_INFO, "Using TPIU : true (ITM on channel %d)" EOL, options.tpiuChannel );
    }
    else
    {
        genericsReport( V_INFO, "Using TPIU : false" EOL );
    }

    genericsReport( V_INFO, "Channels   :" EOL );

    for ( int g = 0; g < NUM_CHANNELS; g++ )
    {
        if ( options.presFormat[g] )
        {
            genericsReport( V_INFO, "             %02d [%s]" EOL, g, genericsEscape( options.presFormat[g] ) );
        }
    }

    return true;
}
// ====================================================================================================
static struct Stream *_tryOpenStream()
{
    if ( options.file != NULL )
    {
        return streamCreateFile( options.file );
    }
    else
    {
        return streamCreateSocket( options.server, options.port );
    }
}
// ====================================================================================================
static void _feedStream( struct Stream *stream )
{
    struct timeval t;
    unsigned char cbw[TRANSFER_SIZE];

    while ( true )
    {
        size_t receivedSize;

        t.tv_sec = 0;
        t.tv_usec = 10000;
        enum ReceiveResult result = stream->receive( stream, cbw, TRANSFER_SIZE, &t, &receivedSize );

        if ( result != RECEIVE_RESULT_OK )
        {
            if ( result == RECEIVE_RESULT_EOF && options.endTerminate )
            {
                return;
            }
            else if ( result == RECEIVE_RESULT_ERROR )
            {
                break;
            }
        }

        unsigned char *c = cbw;

        while ( receivedSize-- )
        {
            _protocolPump( *c++ );
        }

        fflush( stdout );
    }
}

// ====================================================================================================
int main( int argc, char *argv[] )

{
    bool alreadyReported = false;

    if ( !_processOptions( argc, argv ) )
    {
        exit( -1 );
    }

    /* Reset the TPIU handler before we start */
    TPIUDecoderInit( &_r.t );
    ITMDecoderInit( &_r.i, options.forceITMSync );
    MSGSeqInit( &_r.d, &_r.i, MSG_REORDER_BUFLEN );

    while ( true )
    {
        struct Stream *stream = NULL;

        while ( true )
        {
            stream = _tryOpenStream();

            if ( stream != NULL )
            {
                if ( alreadyReported )
                {
                    genericsReport( V_INFO, "Connected" EOL );
                    alreadyReported = false;
                }

                break;
            }

            if ( !alreadyReported )
            {
                genericsReport( V_INFO, EOL "No connection" EOL );
                alreadyReported = true;
            }

            if ( options.endTerminate )
            {
                break;
            }

            /* Checking every 100ms for a connection is quite often enough */
            usleep( 10000 );
        }

        if ( stream != NULL )
        {
            _feedStream( stream );
        }

        stream->close( stream );
        free( stream );

        if ( options.endTerminate )
        {
            break;
        }
    }

    return 0;
}
// ====================================================================================================
