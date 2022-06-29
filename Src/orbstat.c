/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * ITM Profiling module for Orbuculum
 * ==================================
 *
 */

#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "git_version_info.h"
#include "uthash.h"
#include "generics.h"
#include "itmDecoder.h"
#include "tpiuDecoder.h"
#include "msgDecoder.h"
#include "symbols.h"
#include "nw.h"
#include "ext_fileformats.h"
#include "stream.h"

#define TICK_TIME_MS        (1)          /* Time intervals for checks */
#define DEFAULT_DURATION_MS (1000)       /* Default time to sample, in mS */

#define DEFAULT_TRACE_CHANNEL  30        /* ITM Channel that we expect trace data to arrive on */
#define DEFAULT_FILE_CHANNEL   29        /* ITM Channel that we expect file data to arrive on */

/* Interface to/from target */
#define COMMS_MASK (0xF0000000)
#define IN_EVENT   (0x40000000)
#define OUT_EVENT  (0x50000000)

/* States for sample reception state machine */
enum CDState { CD_waitinout, CD_waitsrc, CD_waitdst };

/* ---------- CONFIGURATION ----------------- */
struct Options                           /* Record for options, either defaults or from command line */
{
    bool demangle;                       /* Demangle C++ names */
    char *file;                          /* File host connection */
    bool fileTerminate;                  /* Terminate when file read isn't successful */

    char *deleteMaterial;                /* Material to strip off front of filenames for target */
    bool truncateDeleteMaterial;         /* Do we want this material totally removing from file references? */

    char *elffile;                       /* Target program config */
    char *odoptions;                     /* Options to pass directly to objdump */

    int traceChannel;                    /* ITM Channel used for trace */
    int fileChannel;                     /* ITM Channel used for file output */

    char *dotfile;                       /* File to output dot information */
    char *profile;                       /* File to output profile information */
    uint32_t sampleDuration;             /* How long we are going to sample for */
    bool forceITMSync;                   /* Do we assume ITM starts synced? */

    bool useTPIU;                        /* Are we using TPIU, and stripping TPIU frames? */
    uint32_t tpiuITMChannel;             /* Which TPIU channel to use for ITM */

    int port;                            /* Source information for where to connect to */
    char *server;

} _options =
{
    .demangle       = true,
    .sampleDuration = DEFAULT_DURATION_MS,
    .port           = NWCLIENT_SERVER_PORT,
    .traceChannel   = DEFAULT_TRACE_CHANNEL,
    .fileChannel    = DEFAULT_FILE_CHANNEL,
    .forceITMSync   = true,
    .server         = "localhost"
};

/* A block of received data */
struct dataBlock
{
    ssize_t fillLevel;
    uint8_t buffer[TRANSFER_SIZE];
};

/* ----------- LIVE STATE ----------------- */
struct RunTime
{
    struct ITMDecoder i;                /* The decoders and the packets from them */
    struct ITMPacket h;
    struct TPIUDecoder t;
    struct TPIUPacket p;
    struct msg m;                       /* Decoded message out of ITM layer */

    const char *progName;               /* Name by which this program was called */
    bool      ending;                   /* Flag indicating app is terminating */
    uint64_t intervalBytes;             /* Number of bytes transferred in current interval */

    /* Calls related info */
    enum CDState CDState;               /* State of the call data machine */
    struct edge *calls;                 /* Call data table */

    struct subcall *subhead;            /* Calls onstruct data */
    struct subcall **substack;          /* Calls stack data */
    uint32_t substacklen;               /* Calls stack length */

    struct execEntryHash *insthead;     /* Exec table handle for hash */

    struct SymbolSet *s;                /* Symbols read from elf */
    struct Options *options;            /* Our runtime configuration */

    struct dataBlock rawBlock;          /* Datablock received from distribution */

    bool sampling;                      /* Are we actively sampling at the moment */
    uint32_t starttime;                 /* At what time did we start sampling? */

