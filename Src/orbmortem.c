/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Post mortem monitor for parallel trace
 * ======================================
 *
 */

#include <stdarg.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <assert.h>
#include <strings.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>

#include "git_version_info.h"
#include "generics.h"
#include "nw.h"
#include "traceDecoder.h"
#include "tpiuDecoder.h"
#include "loadelf.h"
#include "sio.h"
#include "stream.h"

#define REMOTE_SERVER       (char*)"localhost"

#define SCRATCH_STRING_LEN  (65535)     /* Max length for a string under construction */
//#define DUMP_BLOCK
#define DEFAULT_PM_BUFLEN_K (32)        /* Default size of the Postmortem buffer */
#define MAX_TAGS            (10)        /* How many tags we will allow */

#define INTERVAL_TIME_MS    (1000)      /* Intervaltime between acculumator resets */
#define HANG_TIME_MS        (200)       /* Time without a packet after which we dump the buffer */
#define TICK_TIME_MS        (100)       /* Time intervals for screen updates and keypress check */

/* Record for options, either defaults or from command line */
struct Options {
    /* Source information */
    char *file;                         /* File host connection */
    bool fileTerminate;                 /* Terminate when file read isn't successful */
    char *deleteMaterial;               /* Material to delete off front end of filenames */
    bool demangle;                      /* Indicator that C++ should be demangled */

    char *elffile;                      /* File to use for symbols etc. */
    char *odoptions;                    /* Options to pass directly to objdump */

    int buflen;                         /* Length of post-mortem buffer, in bytes */
    bool useTPIU;                       /* Are we using TPIU, and stripping TPIU frames? */
    int channel;                        /* When TPIU is in use, which channel to decode? */
    int port;                           /* Source information */
    char *server;
    bool mono;                          /* Supress colour in output */
    enum TRACEprotocol protocol;        /* Encoding protocol to use */
    bool noAltAddr;                     /* Flag to *not* use alternate addressing */
    char *openFileCL;                   /* Command line for opening refernced file */

    bool withDebugText;                 /* Include debug text (hidden in) output...screws line numbering a bit */
} _options = {
    .demangle = true,
    .buflen   = DEFAULT_PM_BUFLEN_K * 1024,
    .channel  = 2,
    .port     = NWCLIENT_SERVER_PORT,
    .server   = REMOTE_SERVER,
    .protocol = TRACE_PROT_ETM35,
};

/* A block of received data */
struct dataBlock {
    ssize_t fillLevel;
    uint8_t buffer[TRANSFER_SIZE];
};

/* Materials required to be maintained across callbacks for output construction */
struct opConstruct {
    uint32_t currentFileindex;           /* The filename we're currently in */
    struct symbolFunctionStore *currentFunctionptr;       /* The function we're currently in */
    uint32_t currentLine;                /* The line we're currently in */
    uint32_t workingAddr;                /* The address we're currently in */
};

/* Maximum depth of call stack, defined Section 5.3 or ARM IHI0064H.a ID120820 */
#define MAX_CALL_STACK (15)

struct RunTime {
    struct TRACEDecoder i;
    struct TPIUDecoder t;

    const char *progName;               /* Name by which this program was called */

    struct symbol *s;                   /* Symbols read from elf */
    bool     ending;                    /* Flag indicating app is terminating */
    bool     singleShot;                /* Flag indicating take a single buffer then stop */
    uint64_t newTotalBytes;             /* Number of bytes of real data transferred in total */
    uint64_t oldTotalBytes;             /* Old number of bytes of real data transferred in total */
    uint64_t oldTotalIntervalBytes;     /* Number of bytes transferred in previous interval */
    uint64_t oldTotalHangBytes;         /* Number of bytes transferred in previous hang interval */

    uint8_t *pmBuffer;                  /* The post-mortem buffer */
    int wp;                             /* Index pointers for ring buffer */
    int rp;

    struct sioline *opText;             /* Text of the output buffer */
    int32_t lineNum;                    /* Current line number in output buffer */
    int32_t numLines;                   /* Number of lines in the output buffer */

    int32_t diveline;                   /* Line number we're currently diving into */
    char *divefile;                     /* Filename we're currently diving into */
    bool diving;                        /* Flag indicating we're diving into a file at the moment */

    struct sioline *fileopText;         /* The text lines of the file we're diving into */
    int32_t filenumLines;               /* ...and how many lines of it there are */

    bool held;                          /* If we are actively collecting data */

    struct SIOInstance *sio;            /* Our screen IO instance for managed I/O */

    struct dataBlock rawBlock;          /* Datablock received from distribution */

    struct opConstruct op;              /* The mechanical elements for creating the output buffer */

    struct Options *options;            /* Our runtime configuration */

    bool traceRunning;                  /* Set if we are currently receiving trace */
    uint32_t context;                   /* Context we are currently working under */
    symbolMemaddr callStack[MAX_CALL_STACK]; /* Stack of calls */
    unsigned int stackDepth;            /* Maximum stack depth */
    bool stackDelPending;               /* Possibility to remove an entry from the stack, if address not given */
} _r;

/* For opening the editor (Shift-Right-Arrow) the following command lines work for a few editors;
 *
 * emacs; -c "emacs +%l %f"
 * codium; -c "codium  -g %f:%l"
 * eclipse; -c "eclipse %f:%l"
 */

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Private routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
static void _doExit( void ); /* Forward definition needed */
// ====================================================================================================
static void _intHandler( int sig )

/* Catch CTRL-C so things can be cleaned up properly via atexit functions */
{
    /* CTRL-C exit is not an error... */
    exit( 0 );
}
// ====================================================================================================
static void _printHelp( const char *const progName )

