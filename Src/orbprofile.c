/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Profiling module for Orbuculum
 * ==============================
 *
 */

#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <semaphore.h>
#include <pthread.h>
#include <assert.h>
#include <getopt.h>

#include "git_version_info.h"
#include "uthash.h"
#include "generics.h"
#include "traceDecoder.h"
#include "symbols.h"
#include "nw.h"
#include "ext_fileformats.h"
#include "stream.h"

#define TICK_TIME_MS        (1)          /* Time intervals for checks */
#define DEFAULT_DURATION_MS (1000)       /* Default time to sample, in mS */
#define HANDLE_MASK         (0xFFFFFF)   /* cachegrind cannot cope with large file handle numbers */

/* How many transfer buffers from the source to allocate */
#define NUM_RAW_BLOCKS (1000)

#define DBG_OUT(...) printf(__VA_ARGS__)
//#define DBG_OUT(...)

struct _subcallAccount
{
    struct subcallSig sig;
    uint64_t inTicks;
};


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

    char *dotfile;                       /* File to output dot information */
    char *profile;                       /* File to output profile information */
    int  sampleDuration;                 /* How long we are going to sample for */

    bool noaltAddr;                      /* Dont use alternate addressing */
    bool useTPIU;                        /* Are we using TPIU, and stripping TPIU frames? */
    int  channel;                        /* When TPIU is in use, which channel to decode? */
    enum TRACEprotocol protocol;         /* Encoding protocol to use */

    int  port;                           /* Source information for where to connect to */
    char *server;

} _options =
{
    .demangle       = true,
    .sampleDuration = DEFAULT_DURATION_MS,
    .port           = NWCLIENT_SERVER_PORT,
    .protocol       = TRACE_PROT_ETM35,
    .server         = "localhost"
};

/* State of routine tracking, maintained across TRACE callbacks to reconstruct program flow */
struct opConstruct
{
    struct execEntryHash *h;             /* The exec entry we were in last (file, function, line, addr etc) */
    struct execEntryHash *oldh;          /* The exec entry we're currently in (file, function, line, addr etc) */
    struct execEntryHash *inth;          /* Fake exec entry for an interrupt source */
    uint32_t workingAddr;                /* The address we're currently in */
    uint32_t nextAddr;                   /* The address we will next be accessing (if known) */
    uint32_t lastfn;                     /* The function of the last instruction carried out */

    uint64_t firsttstamp;                /* First timestamp we recorded (that was valid) */
    uint64_t lasttstamp;                 /* Last timestamp we recorded (that was valid) */

    bool isExceptReturn;                 /* Is this flagged as an exception return? */
    bool isException;                    /* Is this flagged as an exception? */
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
    /* Information about the program */
    const char *progName;                       /* Name by which this program was called */

    /* Subsystem data support */
    struct TRACEDecoder i;
    struct SymbolSet *s;                        /* Symbols read from elf */

    /* Calls related info */
    struct edge *calls;                         /* Call data table */
    struct subcall *subhead;                    /* Calls onstruct data */
    struct execEntryHash *insthead;             /* Exec table handle for hash */

    /* Subroutine related info...the call stack and its length */
    struct _subcallAccount *substack;           /* Calls stack data */
    uint32_t substacklen;                       /* Calls stack length */

    /* Stats about the run */
    int instCount;                              /* Number of instruction locations */
    uint64_t callsCount;                        /* Call data count */
    uint64_t intervalBytes;                     /* Number of bytes transferred in current interval */

    /* State of the target tracker */
    struct opConstruct op;                      /* The mechanical elements for creating the output buffer */

    /* Subprocess control and interworking */
    pthread_t processThread;                    /* Thread handling received data flow */
    sem_t     dataForClients;                   /* Semaphore counting data for clients */

    /* Ring buffer for samples ... this 'pads' the rate data arrive and how fast they can be processed */
    int wp;                                     /* Read and write pointers into transfer buffers */
    int rp;
    struct dataBlock rawBlock[NUM_RAW_BLOCKS];  /* Transfer buffers from the receiver */

