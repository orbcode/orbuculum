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
#include <ncurses.h>
#include <sys/ioctl.h>

#include "git_version_info.h"
#include "generics.h"

#include "etmDecoder.h"
#include "symbols.h"

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

/* Colours for output */
enum CP { CP_NONE, CP_EVENT, CP_NORMAL, CP_FILEFUNCTION, CP_LINENO, CP_EXECASSY, CP_NEXECASSY, CP_BASELINE, CP_BASELINETEXT, CP_SEARCH };

/* Search types */
enum SRCH { SRCH_OFF, SRCH_FORWARDS, SRCH_BACKWARDS };

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

    /* Materials for window handling */
    WINDOW *outputWindow;               /* Output window (main one) */
    WINDOW *statusWindow;               /* Status window (interaction one) */
    uint32_t tag[MAX_TAGS];             /* Buffer location tags */
    char **opText;                      /* Text of the output buffer */
    uint32_t opTextWline;               /* Next line number to be written */
    uint32_t opTextRline;               /* Current read position in op buffer */
    uint32_t oldopTextRline;            /* Old read position in op buffer (for redraw) */
    int32_t lines;                      /* Number of lines on current window config */
    int32_t cols;                       /* Number of columns on current window config */

    /* Search stuff */
    enum SRCH searchMode;               /* What kind of search is being conducted */
    char *searchString;                 /* The current searching string */
    char storedFirstSearch;             /* Storage for first char of search string to allow repeats */
    int32_t searchStartPos;             /* Location the search started from (for aborts) */
    bool searchOK;                      /* Is the search currently sucessful? */

    int Key;                            /* Latest keypress */

    bool forceRefresh;                  /* Force a refresh of everything */
    bool outputtingHelp;                /* Output help window */
    bool enteringMark;                  /* Set if we are in the process of marking a location */
    bool held;                          /* If we are actively collecting data */

    struct dataBlock rawBlock;          /* Datablock received from distribution */

    struct Options *options;            /* Our runtime configuration */
} _r =
{
    .options = &_options
};

/* In string markers for information types (for colouring) */
#define CE_CLR       '\377'            /* Flag indicating color change */
#define CE_EV        "\377\001"        /* Event color */
#define CE_FILE      "\377\002"        /* File/line color */
#define CE_SRCLINENO "\377\003"        /* Source lineno color */
#define CE_SRC       "\377\004"        /* Source color */
#define CE_ASSY      "\377\005"        /* Executed assembly color */
#define CE_NASSY     "\377\006"        /* Non-executed assembly color */

/* Window sizes/positions */
#define OUTPUT_WINDOW_L (r->lines-2)
#define OUTPUT_WINDOW_W (r->cols)
#define STATUS_WINDOW_L (2)
#define STATUS_WINDOW_W (r->cols)

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
static void _requestRefresh( struct RunTime *r )

/* Flag that output windows should be updated */

{
    r->forceRefresh = true;
}
// ====================================================================================================
static void _flushBuffer( struct RunTime *r )

/* Empty the output buffer, and de-allocate its memory */

{
  /* Remove all of the recorded lines */
    while ( r->opTextWline )
    {
        free( r->opText[r->opTextWline - 1] );
        r->opTextWline--;
    }

    /* ...and reset the tag records */
    for ( uint32_t t = 0; t < MAX_TAGS; t++ )
    {
        r->tag[t] = 0;
    }

    /* and the opText buffer */
    free( r->opText );
    r->opText = NULL;
    r->opTextWline = r->opTextRline = 0;
    _requestRefresh( r );
}
// ====================================================================================================
static void _appendToOPBuffer( struct RunTime *r, const char *fmt, ... )

/* Add line to output buffer, in a printf stylee */