{
    genericsPrintf( "Usage: %s [options]" EOL, progName );
    genericsPrintf( "    -A, --alt-addr-enc: Do not use alternate address encoding" EOL );
    genericsPrintf( "    -b, --buffer-len:   <Length> Length of post-mortem buffer, in KBytes (Default %d KBytes)" EOL, DEFAULT_PM_BUFLEN_K );
    genericsPrintf( "    -C, --editor-cmd:   <command> Command line for external editor (%%f = filename, %%l = line)" EOL );
    genericsPrintf( "    -D, --no-demangle:  Switch off C++ symbol demangling" EOL );
    genericsPrintf( "    -d, --del-prefix:   <String> Material to delete off the front of filenames" EOL );
    genericsPrintf( "    -e, --elf-file:     <ElfFile> to use for symbols and source" EOL );
    genericsPrintf( "    -E, --eof:          When reading from file, terminate at end of file rather than waiting for further input" EOL );
    genericsPrintf( "    -f, --input-file:   <filename>: Take input from specified file" EOL );
    genericsPrintf( "    -h, --help:         This help" EOL );
    genericsPrintf( "    -M, --no-colour:    Supress colour in output" EOL );
    genericsPrintf( "    -O, --objdump-opts: <options> Options to pass directly to objdump" EOL );
    genericsPrintf( "    -p, --trace-proto:  { " );

    for ( int i = TRACE_PROT_LIST_START; i < TRACE_PROT_NUM; i++ ) {
        genericsPrintf( "%s ", TRACEDecodeGetProtocolName( ( enum TRACEprotocol )i ) );
    }

    genericsPrintf( "} trace protocol to use, default is %s" EOL, TRACEDecodeGetProtocolName( TRACE_PROT_LIST_START ) );
    genericsPrintf( "    -s, --server:       <Server>:<Port> to use" EOL );
    genericsPrintf( "    -t, --tpiu:         <channel>: Use TPIU to strip TPIU on specfied channel" EOL );
    genericsPrintf( "    -v, --verbose:      <level> Verbose mode 0(errors)..3(debug)" EOL );
    genericsPrintf( "    -V, --version:      Print version and exit" EOL );
    genericsPrintf( EOL "(Will connect one port higher than that set in -s when TPIU is not used)" EOL );
    genericsPrintf(     "(this will automatically select the second output stream from orb TPIU.)" EOL );
    genericsPrintf( EOL "Environment Variables;" EOL );
    genericsPrintf( "  OBJDUMP: to use non-standard objdump binary" EOL );
}
// ====================================================================================================
void _printVersion( void )

{
    genericsPrintf( "orbmortem version " GIT_DESCRIBE );
}
// ====================================================================================================
static struct option _longOptions[] = {
    {"alt-addr-enc", no_argument, NULL, 'A'},
    {"buffer-len", required_argument, NULL, 'b'},
    {"editor-cmd", required_argument, NULL, 'C'},
    {"no-demangle", required_argument, NULL, 'D'},
    {"del-prefix", required_argument, NULL, 'd'},
    {"elf-file", required_argument, NULL, 'e'},
    {"eof", no_argument, NULL, 'E'},
    {"input-file", required_argument, NULL, 'f'},
    {"help", no_argument, NULL, 'h'},
    {"no-colour", no_argument, NULL, 'M'},
    {"no-color", no_argument, NULL, 'M'},
    {"objdump-opts", required_argument, NULL, 'O'},
    {"trace-proto", required_argument, NULL, 'p'},
    {"server", required_argument, NULL, 's'},
    {"tpiu", required_argument, NULL, 't'},
    {"verbose", required_argument, NULL, 'v'},
    {"version", no_argument, NULL, 'V'},
    {NULL, no_argument, NULL, 0}
};
// ====================================================================================================
static bool _processOptions( int argc, char *argv[], struct RunTime *r )

