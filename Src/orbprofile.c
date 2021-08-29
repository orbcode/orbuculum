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

#include "bfd_wrapper.h"
#if defined OSX
    #include "osxelf.h"
    #include <libusb.h>
#else
    #include <elf.h>
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

#define SCRATCH_STRING_LEN  (65535)     /* Max length for a string under construction */
#define TICK_TIME_MS        (100)       /* Time intervals for screen updates and keypress check */
#define INTERVAL_TIME_MS    (1000)      /* Intervaltime between acculumator resets */
/* An entry in the names table */
struct nameEntryHash
{
    struct nameEntry *n;
    bool seen;
    uint32_t index;
    UT_hash_handle hh;
};

/* A calling edge */
struct edge
{
    uint64_t tstamp;
    const char *srcFile;
    const char *srcFn;
    const char *dstFile;
    const char *dstFn;
    bool in;
};

/* Processed subcalls from routine to routine */
struct subcalls
{
    uint32_t src;
    uint32_t dst;
    uint64_t myCost;
    uint64_t total;
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
    char *objdump;                           /* Non-standard objdump binary */

    char *dotfile;                           /* File to output dot information */
    char *profile;                           /* File to output profile information */

    bool altAddr;                       /* Should alternate addressing be used? */
    bool useTPIU;                       /* Are we using TPIU, and stripping TPIU frames? */
    int channel;                        /* When TPIU is in use, which channel to decode? */

    int port;                                /* Source information for where to connect to */
    char *server;

} _options =
{
    .demangle = true,
    .port = NWCLIENT_SERVER_PORT,
    .server = "localhost"
};

/* Materials required to be maintained across callbacks for output construction */
struct opConstruct
{
    const char *currentFilename;         /* The filename we're currently in */
    const char *currentFunction;         /* The function we're currently in */
    uint32_t currentLine;                /* The line we're currently in */
    uint32_t workingAddr;                /* The address we're currently in */
    bool lastWasJump;                    /* Was the last instruction a jump? */
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
    uint64_t oldintervalBytes;          /* Number of bytes transferred previously */


    /* Calls related info */
    enum CDState CDState;                   /* State of the call data machine */
    struct edge callsConstruct;             /* Call data entry under construction */
    struct edge *calls;                     /* Call data table */
    struct subcalls *sub;                   /* Construct data */

    uint32_t subPsn;                        /* Counter for sub calls */
    uint32_t psn;                           /* Current position in assessment of data */
    uint32_t cdCount;                       /* Call data count */

    struct SymbolSet *s;                    /* Symbols read from elf */
    FILE *c;                                /* Writable file */

    struct opConstruct op;              /* The mechanical elements for creating the output buffer */
    struct Options *options;            /* Our runtime configuration */

    struct dataBlock rawBlock;          /* Datablock received from distribution */

    /* Turn addresses into files and routines tags */
    uint32_t nameCount;
    struct nameEntryHash *name;