{
    char construct[SCRATCH_STRING_LEN];
    va_list va;

    va_start( va, fmt );
    vsnprintf( construct, SCRATCH_STRING_LEN, fmt, va );
    va_end( va );

    r->opText = ( char ** )realloc( r->opText, ( sizeof( char * ) ) * ( r->opTextWline + 1 ) );
    r->opText[r->opTextWline] = strdup( construct );
    r->opTextWline++;
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
                _appendToOPBuffer( r, CE_EV "*** VMID Set to %d" EOL, cpu->vmid );
            }

            if ( ETMStateChanged( &r->i, EV_CH_EX_ENTRY ) )
            {
                _appendToOPBuffer( r, CE_EV "========== Exception Entry (%3d%s) ==========" EOL, cpu->exception,
                                   ETMStateChanged( &r->i, EV_CH_CANCELLED ) ? ", Last Instruction Cancelled" : "" );
            }

            if ( ETMStateChanged( &r->i, EV_CH_EX_EXIT ) )
            {
                _appendToOPBuffer( r, CE_EV "========== Exception Exit ==========" EOL );
            }

            if ( ETMStateChanged( &r->i, EV_CH_TSTAMP ) )
            {
                _appendToOPBuffer( r, CE_EV "*** Timestamp %ld" EOL, cpu->ts );
            }

            if ( ETMStateChanged( &r->i, EV_CH_TRIGGER ) )
            {
                _appendToOPBuffer( r, CE_EV "*** Trigger" EOL );
            }

            if ( ETMStateChanged( &r->i, EV_CH_CLOCKSPEED ) )
            {
                _appendToOPBuffer( r, CE_EV "*** Change Clockspeed" EOL );
            }

            if ( ETMStateChanged( &r->i, EV_CH_ISLSIP ) )
            {
                _appendToOPBuffer( r, "*** ISLSIP Triggered" EOL );
            }

            if ( ETMStateChanged( &r->i, EV_CH_CYCLECOUNT ) )
            {
                _appendToOPBuffer( r, CE_EV "(Cycle Count %d)" EOL, cpu->cycleCount );
            }

            if ( ETMStateChanged( &r->i, EV_CH_VMID ) )
            {
                _appendToOPBuffer( r, CE_EV "(VMID is now %d)" EOL, cpu->vmid );
            }

            if ( ETMStateChanged( &r->i, EV_CH_CONTEXTID ) )
            {
                _appendToOPBuffer( r, CE_EV "(Context ID is now %d)" EOL, cpu->contextID );
            }

            if ( ETMStateChanged( &r->i, EV_CH_SECURE ) )
            {

                _appendToOPBuffer( r, CE_EV "(Non-Secure State is now %s)" EOL, cpu->nonSecure ? "True" : "False" );
            }

            if ( ETMStateChanged( &r->i, EV_CH_ALTISA ) )
            {
                _appendToOPBuffer( r, CE_EV "(Using AltISA  is now %s)" EOL, cpu->altISA ? "True" : "False" );
            }

            if ( ETMStateChanged( &r->i, EV_CH_HYP ) )
            {
                _appendToOPBuffer( r, CE_EV "(Using Hypervisor is now %s)" EOL, cpu->hyp ? "True" : "False" );
            }

            if ( ETMStateChanged( &r->i, EV_CH_JAZELLE ) )
            {
                _appendToOPBuffer( r, CE_EV "(Using Jazelle is now %s)" EOL, cpu->jazelle ? "True" : "False" );
            }

            if ( ETMStateChanged( &r->i, EV_CH_THUMB ) )
            {
                _appendToOPBuffer( r, CE_EV "(Using Thumb is now %s)" EOL, cpu->thumb ? "True" : "False" );
            }

            /* End of dealing with changes introduced by this event =============== */

            while ( incAddr-- )
            {
                if ( SymbolLookup( r->s, workingAddr, &n, r->options->deleteMaterial ) )
                {
                    if ( ( n.filename != currentFilename ) || ( n.function != currentFunction ) )
                    {
                        _appendToOPBuffer( r, CE_FILE "%s::%s" EOL, n.filename, n.function );
                        currentFilename = n.filename;
                        currentFunction = n.function;
                    }

                    if ( n.line != currentLine )
                    {
                        const char *v = n.source;
                        currentLine = n.line;
                        *construct = 0;
                        uint32_t lp;
                        uint32_t sline = 0;

                        while ( *v )
                        {
                            lp = snprintf( construct, SCRATCH_STRING_LEN, CE_SRCLINENO "%5d\t" CE_SRC, currentLine + sline );

                            while ( *v )
                            {
                                if ( ( *v != '\r' ) && ( *v != '\n' ) )
                                {
                                    construct[lp++] = *v++;
                                }
                                else
                                {
                                    construct[lp++] = 0;
                                    _appendToOPBuffer( r, construct );
                                    *construct = 0;
                                    lp = 0;

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
                            _appendToOPBuffer( r, CE_ASSY "\t%s:" EOL, n.assy[n.assyLine].label );
                        }



                        if ( n.assy[n.assyLine].is4Byte )
                        {
                            _appendToOPBuffer( r, "%s\t\t%08x:\t%04x %04x\t%s"  EOL,
                                               ( disposition & 1 ) ? CE_ASSY : CE_NASSY,
                                               n.assy[n.assyLine].addr,
                                               ( n.assy[n.assyLine].codes >> 16 ) & 0xffff,
                                               n.assy[n.assyLine].codes & 0xffff,
                                               n.assy[n.assyLine].lineText );
                            workingAddr += 4;
                        }
                        else
                        {
                            _appendToOPBuffer( r, "%s\t\t%08x:\t%04x     \t%s" EOL,
                                               ( disposition & 1 ) ? CE_ASSY : CE_NASSY,
                                               n.assy[n.assyLine].addr,
                                               n.assy[n.assyLine].codes & 0xffff,
                                               n.assy[n.assyLine].lineText );
                            workingAddr += 2;
                        }

                    }
                    else
                    {
                        _appendToOPBuffer( r, CE_ASSY "\t\tASSEMBLY NOT FOUND" EOL );
                        workingAddr += 2;
                    }

                    disposition >>= 1;
                }
            }
        }

        p = ( p + 1 ) % r->options->buflen;
    }

    r->wp = r-> rp = 0;

    /* Force a re-draw, focussed on the end of the buffer */
    r->opTextRline = r->opTextWline - 1;
    _requestRefresh( r );
}