    /* Turn addresses into files and routines tags */
    struct execEntryHash *from;         /* Where the call was from */
    struct execEntryHash *to;           /* Where the call was to */

    /* Used for stretching number of bits in target timer */
    uint32_t oldt;                      /* Last received timestamp */
    uint64_t highOrdert;                /* High order bits */
    uint64_t tcount;                    /* Constructed current count */
    uint64_t starttcount;               /* Count at which we started */
} _r =
{
    .options = &_options
};

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Internally available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================


// ====================================================================================================
// Callback function for trace messages from the target CPU (via ITM channel)
// ====================================================================================================
static void _handleSW( struct RunTime *r )

{
    struct nameEntry n;
    struct subcallSig sig;
    struct subcall *s;
    static bool isIn;
    uint32_t addr;

    struct swMsg *m = ( struct swMsg * )&r->m;

    if ( m->srcAddr == r->options->traceChannel )
    {
        switch ( r->CDState )
        {
            // -------------------- Reporting the time stamp and if it's an In or Out event
            case CD_waitinout:
                if ( ( m->value & COMMS_MASK ) == IN_EVENT )
                {
                    isIn = true;
                    r->CDState = CD_waitsrc;
                }

                if ( ( m->value & COMMS_MASK ) == OUT_EVENT )
                {
                    isIn = false;
                    r->CDState = CD_waitsrc;
                }

                if ( r->CDState != CD_waitinout )
                {
                    /* Time is encoded in lowest three octets ...accomodate rollover */
                    uint32_t t = m->value & 0xFFFFFF;

                    if ( t < _r.oldt )
                    {
                        r->highOrdert++;
                    }

                    r->oldt = t;
                    r->tcount = ( _r.highOrdert << 24 ) | t;

                    /* Finally, if we're not sampling, then start sampling */
                    if ( !r->sampling )
                    {
                        genericsReport( V_WARN, "Sampling" EOL );
                        /* Fill in a time to start from */
                        r->starttime     = genericsTimestampmS();
                        r->intervalBytes = 0;
                        r->starttcount   = r->tcount;
                        r->sampling      = true;
                    }
                }

                break;

            // -------------------- Reporting the source address
            case CD_waitsrc:
                /* Source address is the address of the _return_, so subtract 4 */
                addr = ( m->value - 4 );
                r->CDState = CD_waitdst;
                HASH_FIND_INT( r->insthead, &addr, r->from );

                if ( !r->from )
                {
                    if ( SymbolLookup( r->s, addr, &n ) )
                    {
                        r->from = calloc( 1, sizeof( struct execEntryHash ) );

                        r->from->addr          = addr;
                        r->from->fileindex     = n.fileindex;
                        r->from->line          = n.line;
                        r->from->functionindex = n.functionindex;
                    }
                    else
                    {
                        genericsReport( V_ERROR, "No symbol for address %08x" EOL, addr );
                        r->CDState = CD_waitinout;
                        return;
                    }

                    HASH_ADD_INT( r->insthead, addr, ( r->from ) );
                }

                r->from->count++;
                break;

            // -------------------- Reporting the return address
            case CD_waitdst:
                addr = m->value;
                r->CDState = CD_waitinout;

                HASH_FIND_INT( r->insthead, &addr, r->to );

                if ( !r->to )
                {
                    if ( SymbolLookup( r->s, addr, &n ) )
                    {
                        r->to = calloc( 1, sizeof( struct execEntryHash ) );

                        r->to->addr          = addr;
                        r->to->fileindex     = n.fileindex;
                        r->to->line          = n.line;
                        r->to->functionindex = n.functionindex;
                    }
                    else
                    {
                        genericsReport( V_ERROR, "No symbol for address %08x" EOL, addr );
                        r->CDState = CD_waitinout;
                        return;
                    }

                    HASH_ADD_INT( r->insthead, addr, ( r->to ) );
                }

                if ( r->from->addr > 0xfffffff0 )
                {
                    printf( "%s" EOL, SymbolFunction( r->s, r->to->functionindex ) );
                }

                r->to->count++;

                /* ----------------------------------------------------------------------------------------------------------*/
                /* We have everything. Record calls between functions. These are flagged via isIn true/false for call/return */
                /* ----------------------------------------------------------------------------------------------------------*/
                if ( isIn )
                {
                    /* Now make calling record */
                    sig.src = r->from->addr;
                    sig.dst = r->to->addr;

                    /* Find, or create, the call record */
                    HASH_FIND( hh, r->subhead, &sig, sizeof( struct subcallSig ), s );

                    if ( !s )
                    {
                        /* This entry doesn't exist...let's create it */
                        s = ( struct subcall * )calloc( 1, sizeof( struct subcall ) );
                        memcpy( &s->sig, &sig, sizeof( struct subcallSig ) );
                        s->srch = r->from;
                        s->dsth = r->to;
                        HASH_ADD( hh, r->subhead, sig, sizeof( struct subcallSig ), s );
                    }


                    /* Now handle calling/return stack */
                    /* However we got here, we've got a subcall record, so initialise its starting ticks */
                    s->inTicks = r->tcount;
                    s->count++;

                    /* ...and add it to the call stack */
                    r->substack = ( struct subcall ** )realloc( r->substack, ( r->substacklen + 1 ) * sizeof( struct subcall * ) );
                    r->substack[r->substacklen++] = s;
                }
                else
                {
                    /* We've come out */
                    if ( r->substacklen )
                    {
                        /* We don't bother deallocating memory here cos it'll be done on the next isSubCall */
                        s = r->substack[--r->substacklen];

                        if ( ( s->sig.src != r->from->addr ) || ( s->sig.dst != r->to->addr ) )
                        {
                            genericsReport( V_WARN, "Address mismatch" EOL );
                        }

                        s->myCost = ( r->tcount - s->inTicks );
                    }
                }

                break;
                // --------------------
        }
    }
}

