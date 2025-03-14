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
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include <getopt.h>
#include <time.h>
#include <signal.h>

#include "nw.h"
#include "git_version_info.h"
#include "generics.h"
#include "itmDecoder.h"
#include "msgDecoder.h"
#include "msgSeq.h"
#include "stream.h"
#include "oflow.h"

#define NUM_CHANNELS  32
#define HW_CHANNEL    (NUM_CHANNELS)      /* Make the hardware fifo on the end of the software ones */

#define MAX_STRING_LENGTH (4096)          /* Maximum length that will be output */
#define DEFAULT_TS_TRIGGER '\n'           /* Default trigger character for timestamp output */

#define MSG_REORDER_BUFLEN  (10)          /* Maximum number of samples to re-order for timekeeping */
#define ONE_SEC_IN_USEC     (1000000L)    /* Used for time conversions...usec in one sec */

/* Formats for timestamping */
#define REL_FORMAT            C_TSTAMP "%6" PRIu64 ".%03" PRIu64 "|" C_RESET
#define REL_FORMAT_INIT       C_TSTAMP " R-Initial|" C_RESET
#define DEL_FORMAT            C_TSTAMP "%5" PRIu64 ".%03" PRIu64 "|" C_RESET
#define DEL_FORMAT_CTD        C_TSTAMP "      +|" C_RESET
#define DEL_FORMAT_INIT       C_TSTAMP "D-Initial|" C_RESET
#define ABS_FORMAT_TM   "%d/%b/%y %H:%M:%S"
#define ABS_FORMAT            C_TSTAMP "%s.%03" PRIu64"|" C_RESET
#define STAMP_FORMAT          C_TSTAMP "%12" PRIu64 "|" C_RESET
#define STAMP_FORMAT_MS       C_TSTAMP "%8" PRIu64 ".%03" PRIu64 "_%03" PRIu64 "|" C_RESET
//#define STAMP_FORMAT_MS_DELTA C_TSTAMP "%5" PRIu64 ".%03" PRIu64 "_%03" PRIu64 "|" C_RESET
#define STAMP_FORMAT_MS_DELTA C_TSTAMP "%5" PRIu64 ".%03" PRIu64 "_%03" PRIu64 "_%01" PRIu64 "|" C_RESET

enum TSType { TSNone, TSAbsolute, TSRelative, TSDelta, TSStamp, TSStampDelta, TSNumTypes };

enum Prot { PROT_OFLOW, PROT_ITM, PROT_UNKNOWN };
const char *protString[] = {"OFLOW", "ITM", NULL};

const char *tsTypeString[TSNumTypes] = { "None", "Absolute", "Relative", "Delta", "System Timestamp", "System Timestamp Delta" };

// Record for options, either defaults or from command line
struct
{
    /* Config information */
    uint32_t tag;                            /* Which OFLOW tag are we decoding? */
    bool forceITMSync;
    uint64_t cps;                            /* Cycles per second for target CPU */

    enum TSType tsType;
    char *tsLineFormat;
    char tsTrigger;
    bool mono;                               /* Supress colour in output */

    /* Sink information */
    char *presFormat[NUM_CHANNELS + 1];      /* Format string for each channel */

    /* Source information */
    int port;                                /* What port to connect to on the server (default to orbuculum) */
    char *server;                            /* Which server to connect to (default to localhost) */
    enum Prot protocol;                      /* What protocol to communicate (default to OFLOW (== orbuculum)) */

    char *file;                              /* File host connection */
    bool endTerminate;                       /* Terminate when file/socket "ends" */
    bool ex;                             /* Support exception reporting */
} options =
{
    .forceITMSync = true,
    .tag = 1,
    .port = OFCLIENT_SERVER_PORT,
    .server = "localhost",
    .tsTrigger = DEFAULT_TS_TRIGGER
};

struct
{
    /* The decoders and the packets from them */
    struct ITMDecoder i;
    struct MSGSeq    d;
    struct ITMPacket h;
    struct OFLOW c;