// ====================================================================================================
static void _setupWindows( struct RunTime *r )

{
    initscr();
    r->lines = LINES;
    r->cols = COLS;

    if ( OK == start_color() )
    {
        init_pair( CP_EVENT, COLOR_YELLOW, COLOR_BLACK );
        init_pair( CP_NORMAL, COLOR_WHITE,  COLOR_BLACK );
        init_pair( CP_FILEFUNCTION, COLOR_RED, COLOR_BLACK );
        init_pair( CP_LINENO, COLOR_YELLOW, COLOR_BLACK );
        init_pair( CP_EXECASSY, COLOR_CYAN, COLOR_BLACK );
        init_pair( CP_NEXECASSY, COLOR_BLUE, COLOR_BLACK );
        init_pair( CP_BASELINE, COLOR_BLUE, COLOR_BLACK );
        init_pair( CP_BASELINETEXT, COLOR_YELLOW, COLOR_BLACK );
        init_pair( CP_SEARCH, COLOR_GREEN, COLOR_BLACK );
    }

    r->outputWindow = newwin( OUTPUT_WINDOW_L, OUTPUT_WINDOW_W, 0, 0 );
    r->statusWindow = newwin( STATUS_WINDOW_L, STATUS_WINDOW_W, OUTPUT_WINDOW_L, 0 );
    wtimeout( r->statusWindow, 0 );
    scrollok( r->outputWindow, false );
    keypad( r->statusWindow, true );
    raw();
    noecho();
    curs_set( 0 );
}
// ====================================================================================================
static void _terminateWindows( void )

{
    noraw();
    endwin();
}
// ====================================================================================================
static bool _processRegularKeys( struct RunTime *r )

/* Handle keys in regular mode */

