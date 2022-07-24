#include <stdio.h>
#include <strings.h>
#include <assert.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdbool.h>
#include <ctype.h>
#include <unistd.h>
#include <string.h>
#include <zmq.h>
#include <inttypes.h>

#include "generics.h"
#include "git_version_info.h"
#include "nw.h"
#include "stream.h"
#include "tpiuDecoder.h"
#include "itmDecoder.h"
#include "msgDecoder.h"

#define NUM_CHANNELS  32
#define HWFIFO_NAME "hwevent"
#define MAX_STRING_LENGTH (100)              /* Maximum length that will be output for a single event */

// Record for options, either defaults or from command line

struct Channel
{
    char *topic;
    char *format;
};

struct
{
    /* Config information */
    bool useTPIU;
    uint32_t tpiuChannel;
    bool forceITMSync;
    uint32_t hwOutputs;

    /* Sink information */
    char *bindUrl;
    struct Channel channel[NUM_CHANNELS + 1];

    /* Source information */
    int port;
    char *server;

    char *file;                                          /* File host connection */
    bool endTerminate;                                  /* Terminate when file/socket "ends" */

} options = {.forceITMSync = true, .tpiuChannel = 1, .bindUrl = NULL, .port = NWCLIENT_SERVER_PORT, .server = "localhost"};

struct
{
    /* The decoders and the packets from them */
    struct ITMDecoder i;
    struct ITMPacket h;
    struct TPIUDecoder t;
    struct TPIUPacket p;

    /* Timestamp info */
    uint64_t lastHWExceptionTS;
    enum timeDelay timeStatus;                    /* Indicator of if this time is exact */
    uint64_t timeStamp;                           /* Latest received time */

    void *zmqContext;
    void *zmqSocket;
} _r;

// ====================================================================================================
void _publishMessage( const char *topic, void *payload, size_t size )
{
    zmq_send( _r.zmqSocket, topic, strlen( topic ), ZMQ_SNDMORE );
    zmq_send( _r.zmqSocket, payload, size, 0 );
}

static const char *hwEventNames[] =
{
    [HWEVENT_TS] = HWFIFO_NAME "TS",
    [HWEVENT_EXCEPTION] = HWFIFO_NAME "EXCP",
    [HWEVENT_PCSample] = HWFIFO_NAME "PC",
    [HWEVENT_DWT] = HWFIFO_NAME "DWT",
    [HWEVENT_RWWT] = HWFIFO_NAME "RWWT",
    [HWEVENT_AWP] = HWFIFO_NAME "AWP",
    [HWEVENT_OFS] = HWFIFO_NAME "OFS",
    [HWEVENT_UNUSED] = NULL,
    [HWEVENT_NISYNC] = NULL,
};

// ====================================================================================================
// Decoders for each message
// ====================================================================================================
void _handleSW( struct swMsg *m, struct ITMDecoder *i )

{
    assert( m->msgtype == MSG_SOFTWARE );

    if ( ( m->srcAddr < NUM_CHANNELS ) && ( options.channel[m->srcAddr].topic ) )
    {
        struct Channel *channel = &options.channel[m->srcAddr];
        char formatted[30];
        size_t size = 0;

        // formatted output....start with specials
        if ( channel->format == NULL )
        {
            memcpy( formatted, &m->value, sizeof( m->value ) );
            size = 4;
        }
        else if ( strstr( channel->format, "%f" ) )
        {
            /* type punning on same host, after correctly building 32bit val
             * only unsafe on systems where u32/float have diff byte order */
            float *nastycast = ( float * )&m->value;
            size = snprintf( formatted, sizeof( formatted ), channel->format, *nastycast, *nastycast, *nastycast, *nastycast );
        }
        else if ( strstr( channel->format, "%c" ) )
        {
            /* Format contains %c, so execute repeatedly for all characters in sent data */
            uint8_t op[4] = {m->value & 0xff, ( m->value >> 8 ) & 0xff, ( m->value >> 16 ) & 0xff, ( m->value >> 24 ) & 0xff};
            uint32_t l = 0;

            do
            {
                size += snprintf( formatted + size, sizeof( formatted ) - size, channel->format, op[l], op[l], op[l] );
            }
            while ( ++l < m->len );
        }
        else
        {
            size = snprintf( formatted, sizeof( formatted ), channel->format, m->value, m->value, m->value, m->value );
        }

        _publishMessage( channel->topic, formatted, size );
    }
}
void _handleException( struct excMsg *m )

