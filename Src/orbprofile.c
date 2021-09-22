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
    struct execEntryHash *h;             /* The exec entry we're currently in (file, function, line, addr etc) */
    struct execEntryHash *inth;          /* Fake exec entry for an interrupt source */
    uint32_t workingAddr;                /* The address we're currently in */
    bool lastWasSubcall;                 /* Was the last instruction an executed subroutine call? */
    bool lastWasReturn;                  /* Was the last instruction an executed return? */
    uint64_t firsttstamp;                /* First timestamp we recorded (that was valid) */
    uint64_t lasttstamp;                 /* Last timestamp we recorded */
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

    /* Subroutine related info */
    struct subcall **substack;                  /* Calls stack data */
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
static void _etmCB( void *d )

/* Callback function for when valid ETM decode is detected */

{
    struct RunTime *r       = ( struct RunTime * )d;
    struct ETMCPUState *cpu = ETMCPUState( &r->i );
    uint32_t incAddr        = 0;
    uint32_t disposition    = 0;
    struct subcall *s;
    struct subcallSig sig;
    struct nameEntry n;

    /* This routine gets called when valid data are available */
    /* if these are the first data, then reset counters etc.  */

    if ( !r->sampling )
    {
        r->op.firsttstamp = cpu->instCount;
        genericsReport( V_INFO, "Sampling" EOL );
        /* Fill in a time to start from */
        r->starttime = genericsTimestampmS();
        r->sampling  = true;

        /* Create false entry for an interrupt source */
        r->op.inth = calloc( 1, sizeof( struct execEntryHash ) );
        r->op.inth->addr          = INTERRUPT;
        r->op.inth->fileindex     = INTERRUPT;
        r->op.inth->line          = NO_LINE;
        r->op.inth->count         = NO_LINE;
        r->op.inth->functionindex = INTERRUPT;
        HASH_ADD_INT( r->insthead, addr, r->op.inth );
    }

    /* Deal with changes introduced by this event ========================= */
    if ( ETMStateChanged( &r->i, EV_CH_ADDRESS ) )
    {
        r->op.workingAddr = cpu->addr;
    }

    if ( ETMStateChanged( &r->i, EV_CH_ENATOMS ) )
    {
        incAddr     = cpu->eatoms + cpu->natoms;
        disposition = cpu->disposition;
    }

    /* If this is going to be an exception or return     */
    /* from one then there's special handling to be done */
    if ( ETMStateChanged( &r->i, EV_CH_EX_ENTRY ) )
    {
        r->op.isException = true;
        r->op.h = r->op.inth;
    }

    if ( ETMStateChanged( &r->i, EV_CH_EX_EXIT ) )
    {
        r->op.isExceptReturn = true;
    }

    /* Action those changes =============================================== */
    while ( incAddr )
    {
        incAddr--;

        struct execEntryHash *h = NULL;

        /* ------------------------------------------------------------------------------------*/
        /* First Stage: Individual address visit accounting.                                   */
        /* Let's find the local hash record for this address, or create it if it doesn't exist */
        /* ------------------------------------------------------------------------------------*/
        HASH_FIND_INT( r->insthead, &r->op.workingAddr, h );

        if ( !h )
        {
            /* We don't have this address captured yet, do it now */
            if ( SymbolLookup( r->s, r->op.workingAddr, &n ) )
            {
                h = calloc( 1, sizeof( struct execEntryHash ) );

                h->addr          = r->op.workingAddr;
                h->fileindex     = n.fileindex;
                h->line          = n.line;
                h->functionindex = n.functionindex;

                if ( n.assyLine == ASSY_NOT_FOUND )
                {
                    genericsExit( -1, "No assembly for function at address %08x, %s" EOL, r->op.workingAddr, SymbolFunction( r->s, n.functionindex ) );
                }

                h->isJump        = n.assy[n.assyLine].isJump;
                h->isSubCall     = n.assy[n.assyLine].isSubCall;
                h->isReturn      = n.assy[n.assyLine].isReturn;
                h->jumpdest      = n.assy[n.assyLine].jumpdest;
                h->is4Byte       = n.assy[n.assyLine].is4Byte;
                h->lineText      = n.assy[n.assyLine].lineText;
                h->count         = h->scount = 0;
            }
            else
            {
                genericsExit( -1, "No symbol for address %08x" EOL, r->op.workingAddr );
            }

            HASH_ADD_INT( r->insthead, addr, h );
        }

        // Uncomment this line to get a trace of instructions...but beware of speed consequences
        // printf( "%c %s", ( disposition & 1 ) ? h->is4Byte ? 'X' : 'x' : '-', h->lineText );

        /* OK, by hook or by crook we've got an address entry now, so increment the number of executions */
        h->count++;

        /* ---------------------------------------------------------------------------------------------------*/
        /* Second stage: Handle calls between functions. These are flagged via isSubCall/isException with the */
        /* End flagged by matching isReturn/isExceptReturn statements.                                        */
        /* ---------------------------------------------------------------------------------------------------*/
        /* If source postion changed then update source code line visitation counts too */
        if ( ( !r->op.h ) || ( h->line != r->op.h->line ) || ( h->functionindex != r->op.h->functionindex ) || ( h->fileindex != r->op.h->fileindex ) )
        {
            h->scount++;
        }

        /* If this is the start of a subroutine call then update, or make, a record for it */
        if ( ( r->op.lastWasSubcall ) || ( r->op.isException ) )
        {
            // sig.src = r->op.isException ? INTERRUPT : r->op.h->addr;
            sig.src = r->op.h->addr;
            sig.dst = h->addr;

            HASH_FIND( hh, r->subhead, &sig, sizeof( struct subcallSig ), s );

            if ( !s )
            {
                /* This entry doesn't exist...let's create it */
                s = ( struct subcall * )calloc( 1, sizeof( struct subcall ) );
                memcpy( &s->sig, &sig, sizeof( struct subcallSig ) );
                s->srch = r->op.h;
                s->dsth = h;
                HASH_ADD( hh, r->subhead, sig, sizeof( struct subcallSig ), s );
            }

            /* If this is an exception, then update the calling routines execution time to now-1 instructions */
            if ( ( r->op.isException ) && ( r->substacklen ) )
            {
                r->substack[r->substacklen - 1]->myCost += cpu->instCount - r->substack[r->substacklen - 1]->inTicks;
            }

            /* However we got here, we've got a subcall record, so initialise its starting ticks */
            s->inTicks = cpu->instCount;
            s->count++;

            /* ...and add it to the call stack */
            r->substack = ( struct subcall ** )realloc( r->substack, ( r->substacklen + 1 ) * sizeof( struct subcall * ) );
            r->substack[r->substacklen++] = s;
            r->op.isException    = false;
        }

        /* If this is an executed return then process it, if we've not emptied the stack already (safety measure) */
        if ( ( r->op.lastWasReturn || r->op.isExceptReturn ) && ( r->substacklen ) )
        {
            /* We don't bother deallocating memory here cos it'll be done on the next isSubCall */
            s = r->substack[--r->substacklen];
            s->myCost += cpu->instCount - s->inTicks;

            /* If this was an interrupt return then re-establish the instruction counter */
            if ( ( r->op.isExceptReturn ) && ( r->substacklen ) )
            {
                r->substack[r->substacklen]->inTicks = cpu->instCount;

                /* Have to update h since we don't want a record of the interrupt */
                r->op.h = r->substack[r->substacklen]->dsth;
            }

            r->op.isExceptReturn = false;
        }

        /* Record details of this instruction */
        r->op.h              = h;
        r->op.lasttstamp     = cpu->instCount;
        r->op.lastWasReturn  = h->isReturn && ( disposition & 1 );
        r->op.lastWasSubcall = h->isSubCall && ( disposition & 1 );

        if ( disposition & 1 )
        {
            if ( h->isSubCall )
            {
                /* Take this call */
                r->op.workingAddr = h->jumpdest;
                disposition >>= 1;
                continue;
            }

            if ( h->isJump )
            {
                /* This is a fixed jump that _was_ taken, so update working address */
                r->op.workingAddr = h->jumpdest;
                disposition >>= 1;
                continue;
            }
        }

        /* If it wasn't a jump or subroutine then increment the address */
        r->op.workingAddr += ( h->is4Byte ) ? 4 : 2;
        disposition >>= 1;
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
            ETMDecoderPump( &r->i, r->rawBlock[r->rp].buffer, r->rawBlock[r->rp].fillLevel, _etmCB, &_r );

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
    }

    return OK;
}

// ====================================================================================================