    /* State info */
    volatile bool ending;                       /* Flag indicating app is terminating */
    bool     sampling;                          /* Are we actively sampling at the moment */
    uint32_t starttime;                         /* At what time did we start sampling? */

    /* Turn addresses into files and routines tags */
    uint32_t nameCount;
    struct nameEntryHash *name;

    struct Options *options;                    /* Our runtime configuration */

} _r;

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Internally available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
static void _callEvent( struct RunTime *r, uint32_t retAddr, uint32_t to )

/* This is a call or a return, manipulate stack tracking appropriately */

{
    struct TRACECPUState *cpu = TRACECPUState( &r->i );
    struct subcall *s;

    /* ...add it to the call stack */
    r->substack = ( struct _subcallAccount * )realloc( r->substack, ( r->substacklen + 1 ) * sizeof( struct _subcallAccount ) );

    /* This is a call */
    r->substack[r->substacklen].sig.src     = retAddr;
    r->substack[r->substacklen].sig.dst     = to;
    r->substack[r->substacklen].inTicks     = cpu->instCount;

    /* Find a record for this source/dest pair */
    HASH_FIND( hh, r->subhead, &r->substack[r->substacklen].sig, sizeof( struct subcallSig ), s );

    if ( !s )
    {
        /* This call entry doesn't exist (i.e. it's the first time this from/to pair have been seen...let's create it */
        s = ( struct subcall * )calloc( 1, sizeof( struct subcall ) );
        memcpy( &s->sig, &r->substack[r->substacklen].sig, sizeof( struct subcallSig ) );
        HASH_ADD( hh, r->subhead, sig, sizeof( struct subcallSig ), s );
    }

    r->substacklen++;

    for ( uint32_t g = 0; g < r->substacklen; g++ )
    {
        putchar( ' ' );
    }

    DBG_OUT( "INC:%3d %08x -> %08x" EOL, r->substacklen, retAddr, to );
}
// ====================================================================================================
static void _returnEvent( struct RunTime *r, uint32_t to )

/* This is a return, manipulate stack tracking appropriately */

{
    struct TRACECPUState *cpu = TRACECPUState( &r->i );
    struct subcall *s;
    uint32_t orig = r->substacklen;

    /* Cover the startup case that we happen to hit a return before a call */
    if ( !r->substack )
    {
        return;
    }

    /* Check we've got a valid stack entry to match to */
    do
    {
        if ( !r->substacklen )
        {
            DBG_OUT( "OUT OUT OF STACK ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^" EOL );
            break;
        }

        /* The -1th entry was the last written, so see if that is back far enough */
        r->substacklen--;

        for ( uint32_t g = 0; g < r->substacklen + 1; g++ )
        {
            putchar( ' ' );
        }

        DBG_OUT( " DEC:%3d %08x " EOL, r->substacklen + 1, r->substack[r->substacklen].sig.src );
        HASH_FIND( hh, r->subhead, &r->substack[r->substacklen].sig, sizeof( struct subcallSig ), s );
        assert( s );

        /* We don't bother deallocating memory here cos it'll be done the next time we make a call */
        s->myCost += cpu->instCount - r->substack[r->substacklen].inTicks;
        s->count++;
    }
    while ( to != r->substack[r->substacklen].sig.src );

    /* Check function we popped back to matches where we think we should be */
    if ( to != r->substack[r->substacklen].sig.src )
    {
        for ( uint32_t ty = 0; ty < orig; ty++ )
        {
            DBG_OUT( "%d:%08X ", ty, r->substack[ty].sig.src );
        }

        DBG_OUT( "(wanted %08x, got %08x)" EOL, to, r->substack[r->substacklen].sig.src );
    }
}
// ====================================================================================================
static void _hashFindOrCreate( struct RunTime *r, uint32_t addr, struct execEntryHash **h )
{
    struct nameEntry n;

    HASH_FIND_INT( r->insthead, &addr, *h );

    if ( !( *h ) )
    {
        /* We don't have this address captured yet, do it now */
        if ( SymbolLookup( r->s, r->op.workingAddr, &n ) )
        {
            if ( n.assyLine == ASSY_NOT_FOUND )
            {
                genericsExit( -1, "No assembly for function at address %08x, %s" EOL, r->op.workingAddr, SymbolFunction( r->s, n.functionindex ) );
            }

            *h = calloc( 1, sizeof( struct execEntryHash ) );

            ( *h )->addr          = r->op.workingAddr;
            ( *h )->fileindex     = n.fileindex;
            ( *h )->line          = n.line;
            ( *h )->functionindex = n.functionindex;
            ( *h )->isJump        = n.assy[n.assyLine].isJump;
            ( *h )->isSubCall     = n.assy[n.assyLine].isSubCall;
            ( *h )->isReturn      = n.assy[n.assyLine].isReturn;
            ( *h )->jumpdest      = n.assy[n.assyLine].jumpdest;
            ( *h )->is4Byte       = n.assy[n.assyLine].is4Byte;
            ( *h )->codes         = n.assy[n.assyLine].codes;
            ( *h )->assyText      = n.assy[n.assyLine].lineText;
        }
        else
        {
            genericsExit( -1, "No symbol for address %08x" EOL, r->op.workingAddr );
        }

        HASH_ADD_INT( r->insthead, addr, ( *h ) );
    }
}
// ====================================================================================================
static void _handleInstruction( struct RunTime *r, bool actioned )