{
    assert( m->msgtype == MSG_EXCEPTION );

    if ( !( options.hwOutputs & ( 1 << HWEVENT_EXCEPTION ) ) )
    {
        return;
    }

    char outputString[MAX_STRING_LENGTH];
    int opLen;
    uint64_t eventdifftS = m->ts - _r.lastHWExceptionTS;

    const char *exNames[] = {"Thread", "Reset", "NMI", "HardFault", "MemManage", "BusFault", "UsageFault", "UNKNOWN_7",
                             "UNKNOWN_8", "UNKNOWN_9", "UNKNOWN_10", "SVCall", "Debug Monitor", "UNKNOWN_13", "PendSV", "SysTick"
                            };
    const char *exEvent[] = {"Unknown", "Enter", "Exit", "Resume"};

    _r.lastHWExceptionTS = m->ts;

    if ( m->exceptionNumber < 16 )
    {
        /* This is a system based exception */
        opLen = snprintf( outputString, MAX_STRING_LENGTH, "%" PRIu64 ",%s,%s", eventdifftS, exEvent[m->eventType & 0x03], exNames[m->exceptionNumber & 0x0F] );
    }
    else
    {
        /* This is a CPU defined exception */
        opLen = snprintf( outputString, MAX_STRING_LENGTH, "%" PRIu64 ",%s,External,%d", eventdifftS, exEvent[m->eventType & 0x03], m->exceptionNumber - 16 );
    }

    _publishMessage( hwEventNames[HWEVENT_EXCEPTION], outputString, opLen );
}
// ====================================================================================================
void _handleDWTEvent( struct dwtMsg *m )

{
    assert( m->msgtype == MSG_DWT_EVENT );

    if ( !( options.hwOutputs & ( 1 << HWEVENT_DWT ) ) )
    {
        return;
    }

    char outputString[MAX_STRING_LENGTH];
    int opLen;

#define NUM_EVENTS 6
    const char *evName[NUM_EVENTS] = {"CPI", "Exc", "Sleep", "LSU", "Fold", "Cyc"};
    uint64_t eventdifftS = m->ts - _r.lastHWExceptionTS;

    _r.lastHWExceptionTS = m->ts;
    opLen = snprintf( outputString, MAX_STRING_LENGTH, "%" PRIu64, eventdifftS );

    for ( uint32_t i = 0; i < NUM_EVENTS; i++ )
    {
        if ( m->event & ( 1 << i ) )
        {
            // Copy this event into the output string
            outputString[opLen++] = ',';
            const char *u = evName[i];

            do
            {
                outputString[opLen++] = *u++;
            }
            while ( *u );
        }
    }

    _publishMessage( hwEventNames[HWEVENT_DWT], outputString, opLen );
}
// ====================================================================================================
void _handlePCSample( struct pcSampleMsg *m )

/* We got a sample of the PC */

{
    assert( m->msgtype == MSG_PC_SAMPLE );

    if ( !( options.hwOutputs & ( 1 << HWEVENT_PCSample ) ) )
    {
        return;
    }

    char outputString[MAX_STRING_LENGTH];
    int opLen;
    uint64_t eventdifftS = m->ts - _r.lastHWExceptionTS;

    _r.lastHWExceptionTS = m->ts;

    if ( m->sleep )
    {
        /* This is a sleep packet */
        opLen = snprintf( outputString, ( MAX_STRING_LENGTH - 1 ), "%" PRIu64 ",**SLEEP**", eventdifftS );
    }
    else
    {
        opLen = snprintf( outputString, ( MAX_STRING_LENGTH - 1 ), "%" PRIu64 ",0x%08x", eventdifftS, m->pc );
    }

    _publishMessage( hwEventNames[HWEVENT_PCSample], outputString, opLen );
}
// ====================================================================================================
void _handleDataRWWP( struct watchMsg *m )

/* We got an alert due to a watch pointer */

{
    assert( m->msgtype == MSG_DATA_RWWP );

    if ( !( options.hwOutputs & ( 1 << HWEVENT_RWWT ) ) )
    {
        return;
    }

    char outputString[MAX_STRING_LENGTH];
    int opLen;
    uint64_t eventdifftS = m->ts - _r.lastHWExceptionTS;

    _r.lastHWExceptionTS = m->ts;

    opLen = snprintf( outputString, MAX_STRING_LENGTH, "%" PRIu64 ",%d,%s,0x%x", eventdifftS, m->comp, m->isWrite ? "Write" : "Read", m->data );
    _publishMessage( hwEventNames[HWEVENT_RWWT], outputString, opLen );
}
// ====================================================================================================
void _handleDataAccessWP( struct wptMsg *m )