{
    bool retcode = false;

    switch ( r->Key )
    {
        case ERR:
            retcode = true;
            break;

        case 19: /* ----------------------------- CTRL-S Search Forwards ------------------------- */
            r->searchMode = SRCH_FORWARDS;
            *r->searchString = 0;
            r->searchOK = true;
            r->searchStartPos = r->opTextRline;
            curs_set( 1 );
            retcode = true;
            break;

        case 18: /* ----------------------------- CTRL-R Search Reverse -------------------------- */
            r->searchMode = SRCH_BACKWARDS;
            *r->searchString = 0;
            r->searchOK = true;
            r->searchStartPos = r->opTextRline;
            curs_set( 1 );
            retcode = true;
            break;

        case 3: /* ------------------------------ CTRL-C Exit ------------------------------------ */
            _doExit();
            break;

        case '?': /* ---------------------------- Help ------------------------------------------- */
            r->outputtingHelp = !r->outputtingHelp;
            _requestRefresh( r );
            break;

        case 'm':
        case 'M': /* ---------------------------- Enter Mark ------------------------------------- */
            r->enteringMark = !r->enteringMark;
            break;

        case '0' ... '0'+MAX_TAGS: /* ----------- Tagged Location -------------------------------- */
            if ( r->enteringMark )
            {
                r->tag[r->Key - '0'] = r->opTextRline + 1;
                r->enteringMark = false;
            }
            else
            {
                if ( ( r->tag[r->Key - '0'] ) && ( r->tag[r->Key - '0'] < r->opTextWline ) )
                {
                    r->opTextRline = r->tag[r->Key - '0'] - 1;
                }
                else
                {
                    beep();
                }
            }

            break;

        case 259: /* ---------------------------- UP --------------------------------------------- */

            r->opTextRline = ( r->opTextRline > 0 ) ? r->opTextRline - 1 : 0;
            retcode = true;
            break;


        case 398: /* ---------------------------- Shift PgUp ------------------------------------- */
            r->opTextRline = ( r->opTextRline > 10 * OUTPUT_WINDOW_L ) ? r->opTextRline - 10 * OUTPUT_WINDOW_L : 0;
            retcode = true;
            break;

        case 339: /* ---------------------------- PREV PAGE -------------------------------------- */
            r->opTextRline = ( r->opTextRline > OUTPUT_WINDOW_L ) ? r->opTextRline - OUTPUT_WINDOW_L : 0;
            retcode = true;
            break;

        case 338: /* ---------------------------- NEXT PAGE -------------------------------------- */
            r->opTextRline = ( r->opTextRline + OUTPUT_WINDOW_L < r->opTextWline ) ? r->opTextRline + OUTPUT_WINDOW_L : r->opTextWline - 1;
            retcode = true;
            break;

        case 262: /* ---------------------------- HOME ------------------------------------------- */
            r->opTextRline = r->opTextWline < OUTPUT_WINDOW_L ? r->opTextWline : 0;
            retcode = true;
            break;

        case 360: /* ---------------------------- END -------------------------------------------- */
            r->opTextRline = r->opTextWline - 1;
            retcode = true;
            break;

        case 258: /* ---------------------------- DOWN ------------------------------------------- */
            r->opTextRline = ( r->opTextRline < ( r->opTextWline - 1 ) ) ? r->opTextRline + 1 :
                             r->opTextRline;
            retcode = true;
            break;

        case 396: /* ---------------------------- With shift for added speeeeeed ----------------- */
            r->opTextRline = ( r->opTextRline + 10 * OUTPUT_WINDOW_L < r->opTextWline ) ? r->opTextRline + 10 * OUTPUT_WINDOW_L : r->opTextWline - 1;
            retcode = true;
            break;

        default: /* ------------------------------------------------------------------------------ */
            break;
    }

    return retcode;
}
// ====================================================================================================
static bool _updateSearch( struct RunTime *r )

/* Progress search to next element, or ping and return false if we can't */

{
    for ( int32_t l = r->opTextRline;
            ( r->searchMode == SRCH_FORWARDS ) ? ( l < r->opTextWline - 1 ) : ( l > 0 );
            ( r->searchMode == SRCH_FORWARDS ) ? l++ : l-- )
    {
        if ( strstr( r->opText[l], r->searchString ) )
        {
            /* This is a match */
            r->opTextRline = l;
            r->searchOK = true;
            return true;
        }
    }

    /* If we get here then we had no match */
    beep();
    r->searchOK = false;
    return false;
}
// ====================================================================================================
static bool _processSearchKeys( struct RunTime *r )