    struct Frame cobsPart;               /* Any part frame that has been received */
    enum timeDelay timeStatus;           /* Indicator of if this time is exact */
    uint64_t timeStamp;                  /* Latest received time */
    uint64_t lastTimeStamp;              /* Last received time */
    uint64_t te;                         /* Time on host side for line stamping */
    bool gotte;                          /* Flag that we have the initial time */
    bool inLine;                         /* We are in progress with a line that has been timestamped already */
    uint64_t dwtte;                      /* Timestamp for dwt print age */
    uint64_t oldte;                      /* Old time for interval calculation */
    char dwtText[MAX_STRING_LENGTH];     /* DWT text that arrived while a line was in progress */
    bool ending;                         /* Time to shut up shop */
} _r;

#define DWT_TO_US (100000L)

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
static void _printTimestamp( char *strstore )

{
    /* Lets output a timestamp */
    char opConstruct[MAX_STRING_LENGTH];
    uint64_t res;
    struct tm tm;
    time_t td;

    *strstore = 0;

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
                sprintf( strstore, REL_FORMAT_INIT );
            }
            else
            {
                res = _timestamp() - _r.oldte;
                sprintf( strstore, REL_FORMAT, res / ONE_SEC_IN_USEC, ( res / ( ONE_SEC_IN_USEC / 1000 ) ) % 1000 );
            }

            break;

        case TSAbsolute: // -------------------------------------------------------------------
            res = _timestamp();
            td = ( time_t )res / ONE_SEC_IN_USEC;
            tm = *localtime( &td );
            strftime( opConstruct, MAX_STRING_LENGTH, ABS_FORMAT_TM, &tm );
            sprintf( strstore, ABS_FORMAT, opConstruct, ( res / ( ONE_SEC_IN_USEC / 1000 ) ) % 1000 );
            break;

        case TSDelta: // ----------------------------------------------------------------------
            if ( !_r.gotte )
            {
                /* Get the starting time */
                _r.oldte = _timestamp();
                _r.gotte = true;
                sprintf( strstore, DEL_FORMAT_INIT );
            }
            else
            {
                uint64_t t = _timestamp();
                res = t - _r.oldte;
                _r.oldte = t;

                if ( res / 1000 )
                {
                    sprintf( strstore, DEL_FORMAT, res / ONE_SEC_IN_USEC, ( res / 1000 ) % 1000 );
                }
                else
                {
                    sprintf( strstore, DEL_FORMAT_CTD );
                }
            }

            break;

        case TSStamp: // -----------------------------------------------------------------------
            if ( options.cps )
            {
                uint64_t tms = ( _r.timeStamp * 1000000 ) / options.cps;
                sprintf( strstore, STAMP_FORMAT_MS, tms / 1000000, ( tms / 1000 ) % 1000, tms % 1000 );
            }
            else
            {
                sprintf( strstore, STAMP_FORMAT, _r.timeStamp );
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
                /* Provide some rounding .. we're at the limits of what's sensible here */
                uint64_t tms = ( delta * 10000000 + options.cps / 2 ) / options.cps;
                sprintf( strstore, STAMP_FORMAT_MS_DELTA, tms / 10000000, ( tms / 10000 ) % 10000, ( tms / 10 ) % 1000, tms % 10 );
            }
            else
            {
                sprintf( strstore, STAMP_FORMAT, delta );
            }

            break;

        default: // ----------------------------------------------------------------------------
            assert( false );
    }
}
// ====================================================================================================
static void _outputText( char *p )

{
    char opConstruct[MAX_STRING_LENGTH];
    /* Process the buffer and make sure it gets timestamped correctly as it's output */

    char *q;

    while ( *p )
    {
        /* If this is the first character in a new line, then we need to generate a timestamp */
        if ( !_r.inLine )
        {
            _printTimestamp( opConstruct );
            genericsFPrintf( stdout, "%s", opConstruct );
            _r.inLine = true;
        }

        /* See if there is a trigger in these data...if so then output everything prior to it */
        q = strchr( p, options.tsTrigger );

        if ( q )
        {
            *q = 0;
            genericsFPrintf( stdout, "%s" EOL, p );
            /* Once we've output these data then we're not in a line any more */
            _r.inLine = false;

            /* ...and if there were any DWT messages to print we'd better output those */
            if ( _r.dwtText[0] )
            {
                genericsFPrintf( stdout, "%s" EOL, _r.dwtText );
                _r.dwtText[0] = 0;
            }
        }
        else
        {
            /* Just output the whole of the data we've got, then we're done */
            genericsFPrintf( stdout, "%s", p );
            break;
        }

        /* Move past this trigger in case there are more data to output ... this will be \0 if not */
        p = q + 1;
    }
}
// ====================================================================================================
// Decoders for each message
// ====================================================================================================

