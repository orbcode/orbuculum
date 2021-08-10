/*
 * Post mortem monitor for parallel trace
 * ======================================
 *
 * Copyright (C) 2017, 2019, 2020, 2021  Dave Marples  <dave@marples.net>
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
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <sys/ioctl.h>

#include "git_version_info.h"
#include "generics.h"

#include "etmDecoder.h"
#include "symbols.h"
#include "sio.h"

#define TRANSFER_SIZE       (65536)
#define REMOTE_ETM_PORT     (3443)            /* Server port definition */
#define REMOTE_SERVER       "localhost"

#define SCRATCH_STRING_LEN  (65535)            /* Max length for a string under construction */
//#define DUMP_BLOCK
#define DEFAULT_PM_BUFLEN_K (32)               /* Default size of the Postmortem buffer */
#define MAX_TAGS            (10)               /* How many tags we will allow */

#define INTERVAL_TIME_MS    (1000)             /* Intervaltime between acculumator resets */
#define HANG_TIME_MS        (490)              /* Time without a packet after which we dump the buffer */
#define TICK_TIME_MS        (100)              /* Time intervals for screen updates and keypress check */

/* Record for options, either defaults or from command line */
struct Options
{
    /* Source information */
    char *file;                         /* File host connection */
    bool fileTerminate;                 /* Terminate when file read isn't successful */
    char *deleteMaterial;               /* Material to delete off front end of filenames */
    bool demangle;                      /* Indicator that C++ should be demangled */

    char *elffile;                      /* File to use for symbols etc. */
    char *objdump;                      /* Novel Objdump file */

    uint32_t buflen;                    /* Length of post-mortem buffer, in bytes */
    bool useTPIU;                       /* Are we using TPIU, and stripping TPIU frames? */
    uint8_t channel;                    /* When TPIU is in use, which channel to decode? */
    int port;                           /* Source information */
    char *server;
    bool altAddr;                       /* Should alternate addressing be used? */
} _options =
{
    .port = REMOTE_ETM_PORT,
    .server = REMOTE_SERVER,
    .demangle = true,
    .channel = 2,
    .buflen = DEFAULT_PM_BUFLEN_K * 1024
};

struct dataBlock
{
    ssize_t fillLevel;
    uint8_t buffer[TRANSFER_SIZE];
};

struct RunTime
{
    struct ETMDecoder i;

    const char *progName;               /* Name by which this program was called */
    struct SymbolSet *s;                /* Symbols read from elf */
    bool      ending;                   /* Flag indicating app is terminating */
    uint64_t intervalBytes;             /* Number of bytes transferred in current interval */
    uint64_t oldintervalBytes;          /* Number of bytes transferred previously */
    uint8_t *pmBuffer;                  /* The post-mortem buffer */
    uint32_t wp;                        /* Index pointers for ring buffer */
    uint32_t rp;

    struct line *opText;                /* Text of the output buffer */
    int32_t numLines;                   /* Number of lines in the output buffer */

    bool held;                          /* If we are actively collecting data */

    struct SIOInstance *sio;            /* Our screen IO instance for managed I/O */

    struct dataBlock rawBlock;          /* Datablock received from distribution */

    struct Options *options;            /* Our runtime configuration */
} _r =
{
    .options = &_options
};

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
static void _printHelp( struct RunTime *r )