// ====================================================================================================
void _itmPumpProcess( struct RunTime *r, char c )

/* Handle individual characters into the itm decoder */

{

    typedef void ( *handlers )( struct RunTime * r );

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
        /* MSG_TS */              NULL
    };

    switch ( ITMPump( &r->i, c ) )
    {
        // ------------------------------------
        case ITM_EV_NONE:
            break;

        // ------------------------------------
        case ITM_EV_UNSYNCED:
            genericsReport( V_INFO, "ITM Lost Sync (%d)" EOL, ITMDecoderGetStats( &r->i )->lostSyncCount );
            break;

        // ------------------------------------
        case ITM_EV_SYNCED:
            genericsReport( V_INFO, "ITM In Sync (%d)" EOL, ITMDecoderGetStats( &r->i )->syncCount );
            break;

        // ------------------------------------
        case ITM_EV_OVERFLOW:
            genericsReport( V_WARN, "ITM Overflow (%d)" EOL, ITMDecoderGetStats( &r->i )->overflow );
            break;

        // ------------------------------------
        case ITM_EV_ERROR:
            genericsReport( V_WARN, "ITM Error" EOL );
            break;

        // ------------------------------------
        case ITM_EV_PACKET_RXED:
            ITMGetDecodedPacket( &r->i, &r->m );

            /* See if we decoded a dispatchable match. genericMsg is just used to access */
            /* the first two members of the decoded structs in a portable way.           */
            if ( h[r->m.genericMsg.msgtype] )
            {
                ( h[r->m.genericMsg.msgtype] )( r );
            }

            break;

            // ------------------------------------
    }
}
// ====================================================================================================
void _protocolPump( struct RunTime *r, uint8_t c )

/* Top level protocol pump */

