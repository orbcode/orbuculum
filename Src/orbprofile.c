/*
 * Profiling module for orb suite
 * ==============================
 *
 * Copyright (C) 2017, 2019, 2021  Dave Marples  <dave@marples.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * * Neither the names Orbtrace, Orbuculum nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>

#if defined OSX
    #include <libusb.h>
#else
    #if defined LINUX
        #include <libusb-1.0/libusb.h>
    #else
        #error "Unknown OS"
    #endif
#endif
#include <stdint.h>
#include <limits.h>
#include <termios.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <assert.h>
#include <netdb.h>
#include <inttypes.h>

#include "git_version_info.h"
#include "uthash.h"
#include "generics.h"
#include "etmDecoder.h"
#include "symbols.h"
#include "nw.h"

#define SCRATCH_STRING_LEN  (65535)      /* Max length for a string under construction */
#define TICK_TIME_MS        (1)          /* Time intervals for checks */
#define ADDR_INTERRUPT      (0xFFFFFFFA) /* False address indicating an interrupt source */
#define DEFAULT_DURATION_MS (1000)       /* Default time to sample, in mS */

/* An entry in the names table */
struct nameEntryHash
{
    struct nameEntry *n;
    bool seen;
    uint32_t index;
    UT_hash_handle hh;
};

struct execEntryHash
{
    /* The address is the memory map */
    uint32_t addr;

    /* Counter at assembly and source line levels */
    uint64_t count;
    uint64_t scount;

    /* Details about this instruction */
    bool isJump;
    bool is4Byte;
    bool isSubCall;
    bool isReturn;
    uint32_t jumpdest;

    /* Location of this line in source code */
    uint32_t fileindex;
    uint32_t functionindex;
    uint32_t line;
    UT_hash_handle hh;
};


/* A calling edge */
struct edge
{
    uint32_t src;
    uint32_t dst;
    uint64_t spentcycles;
    bool in;
};

/* Signature for a source/dest calling pair */
struct subcallSig
{
    uint32_t srcfileindex;
    uint32_t srcfunctionindex;
    uint32_t srcline;
    uint32_t dstfileindex;
    uint32_t dstfunctionindex;
    uint32_t dstline;
};

/* Processed subcalls from routine to routine */
struct subcall
{
    struct subcallSig sig;

    uint32_t src;
    uint32_t dst;
    uint32_t srcline;
    uint32_t dstline;

    uint64_t myCost;
    uint64_t count;
    uint64_t inTicks;

    UT_hash_handle hh;
};

/* States for sample reception state machine */
enum CDState { CD_waitinout, CD_waitsrc, CD_waitdst };

/* ---------- CONFIGURATION ----------------- */
struct Options                               /* Record for options, either defaults or from command line */
{
    bool demangle;                           /* Demangle C++ names */
    char *file;                         /* File host connection */
    bool fileTerminate;                 /* Terminate when file read isn't successful */

    char *deleteMaterial;                    /* Material to strip off front of filenames for target */

    char *elffile;                           /* Target program config */

    char *dotfile;                           /* File to output dot information */
    char *profile;                           /* File to output profile information */
    uint32_t sampleDuration;            /* How long we are going to sample for */

    bool altAddr;                       /* Should alternate addressing be used? */
    bool useTPIU;                       /* Are we using TPIU, and stripping TPIU frames? */
    int channel;                        /* When TPIU is in use, which channel to decode? */

    int port;                                /* Source information for where to connect to */
    char *server;

} _options =
{
    .demangle = true,
    .sampleDuration = DEFAULT_DURATION_MS,
    .port = NWCLIENT_SERVER_PORT,
    .server = "localhost"
};

/* State of routine tracking, maintained across ETM callbacks to reconstruct program flow */
struct opConstruct
{
    struct execEntryHash *h;             /* The exec entry we're currently in (file, function, line, addr etc) */
    uint32_t workingAddr;                /* The address we're currently in */
    bool lastWasSubcall;                 /* Was the last instruction an executed subroutine call? */
    bool lastWasReturn;                  /* Was the last instruction an executed return? */
    uint64_t firsttstamp;                /* First timestamp we recorded (that was valid) */
    uint64_t lasttstamp;                 /* Last timestamp we recorded */
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
    struct ETMDecoder i;

    const char *progName;               /* Name by which this program was called */
    bool      ending;                   /* Flag indicating app is terminating */
    uint64_t intervalBytes;             /* Number of bytes transferred in current interval */