/* Handle keys in search mode */

{
    bool retcode = false;

    switch ( r->Key )
    {
        case ERR:
            retcode = true;
            break;

        case 10: /* ----------------------------- Newline Commit Search -------------------------- */
            /* Commit the search */
            r->searchMode = SRCH_OFF;
            curs_set( 0 );
            r->storedFirstSearch = *r->searchString;
            *r->searchString = 0;
            _requestRefresh( r );
            retcode = true;
            break;

        case 3: /* ------------------------------ CTRL-C Abort Search ---------------------------- */
            /* Abort the search */
            r->searchMode = SRCH_OFF;
            r->opTextRline = r->searchStartPos;
            r->storedFirstSearch = *r->searchString;
            *r->searchString = 0;
            _requestRefresh( r );
            curs_set( 0 );
            retcode = true;
            break;

        case 19: /* ----------------------------- CTRL-S Search Forwards ------------------------- */
            if ( !*r->searchString )
            {
                /* Try to re-instate old search */
                *r->searchString = r->storedFirstSearch;
            }

            r->searchMode = SRCH_FORWARDS;

            /* Find next match */
            if ( ( r->searchOK ) && ( r->opTextRline < r->opTextWline - 1 ) )
            {
                r->opTextRline++;
            }

            _updateSearch( r );
            _requestRefresh( r );
            retcode = true;
            break;

        case 18: /* ---------------------------- CTRL-R Search Backwards ------------------------- */
            if ( !*r->searchString )
            {
                /* Try to re-instate old search */
                *r->searchString = r->storedFirstSearch;
            }

            /* Find prev match */
            r->searchMode = SRCH_BACKWARDS;

            if ( ( r->searchOK ) && ( r->opTextRline > 0 ) )
            {
                r->opTextRline--;
            }

            _updateSearch( r );
            _requestRefresh( r );
            retcode = true;
            break;

        case 263: /* --------------------------- Del Remove char from search string -------------- */

            /* Delete last character in search string */
            if ( strlen( r->searchString ) )
            {
                r->searchString[strlen( r->searchString ) - 1] = 0;
            }

            _updateSearch( r );
            _requestRefresh( r );
            retcode = true;
            break;

        default: /* ---------------------------- Add valid chars to search string ---------------- */
            if ( ( r->Key > 31 ) && ( r->Key < 255 ) )
            {
                r->searchString = ( char * )realloc( r->searchString, strlen( r->searchString ) + 1 );
                r->searchString[strlen( r->searchString ) + 1] = 0;
                r->searchString[strlen( r->searchString )] = r->Key;
                _updateSearch( r );
                _requestRefresh( r );
                retcode = true;
            }

            break;
    }

    return retcode;
}
// ====================================================================================================
static void _outputHelp( struct RunTime *r )

/* ...as the name suggests. Just replace the output window with help text */

{
    werase( r->outputWindow );
    wattrset( r->outputWindow, A_BOLD | COLOR_PAIR( CP_NORMAL ) );
    wprintw( r->outputWindow, EOL "  Important Keys..." EOL EOL );
    wprintw( r->outputWindow, "       H: Hold or resume sampling" EOL );
    wprintw( r->outputWindow, "       M: Mark a location in the sample buffer, followed by 0..%d" EOL, MAX_TAGS - 1 );
    wprintw( r->outputWindow, "       Q: Quit the application" EOL );
    wprintw( r->outputWindow, "       ?: This help" EOL );
    wprintw( r->outputWindow, "    0..%d: Move to mark in sample buffer, if its defined" EOL EOL, MAX_TAGS - 1 );
    wprintw( r->outputWindow, "  CTRL-C: Quit the current task or the application" EOL );
    wprintw( r->outputWindow, "  CTRL-L: Refresh the screen" EOL );
    wprintw( r->outputWindow, "  CTRL-R: Search backwards, CTRL-R again for next match" EOL );
    wprintw( r->outputWindow, "  CTRL-S: Search forwards, CTRL-S again for next match" EOL );
    wprintw( r->outputWindow, EOL "  Use PgUp/PgDown/Home/End and the arrow keys to move around the sample buffer" EOL );
    wprintw( r->outputWindow, "  Shift-PgUp and Shift-PgDown move more quickly" EOL );
    wprintw( r->outputWindow, EOL "       <?> again to leave this help screeh." EOL );
}