void _expex( const char *fmt, ... )

/* Print to exception buffer and output if appropriate */

{
    if ( ( _r.inLine ) && ( !_r.dwtText[0] ) )
    {
        /* Timestamp this so it gets output at an interval later, worst case */
        _r.dwtte = _timestamp();
    }

    /* See if we exceeded max length...if so then output what we have and start a fresh buffer */
    if ( MAX_STRING_LENGTH - strlen( _r.dwtText ) < 100 )
    {
        genericsFPrintf( stdout, "%s", _r.dwtText );
        _r.dwtText[0] = 0;
    }

    /* Construct the output */
    _printTimestamp( &_r.dwtText[strlen( _r.dwtText )] );
    int maxLen = MAX_STRING_LENGTH - strlen( _r.dwtText );
    va_list va;
    va_start( va, fmt );
    vsnprintf( &_r.dwtText[strlen( _r.dwtText )], maxLen, fmt, va );
    va_end( va );

    if ( !_r.inLine )
    {
        genericsFPrintf( stdout, "%s", _r.dwtText );
        _r.dwtText[0] = 0;
    }
}

// ====================================================================================================

void _handleException( struct excMsg *m, struct ITMDecoder *i )

{
    if ( options.ex )
    {
        const char *exNames[] = {"Thread", "Reset", "NMI", "HardFault", "MemManage", "BusFault", "UsageFault", "UNKNOWN_7",
                                 "UNKNOWN_8", "UNKNOWN_9", "UNKNOWN_10", "SVCall", "Debug Monitor", "UNKNOWN_13", "PendSV", "SysTick"
                                };
        const char *exEvent[] = {"Unknown", "Enter", "Exit", "Resume"};


        if ( m->exceptionNumber < 16 )
        {
            /* This is a system based exception */
            _expex( "HWEVENT_SYSTEM_EXCEPTION event %s type %s" EOL, exEvent[m->eventType & 0x03], exNames[m->exceptionNumber & 0x0F] );
        }
        else
        {
            /* This is a CPU defined exception */
            _expex( "HWEVENT_INTERRUPT_EXCEPTION event %s external interrupt %d" EOL, exEvent[m->eventType & 0x03], m->exceptionNumber - 16 );
        }
    }
}
// ====================================================================================================
void _handleDWTEvent( struct dwtMsg *m, struct ITMDecoder *i )

{
#define NUM_EVENTS 6

    char op[MAX_STRING_LENGTH];

    if ( options.ex )
    {
        const char *evName[NUM_EVENTS] = {"CPI", "Exc", "Sleep", "LSU", "Fold", "Cyc"};

        sprintf( op, "HWEVENT_DWT type " );

        uint32_t opLen = strlen( op );

        for ( uint32_t i = 0; i < NUM_EVENTS; i++ )
        {
            if ( m->event & ( 1 << i ) )
            {
                // Copy this event into the output string
                op[opLen++] = ',';
                const char *u = evName[i];

                do
                {
                    op[opLen++] = *u++;
                }
                while ( *u );
            }
        }

        _expex( "%s", op );
    }
}

// ====================================================================================================
void _handleDataRWWP( struct watchMsg *m, struct ITMDecoder *i )

/* We got an alert due to a watch pointer */

{
    if ( options.ex )
    {
        _expex( "HWEVENT_RWWT type %d for %s data 0x%x" EOL, m->comp, m->isWrite ? "Write" : "Read", m->data );
    }
}
// ====================================================================================================
void _handleDataAccessWP( struct wptMsg *m, struct ITMDecoder *i )

/* We got an alert due to a watchpoint */

{
    if ( options.ex )
    {
        _expex( "HWEVENT_AWP type %d at address 0x%08x" EOL, m->comp, m->data );
    }
}
// ====================================================================================================
void _handleDataOffsetWP( struct oswMsg *m, struct ITMDecoder *i )

/* We got an alert due to an offset write event */