{
    /* ------------------------------------------------------------------------------------*/
    /* First Stage: Individual address visit accounting.                                   */
    /* Let's find the local hash record for this address, or create it if it doesn't exist */
    /* ------------------------------------------------------------------------------------*/

    r->op.oldh = r->op.h;
    _hashFindOrCreate( r, r->op.workingAddr, &r->op.h );

    //    if ( r->op.oldh ) DBG_OUT( "%4d,%4d %4d %s%s%c %s", r->op.h->functionindex, ( r->op.h ? r->op.oldh->functionindex : 0 ),  r->op.h->fileindex,
    //                               ( r->op.h->isJump ) ? "J" : " ", ( r->op.h->isSubCall ) ? "S" : r->op.h->isReturn ? "R" : " ", ( actioned ) ? ( r->op.h->is4Byte ? 'X' : 'x' ) : '-',
    //                               r->op.h->assyText );

    /* OK, by hook or by crook we've got an address entry now, so increment the number of executions */
    r->op.h->count++;

    /* If source postion changed then update source code line visitation counts too */
    if ( ( r->op.oldh ) && ( ( r->op.h->line != r->op.oldh->line ) || ( r->op.h->functionindex != r->op.oldh->functionindex ) ) )
    {
        r->op.h->scount++;
    }

    /* If this is a computable destination then action it */
    if ( ( actioned ) && ( ( r->op.h->isJump ) || ( r->op.h->isSubCall ) ) )
    {
        /* Take this call ... note that the jumpdest may not be known at this point */
        r->op.workingAddr = r->op.h->jumpdest;
    }
    else
    {
        /* If it wasn't a jump or subroutine then increment the address */
        r->op.workingAddr += ( r->op.h->is4Byte ) ? 4 : 2;
    }
}

// ====================================================================================================
static void _checkJumps( struct RunTime *r )

{
    if ( r->op.h )
    {

        if ( ( TRACEStateChanged( &r->i, EV_CH_EX_EXIT ) ) || ( r->op.h->isReturn ) )
        {
            _returnEvent( r, r->op.workingAddr );
        }

        if ( r->op.h->isSubCall )
        {
            _callEvent( r, r->op.h->addr + ( ( r->op.h->is4Byte ) ? 4 : 2 ), r->op.workingAddr );
        }
    }
}
// ====================================================================================================
static void _traceCB( void *d )