    /* Calls related info */
    enum CDState CDState;               /* State of the call data machine */
    struct edge callsConstruct;         /* Call data entry under construction */
    struct edge *calls;                 /* Call data table */

    struct subcall *subhead;            /* Calls onstruct data */
    struct subcall **substack;          /* Calls stack data */
    uint32_t substacklen;               /* Calls stack length */

    struct execEntryHash *insthead;     /* Exec table handle for hash */

    int instCount;                      /* Number of instruction locations */

    uint64_t callsCount;                   /* Call data count */

    struct SymbolSet *s;                /* Symbols read from elf */
    FILE *c;                            /* Writable file */

    struct opConstruct op;              /* The mechanical elements for creating the output buffer */
    struct Options *options;            /* Our runtime configuration */

    struct dataBlock rawBlock;          /* Datablock received from distribution */

    bool sampling;                      /* Are we actively sampling at the moment */
    uint32_t starttime;                 /* At what time did we start sampling? */

    /* Turn addresses into files and routines tags */
    uint32_t nameCount;
    struct nameEntryHash *name;
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

// ====================================================================================================
// ====================================================================================================
// Dot support
// ====================================================================================================
// ====================================================================================================
static int _calls_sort_src_fn( const void *a_v, const void *b_v )

/* Sort addresses first by src, then by dst */

{
    struct edge *a = ( struct edge * )a_v;
    struct edge *b = ( struct edge * )b_v;
    int32_t c;

    c = ( int32_t )a->src - ( int32_t )b->src;

    if ( !c )
    {
        c = ( int32_t )a->dst - ( int32_t )b->dst;
    }

    return c;
}
// ====================================================================================================
static int _calls_sort_dest_fn( const void *a_v, const void *b_v )

/* Sort addresses first by dst, then by src */

{
    struct edge *a = ( struct edge * )a_v;
    struct edge *b = ( struct edge * )b_v;
    int32_t c;

    c = ( int32_t )a->dst - ( int32_t )b->dst;

    if ( !c )
    {
        c = ( int32_t )a->src - ( int32_t )b->src;
    }

    return c;
}
// ====================================================================================================
bool _outputDot( struct RunTime *r )

/* Output call graph to dot file */

{
#if 0
    FILE *c;

    if ( !r->options->dotfile )
    {
        return false;
    }

    /* Sort according to addresses visited. */
    qsort( r->calls, r->callsCount, sizeof( struct edge ), _calls_sort_dest_fn );

    c = fopen( r->options->dotfile, "w" );
    fprintf( c, "digraph calls\n{\n  overlap=false; splines=true; size=\"7.75,10.25\"; orientation=portrait; sep=0.1; nodesep=0.1;\n" );

    /* firstly write out the nodes in each subgraph - dest side clustered */
    for ( uint32_t x = 1; x < r->callsCount; x++ )
    {
        fprintf( c, "  subgraph \"cluster_%s\"\n  {\n    label=\"%s\";\n    bgcolor=lightgrey;\n", r->calls[x - 1].dstFile, r->calls[x - 1].dstFile );

        while ( x < r->callsCount )
        {
            /* Now output each function in the subgraph */
            fprintf( c, "    %s [style=filled, fillcolor=white];\n", r->calls[x - 1].dstFn );

            /* Spin forwards until the function name _or_ filename changes */
            while ( ( x < r->callsCount ) && ( r->calls[x - 1].dstFn == r->calls[x].dstFn ) )
            {
                x++;
            }

            if ( ( x >= r->callsCount ) || ( r->calls[x - 1].dstFile != r->calls[x].dstFile ) )
            {
                break;
            }

            x++;
        }

        fprintf( c, "  }\n\n" );
    }

    printf( "Sort completed" EOL );
    /* now write out the nodes in each subgraph - source side clustered */
    qsort( r->calls, r->callsCount, sizeof( struct edge ), _calls_sort_src_fn );

    for ( uint32_t x = 1; x < r->callsCount; x++ )
    {
        fprintf( c, "  subgraph \"cluster_%s\"\n  {\n    label=\"%s\";\n    bgcolor=lightgrey;\n", r->calls[x - 1].srcFile, r->calls[x - 1].srcFile );

        while ( x < r->callsCount )
        {
            /* Now output each function in the subgraph */
            fprintf( c, "    %s [style=filled, fillcolor=white];\n", r->calls[x - 1].srcFn );

            /* Spin forwards until the function name _or_ filename changes */
            while ( ( x < r->callsCount ) && ( r->calls[x - 1].srcFn == r->calls[x].srcFn ) )
            {
                x++;
            }

            if ( ( x >= r->callsCount ) || ( r->calls[x - 1].srcFile != r->calls[x].srcFile ) )
            {
                break;
            }

            x++;
        }

        fprintf( c, "  }\n\n" );
    }

    /* Now go through and label the arrows... */
    for ( uint32_t x = 0; x < r->callsCount; x++ )
    {
        int cnt = 0;

        while ( ( x < r->callsCount - 1 ) && ( r->calls[x].srcFn == r->calls[x + 1].srcFn ) && ( r->calls[x].dstFn == r->calls[x + 1].dstFn ) )
        {
            cnt++;
            x++;
        }

        fprintf( c, "    %s -> ", r->calls[x].srcFn );
        fprintf( c, "%s [label=%d , weight=0.1;];\n", r->calls[x].dstFn, cnt );
    }

    fprintf( c, "}\n" );
    fclose( c );
#endif
    return true;
}
// ====================================================================================================
// ====================================================================================================
// KCacheGrind support
// ====================================================================================================
// ====================================================================================================
static int _inst_sort_fn( const void *a, const void *b )

/* Sort instructions by address */

{
    return ( int )( ( ( struct execEntryHash * )a )->addr ) - ( int )( ( ( struct execEntryHash * )b )->addr );
}
// ====================================================================================================
bool _outputProfile( struct RunTime *r )

/* Output a KCacheGrind compatible profile */

{
    struct nameEntry n;
    uint32_t prevfile = -1;
    uint32_t prevfn = -1;
    uint32_t prevaddr = -1;
    uint32_t prevline = -1;

    if ( !r->options->profile )
    {
        return false;
    }


    r->c = fopen( r->options->profile, "w" );
    fprintf( r->c, "# callgrind format\n" );
    fprintf( r->c, "creator: orbprofile\npositions: instr line visits\nevent: Inst : CPU Instructions\nevent: Visits : Visits to source line\nevents: Inst Visits\n" );
    /* Samples are in time order, so we can determine the extent of time.... */
    fprintf( r->c, "summary: %" PRIu64 "\n", r->op.lasttstamp - r->op.firsttstamp );
    fprintf( r->c, "ob=%s\n", r->options->elffile );

    HASH_SORT( r->insthead, _inst_sort_fn );
    struct execEntryHash *f = r->insthead;

    while ( f )
    {
        SymbolLookup( r->s, f->addr, &n, r->options->deleteMaterial );

        if ( prevfile != n.fileindex )
        {
            fprintf( r->c, "fl=(%d) %s%s\n", n.fileindex, r->options->deleteMaterial ? r->options->deleteMaterial : "", SymbolFilename( r->s, n.fileindex ) );
        }
        else
        {
            fprintf( r->c, "fl=(%d)\n", n.fileindex );
        }

        if ( prevfn != n.functionindex )
        {
            fprintf( r->c, "fn=(%d) %s\n", n.functionindex, SymbolFunction( r->s, n.functionindex ) );
        }
        else
        {
            fprintf( r->c, "fn=(%d)\n", n.functionindex );
        }

        if ( 1 ) //(prevline == -1) || (prevaddr==-1))
        {
            fprintf( r->c, "0x%08x %d ", f->addr, n.line );
        }
        else
        {
            if ( prevaddr == f->addr )
            {
                fprintf( r->c, "* " );
            }
            else
            {
                fprintf( r->c, "%s%d ", f->addr > prevaddr ? "+" : "", ( int )f->addr - prevaddr );
            }

            if ( prevline == n.line )
            {
                fprintf( r->c, "* " );
            }
            else
            {
                fprintf( r->c, "%s%d ", n.line > prevline ? "+" : "", ( int )n.line - prevline );
            }
        }

        fprintf( r->c, "%" PRIu64 "%" PRIu64 "\n", f->count, f->scount );


        prevline = n.line;
        prevaddr = f->addr;
        prevfile = n.fileindex;
        prevfn = n.functionindex;
        f = f->hh.next;
    }

    struct subcall *s = r->subhead;

    while ( s )
    {
        fprintf( r->c, "fl=(%d)\nfn=(%d)\n", s->sig.srcfileindex, s->sig.srcfunctionindex );

        /* Now publish the call destination. By definition is is known, so can be shortformed */
        fprintf( r->c, "cfi=(%d)\ncfn=(%d)\ncalls=%" PRIu64 " 0x%08x %d\n", s->sig.dstfileindex, s->sig.dstfunctionindex, s->count, s->dst, s->dstline );
        fprintf( r->c, "0x%08x %d %" PRIu64 "\n", s->src, s->srcline, s->myCost );
        s = s->hh.next;
    }

    fclose( r->c );

    return true;
}
// ====================================================================================================
static void _etmCB( void *d )

/* Callback function for when valid ETM decode is detected */

{
    struct RunTime *r = ( struct RunTime * )d;
    struct ETMCPUState *cpu = ETMCPUState( &r->i );
    uint32_t incAddr = 0;
    struct subcall *s;
    struct subcallSig sig;
    struct nameEntry n;
    uint32_t disposition;


    /* This routine gets called when valid data are available, if these are the first, reset counters etc */

    if ( !r->sampling )
    {
        r->sampling = true;
        r->op.firsttstamp = cpu->instCount;
        genericsReport( V_WARN, "Sampling" EOL );
        /* Fill in a time to start from */
        r->starttime = genericsTimestampmS();
        r->intervalBytes = 0;
    }

    /* Deal with changes introduced by this event ========================= */
    if ( ETMStateChanged( &r->i, EV_CH_ADDRESS ) )
    {
        r->op.workingAddr = cpu->addr;
    }

    if ( ETMStateChanged( &r->i, EV_CH_ENATOMS ) )
    {
        incAddr = cpu->eatoms + cpu->natoms;
        disposition = cpu->disposition;
    }

    while ( incAddr )
    {
        incAddr--;

        struct execEntryHash *h = NULL;

        /* Firstly, let's find the local hash record for this address, or create it if it doesn't exist */
        HASH_FIND_INT( r->insthead, &r->op.workingAddr, h );

        if ( !h )
        {
            /* We don't have this address captured yet, do it now */
            if ( SymbolLookup( r->s, r->op.workingAddr, &n, r->options->deleteMaterial ) )
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
                h->count         = h->scount = 0;
            }
            else
            {
                genericsExit( -1, "No symbol for address %08x" EOL, r->op.workingAddr );
            }

            HASH_ADD_INT( r->insthead, addr, h );
        }

        /* OK, by hook or by crook we've got an address entry now, so increment the number of executions */
        h->count++;

        /* If source postion changed then update source code line visitation counts too */
        if ( ( !r->op.h ) || ( h->fileindex != r->op.h->fileindex ) || ( h->functionindex != r->op.h->functionindex ) || ( h->line != r->op.h->line ) )
        {
            h->scount++;
        }

        /* If this was an executed call then update, or make a record for it */
        if ( r->op.lastWasSubcall )
        {
            sig.srcfileindex     = r->op.h->fileindex;
            sig.srcfunctionindex = r->op.h->functionindex;
            sig.srcline          = r->op.h->line;
            sig.dstfileindex     = h->fileindex;
            sig.dstfunctionindex = h->functionindex;
            sig.dstline          = h->line;

            /* Find a call record for this call. When this is executed we're at the point the call has been done */
            HASH_FIND( hh, r->subhead, &sig, sizeof( struct subcallSig ), s );

            if ( !s )
            {
                /* This entry doesn't exist...let's create it */
                s = ( struct subcall * )calloc( 1, sizeof( struct subcall ) );
                memcpy( &s->sig, &sig, sizeof( struct subcallSig ) );
                s->src = r->op.h->addr;
                s->dst = r->op.workingAddr;
                HASH_ADD( hh, r->subhead, sig, sizeof( struct subcallSig ), s );
            }

            /* However we got here, we've got a subcall record, so initialise starting ticks */
            s->inTicks = cpu->instCount;
            s->count++;

            /* ...and add it to the call stack */
            r->substack = ( struct subcall ** )realloc( r->substack, ( r->substacklen + 1 ) * sizeof( struct subcall * ) );
            r->substack[r->substacklen++] = s;
        }


        /* If this is an executed return then process it */
        if ( ( r->op.lastWasReturn ) && ( r->substacklen ) )
        {
            /* We don't bother deallocating memory here cos it'll be done on the next isSubCall */
            s = r->substack[--r->substacklen];
            s->myCost += cpu->instCount - s->inTicks;
        }

        /* Record details of this instruction */
        r->op.h              = h;
        r->op.lasttstamp     = cpu->instCount;
        r->op.lastWasReturn  = h->isReturn && ( disposition & 1 );
        r->op.lastWasSubcall = h->isSubCall && ( disposition & 1 );

        if ( ( h->isJump ) && ( disposition & 1 ) )
        {
            /* This is a fixed jump that _was_ taken, so update working address */
            r->op.workingAddr = h->jumpdest;
        }
        else
        {
            r->op.workingAddr += ( h->is4Byte ) ? 4 : 2;
        }

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
    genericsPrintf( "       -a: Use alternate address encoding" EOL );
    genericsPrintf( "       -D: Switch off C++ symbol demangling" EOL );
    genericsPrintf( "       -d: <String> Material to delete off front of filenames" EOL );
    genericsPrintf( "       -E: When reading from file, terminate at end of file rather than waiting for further input" EOL );
    genericsPrintf( "       -e: <ElfFile> to use for symbols" EOL );
    genericsPrintf( "       -f <filename>: Take input from specified file" EOL );
    genericsPrintf( "       -h: This help" EOL );
    genericsPrintf( "       -s <Duration>: Time to sample (in mS)" EOL );
    genericsPrintf( "       -s: <Server>:<Port> to use" EOL );
    //genericsPrintf( "       -t <channel>: Use TPIU to strip TPIU on specfied channel (defaults to 2)" EOL );
    genericsPrintf( "       -v: <level> Verbose mode 0(errors)..3(debug)" EOL );
    genericsPrintf( "       -y: <Filename> dotty filename for structured callgraph output" EOL );
    genericsPrintf( "       -z: <Filename> profile filename for kcachegrind output" EOL );
    genericsPrintf( EOL "(Will connect one port higher than that set in -s when TPIU is not used)" EOL );

}
// ====================================================================================================
static bool _processOptions( int argc, char *argv[], struct RunTime *r )

{
    int c;

    while ( ( c = getopt ( argc, argv, "aDd:Ee:f:hr:s:v:y:z:" ) ) != -1 )

        switch ( c )
        {
            // ------------------------------------
            case 'a':
                r->options->altAddr = true;
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
            case 'r':
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
    genericsReport( V_INFO, "Delete Mat      : %s" EOL, r->options->deleteMaterial ? r->options->deleteMaterial : "None" );
    genericsReport( V_INFO, "Elf File        : %s" EOL, r->options->elffile );
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

    ETMDecoderInit( &_r.i, &_r.options->altAddr );

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


        /* We need symbols constantly while running */
        if ( !SymbolSetValid( &_r.s, _r.options->elffile ) )
        {
            if ( !( _r.s = SymbolSetCreate( _r.options->elffile, _r.options->demangle, true, true ) ) )
            {
                genericsExit( -1, "Elf file or symbols in it not found" EOL );
            }
            else
            {
                genericsReport( V_DEBUG, "Loaded %s" EOL, _r.options->elffile );
            }
        }


        FD_ZERO( &readfds );

        /* ----------------------------------------------------------------------------- */
        /* This is the main active loop...only break out of this when ending or on error */
        /* ----------------------------------------------------------------------------- */
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

            if ( FD_ISSET( sourcefd, &readfds ) )
            {
                /* We always read the data, even if we're held, to keep the socket alive */
                _r.rawBlock.fillLevel = read( sourcefd, _r.rawBlock.buffer, TRANSFER_SIZE );

                if ( _r.rawBlock.fillLevel <= 0 )
                {
                    /* We are at EOF (Probably the descriptor closed) */
                    break;
                }


                _r.intervalBytes += _r.rawBlock.fillLevel;
                /* Pump all of the data through the protocol handler */
                ETMDecoderPump( &_r.i, _r.rawBlock.buffer, _r.rawBlock.fillLevel, _etmCB, &_r );
            }

            /* Update the intervals */
            if ( ( _r.sampling ) && ( ( genericsTimestampmS() - _r.starttime ) > _r.options->sampleDuration ) )
            {
                _r.ending = true;
                genericsReport( V_WARN, "Received %d raw sample bytes, %ld function changes, %ld distinct addresses" EOL, _r.intervalBytes, HASH_COUNT( _r.subhead ), HASH_COUNT( _r.insthead ) );

                if ( HASH_COUNT( _r.subhead ) )
                {
                    if ( _outputDot( &_r ) )
                    {
                        genericsReport( V_WARN, "Output DOT" EOL );
                    }

                    if ( _outputProfile( &_r ) )
                    {
                        genericsReport( V_WARN, "Output Profile" EOL );
                    }
                }

            }
        }

        /* ----------------------------------------------------------------------------- */
        /* End of main loop ... we get here because something forced us out              */
        /* ----------------------------------------------------------------------------- */

        close( sourcefd );

        if ( _r.options->fileTerminate )
        {
            _r.ending = true;
        }
    }

    return OK;
}

// ====================================================================================================