/* We got an alert due to a watchpoint */

{
    assert( m->msgtype == MSG_DATA_ACCESS_WP );

    if ( !( options.hwOutputs & ( 1 << HWEVENT_AWP ) ) )
    {
        return;
    }

    char outputString[MAX_STRING_LENGTH];
    int opLen;

    uint64_t eventdifftS = m->ts - _r.lastHWExceptionTS;

    _r.lastHWExceptionTS = m->ts;
    opLen = snprintf( outputString, MAX_STRING_LENGTH, "%" PRIu64 ",%d,0x%08x", eventdifftS, m->comp, m->data );
    _publishMessage( hwEventNames[HWEVENT_AWP], outputString, opLen );
}
// ====================================================================================================
void _handleDataOffsetWP( struct oswMsg *m )

/* We got an alert due to an offset write event */

{
    assert( m->msgtype == MSG_OSW );

    if ( !( options.hwOutputs & ( 1 << HWEVENT_OFS ) ) )
    {
        return;
    }

    char outputString[MAX_STRING_LENGTH];
    int opLen;
    uint64_t eventdifftS = m->ts - _r.lastHWExceptionTS;

    _r.lastHWExceptionTS = m->ts;
    opLen = snprintf( outputString, MAX_STRING_LENGTH, "%" PRIu64 ",%d,0x%04x", eventdifftS, m->comp, m->offset );
    _publishMessage( hwEventNames[HWEVENT_OFS], outputString, opLen );
}
// ====================================================================================================
void _handleTS( struct TSMsg *m )

/* ... a timestamp */