/* Callback function for when valid TRACE decode is detected */

{
    struct RunTime *r       = ( struct RunTime * )d;
    struct TRACECPUState *cpu = TRACECPUState( &r->i );
    static uint32_t incAddr        = 0;
    static uint32_t disposition    = 0;

    /* This routine gets called when valid data are available */
    /* if these are the first data, then reset counters etc.  */
    if ( !r->sampling )
    {
        r->op.firsttstamp = cpu->instCount;
        genericsReport( V_INFO, "Sampling" EOL );
        /* Fill in a time to start from */
        r->starttime = genericsTimestampmS();

        if ( TRACEStateChanged( &r->i, EV_CH_ADDRESS ) )
        {
            r->op.workingAddr = cpu->addr;
            printf( "Got initial address %08x" EOL, r->op.workingAddr );
            r->sampling  = true;
        }

        /* Create false entry for an interrupt source */
        r->op.inth = calloc( 1, sizeof( struct execEntryHash ) );
        r->op.inth->addr          = INTERRUPT;
        r->op.inth->fileindex     = INTERRUPT;
        r->op.inth->line          = NO_LINE;
        r->op.inth->count         = NO_LINE;
        r->op.inth->functionindex = INTERRUPT;
        HASH_ADD_INT( r->insthead, addr, r->op.inth );
    }

    r->op.lasttstamp = cpu->instCount;

    /* Pull changes introduced by this event ============================== */

    if ( TRACEStateChanged( &r->i, EV_CH_ENATOMS ) )
    {
        /* We are going to execute some instructions. Check if the last of the old batch of    */
        /* instructions was cancelled and, if it wasn't and it's still outstanding, action it. */
        if ( TRACEStateChanged( &r->i, EV_CH_CANCELLED ) )
        {
            printf( "CANCELLED" EOL );
        }
        else
        {
            if ( incAddr )
            {
                printf( "***" EOL );
                _handleInstruction( r, disposition & 1 );

                if ( ( r->op.h->isJump ) || ( r->op.h->isSubCall ) || ( r->op.h->isReturn ) )
                {
                    if ( TRACEStateChanged( &r->i, EV_CH_ADDRESS ) )
                    {
                        printf( "New addr %08x" EOL, cpu->addr );
                        r->op.workingAddr = cpu->addr;
                    }

                    _checkJumps( r );
                }
            }
        }

        if ( TRACEStateChanged( &r->i, EV_CH_ADDRESS ) )
        {
            if ( TRACEStateChanged( &r->i, EV_CH_EX_ENTRY ) )
            {
                printf( "INTERRUPT!!" EOL );
                _callEvent( r, r->op.workingAddr, cpu->addr );
            }

            r->op.workingAddr = cpu->addr;
            printf( "A:%08x" EOL, cpu->addr );
        }

        /* ================================================ */
        /* OK, now collect the next iterations worth of fun */
        /* ================================================ */
        incAddr     = cpu->eatoms + cpu->natoms;
        disposition = cpu->disposition;
        printf( "E:%d N:%d" EOL, cpu->eatoms, cpu->natoms );

        /* Action those changes, except the last one */
        while ( incAddr > 1 )
        {
            incAddr--;
            _handleInstruction( r, disposition & 1 );
            _checkJumps( r );
            disposition >>= 1;
        }
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
static void _printHelp( const char *const progName )

{
    genericsPrintf( "Usage: %s [options]" EOL, progName );
    genericsPrintf( "    -A, --alt-addr-enc: Switch off alternate address decoding (on by default)" EOL );
    genericsPrintf( "    -D, --no-demangle:  Switch off C++ symbol demangling" EOL );
    genericsPrintf( "    -d, --del-prefix:   <String> Material to delete off front of filenames" EOL );
    genericsPrintf( "    -E, --elf-file:     <ElfFile> to use for symbols" EOL );
    genericsPrintf( "    -e, --eof:          When reading from file, terminate at end of file rather than waiting for further input" EOL );
    genericsPrintf( "    -f, --input-file:   Take input from specified file" EOL );
    genericsPrintf( "    -h, --help:         This help" EOL );
    genericsPrintf( "    -I, --interval:     <Interval> Time between samples (in ms)" EOL );
    genericsPrintf( "    -O, --objdump-opts: <options> Options to pass directly to objdump" EOL );
    genericsPrintf( "    -p, --trace-proto:  {ETM35|MTB} trace protocol to use, default is ETM35" EOL );
    genericsPrintf( "    -s, --server:       <Server>:<Port> to use" EOL );
    //genericsPrintf( "    -t, --tpiu:         <channel>: Use TPIU to strip TPIU on specfied channel (defaults to 2)" EOL );
    genericsPrintf( "    -T, --all-truncate: truncate -d material off all references (i.e. make output relative)" EOL );
    genericsPrintf( "    -v, --verbose:      <level> Verbose mode 0(errors)..3(debug)" EOL );
    genericsPrintf( "    -V, --version:      Print version and exit" EOL );
    genericsPrintf( "    -y, --graph-file:   <Filename> dotty filename for structured callgraph output" EOL );
    genericsPrintf( "    -z, --cache-file:   <Filename> profile filename for kcachegrind output" EOL );
    genericsPrintf( EOL "(Will connect one port higher than that set in -s when TPIU is not used)" EOL );
}
// ====================================================================================================
void _printVersion( void )

{
    genericsPrintf( "orbprofile version " GIT_DESCRIBE EOL );
}
// ====================================================================================================
struct option longOptions[] =
{
    {"alt-addr-enc", no_argument, NULL, 'A'},
    {"no-demangle", required_argument, NULL, 'D'},
    {"del-prefix", required_argument, NULL, 'd'},
    {"elf-file", required_argument, NULL, 'E'},
    {"eof", no_argument, NULL, 'e'},
    {"input-file", required_argument, NULL, 'f'},
    {"help", no_argument, NULL, 'h'},
    {"interval", required_argument, NULL, 'I'},
    {"objdump-opts", required_argument, NULL, 'O'},
    {"trace-proto", required_argument, NULL, 'p'},
    {"server", required_argument, NULL, 's'},
    {"all-truncate", no_argument, NULL, 'T'},
    {"verbose", required_argument, NULL, 'v'},
    {"version", no_argument, NULL, 'V'},
    {"graph-file", required_argument, NULL, 'y'},
    {"cache-file", required_argument, NULL, 'z'},
    {NULL, no_argument, NULL, 0}
};
// ====================================================================================================
static bool _processOptions( int argc, char *argv[], struct RunTime *r )

{
    int c, optionIndex = 0;

    while ( ( c = getopt_long ( argc, argv, "ADd:eE:f:hVI:O:p:s:Tv:y:z:", longOptions, &optionIndex ) ) != -1 )

        switch ( c )
        {
            // ------------------------------------
            case 'A':
                r->options->noaltAddr = true;
                break;

            // ------------------------------------
            case 'd':
                r->options->deleteMaterial = optarg;
                break;

            // ------------------------------------
            case 'D':
                r->options->demangle = false;
                break;

            // ------------------------------------
            case 'e':
                r->options->fileTerminate = true;
                break;

            // ------------------------------------
            case 'E':
                r->options->elffile = optarg;
                break;

            // ------------------------------------
            case 'f':
                r->options->file = optarg;
                break;

            // ------------------------------------
            case 'h':
                _printHelp( r->progName );
                exit( 0 );

            // ------------------------------------
            case 'V':
                _printVersion();
                return false;

            // ------------------------------------
            case 'I':
                r->options->sampleDuration = atoi( optarg );
                break;

            // ------------------------------------

            case 'O':
                r->options->odoptions = optarg;
                break;

            // ------------------------------------

            case 'p':

                /* Index through protocol strings looking for match or end of list */
                for ( r->options->protocol = TRACE_PROT_LIST_START;
                        ( ( r->options->protocol != TRACE_PROT_LIST_END ) && strcasecmp( optarg, TRACEprotocolString[r->options->protocol] ) );
                        r->options->protocol++ )
                {}

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
        genericsExit( -2, "Elf File not specified" EOL );
    }

    if ( !r->options->sampleDuration )
    {
        genericsExit( -2, "Illegal sample duration" EOL );
    }

    if ( r->options->protocol >= TRACE_PROT_NONE )
    {
        genericsExit( V_ERROR, "Unrecognised decode protocol" EOL );
    }


    genericsReport( V_INFO, "%s V" VERSION " (Git %08X %s, Built " BUILD_DATE ")" EOL, r->progName, GIT_HASH, ( GIT_DIRTY ? "Dirty" : "Clean" ) );
    genericsReport( V_INFO, "Server          : %s:%d" EOL, r->options->server, r->options->port );
    genericsReport( V_INFO, "Delete Material : %s" EOL, r->options->deleteMaterial ? r->options->deleteMaterial : "None" );
    genericsReport( V_INFO, "Elf File        : %s (%s Names)" EOL, r->options->elffile, r->options->truncateDeleteMaterial ? "Truncate" : "Don't Truncate" );
    genericsReport( V_INFO, "Objdump options : %s" EOL, r->options->odoptions ? r->options->odoptions : "None" );
    genericsReport( V_INFO, "Protocol        : %s" EOL, TRACEprotocolString[r->options->protocol] );
    genericsReport( V_INFO, "DOT file        : %s" EOL, r->options->dotfile ? r->options->dotfile : "None" );
    genericsReport( V_INFO, "Sample Duration : %d mS" EOL, r->options->sampleDuration );

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
static void *_processBlocks( void *params )

/* Generic block processor for received data. This runs in a task parallel to the receiver and *
 * processes all of the data that arrive.                                                      */

{
    struct RunTime *r = ( struct RunTime * )params;

    while ( true )
    {
        sem_wait( &r->dataForClients );

        if ( r->rp != ( volatile int )r->wp )
        {
            genericsReport( V_DEBUG, "RXED Packet of %d bytes" EOL, r->rawBlock[r->rp].fillLevel );

            /* Check to see if we've finished (a zero length packet */
            if ( !r->rawBlock[r->rp].fillLevel )
            {
                break;
            }

#ifdef DUMP_BLOCK
            uint8_t *c = r->rawBlock[r->rp].buffer;
            uint32_t y = r->rawBlock[r->rp].fillLevel;

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
            /* Pump all of the data through the protocol handler */
            TRACEDecoderPump( &r->i, r->rawBlock[r->rp].buffer, r->rawBlock[r->rp].fillLevel, _traceCB, genericsReport, &_r );

            r->rp = ( r->rp + 1 ) % NUM_RAW_BLOCKS;
        }
    }

    return NULL;
}
// ====================================================================================================
int main( int argc, char *argv[] )

{
    struct timeval tv;
    struct Stream *stream = NULL;
    enum symbolErr r;

    /* Have a basic name and search string set up */
    _r.progName = genericsBasename( argv[0] );
    _r.options = &_options;

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

#if !defined(WIN32)

    /* Don't kill a sub-process when any reader or writer evaporates */
    if ( SIG_ERR == signal( SIGPIPE, SIG_IGN ) )
    {
        genericsExit( -1, "Failed to ignore SIGPIPEs" EOL );
    }

#endif

    TRACEDecoderInit( &_r.i, _r.i.protocol, !_r.options->noaltAddr );

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


        /* We need symbols constantly while running ... lets get them */
        if ( !SymbolSetValid( &_r.s, _r.options->elffile ) )
        {
            r = SymbolSetCreate( &_r.s, _r.options->elffile, _r.options->deleteMaterial, _r.options->demangle, true, true, _r.options->odoptions );

            switch ( r )
            {
                case SYMBOL_NOELF:
                    genericsExit( -1, "Elf file or symbols in it not found" EOL );
                    break;

                case SYMBOL_NOOBJDUMP:
                    genericsExit( -1, "No objdump found" EOL );
                    break;

                case SYMBOL_UNSPECIFIED:
                    genericsExit( -1, "Unknown error in symbol subsystem" EOL );
                    break;

                default:
                    break;
            }

            genericsReport( V_WARN, "Loaded %s" EOL, _r.options->elffile );
        }

        _r.intervalBytes = 0;

        /* Now start the result processing task */
        pthread_create( &_r.processThread, NULL, &_processBlocks, &_r );

        /* ----------------------------------------------------------------------------- */
        /* This is the main active loop...only break out of this when ending or on error */
        /* ----------------------------------------------------------------------------- */

        while ( !_r.ending )
        {
            /* Each time segment is restricted */
            tv.tv_sec = 0;
            tv.tv_usec  = TICK_TIME_MS * 1000;


            struct dataBlock *rxBlock = &_r.rawBlock[_r.wp];

            enum ReceiveResult result = stream->receive( stream, rxBlock->buffer, TRANSFER_SIZE, &tv, ( size_t * )&rxBlock->fillLevel );

            if ( ( result == RECEIVE_RESULT_EOF ) || ( result == RECEIVE_RESULT_ERROR ) )
            {
                break;
            }

            if ( rxBlock->fillLevel <= 0 )
            {
                /* We are at EOF (Probably the descriptor closed) */
                break;
            }

            /* ...record the fact that we received some data */
            _r.intervalBytes += rxBlock->fillLevel;

            int nwp = ( _r.wp + 1 ) % NUM_RAW_BLOCKS;

            if ( nwp == ( volatile int )_r.rp )
            {
                genericsExit( -1, "Overflow" EOL );
            }

            _r.wp = nwp;
            sem_post( &_r.dataForClients );

            /* Update the intervals */
            if ( ( ( volatile bool ) _r.sampling ) && ( ( genericsTimestampmS() - ( volatile uint32_t )_r.starttime ) > _r.options->sampleDuration ) )
            {
                _r.ending = true;

                /* Post an empty data packet to flag to packet processor that it's done */
                int nwp = ( _r.wp + 1 ) % NUM_RAW_BLOCKS;

                if ( nwp == ( volatile int )_r.rp )
                {
                    genericsExit( -1, "Overflow" EOL );
                }

                _r.rawBlock[_r.wp].fillLevel = 0;
                _r.wp = nwp;
                sem_post( &_r.dataForClients );
            }
        }

        stream->close( stream );
        free( stream );
    }

    /* Wait for data processing to be completed */
    pthread_join( _r.processThread, NULL );

    /* Data are collected, now process and report */
    genericsReport( V_INFO, "Received %d raw sample bytes, %ld function changes, %ld distinct addresses" EOL,
                    _r.intervalBytes, HASH_COUNT( _r.subhead ), HASH_COUNT( _r.insthead ) );

    if ( HASH_COUNT( _r.subhead ) )
    {
        if ( ext_ff_outputDot( _r.options->dotfile, _r.subhead, _r.s ) )
        {
            genericsReport( V_INFO, "Output DOT" EOL );
        }
        else
        {
            if ( _r.options->dotfile )
            {
                genericsExit( -1, "Failed to output DOT" EOL );
            }
        }

        if ( ext_ff_outputProfile( _r.options->profile, _r.options->elffile,
                                   _r.options->truncateDeleteMaterial ? _r.options->deleteMaterial : NULL,
                                   true,
                                   _r.op.lasttstamp - _r.op.firsttstamp,
                                   _r.insthead,
                                   _r.subhead,
                                   _r.s ) )
        {
            genericsReport( V_INFO, "Output Profile" EOL );
        }
        else
        {
            if ( _r.options->profile )
            {
                genericsExit( -1, "Failed to output profile" EOL );
            }
        }
    }

    return OK;
}

// ====================================================================================================