{
    genericsPrintf( "Usage: %s [options]" EOL, r->progName );
    genericsPrintf( "       -a: Use alternate address encoding" EOL );
    genericsPrintf( "       -b: <Length> Length of post-mortem buffer, in KBytes (Default %d KBytes)" EOL, DEFAULT_PM_BUFLEN_K );
    genericsPrintf( "       -D: Switch off C++ symbol demangling" EOL );
    genericsPrintf( "       -d: <String> Material to delete off front of filenames" EOL );
    genericsPrintf( "       -e: <ElfFile> to use for symbols and source" EOL );
    genericsPrintf( "       -E: When reading from file, terminate at end of file rather than waiting for further input" EOL );
    genericsPrintf( "       -f <filename>: Take input from specified file" EOL );
    genericsPrintf( "       -h: This help" EOL );
    genericsPrintf( "       -O: <program> Use non-standard obbdump binary" EOL );
    genericsPrintf( "       -s: <Server>:<Port> to use" EOL );
    //genericsPrintf( "       -t <channel>: Use TPIU to strip TPIU on specfied channel (defaults to 2)" EOL );
    genericsPrintf( "       -v: <level> Verbose mode 0(errors)..3(debug)" EOL );
    genericsPrintf( EOL "(Will connect one port higher than that set in -s when TPIU is not used)" EOL );
}
// ====================================================================================================
static int _processOptions( int argc, char *argv[], struct RunTime *r )

{
    int c;

    while ( ( c = getopt ( argc, argv, "ab:Dd:Ee:f:hO:s:v:" ) ) != -1 )
        switch ( c )
        {
            // ------------------------------------
            case 'a':
                r->options->altAddr = true;
                break;

            // ------------------------------------
            case 'b':
                r->options->buflen = atoi( optarg ) * 1024;
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
                _printHelp( r );
                return false;

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
                    r->options->port = REMOTE_ETM_PORT;
                }

                break;

            // ------------------------------------

            case 't':
                r->options->useTPIU = true;
                r->options->channel = atoi( optarg );
                break;

            // ------------------------------------

            case 'v':
                genericsSetReportLevel( atoi( optarg ) );
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
                genericsReport( V_ERROR, "Unrecognised option '%c'" EOL, c );
                return false;
                // ------------------------------------
        }

    /* ... and dump the config if we're being verbose */
    genericsReport( V_INFO, "%s V" VERSION " (Git %08X %s, Built " BUILD_DATE ")" EOL, argv[0], GIT_HASH, ( GIT_DIRTY ? "Dirty" : "Clean" ) );

    if ( !r->options->elffile )
    {
        genericsExit( V_ERROR, "Elf File not specified" EOL );
    }

    if ( !r->options->buflen )
    {
        genericsExit( -1, "Illegal value for Post Mortem Buffer length" EOL );
    }

    return true;
}
// ====================================================================================================
static void _processBlock( struct RunTime *r )

/* Generic block processor for received data */

{
    uint8_t *c = r->rawBlock.buffer;
    uint32_t y = r->rawBlock.fillLevel;

    genericsReport( V_DEBUG, "RXED Packet of %d bytes" EOL, y );

    /* Account for this reception */
    r->intervalBytes += y;

    if ( y )
    {
#ifdef DUMP_BLOCK
        fprintf( stderr, EOL );

        while ( y-- )
        {
            fprintf( stderr, "%02X ", *c++ );

            if ( !( y % 16 ) )
            {
                fprintf( stderr, EOL );
            }
        }

        c = r->rawBlock.buffer;
        y = r->rawBlock.fillLevel;
#endif

        while ( y-- )
        {
            r->pmBuffer[r->wp] = *c++;
            r->wp = ( r->wp + 1 ) % r->options->buflen;

            if ( r->wp == r->rp )
            {
                r->rp++;
            }
        }
    }
}
// ====================================================================================================
static void _flushBuffer( struct RunTime *r )

/* Empty the output buffer, and de-allocate its memory */

{
    /* Tell the UI there's nothing more to show */
    SIOsetOutputBuffer( r->sio, 0, 0, NULL );

    /* Remove all of the recorded lines */
    while ( r->numLines )
    {
        free( r->opText[r->numLines - 1].buffer );
        r->numLines--;
    }

    /* and the opText buffer */
    free( r->opText );
    r->opText = NULL;
    r->numLines = 0;
}
// ====================================================================================================
static void _appendToOPBuffer( struct RunTime *r, int32_t lineno, enum LineType lt, const char *fmt, ... )

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

    r->opText = ( struct line * )realloc( r->opText, ( sizeof( struct line ) ) * ( r->numLines + 1 ) );
    r->opText[r->numLines].buffer = strdup( construct );
    r->opText[r->numLines].lt     = lt;
    r->opText[r->numLines].line   = lineno;
    r->numLines++;
}
// ====================================================================================================
static void _dumpBuffer( struct RunTime *r )

