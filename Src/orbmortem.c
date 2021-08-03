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

#include "git_version_info.h"
#include "generics.h"

#include "etmDecoder.h"
#include "symbols.h"

#define TRANSFER_SIZE       (4096)
#define REMOTE_ETM_PORT     (3443)            /* Server port definition */
#define REMOTE_SERVER       "localhost"

#define SCRATCH_STRING_LEN  (65535)            /* Max length for a string under construction */
//#define DUMP_BLOCK
#define DEFAULT_PM_BUFLEN_K (32)

#define INTERVAL_TIME_MS    (1000)             /* Intervaltime between acculumator resets */
#define HANG_TIME_MS        (490)              /* Time without a packet after which we dump the buffer */
#define TICK_TIME_MS        (100)              /* Time intervals for screen updates and keypress check */
#define HB_GRAPHIC          ".oO0Oo"           /* An affectation, keep-alive indication */
#define HB_GRAPHIC_LEN      strlen(HB_GRAPHIC) /* Length of KA indication */

enum CP { CP_EVENT, CP_NORMAL, CP_FILEFUNCTION, CP_LINENO, CP_EXECASSY, CP_NEXECASSY, CP_BASELINE, CP_BASELINETEXT };

/* Record for options, either defaults or from command line */
struct Options
{
    /* Config information */

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

struct visitedAddr                      /* Structure for Hashmap of visited/observed addresses */
{
    uint64_t visits;
    struct nameEntry *n;

    UT_hash_handle hh;
};


struct dataBlock
{
    ssize_t fillLevel;
    uint8_t buffer[TRANSFER_SIZE];
};

struct RunTime
{
    struct ETMDecoder i;

    struct SymbolSet *s;                /* Symbols read from elf */
    bool      ending;                   /* Flag indicating app is terminating */
    uint64_t intervalBytes;             /* Number of bytes transferred in current interval */
    uint64_t oldintervalBytes;          /* Number of bytes transferred previously */
    uint8_t *pmBuffer;                  /* The post-mortem buffer */
    uint32_t wp;                        /* Index pointers for ring buffer */
    uint32_t rp;

    int hb;
    const char *progName;
    WINDOW *outputWindow;
    WINDOW *statusWindow;

    char **opText;
    uint32_t opTextWline;
    uint32_t opTextRline;
    uint32_t oldopTextRline;

    struct visitedAddr *addresses;      /* Addresses we received in the SWV */

    struct dataBlock rawBlock;          /* Datablock received from distribution */

    struct Options *options;            /* Our runtime configuration */
} _r =
{
    .options = &_options
};

/* In string markers for information types (for colouring) */
#define CE_CLR       '\377'     /* Flag indicating color change */
#define CE_EV        "\377\001" /* Event color */
#define CE_FILE      "\377\002" /* File/line color */
#define CE_SRCLINENO "\377\003" /* Source lineno color */
#define CE_SRC       "\377\004" /* Source color */
#define CE_ASSY      "\377\005" /* Executed assembly color */
#define CE_NASSY     "\377\006" /* Non-executed assembly color */

/* Window sizes/positions */
#define OUTPUT_WINDOW_L (LINES-2)
#define OUTPUT_WINDOW_W (COLS)
#define STATUS_WINDOW_L (2)
#define STATUS_WINDOW_W  (COLS)

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Private routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
static void _intHandler( int sig )

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

    while ( ( c = getopt ( argc, argv, "ab:d:Ee:f:hO:s:v:" ) ) != -1 )
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


    genericsReport( V_INFO, "PM Buflen     : %d KBytes" EOL, r->options->buflen / 1024 );
    genericsReport( V_INFO, "Elf File      : %s" EOL, r->options->elffile );

    if ( r->options->file )
    {
        genericsReport( V_INFO, "Input File  : %s", r->options->file );

        if ( r->options->fileTerminate )
        {
            genericsReport( V_INFO, " (Terminate on exhaustion)" EOL );
        }
        else
        {
            genericsReport( V_INFO, " (Ongoing read)" EOL );
        }
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

{
    while ( r->opTextWline )
    {
        free( r->opText[r->opTextWline - 1] );
        r->opTextWline--;
    }

    free( r->opText );
    r->opText = NULL;
    r->opTextWline = r->opTextRline = 0; //r->oldopTextRline = 0;
}
// ====================================================================================================
static void _appendToOPBuffer( struct RunTime *r, const char *fmt, ... )

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
    r->oldopTextRline = 0;
    r->opTextRline = r->opTextWline;
}

// ====================================================================================================
static void _setupWindows( void )

{
    initscr();

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
    }