{
    if ( options.ex )
    {
        _expex( "HWEVENT_OFS comparison %d at offset 0x%04x" EOL, m->comp, m->offset );
    }
}
// ====================================================================================================
void _handleNISYNC( struct nisyncMsg *m, struct ITMDecoder *i )

{
    if ( options.ex )
    {
        _expex( "HWEVENT_NISYNC type %02x at address 0x%08x" EOL, m->type, m->addr );
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
        /* MSG_NISYNC */          ( handlers )_handleNISYNC,
        /* MSG_OSW */             ( handlers )_handleDataOffsetWP,
        /* MSG_DATA_ACCESS_WP */  ( handlers )_handleDataAccessWP,
        /* MSG_DATA_RWWP */       ( handlers )_handleDataRWWP,
        /* MSG_PC_SAMPLE */       NULL,
        /* MSG_DWT_EVENT */       ( handlers )_handleDWTEvent,
        /* MSG_EXCEPTION */       ( handlers )_handleException,
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

static void _printHelp( const char *const progName )

{
    genericsFPrintf( stderr, "Usage: %s [options]" EOL, progName );
    genericsFPrintf( stderr, "    -c, --channel:      <Number>,<Format> of channel to add into output stream (repeat per channel)" EOL );
    genericsFPrintf( stderr, "    -C, --cpufreq:      <Frequency in KHz> (Scaled) speed of the CPU" EOL
                     "                        generally /1, /4, /16 or /64 of the real CPU speed," EOL );
    genericsFPrintf( stderr, "    -E, --eof:          Terminate when the file/socket ends/is closed, or wait for more/reconnect" EOL );
    genericsFPrintf( stderr, "    -f, --input-file:   <filename> Take input from specified file" EOL );
    genericsFPrintf( stderr, "    -g, --trigger:      <char> to use to trigger timestamp (default is newline)" EOL );
    genericsFPrintf( stderr, "    -h, --help:         This help" EOL );
    genericsFPrintf( stderr, "    -n, --itm-sync:     Enforce sync requirement for ITM (i.e. ITM needs to issue syncs)" EOL );
    genericsFPrintf( stderr, "    -p, --protocol:     Protocol to communicate. Defaults to OFLOW if -s is not set, otherwise ITM" EOL );
    genericsFPrintf( stderr, "    -s, --server:       <Server>:<Port> to use" EOL );
    genericsFPrintf( stderr, "    -t, --tag:          <stream>: Which orbflow tag to use (normally 1)" EOL );
    genericsFPrintf( stderr, "    -T, --timestamp:    <a|r|d|s|t>: Add absolute, relative (to session start)," EOL
                     "                        delta, system timestamp or system timestamp delta to output. Note" EOL
                     "                        the accuracy of a,r & d are host dependent." EOL );
    genericsFPrintf( stderr, "    -v, --verbose:      <level> Verbose mode 0(errors)..3(debug)" EOL );
    genericsFPrintf( stderr, "    -V, --version:      Print version and exit" EOL );
    genericsFPrintf( stderr, "    -x, --exceptions:   Include exception information in output, in time order" EOL );
}
// ====================================================================================================
static void _printVersion( void )

{
    genericsFPrintf( stderr, "orbcat version " GIT_DESCRIBE EOL );
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
    {"no-colour", no_argument, NULL, 'M'},
    {"no-color", no_argument, NULL, 'M'},
    {"protocol", required_argument, NULL, 'p'},
    {"server", required_argument, NULL, 's'},
    {"tag", required_argument, NULL, 't'},
    {"timestamp", required_argument, NULL, 'T'},
    {"verbose", required_argument, NULL, 'v'},
    {"version", no_argument, NULL, 'V'},
    {"exceptions", no_argument, NULL, 'x'},
    {NULL, no_argument, NULL, 0}
};
// ====================================================================================================
bool _processOptions( int argc, char *argv[] )

{
    int c, optionIndex = 0;
    unsigned int chan;
    char *chanIndex;
    bool protExplicit = false;
    bool serverExplicit = false;
    bool portExplicit = false;

#define DELIMITER ','

    while ( ( c = getopt_long ( argc, argv, "c:C:Ef:g:hVnMp:s:t:T:v:x", _longOptions, &optionIndex ) ) != -1 )
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
            case 'M':
                options.mono = true;
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
            case 't':
                options.tag = atoi( optarg );

                if ( !options.tag || ( options.tag > 255 ) )
                {
                    genericsReport( V_ERROR, "tag out of range" EOL );
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
            case 'x':
                options.ex = true;
                break;

            // ------------------------------------
            default:
                return false;
                // ------------------------------------
        }

    /* If we set an explicit server and port and didn't set a protocol chances are we want ITM, not OFLOW */
    if ( serverExplicit && !protExplicit )
    {
        options.protocol = PROT_ITM;
    }

    if ( ( options.protocol == PROT_ITM ) && !portExplicit )
    {
        options.port = NWCLIENT_SERVER_PORT;
    }

    genericsReport( V_INFO, "orbcat version " GIT_DESCRIBE EOL );
    genericsReport( V_INFO, "Server     : %s:%d" EOL, options.server, options.port );
    genericsReport( V_INFO, "ForceSync  : %s" EOL, options.forceITMSync ? "true" : "false" );
    genericsReport( V_INFO, "Timestamp  : %s" EOL, tsTypeString[options.tsType] );
    genericsReport( V_INFO, "Exceptions : %s" EOL, options.ex ? "On" : "Off" );

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

    switch ( options.protocol )
    {
        case PROT_OFLOW:
            genericsReport( V_INFO, "Decoding OFLOW (Orbuculum) with ITM in stream %d" EOL, options.tag );
            break;

        case PROT_ITM:
            genericsReport( V_INFO, "Decoding ITM" EOL );
            break;

        default:
            genericsReport( V_INFO, "Decoding unknown" EOL );
            break;
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
static struct Stream *_tryOpenStream( void )
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

static void _OFLOWpacketRxed ( struct OFLOWFrame *p, void *param )

{
    if ( !p->good )
    {
        genericsReport( V_INFO, "Bad packet received" EOL );
    }
    else
    {
        if ( p->tag == options.tag )
        {
            for ( int i = 0; i < p->len; i++ )
            {
                _itmPumpProcess( p->d[i] );
            }
        }
    }
}

// ====================================================================================================

static void _feedStream( struct Stream *stream )
{
    struct timeval t;
    unsigned char cbw[TRANSFER_SIZE];

    while ( !_r.ending )
    {
        size_t receivedSize;

        t.tv_sec = 0;
        t.tv_usec = 100000;
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


        if ( receivedSize )
        {
            if ( PROT_OFLOW == options.protocol )
            {
                OFLOWPump( &_r.c, cbw, receivedSize, _OFLOWpacketRxed, &_r );
            }
            else
            {
                /* ITM goes directly through the protocol pump */
                unsigned char *c = cbw;

                while ( receivedSize-- )
                {
                    _itmPumpProcess( *c++ );
                }
            }

            /* Check if an exception report timed out */
            if ( ( _r.inLine ) && _r.dwtText[0] && ( _timestamp() - _r.dwtte > DWT_TO_US ) )
            {
                genericsFPrintf( stderr, EOL "%s", _r.dwtText );
                _r.dwtText[0] = 0;
                _r.inLine = false;
            }

            fflush( stdout );
        }
    }
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
    bool alreadyReported = false;

    if ( !_processOptions( argc, argv ) )
    {
        exit( -1 );
    }

    genericsScreenHandling( !options.mono );

    /* Reset the handlers before we start */
    ITMDecoderInit( &_r.i, options.forceITMSync );
    OFLOWInit( &_r.c );
    MSGSeqInit( &_r.d, &_r.i, MSG_REORDER_BUFLEN );

    /* This ensures the signal handler gets called */
    if ( SIG_ERR == signal( SIGINT, _intHandler ) )
    {
        genericsExit( -1, "Failed to establish Int handler" EOL );
    }

    while ( !_r.ending )
    {
        struct Stream *stream = NULL;

        while ( !_r.ending )
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

            /* Checking every 10ms for a connection is quite often enough */
            usleep( 10000 );
        }

        if ( stream != NULL )
        {
            _feedStream( stream );

            stream->close( stream );
            free( stream );
        }

        if ( options.endTerminate )
        {
            break;
        }
    }

    return 0;
}
// ====================================================================================================