{
    assert( m->msgtype == MSG_TS );

    if ( !( options.hwOutputs & ( 1 << HWEVENT_TS ) ) )
    {
        return;
    }

    assert( m->msgtype == MSG_TS );
    char outputString[MAX_STRING_LENGTH];
    int opLen;

    _r.timeStamp += m->timeInc;
    _r.timeStatus = m->timeStatus;

    opLen = snprintf( outputString, MAX_STRING_LENGTH, "%d,%" PRIu32, m->timeStatus, m->timeInc );
    _publishMessage( hwEventNames[HWEVENT_TS], outputString, opLen );
}
// ====================================================================================================
void _itmPumpProcess( char c )
{
    struct msg decoded;

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
        /* MSG_OSW */             ( handlers )_handleDataOffsetWP,
        /* MSG_DATA_ACCESS_WP */  ( handlers )_handleDataAccessWP,
        /* MSG_DATA_RWWP */       ( handlers )_handleDataRWWP,
        /* MSG_PC_SAMPLE */       ( handlers )_handlePCSample,
        /* MSG_DWT_EVENT */       ( handlers )_handleDWTEvent,
        /* MSG_EXCEPTION */       ( handlers )_handleException,
        /* MSG_TS */              ( handlers )_handleTS
    };

    switch ( ITMPump( &_r.i, c ) )
    {
        case ITM_EV_NONE:
            break;

        case ITM_EV_UNSYNCED:
            genericsReport( V_INFO, "ITM Unsynced" EOL );
            break;

        case ITM_EV_SYNCED:
            genericsReport( V_DEBUG, "ITM Synced" EOL );
            break;

        case ITM_EV_OVERFLOW:
            genericsReport( V_WARN, "ITM Overflow" EOL );
            break;

        case ITM_EV_ERROR:
            genericsReport( V_WARN, "ITM Error" EOL );
            break;

        case ITM_EV_PACKET_RXED:
            ITMGetDecodedPacket( &_r.i, &decoded );

            /* See if we decoded a dispatchable match. genericMsg is just used to access */
            /* the first two members of the decoded structs in a portable way.           */
            if ( h[decoded.genericMsg.msgtype] )
            {
                ( h[decoded.genericMsg.msgtype] )( &decoded, &_r.i );
            }

            break;

        default:
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
void _printHelp( const char *const progName )

{
    fprintf( stdout, "Usage: %s [options]" EOL, progName );
    fprintf( stdout, "    -b, --bind:         <url>: ZeroMQ bind URL" EOL );
    fprintf( stdout, "    -c, --channel:      <Number>,<Name>,<Format> of channel to populate (repeat per channel)" EOL );
    fprintf( stdout, "    -e, --hwevent:      Comma-separated list of published hwevents" EOL );
    fprintf( stdout, "    -E, --eof:          Terminate when the file/socket ends/is closed, or attempt to wait for more / reconnect" EOL );
    fprintf( stdout, "    -f, --input-file:   <filename> Take input from specified file" EOL );
    fprintf( stdout, "    -h, --help:         This help" EOL );
    fprintf( stdout, "    -n, --itm-sync:     Enforce sync requirement for ITM (i.e. ITM needsd to issue syncs)" EOL );
    fprintf( stdout, "    -s, --server:       <Server>:<Port> to use" EOL );
    fprintf( stdout, "    -t, --tpiu:         <channel>: Use TPIU decoder on specified channel (normally 1)" EOL );
    fprintf( stdout, "    -v, --verbose:      <level> Verbose mode 0(errors)..3(debug)" EOL );
    fprintf( stdout, "    -V, --version:      Print version and exit" EOL );
    fprintf( stdout, EOL );
    fprintf( stdout, "Available HW events: " EOL );
    fprintf( stdout, "   * all  - All hwevents" EOL );
    fprintf( stdout, "   * TS   - Timestamp" EOL );
    fprintf( stdout, "   * EXCP - Exception entry/exit" EOL );
    fprintf( stdout, "   * PC   - PC sampling" EOL );
    fprintf( stdout, "   * DWT  - DWT event" EOL );
    fprintf( stdout, "   * RWWT - Read/write watchpoint" EOL );
    fprintf( stdout, "   * AWP  - Access watchpoint" EOL );
    fprintf( stdout, "   * OFS  - Data offset" EOL );
}
// ====================================================================================================
void _printVersion( void )

{
    genericsPrintf( "orbcat version " GIT_DESCRIBE EOL );
}

static int32_t _parseHWEventsArg( char *s )
{
    if ( strcasecmp( s, "all" ) == 0 )
    {
        return 0xFF;
    }

    uint32_t result = 0;

    char *token = strtok( s, "," );

    while ( token )
    {
        if ( strcasecmp( token, "TS" ) == 0 )
        {
            result |= 1 << HWEVENT_TS;
        }
        else if ( strcasecmp( token, "EXCP" ) == 0 )
        {
            result |= 1 << HWEVENT_EXCEPTION;
        }
        else if ( strcasecmp( token, "PC" ) == 0 )
        {
            result |= 1 << HWEVENT_PCSample;
        }
        else if ( strcasecmp( token, "DWT" ) == 0 )
        {
            result |= 1 << HWEVENT_DWT;
        }
        else if ( strcasecmp( token, "RWWT" ) == 0 )
        {
            result |= 1 << HWEVENT_RWWT;
        }
        else if ( strcasecmp( token, "AWP" ) == 0 )
        {
            result |= 1 << HWEVENT_AWP;
        }
        else if ( strcasecmp( token, "OFS" ) == 0 )
        {
            result |= 1 << HWEVENT_OFS;
        }
        else
        {
            genericsReport( V_ERROR, "Unrecognied hardware event '%s'" EOL, token );
            return -1;
        }

        token = strtok( NULL, "," );
    }

    return result;
}

// ====================================================================================================
static struct option _longOptions[] =
{
    {"bind", required_argument, NULL, 'b'},
    {"channel", required_argument, NULL, 'c'},
    {"hwevent", required_argument, NULL, 'e'},
    {"eof", no_argument, NULL, 'E'},
    {"input-file", required_argument, NULL, 'f'},
    {"help", no_argument, NULL, 'h'},
    {"itm-sync", no_argument, NULL, 'n'},
    {"server", required_argument, NULL, 's'},
    {"tpiu", required_argument, NULL, 't'},
    {"verbose", required_argument, NULL, 'v'},
    {"version", no_argument, NULL, 'V'},
    {NULL, no_argument, NULL, 0}
};
// ====================================================================================================
bool _processOptions( int argc, char *argv[] )
{
    int c, optionIndex = 0;
#define DELIMITER ','

    char *chanConfig;
    char *chanName;
    unsigned int chan;
    char *chanIndex;
    int32_t hwOutputs;

    for ( int g = 0; g < NUM_CHANNELS; g++ )
    {
        options.channel[g].topic = NULL;
    }

    while ( ( c = getopt_long ( argc, argv, "l:c:e:Ef:hns:t:v:V", _longOptions, &optionIndex ) ) != -1 )
    {
        switch ( c )
        {
            // ------------------------------------
            case 'h':
                _printHelp( argv[0] );
                return false;

            // ------------------------------------
            case 'V':
                _printVersion();
                return false;

            // ------------------------------------
            case 'l':
                options.bindUrl = optarg;
                break;

            // ------------------------------------
            case 'E':
                options.endTerminate = true;
                break;

            // ------------------------------------
            case 'f':
                options.file = optarg;
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
                break;

            // ------------------------------------
            case 'v':
                genericsSetReportLevel( atoi( optarg ) );
                break;

            // ------------------------------------
            /* Individual channel setup */
            case 'c':
                chanIndex = chanConfig = strdup( optarg );

                if ( NULL == chanConfig )
                {
                    genericsReport( V_ERROR, "Couldn't allocate memory at %s::%d" EOL, __FILE__, __LINE__ );
                    return false;
                }

                chan = atoi( optarg );

                if ( chan >= NUM_CHANNELS )
                {
                    genericsReport( V_ERROR, "Channel index out of range" EOL );
                    free( chanConfig );
                    return false;
                }

                /* Scan for start of topic */
                while ( ( *chanIndex ) && ( *chanIndex != DELIMITER ) )
                {
                    chanIndex++;
                }

                if ( !*chanIndex )
                {
                    genericsReport( V_ERROR, "No filename for channel %d" EOL, chan );
                    free( chanConfig );
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
                    break;
                }

                *chanIndex++ = 0;
                options.channel[chan].topic = strdup( chanName );

                if ( strcmp( chanIndex, "" ) != 0 )
                {
                    options.channel[chan].format = strdup( genericsUnescape( chanIndex ) );
                }

                break;

            case 'e':
                hwOutputs = _parseHWEventsArg( optarg );

                if ( hwOutputs == -1 )
                {
                    return false;
                }

                options.hwOutputs = hwOutputs;
                break;

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
    }

    if ( ( options.useTPIU ) && ( !options.tpiuChannel ) )
    {
        genericsReport( V_ERROR, "TPIU set for use but no channel set for ITM output" EOL );
        return false;
    }

    genericsReport( V_INFO, "orbzmq version " GIT_DESCRIBE EOL );
    genericsReport( V_INFO, "Server      : %s:%d" EOL, options.server, options.port );
    genericsReport( V_INFO, "ForceSync   : %s" EOL, options.forceITMSync ? "true" : "false" );

    if ( options.file )
    {

        genericsReport( V_INFO, "Input File  : %s", options.file );

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
        genericsReport( V_INFO, "Using TPIU  : true (ITM on channel %d)" EOL, options.tpiuChannel );
    }
    else
    {
        genericsReport( V_INFO, "Using TPIU  : false" EOL );
    }

    genericsReport( V_INFO, "ZeroMQ bind : %s" EOL, options.bindUrl );
    genericsReport( V_INFO, "Channels    :" EOL );

    for ( int g = 0; g < NUM_CHANNELS; g++ )
    {
        struct Channel *channel = &options.channel[g];

        if ( channel->topic )
        {
            genericsReport( V_INFO, "             %02d [%s] [%s]" EOL, g, genericsEscape( channel->format ? : "RAW" ), channel->topic );
        }
    }

    for ( int g = 0; g < ( sizeof( hwEventNames ) / sizeof( *hwEventNames ) ); g++ )
    {
        if ( hwEventNames[g] == NULL )
        {
            continue;
        }

        if ( ( options.hwOutputs & ( 1 << g ) ) == 0 )
        {
            continue;;
        }

        genericsReport( V_INFO, "             HW [Predefined] [%s]" EOL, hwEventNames[g] );
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
    unsigned char cbw[TRANSFER_SIZE];

    while ( true )
    {
        size_t receivedSize;
        enum ReceiveResult result = stream->receive( stream, cbw, TRANSFER_SIZE, NULL, &receivedSize );

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
            else
            {
                usleep( 100000 );
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


int main( int argc, char *argv[] )
{
    bool alreadyReported = false;

    if ( !_processOptions( argc, argv ) )
    {
        exit( -1 );
    }

    _r.zmqContext = zmq_ctx_new();
    _r.zmqSocket = zmq_socket( _r.zmqContext, ZMQ_PUB );
    zmq_bind( _r.zmqSocket, options.bindUrl );

    /* Reset the TPIU handler before we start */
    TPIUDecoderInit( &_r.t );
    ITMDecoderInit( &_r.i, options.forceITMSync );

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