{
    if ( r->options->useTPIU )
    {
        switch ( TPIUPump( &r->t, c ) )
        {
            // ------------------------------------
            case TPIU_EV_NEWSYNC:
                genericsReport( V_INFO, "TPIU In Sync (%d)" EOL, TPIUDecoderGetStats( &r->t )->syncCount );

            case TPIU_EV_SYNCED:
                ITMDecoderForceSync( &r->i, true );
                break;

            // ------------------------------------
            case TPIU_EV_RXING:
            case TPIU_EV_NONE:
                break;

            // ------------------------------------
            case TPIU_EV_UNSYNCED:
                genericsReport( V_INFO, "TPIU Lost Sync (%d)" EOL, TPIUDecoderGetStats( &r->t )->lostSync );
                ITMDecoderForceSync( &r->i, false );
                break;

            // ------------------------------------
            case TPIU_EV_RXEDPACKET:
                if ( !TPIUGetPacket( &r->t, &r->p ) )
                {
                    genericsReport( V_WARN, "TPIUGetPacket fell over" EOL );
                }

                for ( uint32_t g = 0; g < r->p.len; g++ )
                {
                    if ( r->p.packet[g].s == r->options->tpiuITMChannel )
                    {
                        _itmPumpProcess( r, r->p.packet[g].d );
                        continue;
                    }

                    if ( r->p.packet[g].s != 0 )
                    {
                        genericsReport( V_WARN, "Unknown TPIU channel %02x" EOL, r->p.packet[g].s );
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
        _itmPumpProcess( r, c );
    }
}
// ====================================================================================================
static void _intHandler( int sig )

/* Catch CTRL-C so things can be cleaned up properly via atexit functions */
{
    /* CTRL-C exit is not an error... */
    exit( 0 );
}
// ====================================================================================================
static void _printHelp( struct RunTime *r )

{
    genericsPrintf( "Usage: %s [options]" EOL, r->progName );
    genericsPrintf( "       -D: Switch off C++ symbol demangling" EOL );
    genericsPrintf( "       -d: <String> Material to delete off front of filenames" EOL );
    genericsPrintf( "       -E: When reading from file, terminate at end of file rather than waiting for further input" EOL );
    genericsPrintf( "       -e: <ElfFile> to use for symbols" EOL );
    genericsPrintf( "       -f <filename>: Take input from specified file" EOL );
    genericsPrintf( "       -g: <TraceChannel> for trace output (default %d)" EOL, r->options->traceChannel );
    genericsPrintf( "       -h: This help" EOL );
    genericsPrintf( "       -I <Interval>: Time to sample (in mS)" EOL );
    genericsPrintf( "       -n: Enforce sync requirement for ITM (i.e. ITM needs to issue syncs)" EOL );
    genericsPrintf( "       -O: <options> Options to pass directly to objdump" EOL );
    genericsPrintf( "       -s: <Server>:<Port> to use" EOL );
    genericsPrintf( "       -t <channel>: Use TPIU to strip TPIU on specfied channel (defaults to 1)" EOL );
    genericsPrintf( "       -T: truncate -d material off all references (i.e. make output relative)" EOL );
    genericsPrintf( "       -v: <level> Verbose mode 0(errors)..3(debug)" EOL );
    genericsPrintf( "       -y: <Filename> dotty filename for structured callgraph output" EOL );
    genericsPrintf( "       -z: <Filename> profile filename for kcachegrind output" EOL );
    genericsPrintf( EOL "(Will connect one port higher than that set in -s when TPIU is not used)" EOL );

}
// ====================================================================================================
static bool _processOptions( int argc, char *argv[], struct RunTime *r )

{
    int c;

    while ( ( c = getopt ( argc, argv, "Dd:Ee:f:g:hI:n:O:s:Tt:v:y:z:" ) ) != -1 )
        switch ( c )
        {
            // ------------------------------------
            case 'd':
                r->options->deleteMaterial = optarg;
                break;

            // ------------------------------------
            case 'D':
                r->options->demangle = false;
                break;

            // ------------------------------------
            case 'E':
                r->options->fileTerminate = true;
                break;

            // ------------------------------------
            case 'e':
                r->options->elffile = optarg;
                break;

            // ------------------------------------
            case 'f':
                r->options->file = optarg;
                break;

            // ------------------------------------
            case 'g':
                r->options->traceChannel = atoi( optarg );
                break;

            // ------------------------------------
            case 'h':
                _printHelp( r );
                exit( 0 );

            // ------------------------------------
            case 'I':
                r->options->sampleDuration = atoi( optarg );
                break;

            // ------------------------------------
            case 'n':
                r->options->forceITMSync = false;
                break;

            // ------------------------------------

            case 'O':
                r->options->odoptions = optarg;
                break;

            // ------------------------------------
            case 's':
                r->options->server = optarg;

                // See if we have an optional port number too
                char *a = optarg;

                while ( ( *a ) && ( *a != ':' ) )
                {
                    a++;
                }

                if ( *a == ':' )
                {
                    *a = 0;
                    r->options->port = atoi( ++a );
                }

                if ( !r->options->port )
                {
                    r->options->port = NWCLIENT_SERVER_PORT;
                }

                break;

            // ------------------------------------
            case 'T':
                r->options->truncateDeleteMaterial = true;
                break;

            // ------------------------------------
            case 't':
                r->options->tpiuITMChannel = atoi( optarg );
                r->options->useTPIU = !r->options->useTPIU;
                break;

            // ------------------------------------
            case 'v':
                genericsSetReportLevel( atoi( optarg ) );
                break;

            // ------------------------------------
            case 'y':
                r->options->dotfile = optarg;
                break;

            // ------------------------------------
            case 'z':
                r->options->profile = optarg;
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
                genericsReport( V_ERROR, "Unknown option %c" EOL, optopt );
                return false;
                // ------------------------------------
        }

    if ( !r->options->elffile )
    {
        genericsReport( V_ERROR, "Elf File not specified" EOL );
        exit( -2 );
    }

    if ( !r->options->sampleDuration )
    {
        genericsReport( V_ERROR, "Illegal sample duration" EOL );
        exit( -2 );
    }

    genericsReport( V_INFO, "%s V" VERSION " (Git %08X %s, Built " BUILD_DATE ")" EOL, r->progName, GIT_HASH, ( GIT_DIRTY ? "Dirty" : "Clean" ) );
    genericsReport( V_INFO, "Server          : %s:%d" EOL, r->options->server, r->options->port );
    genericsReport( V_INFO, "Delete Material : %s" EOL, r->options->deleteMaterial ? r->options->deleteMaterial : "None" );
    genericsReport( V_INFO, "Elf File        : %s %s" EOL, r->options->elffile, r->options->truncateDeleteMaterial ? "(Truncate)" : "(Don't Truncate)" );
    genericsReport( V_INFO, "DOT file        : %s" EOL, r->options->dotfile ? r->options->dotfile : "None" );
    genericsReport( V_INFO, "ForceSync       : %s" EOL, r->options->forceITMSync ? "true" : "false" );
    genericsReport( V_INFO, "Trace Channel   : %d" EOL, r->options->traceChannel );
    genericsReport( V_INFO, "Sample Duration : %d mS" EOL, r->options->sampleDuration );
    genericsReport( V_INFO, "Objdump options  : %s" EOL, r->options->odoptions ? r->options->odoptions : "None" );
    return true;
}
// ====================================================================================================
// ====================================================================================================
static void _doExit( void )

/* Perform any explicit exit functions */

{
    _r.ending = true;
    /* Give them a bit of time, then we're leaving anyway */
    usleep( 200 );
}
// ====================================================================================================
int main( int argc, char *argv[] )

{
    struct Stream *stream = NULL;

    struct timeval tv;

    /* Have a basic name and search string set up */
    _r.progName = genericsBasename( argv[0] );

    if ( !_processOptions( argc, argv, &_r ) )
    {
        /* processOptions generates its own error messages */
        genericsExit( -1, "" EOL );
    }

    /* Make sure the fifos get removed at the end */
    atexit( _doExit );

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

    /* Reset the TPIU handler before we start */
    TPIUDecoderInit( &_r.t );
    ITMDecoderInit( &_r.i, _r.options->forceITMSync );

    while ( !_r.ending )
    {
        if ( _r.options->file != NULL )
        {
            stream = streamCreateFile( _r.options->file );
        }
        else
        {
            while ( 1 )
            {
                stream = streamCreateSocket( _r.options->server, _r.options->port );

                if ( !stream )
                {
                    break;
                }

                perror( "Could not connect" );
                usleep( 1000000 );
            }
        }

        /* We need symbols constantly while running ... check they are current */
        if ( !SymbolSetValid( &_r.s, _r.options->elffile ) )
        {
            if ( !( _r.s = SymbolSetCreate( _r.options->elffile, _r.options->deleteMaterial, _r.options->demangle, true, true, _r.options->odoptions, false ) ) )
            {
                genericsExit( -1, "Elf file or symbols in it not found" EOL );
            }
            else
            {
                genericsReport( V_DEBUG, "Loaded %s" EOL, _r.options->elffile );
            }
        }


        /* ----------------------------------------------------------------------------- */
        /* This is the main active loop...only break out of this when ending or on error */
        /* ----------------------------------------------------------------------------- */
        while ( !_r.ending )
        {
            /* Each time segment is restricted */
            tv.tv_sec = 0;
            tv.tv_usec  = TICK_TIME_MS * 1000;

            enum ReceiveResult result = stream->receive( stream, _r.rawBlock.buffer, TRANSFER_SIZE, &tv, ( size_t * )&_r.rawBlock.fillLevel );

            if ( result != RECEIVE_RESULT_OK )
            {
                if ( result == RECEIVE_RESULT_EOF && _r.options->fileTerminate )
                {
                    _r.ending = true;
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


            if ( _r.rawBlock.fillLevel <= 0 )
            {
                /* We are at EOF (Probably the descriptor closed) */
                break;
            }

            /* ...and record the fact that we received some data */
            _r.intervalBytes += _r.rawBlock.fillLevel;

            /* Pump all of the data through the protocol handler */
            uint8_t *c = _r.rawBlock.buffer;

            while ( _r.rawBlock.fillLevel-- )
            {
                _protocolPump( &_r, *c++ );
            }

            /* Check to make sure there's not an unexpected TPIU in here */
            if ( ITMDecoderGetStats( &_r.i )->tpiuSyncCount )
            {
                genericsReport( V_WARN, "Got a TPIU sync while decoding ITM...did you miss a -t option?" EOL );
            }

            /* Update the intervals */
            if ( ( _r.sampling ) && ( ( genericsTimestampmS() - _r.starttime ) > _r.options->sampleDuration ) )
            {
                _r.ending = true;
            }
        }

        stream->close( stream );
        free( stream );
    }

    /* Data are collected, now process and report */
    genericsReport( V_WARN, "Received %d raw sample bytes, %ld function changes, %ld distinct addresses" EOL, _r.intervalBytes, HASH_COUNT( _r.subhead ), HASH_COUNT( _r.insthead ) );

    if ( HASH_COUNT( _r.subhead ) )
    {
        if ( ext_ff_outputDot( _r.options->dotfile, _r.subhead, _r.s ) )
        {
            genericsReport( V_WARN, "Output DOT" EOL );
        }

        if ( ext_ff_outputProfile( _r.options->profile, _r.options->elffile, _r.options->truncateDeleteMaterial ? _r.options->deleteMaterial : NULL, false,
                                   _r.tcount - _r.starttcount, _r.insthead, _r.subhead, _r.s ) )
        {
            genericsReport( V_WARN, "Output Profile" EOL );
        }
    }

    return OK;
}

// ====================================================================================================
