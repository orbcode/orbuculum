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
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <semaphore.h>
#include <pthread.h>
#include <assert.h>

#include "git_version_info.h"
#include "uthash.h"
#include "generics.h"
#include "etmDecoder.h"
#include "symbols.h"
#include "nw.h"
#include "ext_fileformats.h"

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

    char *dotfile;                       /* File to output dot information */
    char *profile;                       /* File to output profile information */
    int  sampleDuration;                 /* How long we are going to sample for */

    bool noaltAddr;                      /* Dont use alternate addressing */
    bool useTPIU;                        /* Are we using TPIU, and stripping TPIU frames? */
    int  channel;                        /* When TPIU is in use, which channel to decode? */

    int  port;                           /* Source information for where to connect to */
    char *server;

} _options =
{
    .demangle       = true,
    .sampleDuration = DEFAULT_DURATION_MS,
    .port           = NWCLIENT_SERVER_PORT,
    .server         = "localhost"
};

/* State of routine tracking, maintained across ETM callbacks to reconstruct program flow */
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
    struct ETMDecoder i;
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
static void _callEvent( struct RunTime *r, uint32_t retAddr, uint32_t to )

/* This is a call or a return, manipulate stack tracking appropriately */

{
    struct ETMCPUState *cpu = ETMCPUState( &r->i );
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
    struct ETMCPUState *cpu = ETMCPUState( &r->i );
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

        if ( ( ETMStateChanged( &r->i, EV_CH_EX_EXIT ) ) || ( r->op.h->isReturn ) )
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
static void _etmCB( void *d )

/* Callback function for when valid ETM decode is detected */

{
    struct RunTime *r       = ( struct RunTime * )d;
    struct ETMCPUState *cpu = ETMCPUState( &r->i );
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

        if ( ETMStateChanged( &r->i, EV_CH_ADDRESS ) )
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

    if ( ETMStateChanged( &r->i, EV_CH_ENATOMS ) )
    {
        /* We are going to execute some instructions. Check if the last of the old batch of    */
        /* instructions was cancelled and, if it wasn't and it's still outstanding, action it. */
        if ( ETMStateChanged( &r->i, EV_CH_CANCELLED ) )
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
                    if ( ETMStateChanged( &r->i, EV_CH_ADDRESS ) )
                    {
                        printf( "New addr %08x" EOL, cpu->addr );
                        r->op.workingAddr = cpu->addr;
                    }

                    _checkJumps( r );
                }
            }
        }

        if ( ETMStateChanged( &r->i, EV_CH_ADDRESS ) )
        {
            if ( ETMStateChanged( &r->i, EV_CH_EX_ENTRY ) )
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
static void _printHelp( struct RunTime *r )

{
    genericsPrintf( "Usage: %s [options]" EOL, r->progName );
    genericsPrintf( "       -a: Switch off alternate address decoding (on by default)" EOL );
    genericsPrintf( "       -D: Switch off C++ symbol demangling" EOL );
    genericsPrintf( "       -d: <String> Material to delete off front of filenames" EOL );
    genericsPrintf( "       -E: When reading from file, terminate at end of file rather than waiting for further input" EOL );
    genericsPrintf( "       -e: <ElfFile> to use for symbols" EOL );
    genericsPrintf( "       -f <filename>: Take input from specified file" EOL );
    genericsPrintf( "       -h: This help" EOL );
    genericsPrintf( "       -I <Interval>: Time to sample (in mS)" EOL );
    genericsPrintf( "       -s: <Server>:<Port> to use" EOL );
    //genericsPrintf( "       -t <channel>: Use TPIU to strip TPIU on specfied channel (defaults to 2)" EOL );
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

    while ( ( c = getopt ( argc, argv, "aDd:Ee:f:hI:s:Tv:y:z:" ) ) != -1 )

        switch ( c )
        {
            // ------------------------------------
            case 'a':
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
                _printHelp( r );
                exit( 0 );

            // ------------------------------------
            case 'I':
                r->options->sampleDuration = atoi( optarg );
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

    genericsReport( V_INFO, "%s V" VERSION " (Git %08X %s, Built " BUILD_DATE ")" EOL, r->progName, GIT_HASH, ( GIT_DIRTY ? "Dirty" : "Clean" ) );
    genericsReport( V_INFO, "Server          : %s:%d" EOL, r->options->server, r->options->port );
    genericsReport( V_INFO, "Delete Material : %s" EOL, r->options->deleteMaterial ? r->options->deleteMaterial : "None" );
    genericsReport( V_INFO, "Elf File        : %s (%s Names)" EOL, r->options->elffile, r->options->truncateDeleteMaterial ? "Truncate" : "Don't Truncate" );
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
            ETMDecoderPump( &r->i, r->rawBlock[r->rp].buffer, r->rawBlock[r->rp].fillLevel, _etmCB, genericsReport, &_r );

            r->rp = ( r->rp + 1 ) % NUM_RAW_BLOCKS;
        }
    }

    return NULL;
}
// ====================================================================================================
int main( int argc, char *argv[] )