// ====================================================================================================
static void _updateWindows( struct RunTime *r, bool isTick, bool isKey )

/* Update all the visible outputs onscreen */

{
    int y, x;                   /* Current size of output window */
    attr_t attr;                /* On-screen attributes */
    short pair;
    char *ssp;                  /* Position in search match string */
    bool refreshOutput = false; /* Flag indicating that output window needs updating */
    bool refreshStatus = false; /* Flag indicating that status window needs updating */

    /* First, work with the output window */
    if ( r->outputtingHelp )
    {
        _outputHelp( r );
        refreshOutput = true;
    }
    else
    {
        if ( ( r->oldopTextRline != r->opTextRline ) || ( r->forceRefresh ) )
        {
            werase( r->outputWindow );

            for ( int32_t sline = -OUTPUT_WINDOW_L / 2; sline < ( OUTPUT_WINDOW_L + 1 ) / 2; sline++ )
            {
                /* Make sure we've not fallen off one end of the list or the other */
                if ( ( sline + r->opTextRline < 0 ) ||
                        ( sline + r->opTextRline >= r->opTextWline ) )
                {
                    continue;
                }

                char *u = r->opText[sline + r->opTextRline];
                ssp = r->searchString;

                wmove( r->outputWindow, ( OUTPUT_WINDOW_L / 2 ) + sline, 0 );

                while ( *u )
                {
                    switch ( *u )
                    {
                        case '\r': /* ----------- NL/CR are ignored in the sourced data ---------- */
                        case '\n':
                            u++;
                            break;

                        case CE_CLR: /* --------- This is a command character -------------------- */
                            ++u;

                            switch ( *u )
                            {
                                case 1: /* Event color */
                                    wattrset( r->outputWindow, ( ( sline == 0 ) ? A_STANDOUT : 0 ) | A_BOLD | COLOR_PAIR( CP_EVENT ) );
                                    break;

                                case 2: /* File/line color */
                                    wattrset( r->outputWindow, ( ( sline == 0 ) ? A_STANDOUT : 0 ) | A_BOLD | COLOR_PAIR( CP_FILEFUNCTION ) );
                                    break;

                                case 3: /* Source lineno color */
                                    wattrset( r->outputWindow, ( ( sline == 0 ) ? A_STANDOUT : 0 ) | COLOR_PAIR( CP_LINENO ) );
                                    break;

                                case 4: /* Source color */
                                    wattrset( r->outputWindow, ( ( sline == 0 ) ? A_STANDOUT : 0 ) | A_BOLD | COLOR_PAIR( CP_NORMAL ) );
                                    break;

                                case 5: /* Executed assembly color */
                                    wattrset( r->outputWindow, ( ( sline == 0 ) ? A_STANDOUT : 0 ) | COLOR_PAIR( CP_EXECASSY ) );
                                    break;

                                case 6: /* Non-executed assembly color */
                                    wattrset( r->outputWindow, ( ( sline == 0 ) ? A_STANDOUT : 0 ) | COLOR_PAIR( CP_NEXECASSY ) );
                                    break;

                                default:
                                    wattrset( r->outputWindow, ( ( sline == 0 ) ? A_STANDOUT : 0 ) | COLOR_PAIR( CP_NORMAL ) );
                                    break;
                            }

                            /* Store the attributes in case we need them later (e.g. as a result of search changing things) */
                            wattr_get( r->outputWindow, &attr, &pair, NULL );
                            u++;
                            break;

                    default: /* ------------------------------------------------------------- */

                            /* Colour matches if we're in search mode, but whatever is happening, output the characters */
                            if ( ( r->searchMode != SRCH_OFF ) && ( *r->searchString ) && ( !strncmp( u, ssp, strlen( ssp ) ) ) )
                            {
                                wattrset( r->outputWindow, A_BOLD | COLOR_PAIR( CP_SEARCH ) );
                                ssp++;

                                if ( !*ssp )
                                {
                                    ssp = r->searchString;
                                }
                            }
                            else
                            {
                                wattr_set( r->outputWindow, attr, pair, NULL );
                            }

                            waddch( r->outputWindow, *u++ );
                            break;
                    }
                }

                /* Pad out this line with spaces */
                while ( true )
                {
                    getyx( r->outputWindow, y, x );
                    ( void )y;
                    waddch( r->outputWindow, ' ' );

                    if ( x == OUTPUT_WINDOW_W - 1 )
                    {
                        break;
                    }
                }
            }

            r->oldopTextRline = r->opTextRline;
            refreshOutput = true;
        }
    }


    /* Now update the status */
    if ( ( isTick ) || ( isKey ) || ( r->forceRefresh ) )
    {
        werase( r->statusWindow );
        wattrset( r->statusWindow, A_BOLD | COLOR_PAIR( CP_BASELINE ) );
        mvwhline( r->statusWindow, 0, 0, ACS_HLINE, COLS );
        mvwprintw( r->statusWindow, 0, COLS - 4 - ( strlen( r->progName ) + strlen( genericsBasename( r->options->elffile ) ) ),
                   " %s:%s ", r->progName, genericsBasename( r->options->elffile ) );

        if ( r->opTextWline )
        {
            /* We have some opData stored, indicate where we are in it */
            mvwprintw( r->statusWindow, 0, 2, " %d%% (%d/%d) ", ( r->opTextRline * 100 ) / ( r->opTextWline - 1 ), r->opTextRline, r->opTextWline );
        }

        wattrset( r->statusWindow, A_BOLD | COLOR_PAIR( CP_BASELINETEXT ) );

        if ( !r->held )
        {
            if ( r->oldintervalBytes )
            {
                if ( r->oldintervalBytes < 9999 )
                {
                    mvwprintw( r->statusWindow, 1, COLS - 38, "%ld Bps (~%ld Ips)", r->oldintervalBytes, ( r->oldintervalBytes * 8 ) / 11 );
                }
                else if ( r->oldintervalBytes < 9999999 )
                {
                    mvwprintw( r->statusWindow, 1, COLS - 38, "%ld KBps (~%ld KIps)", r->oldintervalBytes / 1000, r->oldintervalBytes * 8 / 1120 );
                }
                else
                {
                    mvwprintw( r->statusWindow, 1, COLS - 38, "%ld MBps (~%ld MIps)", r->oldintervalBytes / 1000000, ( r->oldintervalBytes * 8 ) / 1120000 );
                }
            }

            mvwprintw( r->statusWindow, 1, COLS - 11, r->intervalBytes ? "Capturing" : "  Waiting" );
        }
        else
        {
            mvwprintw( r->statusWindow, 1, COLS - 6, "Hold" );
        }

        mvwprintw( r->statusWindow, 0, 30, " " );

        for ( uint32_t t = 0; t < MAX_TAGS; t++ )
        {
            if ( r->tag[t] )
            {
                wattrset( r->statusWindow, A_BOLD | COLOR_PAIR( CP_BASELINETEXT ) );

                if ( ( r->tag[t] >= ( r->opTextRline - OUTPUT_WINDOW_L / 2 ) ) &&
                        ( r->tag[t] <= ( r->opTextRline + ( OUTPUT_WINDOW_L + 1 ) / 2 ) ) )
                {
                    /* This tag is on the visible page */
                    wattrset( r->outputWindow, A_BOLD | COLOR_PAIR( CP_BASELINETEXT ) );
                    mvwprintw( r->outputWindow, ( OUTPUT_WINDOW_L + 1 ) / 2 + r->tag[t] - r->opTextRline - 1, OUTPUT_WINDOW_W - 1, "%d", t );
                    refreshOutput = true;
                }
            }
            else
            {
                wattrset( r->statusWindow, A_BOLD | COLOR_PAIR( CP_BASELINE ) );
            }

            wprintw( r->statusWindow, "%d", t );
        }

        wprintw( r->statusWindow, " " );

        if ( r->searchMode )
        {
            wattrset( r->statusWindow, A_BOLD | COLOR_PAIR( CP_SEARCH ) );
            mvwprintw( r->statusWindow, 1, 2, "%sSearch %s :%s", r->searchOK ? "" : "(Failing) ", ( r->searchMode == SRCH_FORWARDS ) ? "Forwards" : "Backwards", r->searchString );
        }

        if ( r->enteringMark )
        {
            wattrset( r->statusWindow, A_BOLD | COLOR_PAIR( CP_SEARCH ) );
            mvwprintw( r->statusWindow, 1, 2, "Mark Number?" );
        }

        // Uncomment this line to get a track of latest key values
        // mvwprintw( r->statusWindow, 1, 40, "Key %d", r->Key );
    }

    if ( refreshOutput )
    {
        wrefresh( r->outputWindow );
    }

    if ( refreshStatus )
    {
        wrefresh( r->statusWindow );
    }

    r->forceRefresh = false;
}
// ====================================================================================================
static bool _updateWindowsAndGetKey( struct RunTime *r, bool isTick )