{
    int c, optionIndex = 0;
    char *a;

    while ( ( c = getopt_long ( argc, argv, "Ab:C:Dd:Ee:f:hVMO:p:s:t:v:w", _longOptions, &optionIndex ) ) != -1 )
        switch ( c ) {
            // ------------------------------------
            case 'A':
                r->options->noAltAddr = true;
                break;

            // ------------------------------------
            case 'b':
                r->options->buflen = atoi( optarg ) * 1024;
                break;

            // ------------------------------------
            case 'C':
                r->options->openFileCL = optarg;
                break;

            // ------------------------------------
            case 'D':
                r->options->demangle = false;
                break;

            // ------------------------------------
            case 'd':
                r->options->deleteMaterial = optarg;
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

            case 'h':
                _printHelp( r->progName );
                return false;

            // ------------------------------------
            case 'V':
                _printVersion();
                return false;

            // ------------------------------------

            case 'M':
                r->options->mono = true;
                break;

            // ------------------------------------
            case 'O':
                r->options->odoptions = optarg;
                break;

            // ------------------------------------
            case 'p':

                /* Index through protocol strings looking for match or end of list */
                for ( c = TRACE_PROT_LIST_START;
                      ( ( c != TRACE_PROT_NUM ) && strcasecmp( optarg, TRACEDecodeGetProtocolName( ( enum TRACEprotocol )c ) ) );
                      c++ )
                {}

                r->options->protocol = ( enum TRACEprotocol )c;
                break;

            // ------------------------------------

            case 's':
                r->options->server = optarg;

                // See if we have an optional port number too
                a = optarg;

                while ( ( *a ) && ( *a != ':' ) ) {
                    a++;
                }

                if ( *a == ':' ) {
                    *a = 0;
                    r->options->port = atoi( ++a );
                }

                if ( !r->options->port ) {
                    r->options->port = NWCLIENT_SERVER_PORT;
                }

                break;

            // ------------------------------------

            case 't':
                r->options->useTPIU = true;
                r->options->channel = atoi( optarg );
                break;

            // ------------------------------------

            case 'v':
                if ( !isdigit( *optarg ) ) {
                    genericsReport( V_ERROR, "-v requires a numeric argument." EOL );
                    return false;
                }

                genericsSetReportLevel( ( enum verbLevel )atoi( optarg ) );
                break;

            // ------------------------------------

            case 'w':
                r->options->withDebugText = true;
                break;

            // ------------------------------------

            case '?':
                if ( optopt == 'b' ) {
                    genericsReport( V_ERROR, "Option '%c' requires an argument." EOL, optopt );
                } else if ( !isprint ( optopt ) ) {
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
    genericsReport( V_INFO, "orbmortem version " GIT_DESCRIBE EOL );

    if ( r->options->withDebugText ) {
        genericsReport( V_INFO, "Incoporate debug text in output buffer" EOL );
    }

    if ( r->options->protocol >= TRACE_PROT_NONE ) {
        genericsExit( V_ERROR, "Unrecognised decode protocol" EOL );
    } else {
        genericsReport( V_INFO, "Protocol %s" EOL, TRACEDecodeGetProtocolName( r->options->protocol ) );
    }

    if ( ( r->options->protocol == TRACE_PROT_MTB ) && ( !r->options->file ) ) {
        genericsExit( V_ERROR, "MTB only makes sense when input is from a file" EOL );
    }

    if ( !r->options->elffile ) {
        genericsExit( V_ERROR, "Elf File not specified" EOL );
    }

    if ( !r->options->buflen ) {
        genericsExit( -1, "Illegal value for Post Mortem Buffer length" EOL );
    }

    return true;
}
// ====================================================================================================
static void _processBlock( struct RunTime *r )

/* Generic block processor for received data */

{
    uint8_t *c = r->rawBlock.buffer;
    int32_t y = r->rawBlock.fillLevel;

    genericsReport( V_DEBUG, "RXED Packet of %d bytes" EOL, y );

    if ( y ) {
#ifdef DUMP_BLOCK
        fprintf( stderr, EOL );

        while ( y-- ) {
            fprintf( stderr, "%02X ", *c++ );

            if ( !( y % 16 ) ) {
                fprintf( stderr, EOL );
            }
        }

        c = r->rawBlock.buffer;
        y = r->rawBlock.fillLevel;
#endif

        if ( r->options->useTPIU ) {
            struct TPIUPacket p;

            while ( y-- ) {
                if ( TPIU_EV_RXEDPACKET == TPIUPump( &r->t, *c++ ) ) {
                    if ( !TPIUGetPacket( &r->t, &p ) ) {
                        genericsReport( V_WARN, "TPIUGetPacket fell over" EOL );
                    } else {
                        /* Iterate through the packet, putting bytes for TRACE into the processing buffer */
                        for ( int g = 0; g < p.len; g++ ) {
                            if ( r->options->channel == p.packet[g].s ) {
                                r->pmBuffer[r->wp] = p.packet[g].d;
                                r->newTotalBytes++;
                                int32_t nwp = ( r->wp + 1 ) % r->options->buflen;

                                if ( nwp == r->rp ) {
                                    if ( r->singleShot ) {
                                        r->held = true;
                                        return;
                                    } else {
                                        r->rp = ( r->rp + 1 ) % r->options->buflen;
                                    }
                                }

                                r->wp = nwp;
                            }
                        }
                    }
                }
            }
        } else {
            r->newTotalBytes += y;

            while ( y-- ) {
                r->pmBuffer[r->wp] = *c++;
                int32_t nwp = ( r->wp + 1 ) % r->options->buflen;

                if ( nwp == r->rp ) {
                    if ( r->singleShot ) {
                        r->held = true;
                        return;
                    } else {
                        r->rp = ( r->rp + 1 ) % r->options->buflen;
                    }
                }

                r->wp = nwp;
            }
        }
    }
}
// ====================================================================================================
static void _flushBuffer( struct RunTime *r )

/* Empty the output buffer, and de-allocate its memory */

{
    /* Tell the UI there's nothing more to show */
    SIOsetOutputBuffer( r->sio, 0, 0, NULL, false );

    /* Remove all of the recorded lines */
    while ( r->numLines-- ) {
        if ( !r->opText[r->numLines].isRef ) {
            free( r->opText[r->numLines].buffer );
        }
    }

    /* and the opText buffer */
    free( r->opText );
    r->opText = NULL;
    r->numLines = 0;

    /* ...and the file/line references */
    r->op.currentLine = NO_LINE;
    r->op.currentFileindex = NO_FILE;
    r->op.currentFunctionptr = NULL;
    r->op.workingAddr = NO_DESTADDRESS;
}
// ====================================================================================================
// Strdup leak is deliberately ignored. That is the central purpose of this code. It's cleaned
// upin __flushBuffer above.
#pragma GCC diagnostic push
#if !defined(__clang__)
    #pragma GCC diagnostic ignored "-Wanalyzer-malloc-leak"
#endif

static void _appendToOPBuffer( struct RunTime *r, void *dat, int32_t lineno, enum LineType lt, const char *fmt, ... )

/* Add line to output buffer, in a printf stylee */

{
    char construct[SCRATCH_STRING_LEN];
    va_list va;
    char *p;

    va_start( va, fmt );
    vsnprintf( construct, SCRATCH_STRING_LEN, fmt, va );
    va_end( va );

    /* Make sure we didn't accidentially admit a CR or LF */
    for ( p = construct; ( ( *p ) && ( *p != '\n' ) && ( *p != '\r' ) ); p++ );

    *p = 0;

    r->opText = ( struct sioline * )realloc( r->opText, ( sizeof( struct sioline ) ) * ( r->numLines + 1 ) );
    r->opText[r->numLines].buffer = strdup( construct );
    r->opText[r->numLines].lt     = lt;
    r->opText[r->numLines].line   = lineno;
    r->opText[r->numLines].isRef  = false;
    r->opText[r->numLines].dat    = dat;
    r->numLines++;
}
#pragma GCC diagnostic pop

// ====================================================================================================
static void _appendRefToOPBuffer( struct RunTime *r, void *dat, int32_t lineno, enum LineType lt, const char *ref )

/* Add line to output buffer, as a reference (which don't be free'd later) */

{
    r->opText = ( struct sioline * )realloc( r->opText, ( sizeof( struct sioline ) ) * ( r->numLines + 1 ) );

    /* This line removes the 'const', but we know to not mess with this line */
    r->opText[r->numLines].buffer = ( char * )ref;
    r->opText[r->numLines].lt     = lt;
    r->opText[r->numLines].line   = lineno;
    r->opText[r->numLines].isRef  = true;
    r->opText[r->numLines].dat    = dat;
    r->numLines++;
}
// ====================================================================================================
static void _traceReport( enum verbLevel l, const char *fmt, ... )

/* Debug reporting stream */

{
    if ( _r.options->withDebugText ) {
        static char op[SCRATCH_STRING_LEN];

        va_list va;
        va_start( va, fmt );
        vsnprintf( op, SCRATCH_STRING_LEN, fmt, va );
        va_end( va );
        _appendToOPBuffer( &_r, NULL, _r.op.currentLine, LT_DEBUG, op );
    }
}
// ====================================================================================================
static void _addRetToStack( struct RunTime *r, symbolMemaddr p )

{
    if ( r->stackDepth == MAX_CALL_STACK - 1 ) {
        /* Stack is full, so make room for a new entry */
        memmove( &r->callStack[0], &r->callStack[1], sizeof( symbolMemaddr ) * ( MAX_CALL_STACK - 1 ) );
    }

    r->callStack[r->stackDepth] = p;
    _traceReport( V_DEBUG, "Pushed %08x to return stack", r->callStack[r->stackDepth] );

    if ( r->stackDepth < MAX_CALL_STACK - 1 ) {
        /* We aren't at max depth, so go ahead and remove this entry */
        r->stackDepth++;
    }
}
// ====================================================================================================
static void _reportNonflowEvents( struct RunTime *r )

{
    struct TRACECPUState *cpu = TRACECPUState( &r->i );

    if ( TRACEStateChanged( &r->i, EV_CH_TRACESTART ) ) {
        if ( !r->traceRunning ) {
            _appendRefToOPBuffer( r, NULL, r->op.currentLine, LT_EVENT, "========== TRACE START EVENT ==========" );
            r->traceRunning = true;
        }
    }

    if ( TRACEStateChanged( &r->i, EV_CH_VMID ) ) {
        _appendToOPBuffer( r, NULL, r->op.currentLine, LT_EVENT, "*** VMID Set to %d", cpu->vmid );
    }

    if ( TRACEStateChanged( &r->i, EV_CH_EX_EXIT ) ) {
        _appendRefToOPBuffer( r, NULL, r->op.currentLine, LT_EVENT, "========== Exception Exit ==========" );
    }

    if ( TRACEStateChanged( &r->i, EV_CH_TSTAMP ) ) {
        if ( cpu->ts ) {
            if ( cpu->ts != COUNT_UNKNOWN ) {
                _appendToOPBuffer( r, NULL, r->op.currentLine, LT_EVENT, "*** Timestamp %ld", cpu->ts );
            } else {
                _appendToOPBuffer( r, NULL, r->op.currentLine, LT_EVENT, "*** Timestamp unknown" );
            }
        }
    }

    if ( TRACEStateChanged( &r->i, EV_CH_TRIGGER ) ) {
        _appendRefToOPBuffer( r, NULL, r->op.currentLine, LT_EVENT, "*** Trigger" );
    }

    if ( TRACEStateChanged( &r->i, EV_CH_CLOCKSPEED ) ) {
        _appendRefToOPBuffer( r, NULL, r->op.currentLine, LT_EVENT, "*** Change Clockspeed" );
    }

    if ( TRACEStateChanged( &r->i, EV_CH_ISLSIP ) ) {
        _appendRefToOPBuffer( r, NULL, r->op.currentLine, LT_EVENT, "*** ISLSIP Triggered" );
    }

    if ( TRACEStateChanged( &r->i, EV_CH_CYCLECOUNT ) ) {
        _appendToOPBuffer( r, NULL, r->op.currentLine, LT_EVENT, "(Cycle Count %d)", cpu->cycleCount );
    }

    if ( TRACEStateChanged( &r->i, EV_CH_VMID ) ) {
        _appendToOPBuffer( r, NULL, r->op.currentLine, LT_EVENT, "(VMID is now %d)", cpu->vmid );
    }

    if ( TRACEStateChanged( &r->i, EV_CH_CONTEXTID ) ) {
        if ( r->context != cpu->contextID ) {
            _appendToOPBuffer( r, NULL, r->op.currentLine, LT_EVENT, "(Context ID is now %d)", cpu->contextID );
            r->context = cpu->contextID;
        }
    }

    if ( TRACEStateChanged( &r->i, EV_CH_SECURE ) ) {
        _appendToOPBuffer( r, NULL, r->op.currentLine, LT_EVENT, "(Non-Secure State is now %s)", cpu->nonSecure ? "True" : "False" );
    }

    if ( TRACEStateChanged( &r->i, EV_CH_ALTISA ) ) {
        _appendToOPBuffer( r, NULL, r->op.currentLine, LT_EVENT, "(Using AltISA  is now %s)", cpu->altISA ? "True" : "False" );
    }

    if ( TRACEStateChanged( &r->i, EV_CH_HYP ) ) {
        _appendToOPBuffer( r, NULL, r->op.currentLine,  LT_EVENT, "(Using Hypervisor is now %s)", cpu->hyp ? "True" : "False" );
    }

    if ( TRACEStateChanged( &r->i, EV_CH_JAZELLE ) ) {
        _appendToOPBuffer( r, NULL, r->op.currentLine, LT_EVENT, "(Using Jazelle is now %s)", cpu->jazelle ? "True" : "False" );
    }

    if ( TRACEStateChanged( &r->i, EV_CH_THUMB ) ) {
        _appendToOPBuffer( r, NULL, r->op.currentLine, LT_EVENT, "(Using Thumb is now %s)", cpu->thumb ? "True" : "False" );
    }
}

// ====================================================================================================
static void _traceCB( void *d )

/* Callback function for when valid TRACE decode is detected */

{
    struct RunTime *r = ( struct RunTime * )d;
    struct TRACECPUState *cpu = TRACECPUState( &r->i );
    uint32_t incAddr = 0;
    uint32_t disposition;
    uint32_t targetAddr = 0; /* Just to avoid unitialised variable warning */
    bool linearRun = false;
    int ic;
    symbolMemaddr newaddr;

    /* 1: Report anything that doesn't affect the flow */
    /* =============================================== */
    _reportNonflowEvents( r );

    /* 2: Deal with exception entry */
    /* ============================ */
    if ( TRACEStateChanged( &r->i, EV_CH_EX_ENTRY ) ) {
        switch ( r->options->protocol ) {
            case TRACE_PROT_ETM35:
                _appendToOPBuffer( r, NULL, r->op.currentLine, LT_EVENT, "========== Exception Entry%s (%d (%s) at 0x%08x) ==========",
                                   TRACEStateChanged( &r->i, EV_CH_CANCELLED ) ? ", Last Instruction Cancelled" : "", cpu->exception, TRACEExceptionName( cpu->exception ), cpu->addr );
                break;

            case TRACE_PROT_MTB:
                _appendRefToOPBuffer( r, NULL, r->op.currentLine, LT_EVENT, "========== Exception Entry ==========" );
                break;


            case TRACE_PROT_ETM4:

                /* For the ETM4 case we get a new address with the exception indication. This address is the preferred _return_ address, */
                /* there will be a further address packet, which is the jump destination, along shortly. Note that _this_ address        */
                /* change indication will be consumed here, and won't hit the test below (which is correct behaviour.                    */
                if ( !TRACEStateChanged( &r->i, EV_CH_ADDRESS ) ) {
                    _traceReport( V_DEBUG, "Exception occured without return address specification" );
                } else {
                    _appendToOPBuffer( r, NULL, r->op.currentLine, LT_EVENT, "========== Exception Entry (%d (%s) at 0x%08x return to %08x ) ==========",
                                       cpu->exception, TRACEExceptionName( cpu->exception ), r->op.workingAddr, cpu->addr );
                    _addRetToStack( r, cpu->addr );
                }

                break;

            default:
                _traceReport( V_DEBUG, "Unrecognised trace protocol in exception handler" );
                break;
        }
    }


    /* 3: Collect flow affecting changes introduced by this event */
    /* ========================================================== */
    if ( TRACEStateChanged( &r->i, EV_CH_ADDRESS ) ) {
        /* Make debug report if calculated and reported addresses differ. This is most useful for testing when exhaustive  */
        /* address reporting is switched on. It will give 'false positives' for uncalculable instructions (e.g. bx lr) but */
        /* it's a decent safety net to be sure the jump decoder is working correctly.                                      */

        if ( r->options->protocol != TRACE_PROT_MTB ) {
            _traceReport( V_DEBUG, "%sCommanded CPU Address change (Was:0x%08x Commanded:0x%08x)" EOL,
                          ( r->op.workingAddr == cpu->addr ) ? "" : "***INCONSISTENT*** ", r->op.workingAddr, cpu->addr );
        }

        /* Return Stack: If we had a stack deletion pending because of a candidate match, it wasn't, so abort */
        if ( r->stackDelPending ) {
            _traceReport( V_DEBUG, "Stack delete aborted" );
        }

        r->stackDelPending = false;
        /* Whatever the state was, this is an explicit setting of an address, so we need to respect it */
        r->op.workingAddr = cpu->addr;
    } else {
        /* Return Stack: If we had a stack deletion pending because of a candidate match, the match was good, so commit */
        if ( ( r->stackDelPending == true ) && ( r->stackDepth ) ) {
            r->stackDepth--;
            _traceReport( V_DEBUG, "Stack delete comitted" );
        }

        r->stackDelPending = false;
    }

    if ( TRACEStateChanged( &r->i, EV_CH_LINEAR ) ) {
        /* MTB-Specific mechanism: Execute instructions from the marked starting location to the indicated finishing one */
        /* Disposition is all 1's because every instruction is executed.                                                 */
        r->op.workingAddr = cpu->addr;
        targetAddr        = cpu->toAddr;
        linearRun         = true;
        disposition       = 0xffffffff;
        _traceReport( V_DEBUG, "Linear run 0x%08x to 0x%08x" EOL, cpu->addr, cpu->toAddr );
    }

    if ( TRACEStateChanged( &r->i, EV_CH_ENATOMS ) ) {
        /* Atoms represent instruction steps...some of which will have been executed, some stepped over. The number of steps is the   */
        /* total of the eatoms (executed) and natoms (not executed) and the disposition bitfield shows if each individual instruction */
        /* was executed or not. For ETM3 each 'run' of instructions is a single instruction with the disposition bit telling you if   */
        /* it was executed or not. For ETM4 each 'run' of instructions is from the current address to the next possible change of     */
        /* program flow (and which point the disposition bit tells you if that jump was taken or not).                                */
        incAddr = cpu->eatoms + cpu->natoms;
        disposition = cpu->disposition;
    }

    /* 4: Execute the flow instructions */
    /* ================================ */
    while ( ( incAddr && !linearRun ) || ( ( r->op.workingAddr <= targetAddr ) && linearRun ) ) {
        /* Firstly, lets get the source code line...*/
        struct symbolLineStore *l = symbolLineAt( r->s, r->op.workingAddr );

        if ( l ) {
            /* If we have changed file or function put a header line in */
            if ( l->function ) {
                /* There is a valid function tag recognised here. If it's a change highlight it in the output. */
                if ( ( l->function->filename != r->op.currentFileindex ) || ( l->function != r->op.currentFunctionptr ) ) {
                    _appendToOPBuffer( r, l, r->op.currentLine, LT_FILE, "%s::%s", symbolGetFilename( r->s, l->function->filename ), l->function->funcname );
                    r->op.currentFileindex     = l->function->filename;
                    r->op.currentFunctionptr = l->function;
                    r->op.currentLine = NO_LINE;
                }
            } else {
                /* We didn't find a valid function, but we might have some information to work with.... */
                if ( ( NO_FILE != r->op.currentFileindex ) || ( NULL != r->op.currentFunctionptr ) ) {
                    _appendToOPBuffer( r, l, r->op.currentLine, LT_FILE, "Unknown function" );
                    r->op.currentFileindex     = NO_FILE;
                    r->op.currentFunctionptr = NULL;
                    r->op.currentLine = NO_LINE;
                }
            }
        }

        /* If we have changed line then output the new one */
        if ( l && ( ( l->startline != r->op.currentLine ) ) ) {
            const char *v = symbolSource( r->s, l->filename, l->startline - 1 );
            r->op.currentLine = l->startline;
            _appendRefToOPBuffer( r, l, r->op.currentLine, LT_SOURCE, v );
        }

        /* Now output the matching assembly, and location updates */
        char *a = symbolDisassembleLine( r->s, &ic, r->op.workingAddr, &newaddr );

        if ( a ) {
            /* Calculate if this instruction was executed. This is slightly hairy depending on which protocol we're using;         */
            /*   * ETM3.5: Instructions are executed based on disposition bit (LSB in disposition word)                            */
            /*   * ETM4  : ETM4 everything up to a branch is executed...decision about that branch is based on disposition bit     */
            /*   * MTB   : Everything except jumps are executed, jumps are executed only if they are the last instruction in a run */
            bool insExecuted = (
                                           /* ETM3.5 case - dependent on disposition */
                                           ( ( !linearRun )  && ( r->i.protocol == TRACE_PROT_ETM35 ) && ( disposition & 1 ) ) ||

                                           /* ETM4 case - either not a branch or disposition is 1 */
                                           ( ( !linearRun ) && ( r->i.protocol == TRACE_PROT_ETM4 ) && ( ( !( ic & LE_IC_JUMP ) ) || ( disposition & 1 ) ) ) ||

                                           /* MTB case - a linear run to last address */
                                           ( ( linearRun ) &&
                                             ( ( ( r->op.workingAddr != targetAddr ) && ( ! ( ic & LE_IC_JUMP ) ) )  ||
                                               ( r->op.workingAddr == targetAddr )
                                             ) ) );
            _appendToOPBuffer( r, l, r->op.currentLine, insExecuted ? LT_ASSEMBLY : LT_NASSEMBLY, a );


            /* Move addressing along */
            if ( ( r->i.protocol != TRACE_PROT_ETM4 ) || ( ic & LE_IC_JUMP ) ) {
                if ( r->i.protocol == TRACE_PROT_ETM4 ) {
                    _traceReport( V_DEBUG, "Consumed, %sexecuted (%d left)", insExecuted ? "" : "not ", incAddr - 1 );
                }

                disposition >>= 1;
                incAddr--;
            }

            if ( ic & LE_IC_CALL ) {
                if ( insExecuted ) {
                    /* Push the instruction after this if it's a subroutine or ISR */
                    _traceReport( V_DEBUG, "Call to %08x", newaddr );
                    _addRetToStack( r, r->op.workingAddr + ( ( ic & LE_IC_4BYTE ) ? 4 : 2 ) );
                }

                r->op.workingAddr = insExecuted ? newaddr : r->op.workingAddr + ( ( ic & LE_IC_4BYTE ) ? 4 : 2 );
            } else if ( ic & LE_IC_JUMP ) {
                _traceReport( V_DEBUG, "%sTAKEN JUMP", insExecuted ? "" : "NOT " );

                if ( insExecuted ) {
                    /* Update working address according to if jump was taken */
                    if ( ic & LE_IC_IMMEDIATE ) {
                        /* We have a good address, so update with it */
                        r->op.workingAddr = newaddr;
                    } else {
                        /* We didn't get the address, so need to park the call stack address if we've got one. Either we won't      */
                        /* get an address (in which case this one was correct), or we wont (in which case, don't unstack this one). */
                        if ( r->stackDepth ) {
                            r->op.workingAddr = r->callStack[r->stackDepth - 1];
                            _traceReport( V_DEBUG, "Return with stacked candidate to %08x", r->op.workingAddr );
                        } else {
                            _traceReport( V_DEBUG, "Return with no stacked candidate" );
                        }

                        r->stackDelPending = true;
                    }
                } else {
                    /* The branch wasn't taken, so just move along */
                    r->op.workingAddr += ( ic & LE_IC_4BYTE ) ? 4 : 2;
                }
            } else {
                /* Just a regular instruction, so just move along */
                r->op.workingAddr += ( ic & LE_IC_4BYTE ) ? 4 : 2;
            }
        } else {
            _appendRefToOPBuffer( r, l, r->op.currentLine, LT_ASSEMBLY, "\t\tASSEMBLY NOT FOUND" EOL );
            r->op.workingAddr += 2;
            disposition >>= 1;
            incAddr--;
        }
    }
}

// ====================================================================================================
static bool _dumpBuffer( struct RunTime *r )

/* Dump received data buffer into text buffer */

{
    _flushBuffer( r );

    if ( !symbolSetValid( r->s ) ) {
        symbolDelete( r->s );

        if ( !( r->s = symbolAquire( r->options->elffile, true, true, true ) ) ) {
            genericsReport( V_ERROR, "Elf file or symbols in it not found" EOL );
            return false;
        }

        genericsReport( V_DEBUG, "Loaded %s" EOL, r->options->elffile );
    }

    /* Pump the received messages through the TRACE decoder, it will callback to _traceCB with complete sentences */
    int bytesAvailable = ( ( r->wp + r->options->buflen ) - r->rp ) % r->options->buflen;

    /* If we started wrapping (i.e. the rx ring buffer got full) then any guesses about sync status are invalid */
    if ( ( bytesAvailable == r->options->buflen - 1 ) && ( !r->singleShot ) ) {
        TRACEDecoderForceSync( &r->i, false );
    }

    /* Two calls in case buffer is wrapped - submit both parts */
    TRACEDecoderPump( &r->i, &r->pmBuffer[r->rp], r->options->buflen - r->rp, _traceCB, r );

    /* The length of this second buffer can be 0 for case buffer is not wrapped */
    TRACEDecoderPump( &r->i, &r->pmBuffer[0], r->wp, _traceCB, r );

    /* Submit this constructed buffer for display */
    SIOsetOutputBuffer( r->sio, r->numLines, r->numLines - 1, &r->opText, false );

    return true;
}
// ====================================================================================================
static struct symbolLineStore *_fileAndLine( struct RunTime *r, int32_t i )

{
    /* Search backwards from current position in buffer until we find a line a line record attached */
    /* (b) a filename which contains this line. */

    while ( ( i ) &&
            ( ( ( ( r->opText[i].lt != LT_SOURCE ) && ( r->opText[i].lt != LT_ASSEMBLY ) ) || r->opText[i].dat == NULL ) ) ) {
        i--;
    }

    if ( !i || !r->opText[i].dat ) {
        i = SIOgetCurrentLineno( r->sio );

        while ( ( i ) &&
                ( ( ( ( r->opText[i].lt != LT_SOURCE ) && ( r->opText[i].lt != LT_ASSEMBLY ) ) || r->opText[i].dat == NULL ) ) ) {
            i++;
        }
    }

    return ( struct symbolLineStore * )( ( i < r->numLines ) ? r->opText[i].dat : NULL );
}
// ====================================================================================================
static void _mapFileBuffer( struct RunTime *r, int lineno, int filenameIndex )

/* Map filename records into buffer */

{
    /* Get line reference from current buffer */
    assert( r->fileopText == NULL );
    assert( r->filenumLines == 0 );
    unsigned int index = 0;
    const char *c;

    r->filenumLines = 0;

    while ( ( c = symbolSource( r->s, filenameIndex, index++ ) ) ) {
        r->fileopText = ( struct sioline * )realloc( r->fileopText, ( sizeof( struct sioline ) ) * ( r->filenumLines + 1 ) );

        /* This line removes the 'const', but we know to not mess with this line */
        r->fileopText[r->filenumLines].buffer = ( char * )c;
        r->fileopText[r->filenumLines].dat    = NULL;
        r->fileopText[r->filenumLines].lt     = LT_MU_SOURCE;
        r->fileopText[r->filenumLines].isRef  = true;
        r->fileopText[r->filenumLines].line   = r->filenumLines + 1;
        r->filenumLines++;
    }

    SIOsetOutputBuffer( r->sio, r->filenumLines, lineno - 1, &r->fileopText, true );
    r->diving = true;
}
// ====================================================================================================
static void _doFileDive( struct RunTime *r )

/* Do actions required to get file contents to dive into */

{
    static struct symbolLineStore *l;

    if ( ( r->diving ) || ( !r->numLines ) || ( !r->held ) ) {
        return;
    }

    /* There should be no file read in at the moment */
    assert( !r->fileopText );
    assert( !r->filenumLines );

    if ( !( l = _fileAndLine( r, SIOgetCurrentLineno( r->sio ) ) ) ) {
        SIOalert( r->sio, "Couldn't get filename/line" );
        return;
    }

    /* Cache the line in this file in case we need it later */
    r->lineNum = SIOgetCurrentLineno( r->sio );

    _mapFileBuffer( r,  l->startline, l->filename );
}
// ====================================================================================================
static void _doFilesurface( struct RunTime *r )

/* Come back out of a file we're diving into */

{
    if ( !r->diving ) {
        return;
    }

    /* Buffer is a ref so we don't need to delete it, just remove the index */
    free( r->fileopText );
    r->fileopText = NULL;
    r->filenumLines = 0;
    r->diving = false;

    SIOsetOutputBuffer( r->sio, r->numLines, r->numLines - 1, &r->opText, false );
    SIOsetCurrentLineno( r->sio, r->lineNum );
}
// ====================================================================================================
static void _doSave( struct RunTime *r, bool includeDebug )

/* Save buffer in both raw and processed formats */

{
    FILE *f;
    char fn[SCRATCH_STRING_LEN];
    int32_t w;
    char *p;

    snprintf( fn, SCRATCH_STRING_LEN, "%s.trace", SIOgetSaveFilename( r->sio ) );
    f = fopen( fn, "wb" );

    if ( !f ) {
        SIOalert( r->sio, "Save Trace Failed" );
        return;
    }

    w = r->rp;

    while ( w != r->wp ) {
        fwrite( &r->pmBuffer[w], 1, 1, f );
        w = ( w + 1 ) % r->options->buflen;
    }

    fclose( f );

    snprintf( fn, SCRATCH_STRING_LEN, "%s.report", SIOgetSaveFilename( r->sio ) );
    f = fopen( fn, "wb" );

    if ( !f ) {
        SIOalert( r->sio, "Save Report Failed" );
        return;
    }

    w = 0;

    while ( w != r->numLines ) {
        p = r->opText[w].buffer;

        /* Skip debug lines unless specifically told to include them */
        if ( ( r->opText[w].lt == LT_DEBUG ) && ( !includeDebug ) ) {
            continue;
        }

        if ( ( r->opText[w].lt == LT_SOURCE ) || ( r->opText[w].lt == LT_MU_SOURCE ) ) {
            /* Need a line number on this */
            fwrite( fn, sprintf( fn, "%5d ", r->opText[w].line ), 1, f );
        }

        if ( r->opText[w].lt == LT_NASSEMBLY ) {
            /* This is an _unexecuted_ assembly line, need to mark it */
            fwrite( "(**", 3, 1, f );
        }

        /* Search forward for a NL or 0, both are EOL for this purpose */
        while ( ( *p ) && ( *p != '\n' ) && ( *p != '\r' ) ) {
            p++;
        }

        fwrite( r->opText[w].buffer, p - r->opText[w].buffer, 1, f );

        if ( r->opText[w].lt == LT_NASSEMBLY ) {
            /* This is an _unexecuted_ assembly line, need to mark it */
            fwrite( " **)", 4, 1, f );
        }

        fwrite( EOL, strlen( EOL ), 1, f );
        w++;
    }

    fclose( f );

    SIOalert( r->sio, "Save Complete" );
}
// ====================================================================================================
static void _doExit( void )

/* Perform any explicit exit functions */

{
    _r.ending = true;
    /* Give them a bit of time, then we're leaving anyway */
    usleep( 200 );
    SIOterminate( _r.sio );
}

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Publicly available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
int main( int argc, char *argv[] )

{
    int32_t lastTTime, lastTSTime, lastHTime;
    struct Stream *stream;              /* Stream that we are collecting data from */
    struct timeval tv;
    enum SIOEvent s;

    /* Have a basic name and search string set up */
    _r.progName = genericsBasename( argv[0] );

    /* This is set here to avoid huge .data section in startup image */
    _r.options = &_options;

    if ( !_processOptions( argc, argv, &_r ) ) {
        /* processOptions generates its own error messages */
        genericsExit( -1, "" EOL );
    }

    genericsScreenHandling( !_r.options->mono );

    /* Make sure the fifos get removed at the end */
    atexit( _doExit );

    if ( _r.options->file != NULL ) {
        if ( NULL == ( stream = streamCreateFile( _r.options->file ) ) ) {
            genericsExit( V_ERROR, "File not found" EOL );
            _r.ending = true;
        }
    }

    /* Check we've got _some_ symbols to start from */
    _r.s = symbolAquire( _r.options->elffile, true, true, true );

    if ( !_r.s ) {
        genericsReport( V_ERROR, "Elf file or symbols in it not found" EOL );
        return -1;
    }

    genericsReport( V_DEBUG, "Loaded %s" EOL, _r.options->elffile );

    /* This ensures the atexit gets called */
    if ( SIG_ERR == signal( SIGINT, _intHandler ) ) {
        genericsExit( -1, "Failed to establish Int handler" EOL );
    }

    /* Fill in a time to start from */
    lastHTime = lastTTime = lastTSTime = genericsTimestampmS();


#if !defined( WIN32 )

    /* Don't kill a sub-process when any reader or writer evaporates */
    if ( SIG_ERR == signal( SIGPIPE, SIG_IGN ) ) {
        genericsExit( -1, "Failed to ignore SIGPIPEs" EOL );
    }

#endif

    /* Create the buffer memory */
    _r.pmBuffer = ( uint8_t * )calloc( 1, _r.options->buflen );

    TRACEDecoderInit( &_r.i, _r.options->protocol, !( _r.options->noAltAddr ), _traceReport );

    if ( _r.options->useTPIU ) {
        TPIUDecoderInit( &_r.t );
    }

    /* Create a screen and interaction handler */
    _r.sio = SIOsetup( _r.progName, _r.options->elffile, ( _r.options->file != NULL ) );

    /* Put a record of the protocol in use on screen */
    SIOtagText( _r.sio, TRACEDecodeGetProtocolName( _r.options->protocol ) );

    while ( !_r.ending ) {
        if ( NULL == _r.options->file ) {
            /* Keep trying to open a network connection at half second intervals */
            while ( 1 ) {
                stream = streamCreateSocket( _r.options->server, _r.options->port + ( _r.options->useTPIU ? 0 : 1 ) );

                if ( stream ) {
                    break;
                }

                /* This can happen when the feeder has gone missing... */
                SIOalert( _r.sio, "No connection" );

                if ( SIOHandler( _r.sio, true, 0, _r.options->withDebugText ) == SIO_EV_QUIT ) {
                    _r.ending = true;
                    break;
                }

                usleep( 1000000 );
            }
        }

        /* ----------------------------------------------------------------------------- */
        /* This is the main active loop...only break out of this when ending or on error */
        /* ----------------------------------------------------------------------------- */
        while ( !_r.ending ) {
            tv.tv_sec = 0;
            tv.tv_usec  = 10000;

            if ( stream ) {
                /* We always read the data, even if we're held, to keep the socket alive */
                enum ReceiveResult result = stream->receive( stream, _r.rawBlock.buffer, TRANSFER_SIZE, &tv, ( size_t * )&_r.rawBlock.fillLevel );

                /* Try to re-establish socket if there was an error */
                if ( result == RECEIVE_RESULT_ERROR ) {
                    break;
                }

                if ( ( ( result == RECEIVE_RESULT_EOF ) || ( _r.rawBlock.fillLevel <= 0 ) ) && _r.options->file ) {
                    /* Read from file is complete, remove it */
                    stream->close( stream );
                    free( stream );

                    stream = NULL;
                }
            } else {
                /* No point in checking for keypresses _too_ often! */
                usleep( TICK_TIME_MS * 100 );
            }

            if ( !_r.held ) {
                /* Pump all of the data through the protocol handler */
                _processBlock( &_r );
            }

            /* Update the outputs and deal with any keys that made it up this high */
            /* =================================================================== */
            switch ( ( s = SIOHandler( _r.sio, ( genericsTimestampmS() - lastTTime ) > TICK_TIME_MS, _r.oldTotalIntervalBytes, _r.options->withDebugText ) ) ) {
                case SIO_EV_HOLD:  // ----------------- Request for Hold Start/Stop -------------------------------------
                    if ( !_r.options->file ) {
                        _r.held = !_r.held;

                        if ( !_r.held ) {
                            _r.wp = _r.rp = 0;

                            if ( _r.diving ) {
                                _doFilesurface( &_r );
                            }

                            _flushBuffer( &_r );
                        }

                        /* Flag held status to the UI */
                        SIOheld( _r.sio, _r.held );
                    }

                    break;

                case SIO_EV_PREV:
                case SIO_EV_NEXT: // ----------------- Request for next/prev execution line -----------------------------
                    if ( !_r.diving ) {
                        int32_t l = SIOgetCurrentLineno( _r.sio );

                        if ( ( ( s == SIO_EV_PREV ) && ( !l ) ) || ( ( s == SIO_EV_NEXT ) && ( l >= _r.numLines - 1 ) ) ) {
                            break;
                        }

                        /* In a regular window, scroll back looking for an earlier assembly instruction */
                        do {
                            l += s == SIO_EV_PREV ? -1 : 1;
                        } while ( l && ( l < _r.numLines - 1 ) && ( ( _r.opText[l].lt != LT_ASSEMBLY ) ) );

                        if ( l ) {
                            SIOsetCurrentLineno( _r.sio, l );
                            SIOrequestRefresh( _r.sio );
                        } else {
                            SIObeep();
                        }
                    } else {
                        /* In a diving window, situation is slightly more complicated */
                        int32_t l = _r.lineNum;
                        struct symbolLineStore *oldLine = _fileAndLine( &_r, l );

                        if ( ( ( s == SIO_EV_PREV ) && ( !l ) ) || ( ( s == SIO_EV_NEXT ) && ( l >= _r.numLines - 1 ) ) ) {
                            break;
                        }

                        /* Search for different _source_line_ to the one we started from */
                        do {
                            l += s == SIO_EV_PREV ? -1 : 1;
                        } while ( l && ( l < _r.numLines - 1 ) && ( ( _r.opText[l].lt != LT_SOURCE ) ) );

                        if ( l ) {
                            if ( oldLine->filename == _fileAndLine( &_r, l )->filename ) {
                                /* We are still in the same file, so only the line number to change */
                                _r.lineNum = l;
                                SIOsetCurrentLineno( _r.sio, _fileAndLine( &_r, l )->startline - 1 );
                                SIOrequestRefresh( _r.sio );
                            } else {
                                /* We have changed diving file, surface and enter the new one */
                                _r.lineNum = l;
                                _doFilesurface( &_r );
                                _doFileDive( &_r );
                                SIOrequestRefresh( _r.sio );
                            }
                        } else {
                            SIObeep();
                        }

                    }

                    break;

                case SIO_EV_SAVE: // ------------------ Request for file save -------------------------------------------
                    if ( _r.options->file ) {
                        _doSave( &_r, false );
                    }

                    break;

                case SIO_EV_DIVE: // -------------------- Request for dive into source file -----------------------------
                    _doFileDive( &_r );
                    break;

                case SIO_EV_FOPEN: // ------------------- Request for file open -----------------------------------------
                    if ( _r.options->openFileCL ) {
                        //                        _doFileOpen( &_r, false );
                    }

                    break;

                case SIO_EV_SURFACE: // --------------------- Request for file surface ----------------------------------
                    _doFilesurface( &_r );
                    break;

                case SIO_EV_QUIT: // ------------------------- Request to exit ------------------------------------------
                    _r.ending = true;
                    break;

                default:
                    break;
            }

            /* Deal with possible timeout on sampling, or if this is a read-from-file that is finished */
            if ( ( !_r.numLines )  &&
                 (
                             ( _r.options->file && !stream ) ||
                             ( ( ( genericsTimestampmS() - lastHTime ) > HANG_TIME_MS ) &&
                               ( _r.newTotalBytes - _r.oldTotalHangBytes == 0 ) &&
                               ( _r.wp != _r.rp ) )
                 )
               ) {
                if ( !_dumpBuffer( &_r ) ) {
                    /* Dumping the buffer failed, so give up */
                    _r.ending = true;
                } else {
                    _r.held = true;
                    SIOheld( _r.sio, _r.held );
                }
            }

            /* Update the intervals */
            if ( ( genericsTimestampmS() - lastHTime ) > HANG_TIME_MS ) {
                _r.oldTotalHangBytes = _r.newTotalBytes;
                lastHTime = genericsTimestampmS();
            }

            if ( ( genericsTimestampmS() - lastTTime ) > TICK_TIME_MS ) {
                lastTTime = genericsTimestampmS();
            }

            if ( ( genericsTimestampmS() - lastTSTime ) > INTERVAL_TIME_MS ) {
                _r.oldTotalIntervalBytes = _r.newTotalBytes - _r.oldTotalBytes;
                _r.oldTotalBytes = _r.newTotalBytes;
                lastTSTime = genericsTimestampmS();
            }
        }

        /* ----------------------------------------------------------------------------- */
        /* End of main loop ... we get here because something forced us out              */
        /* ----------------------------------------------------------------------------- */

        if ( stream ) {
            stream->close( stream );
            free( stream );

            stream = NULL;
        }

        if ( _r.options->file ) {
            /* Don't keep re-reading the file if it is a file! */
            _r.held = true;
        }

        if ( _r.options->fileTerminate ) {
            _r.ending = true;
        }
    }

    symbolDelete( _r.s );
    return OK;
}

// ====================================================================================================