    /* Used for stretching number of bits in target timer */
    uint32_t oldt;
    uint64_t highOrdert;
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
static int _calls_sort_src_fn( const void *a_v, const void *b_v )

/* Sort addresses first by src, then by dst */

{
    struct edge *a = ( struct edge * )a_v;
    struct edge *b = ( struct edge * )b_v;
    int32_t c;

    c = strcmp( a->srcFile, b->srcFile );

    if ( !c )
    {
        c = strcmp( a->srcFn, b->srcFn );
    }

    if ( !c )
    {
        c = strcmp( a->dstFile, b->dstFile );
    }

    if ( !c )
    {
        c = strcmp( a->dstFn, b->dstFn );
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

    c = strcmp( a->dstFile, b->dstFile );

    if ( !c )
    {
        c = strcmp( a->dstFn, b->dstFn );
    }

    if ( !c )
    {
        c = strcmp( a->srcFile, b->srcFile );
    }

    if ( !c )
    {
        c = strcmp( a->srcFn, b->srcFn );
    }

    return c;
}
// ====================================================================================================
void _outputDot( struct RunTime *r )

/* Output call graph to dot file */

{
    FILE *c;

    if ( !r->options->dotfile )
    {
        return;
    }

    /* Sort according to addresses visited. */
    qsort( r->calls, r->cdCount, sizeof( struct edge ), _calls_sort_dest_fn );

    c = fopen( r->options->dotfile, "w" );
    fprintf( c, "digraph calls\n{\n  overlap=false; splines=true; size=\"7.75,10.25\"; orientation=portrait; sep=0.1; nodesep=0.1;\n" );

    /* firstly write out the nodes in each subgraph - dest side clustered */
    for ( uint32_t x = 1; x < r->cdCount; x++ )
    {
        fprintf( c, "  subgraph \"cluster_%s\"\n  {\n    label=\"%s\";\n    bgcolor=lightgrey;\n", r->calls[x - 1].dstFile, r->calls[x - 1].dstFile );

        while ( x < r->cdCount )
        {
            /* Now output each function in the subgraph */
            fprintf( c, "    %s [style=filled, fillcolor=white];\n", r->calls[x - 1].dstFn );

            /* Spin forwards until the function name _or_ filename changes */
            while ( ( x < r->cdCount ) && ( r->calls[x - 1].dstFn == r->calls[x].dstFn ) )
            {
                x++;
            }

            if ( ( x >= r->cdCount ) || ( r->calls[x - 1].dstFile != r->calls[x].dstFile ) )
            {
                break;
            }

            x++;
        }

        fprintf( c, "  }\n\n" );
    }

    printf("Sort completed" EOL);
    /* now write out the nodes in each subgraph - source side clustered */
    qsort( r->calls, r->cdCount, sizeof( struct edge ), _calls_sort_src_fn );

    for ( uint32_t x = 1; x < r->cdCount; x++ )
    {
        fprintf( c, "  subgraph \"cluster_%s\"\n  {\n    label=\"%s\";\n    bgcolor=lightgrey;\n", r->calls[x - 1].srcFile, r->calls[x - 1].srcFile );

        while ( x < r->cdCount )
        {
            /* Now output each function in the subgraph */
            fprintf( c, "    %s [style=filled, fillcolor=white];\n", r->calls[x - 1].srcFn );

            /* Spin forwards until the function name _or_ filename changes */
            while ( ( x < r->cdCount ) && ( r->calls[x - 1].srcFn == r->calls[x].srcFn ) )
            {
                x++;
            }

            if ( ( x >= r->cdCount ) || ( r->calls[x - 1].srcFile != r->calls[x].srcFile ) )
            {
                break;
            }

            x++;
        }

        fprintf( c, "  }\n\n" );
    }

    /* Now go through and label the arrows... */
    for ( uint32_t x = 0; x < r->cdCount; x++ )
    {
      int cnt=0;
      while ((x <r->cdCount-1) && (r->calls[x].srcFn == r->calls[x+1].srcFn) && (r->calls[x].dstFn == r->calls[x+1].dstFn))
        {
          cnt++;
          x++;
        }

        fprintf( c, "    %s -> ", r->calls[x].srcFn );
        fprintf( c, "%s [label=%d , weight=0.1;];\n", r->calls[x].dstFn, cnt );
    }

    fprintf( c, "}\n" );
    fclose( c );
}
// ====================================================================================================
static void _etmCB( void *d )

/* Callback function for when valid ETM decode is detected */

{
    struct RunTime *r = ( struct RunTime * )d;
    struct ETMCPUState *cpu = ETMCPUState( &r->i );
    uint32_t incAddr = 0;
    struct nameEntry n;
    uint32_t disposition;

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

    if ( ETMStateChanged( &r->i, EV_CH_EX_ENTRY ) )
    {
        r->op.currentFilename = "INTERRUPT";
        //r->op.currentFunction = NULL;
        r->op.lastWasJump = true;
    }

    if ( ETMStateChanged( &r->i, EV_CH_EX_EXIT ) )
    {

    }

    while ( incAddr )
    {
        incAddr--;

        if ( SymbolLookup( r->s, r->op.workingAddr, &n, r->options->deleteMaterial ) )
        {
            /* If we have changed file or function put a header line in */
            if ( ( n.filename != r->op.currentFilename ) || ( n.function != r->op.currentFunction ) )
            {
              if (r->op.lastWasJump)
                {
                r->calls = ( struct edge * )realloc( r->calls, sizeof( struct edge ) * ( r->cdCount + 1 ) );
                r->calls[r->cdCount].tstamp = cpu->ts; // cpu->cycleCount;
                r->calls[r->cdCount].srcFile = r->op.currentFilename?r->op.currentFilename:"Entry";
                r->calls[r->cdCount].srcFn   = r->op.currentFunction?r->op.currentFunction:"Entry";
                r->calls[r->cdCount].dstFile = n.filename;
                r->calls[r->cdCount].dstFn = n.function;
                r->calls[r->cdCount].in = r->op.lastWasJump;
                r->cdCount++;
                }

                r->op.currentFilename = n.filename;
                r->op.currentFunction = n.function;
            }

            r->op.lastWasJump = false;

            /* If this line has assembly then process it */
            if ( n.assyLine != ASSY_NOT_FOUND )
            {
                if ( ( n.assy[n.assyLine].isJump ) && ( disposition & 1 ) )
                {
                    /* This is a fixed jump that _was_ taken, so update working address */
                    r->op.workingAddr = n.assy[n.assyLine].jumpdest;
                    r->op.lastWasJump = true;
                }
                else
                {
                    r->op.workingAddr += ( n.assy[n.assyLine].is4Byte ) ? 4 : 2;
                }
            }
            else
            {
                r->op.workingAddr += 2;
            }
        }
        else
        {
            /* We didn't have a symbol for this address, so let's just assume a short instruction */
            r->op.workingAddr += 2;
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
    genericsPrintf( "       -O: <program> Use non-standard obbdump binary" EOL );
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

    while ( ( c = getopt ( argc, argv, "aDd:Ee:f:hO:s:v:y:z:" ) ) != -1 )

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
            case 'O':
                r->options->objdump = optarg;
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

    genericsReport( V_INFO, "%s V" VERSION " (Git %08X %s, Built " BUILD_DATE ")" EOL, r->progName, GIT_HASH, ( GIT_DIRTY ? "Dirty" : "Clean" ) );

    genericsReport( V_INFO, "Server        : %s:%d" EOL, r->options->server, r->options->port );
    genericsReport( V_INFO, "Delete Mat    : %s" EOL, r->options->deleteMaterial ? r->options->deleteMaterial : "None" );
    genericsReport( V_INFO, "Elf File      : %s" EOL, r->options->elffile );
    genericsReport( V_INFO, "DOT file      : %s" EOL, r->options->dotfile ? r->options->dotfile : "None" );

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

    int32_t lastTTime, lastTSTime;
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

    /* Fill in a time to start from */
    lastTTime = lastTSTime = genericsTimestampmS();

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
            if ( !( _r.s = SymbolSetCreate( _r.options->elffile, _r.options->objdump, _r.options->demangle, true, true ) ) )
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
            /* Each time segment is restricted to 10mS */
            tv.tv_sec = 0;
            tv.tv_usec  = 10000;

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

                /* Pump all of the data through the protocol handler */
                ETMDecoderPump( &_r.i, _r.rawBlock.buffer, _r.rawBlock.fillLevel, _etmCB, &_r );
                //                lastTime = genericsTimestampmS();
            }

            /* Update the various timers that are running */
            if ( ( genericsTimestampmS() - lastTTime ) > TICK_TIME_MS )
            {
                lastTTime = genericsTimestampmS();
            }

            /* Update the intervals */
            if ( ( genericsTimestampmS() - lastTSTime ) > INTERVAL_TIME_MS )
            {
                _r.oldintervalBytes = _r.intervalBytes;
                _r.intervalBytes = 0;
                lastTSTime = genericsTimestampmS();

                if ( (  _r.cdCount ) && ( _r.options->dotfile ) )
                {
                    _outputDot( &_r );
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