    _r.outputWindow = newwin( OUTPUT_WINDOW_L, OUTPUT_WINDOW_W, 0, 0 );
    _r.statusWindow = newwin( STATUS_WINDOW_L, STATUS_WINDOW_W, OUTPUT_WINDOW_L, 0 );
    timeout( 0 );
    cbreak();
    keypad( stdscr, true );
    noecho();
    curs_set( 0 );
    scrollok( _r.outputWindow, true );
    idlok( _r.outputWindow, true );
}
// ====================================================================================================
static void _terminateWindows( void )

{
    endwin();
}
// ====================================================================================================
static char _updateWindowsAndGetKey( struct RunTime *r )

{
    int k;


    while ( ( k = getch() ) != ERR )
    {
        switch ( k )
        {
            case KEY_RESIZE: /* ------------------------------------------------------------------ */
            case 12:  /* CTRL-L, refresh */
                r->oldopTextRline = 0;
                break;

            case KEY_UP:
                if ( r->opTextRline > OUTPUT_WINDOW_L )
                {
                    r->opTextRline--;
                }

                break;

            case 398: /* This is shift-PPAGE ----------------------------------------------------- */
                if ( r->opTextRline > 11 * OUTPUT_WINDOW_L )
                {
                    r->opTextRline -= 10 * OUTPUT_WINDOW_L;
                }
                else
                {
                    r->opTextRline = r->opTextWline < OUTPUT_WINDOW_L ? r->opTextWline : OUTPUT_WINDOW_L;
                }

                break;

            case KEY_PPAGE: /* ------------------------------------------------------------------- */
                if ( r->opTextRline > 2 * OUTPUT_WINDOW_L )
                {
                    r->opTextRline -= OUTPUT_WINDOW_L;
                }
                else
                {
                    r->opTextRline = r->opTextWline < OUTPUT_WINDOW_L ? r->opTextWline : OUTPUT_WINDOW_L;
                }

                break;

            case KEY_HOME: /* -------------------------------------------------------------------- */
                r->opTextRline = r->opTextWline < OUTPUT_WINDOW_L ? r->opTextWline : OUTPUT_WINDOW_L;
                break;

            case KEY_END: /* --------------------------------------------------------------------- */
                r->opTextRline = r->opTextWline;
                break;

            case 396: /* This is Shift-NPage ----------------------------------------------------- */
                if ( r->opTextRline + 10 * OUTPUT_WINDOW_L < r->opTextWline )
                {
                    r->opTextRline += 10 * OUTPUT_WINDOW_L;
                }
                else
                {
                    r->opTextRline = r->opTextWline;
                }

                break;

            case KEY_NPAGE: /* ------------------------------------------------------------------- */
                if ( r->opTextRline + OUTPUT_WINDOW_L < r->opTextWline )
                {
                    r->opTextRline += OUTPUT_WINDOW_L;
                }
                else
                {
                    r->opTextRline = r->opTextWline;
                }

                break;

            case KEY_DOWN: /* -------------------------------------------------------------------- */
                if ( r->opTextRline < r->opTextWline )
                {
                    r->opTextRline++;
                }

                break;

            default: /* -------------------------------------------------------------------------- */
                /* Not dealt with here, better check if anything upstairs wants it */
                return k;
        }
    }

    if ( r->oldopTextRline != r->opTextRline )
    {
        wclear( r->outputWindow );

        if ( r->opTextRline )
        {
            for ( uint32_t sline = 0; sline < OUTPUT_WINDOW_L; sline++ )
            {

                char *u = r->opText[sline + r->opTextRline - OUTPUT_WINDOW_L];
                wmove( r->outputWindow, sline, 0 );

                while ( *u )
                {
                    switch ( *u )
                    {
                        case '\r':
                        case '\n':
                            u++;
                            break;

                        case CE_CLR:
                            ++u;
                            switch ( *u )
                            {
                                case 1: /* Event color */
                                    wattrset( r->outputWindow, COLOR_PAIR( CP_EVENT ) );
                                    break;

                                case 2: /* File/line color */
                                    wattrset( r->outputWindow, A_BOLD | COLOR_PAIR( CP_FILEFUNCTION ) );
                                    break;

                                case 3: /* Source lineno color */
                                    wattrset( r->outputWindow, COLOR_PAIR( CP_LINENO ) );
                                    break;

                                case 4: /* Source color */
                                    wattrset( r->outputWindow, A_BOLD | COLOR_PAIR( CP_NORMAL ) );
                                    break;

                                case 5: /* Executed assembly color */
                                    wattrset( r->outputWindow, COLOR_PAIR( CP_EXECASSY ) );
                                    break;

                                case 6: /* Non-executed assembly color */
                                    wattrset( r->outputWindow, COLOR_PAIR( CP_NEXECASSY ) );
                                    break;

                                default:
                                    wattrset( r->outputWindow, COLOR_PAIR( CP_NORMAL ) );
                                    break;
                            }
                            u++;
                            break;

                        default:
                            waddch( r->outputWindow, *u++ );
                            break;
                    }
                }
            }
        }

        r->oldopTextRline = r->opTextRline;
        wrefresh( r->outputWindow );
    }

    /* Now update the status */
    wattrset( r->statusWindow, A_BOLD | COLOR_PAIR( CP_BASELINE ) );
    mvwhline( r->statusWindow, 0, 0, ACS_HLINE, COLS );
    mvwprintw( r->statusWindow, 0, 1, " %c ", HB_GRAPHIC[r->hb] );
    mvwprintw( r->statusWindow, 0, COLS - 4 - ( strlen( r->progName ) + strlen( genericsBasename( r->options->elffile ) ) ),
               " %s:%s ", r->progName, genericsBasename( r->options->elffile ) );

    if ( r->opTextWline )
    {
        mvwprintw( r->statusWindow, 0, 5, " %d%% (%d/%d) ", ( r->opTextRline * 100 ) / r->opTextWline, r->opTextRline, r->opTextWline );
    }

    r->hb = ( r->hb + 1 ) % HB_GRAPHIC_LEN;

    wattrset( r->statusWindow, A_BOLD | COLOR_PAIR( CP_BASELINETEXT ) );

    if ( r->intervalBytes )
    {
        if ( r->oldintervalBytes < 9999 )
        {
          mvwprintw( r->statusWindow, 1, COLS - 36, " Capturing %ld Bps (~%ld Ips)   ", r->oldintervalBytes,(r->oldintervalBytes*8)/11 );
        }
        else if ( r->oldintervalBytes < 9999999 )
        {
            mvwprintw( r->statusWindow, 1, COLS - 36, " Capturing %ld KBps (~%ld KIps)   ", r->oldintervalBytes / 1000, r->oldintervalBytes * 8 / 1120 );
        }
        else
        {
            mvwprintw( r->statusWindow, 1, COLS - 36, " Capturing %ld MBps (~%ld MIps)   ", r->oldintervalBytes / 1000000, ( r->oldintervalBytes * 8 ) / 1120000 );
        }
    }
    else
    {
        mvwprintw( r->statusWindow, 1, COLS - 24, "             Waiting " );
    }

    wrefresh( r->statusWindow );
    return k;
}
// ====================================================================================================
static void _doExit( void )

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

    int64_t lastTime, lastTTime, lastTSTime;
    char k;
    int r;
    struct timeval tv;
    fd_set readfds;
    int32_t remainTime;


    _r.progName = genericsBasename( argv[0] );

    if ( !_processOptions( argc, argv, &_r ) )
    {
        /* processOptions generates its own error messages */
        genericsExit( -1, "" EOL );
    }

    /* Make sure the fifos get removed at the end */
    atexit( _doExit );

    _setupWindows();

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

        while ( !_r.ending )
        {
            remainTime = ( ( lastTime + 10 - genericsTimestampmS() ) * 1000 ) - 500;

            r = 0;

            if ( remainTime > 0 )
            {
                tv.tv_sec = remainTime / 1000000;
                tv.tv_usec  = remainTime % 1000000;

                FD_ZERO( &readfds );
                FD_SET( sourcefd, &readfds );
                r = select( sourcefd + 1, &readfds, NULL, NULL, &tv );
            }

            if ( r < 0 )
            {
                /* Something went wrong in the select */
                break;
            }

            if ( r > 0 )
            {
                _r.rawBlock.fillLevel = read( sourcefd, _r.rawBlock.buffer, TRANSFER_SIZE );

                if ( _r.rawBlock.fillLevel <= 0 )
                {
                    /* We are at EOF (Probably the descriptor closed) */
                    break;
                }

                /* Pump all of the data through the protocol handler */
                _processBlock( &_r );
            }

            if ( ( genericsTimestampmS() - lastTTime ) > TICK_TIME_MS )
            {

                while ( ( k = _updateWindowsAndGetKey( &_r ) ) != ERR )
                {
                    switch ( toupper( k ) )
                    {
                        case 'D':
                            _dumpBuffer( &_r );
                            break;

                        case 'C':
                            _flushBuffer( &_r );
                            break;

                        case 'Q':
                            _r.ending = true;
                            break;

                        default:
                            break;
                    }
                }

                lastTTime = genericsTimestampmS();
            }


            /* Deal with possible timeout sample */
            if ( ( ( genericsTimestampmS() - lastTime ) > HANG_TIME_MS ) && ( _r.wp != _r.rp ) )
            {
                _dumpBuffer( &_r );
            }

            if ( ( genericsTimestampmS() - lastTSTime ) > INTERVAL_TIME_MS )
            {
                _r.oldintervalBytes = _r.intervalBytes;
                _r.intervalBytes = 0;
                lastTSTime = genericsTimestampmS();
            }

            lastTime = genericsTimestampmS();
        }

        close( sourcefd );

        if ( _r.options->fileTerminate )
        {
            _r.ending = true;
        }
    }

    return -ESRCH;
}
// ====================================================================================================