/* Top level to deal with all UI aspects */

{
    struct winsize sz;
    bool keyhandled = false;

    r->Key = wgetch( r->statusWindow );

    if ( r->Key != ERR )
    {
        keyhandled = ( r->searchMode ) ? _processSearchKeys( r ) : _processRegularKeys( r );

        if ( !keyhandled )
        {
            switch ( r->Key )
            {
                case KEY_RESIZE:
                case 12:  /* CTRL-L, refresh ----------------------------------------------------- */
                    ioctl( 0, TIOCGWINSZ, &sz );
                    r->lines = sz.ws_row;
                    r->cols = sz.ws_col;
                    clearok( r->statusWindow, true );
                    clearok( r->outputWindow, true );
                    wresize( r->statusWindow, STATUS_WINDOW_L, STATUS_WINDOW_W );
                    wresize( r->outputWindow, OUTPUT_WINDOW_L, OUTPUT_WINDOW_W );
                    mvwin( r->statusWindow, OUTPUT_WINDOW_L, 0 );
                    keyhandled = true;
                    isTick = true;
                    _requestRefresh( r );
                    break;

                default: /* ---------------------------------------------------------------------- */
                    break;
            }
        }
    }
    else
    {
        keyhandled = true;
    }

    /* Now deal with the output windows */
    _updateWindows( r, isTick, r->Key != ERR );

    return keyhandled;
}