{
    int sourcefd;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    int flag = 1;

    int r;
    struct timeval tv;
    fd_set readfds;

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

    ETMDecoderInit( &_r.i, !_r.options->noaltAddr );

    while ( !_r.ending )
    {
        if ( !_r.options->file )
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
            server = gethostbyname( _r.options->server );

            if ( !server )
            {
                perror( "Cannot find host" );
                return -EIO;
            }

            serv_addr.sin_family = AF_INET;
            bcopy( ( char * )server->h_addr,
                   ( char * )&serv_addr.sin_addr.s_addr,
                   server->h_length );
            serv_addr.sin_port = htons( _r.options->port + ( _r.options->useTPIU ? 0 : 1 ) );

            if ( connect( sourcefd, ( struct sockaddr * ) &serv_addr, sizeof( serv_addr ) ) < 0 )
            {
                perror( "Could not connect" );
                close( sourcefd );
                usleep( 1000000 );
                continue;
            }
        }
        else
        {
            if ( ( sourcefd = open( _r.options->file, O_RDONLY ) ) < 0 )
            {
                genericsExit( sourcefd, "Can't open file %s" EOL, _r.options->file );
            }
        }

        /* We need symbols constantly while running ... lets get them */
        if ( !SymbolSetValid( &_r.s, _r.options->elffile ) )
        {
            if ( !( _r.s = SymbolSetCreate( _r.options->elffile, _r.options->deleteMaterial, _r.options->demangle, true, true ) ) )
            {
                genericsExit( -1, "Elf file or symbols in it not found" EOL );
            }
            else
            {
                genericsReport( V_DEBUG, "Loaded %s" EOL, _r.options->elffile );
            }
        }

        _r.intervalBytes = 0;

        /* Now start the result processing task */
        pthread_create( &_r.processThread, NULL, &_processBlocks, &_r );

        /* ----------------------------------------------------------------------------- */
        /* This is the main active loop...only break out of this when ending or on error */
        /* ----------------------------------------------------------------------------- */
        FD_ZERO( &readfds );

        while ( !_r.ending )
        {
            /* Each time segment is restricted */
            tv.tv_sec = 0;
            tv.tv_usec  = TICK_TIME_MS * 1000;

            FD_SET( sourcefd, &readfds );
            FD_SET( STDIN_FILENO, &readfds );
            r = select( sourcefd + 1, &readfds, NULL, NULL, &tv );

            if ( r < 0 )
            {
                /* Something went wrong in the select */
                break;
            }

            struct dataBlock *rxBlock = &_r.rawBlock[_r.wp];

            if ( FD_ISSET( sourcefd, &readfds ) )
            {
                /* We always read the data, even if we're held, to keep the socket alive */
                rxBlock->fillLevel = read( sourcefd, rxBlock->buffer, TRANSFER_SIZE );

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
            }

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

        close( sourcefd );
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