/* Dump received data buffer into text buffer */

{
    struct ETMCPUState *cpu = ETMCPUState( &r->i );
    const char *currentFilename = NULL;
    const char *currentFunction = NULL;
    uint32_t currentLine = 0;
    uint32_t workingAddr = 0, incAddr = 0, p;
    struct nameEntry n;
    uint32_t disposition;
    char construct[SCRATCH_STRING_LEN];

    _flushBuffer( r );

    if ( !SymbolSetValid( &r->s, r->options->elffile ) )
    {
        if ( !( r->s = SymbolSetCreate( r->options->elffile, r->options->objdump, r->options->demangle, true, true ) ) )
        {
            genericsReport( V_ERROR, "Elf file or symbols in it not found" EOL );
            return;
        }
        else
        {
            genericsReport( V_DEBUG, "Loaded %s" EOL, r->options->elffile );
        }
    }

    p = r->rp;

    while ( p != r->wp )
    {
        if ( ETMDecoderPump( &r->i, r->pmBuffer[p] ) == ETM_EV_MSG_RXED )
        {
            incAddr = 0;

            /* Deal with changes introduced by this event ========================= */
            if ( ETMStateChanged( &r->i, EV_CH_ADDRESS ) )
            {
                workingAddr = cpu->addr;
            }

            if ( ETMStateChanged( &r->i, EV_CH_ENATOMS ) )
            {
                incAddr = cpu->eatoms + cpu->natoms;
                disposition = cpu->disposition;
            }

            if ( ETMStateChanged( &r->i, EV_CH_VMID ) )
            {
                _appendToOPBuffer( r, currentLine, LT_EVENT, "*** VMID Set to %d" EOL, cpu->vmid );
            }

            if ( ETMStateChanged( &r->i, EV_CH_EX_ENTRY ) )
            {
                _appendToOPBuffer( r, currentLine, LT_EVENT, "========== Exception Entry%s (%d at %08x) ==========" EOL,
                                   ETMStateChanged( &r->i, EV_CH_CANCELLED ) ? ", Last Instruction Cancelled" : "", cpu->exception, cpu->addr );
            }

            if ( ETMStateChanged( &r->i, EV_CH_EX_EXIT ) )
            {
                _appendToOPBuffer( r, currentLine, LT_EVENT, "========== Exception Exit ==========" EOL );
            }

            if ( ETMStateChanged( &r->i, EV_CH_TSTAMP ) )
            {
                _appendToOPBuffer( r, currentLine, LT_EVENT, "*** Timestamp %ld" EOL, cpu->ts );
            }

            if ( ETMStateChanged( &r->i, EV_CH_TRIGGER ) )
            {
                _appendToOPBuffer( r, currentLine, LT_EVENT, "*** Trigger" EOL );
            }

            if ( ETMStateChanged( &r->i, EV_CH_CLOCKSPEED ) )
            {
                _appendToOPBuffer( r, currentLine, LT_EVENT, "*** Change Clockspeed" EOL );
            }

            if ( ETMStateChanged( &r->i, EV_CH_ISLSIP ) )
            {
                _appendToOPBuffer( r, currentLine, LT_EVENT, "*** ISLSIP Triggered" EOL );
            }

            if ( ETMStateChanged( &r->i, EV_CH_CYCLECOUNT ) )
            {
                _appendToOPBuffer( r, currentLine, LT_EVENT, "(Cycle Count %d)" EOL, cpu->cycleCount );
            }

            if ( ETMStateChanged( &r->i, EV_CH_VMID ) )
            {
                _appendToOPBuffer( r, currentLine, LT_EVENT, "(VMID is now %d)" EOL, cpu->vmid );
            }

            if ( ETMStateChanged( &r->i, EV_CH_CONTEXTID ) )
            {
                _appendToOPBuffer( r, currentLine, LT_EVENT, "(Context ID is now %d)" EOL, cpu->contextID );
            }

            if ( ETMStateChanged( &r->i, EV_CH_SECURE ) )
            {

                _appendToOPBuffer( r, currentLine, LT_EVENT, "(Non-Secure State is now %s)" EOL, cpu->nonSecure ? "True" : "False" );
            }

            if ( ETMStateChanged( &r->i, EV_CH_ALTISA ) )
            {
                _appendToOPBuffer( r, currentLine, LT_EVENT, "(Using AltISA  is now %s)" EOL, cpu->altISA ? "True" : "False" );
            }

            if ( ETMStateChanged( &r->i, EV_CH_HYP ) )
            {
                _appendToOPBuffer( r, currentLine,  LT_EVENT, "(Using Hypervisor is now %s)" EOL, cpu->hyp ? "True" : "False" );
            }

            if ( ETMStateChanged( &r->i, EV_CH_JAZELLE ) )
            {
                _appendToOPBuffer( r, currentLine, LT_EVENT, "(Using Jazelle is now %s)" EOL, cpu->jazelle ? "True" : "False" );
            }

            if ( ETMStateChanged( &r->i, EV_CH_THUMB ) )
            {
                _appendToOPBuffer( r, currentLine, LT_EVENT, "(Using Thumb is now %s)" EOL, cpu->thumb ? "True" : "False" );
            }

            /* End of dealing with changes introduced by this event =============== */

            while ( incAddr-- )
            {
                if ( SymbolLookup( r->s, workingAddr, &n, r->options->deleteMaterial ) )
                {
                    if ( ( n.filename != currentFilename ) || ( n.function != currentFunction ) )
                    {
                        _appendToOPBuffer( r, currentLine, LT_FILE, "%s::%s" EOL, n.filename, n.function );
                        currentFilename = n.filename;
                        currentFunction = n.function;
                    }

                    if ( n.line != currentLine )
                    {
                        const char *v = n.source;
                        currentLine = n.line;
                        *construct = 0;
                        uint32_t lp = 0;
                        uint32_t sline = 0;

                        while ( *v )
                        {
                            while ( *v )
                            {
                                /* Source can cover multiple lines, split into separate ones */
                                if ( ( *v != '\r' ) && ( *v != '\n' ) )
                                {
                                    construct[lp++] = *v++;
                                }
                                else
                                {
                                    construct[lp++] = 0;
                                    _appendToOPBuffer( r, currentLine + sline, LT_SOURCE, construct );

                                    *construct = 0;
                                    lp = 0;

                                    /* Move past the CR/LF */
                                    while ( ( *v == '\r' ) || ( *v == '\n' ) )
                                    {
                                        v++;
                                    }

                                    sline++;
                                    break;
                                }
                            }
                        }
                    }

                    if ( n.assyLine != ASSY_NOT_FOUND )
                    {
                        if ( n.assy[n.assyLine].label )
                        {
                            _appendToOPBuffer( r, currentLine, LT_LABEL, "\t%s:" EOL, n.assy[n.assyLine].label );
                        }



                        if ( n.assy[n.assyLine].is4Byte )
                        {
                            _appendToOPBuffer( r, currentLine, ( disposition & 1 ) ? LT_ASSEMBLY : LT_NASSEMBLY, "\t\t%08x:\t%04x %04x\t%s"  EOL,
                                               n.assy[n.assyLine].addr,
                                               ( n.assy[n.assyLine].codes >> 16 ) & 0xffff,
                                               n.assy[n.assyLine].codes & 0xffff,
                                               n.assy[n.assyLine].lineText );
                            workingAddr += 4;
                        }
                        else
                        {
                            _appendToOPBuffer( r, currentLine, ( disposition & 1 ) ? LT_ASSEMBLY : LT_NASSEMBLY, "\t\t%08x:\t%04x     \t%s" EOL,
                                               n.assy[n.assyLine].addr,
                                               n.assy[n.assyLine].codes & 0xffff,
                                               n.assy[n.assyLine].lineText );
                            workingAddr += 2;
                        }

                    }
                    else
                    {
                        _appendToOPBuffer( r, currentLine, LT_ASSEMBLY, "\t\tASSEMBLY NOT FOUND" EOL );
                        workingAddr += 2;
                    }

                    disposition >>= 1;
                }
            }
        }

        p = ( p + 1 ) % r->options->buflen;
    }

    /* Submit this constructed buffer for display */
    SIOsetOutputBuffer( r->sio, r->numLines, r->numLines - 1, &r->opText );
}
// ====================================================================================================
static void _doSave( struct RunTime *r )