// ====================================================================================================
static void _doExit( void )

/* Perform any explicit exit functions */

{
    _r.ending = true;
    /* Give them a bit of time, then we're leaving anyway */
    usleep( 200 );
    _terminateWindows();
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
    _r.searchString = ( char * )calloc( 2, sizeof( char ) );

    if ( !_processOptions( argc, argv, &_r ) )
    {
        /* processOptions generates its own error messages */
        genericsExit( -1, "" EOL );
    }

    /* Make sure the fifos get removed at the end */
    atexit( _doExit );

    _setupWindows( &_r );

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
                wclear( _r.statusWindow );

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
            if ( !_updateWindowsAndGetKey( &_r, ( genericsTimestampmS() - lastTTime ) > TICK_TIME_MS ) )
            {
                switch ( toupper( _r.Key ) )
                {
                    case 'H':
                        if ( _r.held )
                        {
                            _flushBuffer( &_r );
                        }

                        _r.held = !_r.held;
                        break;

                    case 'Q':
                        _r.ending = true;
                        break;

                    default:
                        break;
                }
            }

            /* Update the various timers that are running */
            if ( ( genericsTimestampmS() - lastTTime ) > TICK_TIME_MS )
            {
                lastTTime = genericsTimestampmS();
            }

            /* Deal with possible timeout on sampling */
            if ( ( ( genericsTimestampmS() - lastTime ) > HANG_TIME_MS ) && ( _r.wp != _r.rp ) )
            {
                _dumpBuffer( &_r );
                /* After we've got a dump we don't want to lose it again!! */
                _r.held = true;
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