{
    FILE *f;
    char fn[SCRATCH_STRING_LEN];
    uint32_t w;

    snprintf( fn, SCRATCH_STRING_LEN, "%s.trace", SIOgetSaveFilename( r->sio ) );
    f = fopen( fn, "wb" );

    if ( !f )
    {
        SIOalert( r->sio, "Save Trace Failed" );
        return;
    }

    w = r->rp;

    while ( w != r->wp )
    {
        fwrite( &r->pmBuffer[w], 1, 1, f );
        w = ( w + 1 ) % r->options->buflen;
    }

    fclose( f );

    snprintf( fn, SCRATCH_STRING_LEN, "%s.report", SIOgetSaveFilename( r->sio ) );
    f = fopen( fn, "wb" );

    if ( !f )
    {
        SIOalert( r->sio, "Save Report Failed" );
        return;
    }

    w = 0;

    while ( w != r->numLines )
    {
        for ( char *u = r->opText[w++].buffer; *u; u++ )
        {
            fwrite( u, strlen( u ), 1, f );
        }
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
int main( int argc, char *argv[] )

{
    int sourcefd;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    int flag = 1;

    int32_t lastTime, lastTTime, lastTSTime;
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

    /* Create a screen and interaction handler */
    _r.sio = SIOsetup( _r.progName, _r.options->elffile );

    /* Fill in a time to start from */
    lastTime = lastTTime = lastTSTime = genericsTimestampmS();

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

    /* Create the buffer memory */
    _r.pmBuffer = ( uint8_t * )calloc( 1, _r.options->buflen );

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

                if ( !_r.held )
                {
                    /* Pump all of the data through the protocol handler */
                    _processBlock( &_r );
                    lastTime = genericsTimestampmS();
                }
            }

            /* Update the outputs and deal with any keys that made it up this high */
            switch ( SIOHandler( _r.sio, ( genericsTimestampmS() - lastTTime ) > TICK_TIME_MS, _r.oldintervalBytes ) )
            {
                case SIO_EV_HOLD:
                    _r.held = !_r.held;

                    if ( !_r.held )
                    {
                        _r.wp = _r.rp = 0;
                        _flushBuffer( &_r );
                    }

                    /* Flag held status to the UI */
                    SIOheld( _r.sio, _r.held );
                    break;

                case SIO_EV_SAVE:
                    _doSave( &_r );
                    break;

                case SIO_EV_QUIT:
                    _r.ending = true;
                    break;

                default:
                    break;
            }

            /* Update the various timers that are running */
            if ( ( genericsTimestampmS() - lastTTime ) > TICK_TIME_MS )
            {
                lastTTime = genericsTimestampmS();
            }

            /* Deal with possible timeout on sampling */
            if ( ( ( genericsTimestampmS() - lastTime ) > HANG_TIME_MS ) && ( !_r.numLines ) &&  ( _r.wp != _r.rp ) )
            {
                _dumpBuffer( &_r );
                _r.held = true;
                SIOheld( _r.sio, _r.held );
            }

            /* Update the intervals */
            if ( ( genericsTimestampmS() - lastTSTime ) > INTERVAL_TIME_MS )
            {
                _r.oldintervalBytes = _r.intervalBytes;
                _r.intervalBytes = 0;
                lastTSTime = genericsTimestampmS();
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
