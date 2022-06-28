/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Post mortem monitor for parallel trace : Screen I/O Routines
 * ============================================================
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#if defined(WIN32)
    #include <ncurses/ncurses.h>
#else
    #include <ncurses.h>
    #include <sys/ioctl.h>
#endif
#include <assert.h>

#include "generics.h"
#include "sio.h"

/* Colours for output */
enum CP { CP_NONE, CP_EVENT, CP_NORMAL, CP_FILEFUNCTION, CP_LINENO, CP_EXECASSY, CP_NEXECASSY, CP_BASELINE, CP_BASELINETEXT, CP_SEARCH, CP_DEBUG };

/* Search types */
enum SRCH { SRCH_OFF, SRCH_FORWARDS, SRCH_BACKWARDS };

/* Display modes */
enum DISP { DISP_BOTH, DISP_SRC, DISP_ASSY, DISP_MAX_OPTIONS };

struct SIOInstance
{
    /* Materials for window handling */
    WINDOW *outputWindow;               /* Output window (main one) */
    WINDOW *statusWindow;               /* Status window (interaction one) */
    int32_t lines;                      /* Number of lines on current window config */
    int32_t cols;                       /* Number of columns on current window config */
    bool forceRefresh;                  /* Force a refresh of everything */
    int Key;                            /* Latest keypress */

    /* Tagging */
    int32_t tag[MAX_TAGS];              /* Buffer location tags */

    /* Search stuff */
    enum SRCH searchMode;               /* What kind of search is being conducted */
    char *searchString;                 /* The current searching string */
    char storedFirstSearch;             /* Storage for first char of search string to allow repeats */
    int32_t searchStartPos;             /* Location the search started from (for aborts) */
    bool searchOK;                      /* Is the search currently sucessful? */

    /* Save stuff */
    char *saveFilename;                 /* Filename under construction */

    /* Warning and info messages */
    char *warnText;                     /* Text of the warning message */
    uint32_t warnTimeout;               /* Time at which it should be removed, or 0 if it's not active */

    /* Current position in buffer */
    struct line **opText;               /* Pointer to lines of Text of the output buffer */
    int32_t opTextWline;                /* Next line number to be written */
    int32_t opTextRline;                /* Current read position in op buffer */
    int32_t oldopTextRline;             /* Old read position in op buffer (for redraw) */
    enum DISP displayMode;              /* How we want the file displaying */

    /* Diving */
    int32_t pushedopTextRline;          /* Buffered cursor position for when we're recovering from diving */

    /* UI State information */
    bool held;
    bool enteringSaveFilename;          /* State indicator that we're entering filename */
    bool amDiving;                      /* Indicator that we're in a diving buffer */
    bool outputtingHelp;                /* Output help window */
    bool enteringMark;                  /* Set if we are in the process of marking a location */
    bool outputDebug;                   /* Output debug tagged lines */
    bool isFile;                        /* Indicator that we're reading from a file */
    const char *ttext;                  /* Tagline text (if any) */

    const char *progName;
    const char *elffile;
};

/* Window sizes/positions */
#define OUTPUT_WINDOW_L (sio->lines-2)
#define OUTPUT_WINDOW_W (sio->cols)
#define STATUS_WINDOW_L (2)
#define STATUS_WINDOW_W (sio->cols)

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Internal routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
static void _deleteTags( struct SIOInstance *sio )

/* reset the tag records */

{
    for ( uint32_t t = 0; t < MAX_TAGS; t++ )
    {
        sio->tag[t] = 0;
    }
}
// ====================================================================================================
static enum SIOEvent _processSaveFilename( struct SIOInstance *sio )

{
    enum SIOEvent op = SIO_EV_NONE;

    switch ( sio->Key )
    {
        case ERR:
            op = SIO_EV_CONSUMED;
            break;

        case 3: /* ------------------------------ CTRL-C Exit ------------------------------------ */
            sio->enteringSaveFilename = false;
            curs_set( 0 );
            op = SIO_EV_CONSUMED;
            break;

        case 263: /* --------------------------- Del Remove char from save string ----------------- */

            /* Delete last character in save string */
            if ( strlen( sio->saveFilename ) )
            {
                sio->saveFilename[strlen( sio->saveFilename ) - 1] = 0;
            }

            op = SIO_EV_CONSUMED;
            break;

        case 10: /* ----------------------------- Newline Commit Save ---------------------------- */
            /* Commit the save */
            curs_set( 0 );
            sio->enteringSaveFilename = false;
            op = SIO_EV_SAVE;
            break;

        default: /* ---------------------------- Add valid chars to save string ------------------ */
            if ( ( sio->Key > 31 ) && ( sio->Key < 255 ) )
            {
                sio->saveFilename = ( char * )realloc( sio->saveFilename, strlen( sio->saveFilename ) + 2 );
                sio->saveFilename[strlen( sio->saveFilename ) + 1] = 0;
                sio->saveFilename[strlen( sio->saveFilename )] = sio->Key;
                op = SIO_EV_CONSUMED;
            }

            break;
    }

    return op;
}
// ====================================================================================================
static void _outputHelp( struct SIOInstance *sio )

/* ...as the name suggests. Just replace the output window with help text */

{
    werase( sio->outputWindow );
    wattrset( sio->outputWindow, A_BOLD | COLOR_PAIR( CP_NORMAL ) );
    wprintw( sio->outputWindow, EOL "  Important Keys..." EOL EOL );
    wprintw( sio->outputWindow, "       D: Cycle through available display modes" EOL );
    wprintw( sio->outputWindow, "       H: Hold or resume sampling" EOL );
    wprintw( sio->outputWindow, "       M: Mark a location in the sample buffer, followed by 0..%d" EOL, MAX_TAGS - 1 );
    wprintw( sio->outputWindow, "       S: Save current buffer to file" EOL );
    wprintw( sio->outputWindow, "       Q: Quit the application" EOL );
    wprintw( sio->outputWindow, "       ?: This help" EOL );
    wprintw( sio->outputWindow, "    0..%d: Move to mark in sample buffer, if its defined" EOL EOL, MAX_TAGS - 1 );
    wprintw( sio->outputWindow, "      ->: Dive into source file that contains the referenced code" EOL );
    wprintw( sio->outputWindow, "      <-: Surface from source file that contains the referenced code" EOL );
    wprintw( sio->outputWindow, " Shift->: Open source file that contains the referenced code in external editor (if defined)" EOL );
    wprintw( sio->outputWindow, "  CTRL-C: Quit the current task or the application" EOL );
    wprintw( sio->outputWindow, "  CTRL-L: Refresh the screen" EOL );
    wprintw( sio->outputWindow, "  CTRL-R: Search backwards, CTRL-R again for next match" EOL );
    wprintw( sio->outputWindow, "  CTRL-F: Search forwards, CTRL-F again for next match" EOL );
    wprintw( sio->outputWindow, EOL "  Use PgUp/PgDown/Home/End and the arrow keys to move around the sample buffer" EOL );
    wprintw( sio->outputWindow, "  Shift-PgUp and Shift-PgDown move more quickly" EOL );
    wprintw( sio->outputWindow, EOL "       <?> again to leave this help screen." EOL );
}
// ====================================================================================================
static bool _onDisplay( struct SIOInstance *sio, int32_t lineNum )

/* Return true if this lineNum is currently to be displayed, according to the set filter criteria */

{
    return !(
                       /* Debug line and not in debug mode */
                       ( ( ( *sio->opText )[lineNum].lt == LT_DEBUG ) && ( !sio->outputDebug ) ) ||

                       /* Label or Assembly line and in Source Mode */
                       ( ( sio->displayMode == DISP_SRC )  &&
                         ( ( ( *sio->opText )[lineNum].lt == LT_LABEL ) ||
                           ( ( *sio->opText )[lineNum].lt == LT_ASSEMBLY ) ||
                           ( ( *sio->opText )[lineNum].lt == LT_NASSEMBLY ) ) ) ||

                       /* Source Line and in Assembly Mode */
                       ( ( sio->displayMode == DISP_ASSY ) &&
                         ( ( *sio->opText )[lineNum].lt == LT_SOURCE ) )
           );
}
// ====================================================================================================
static void _moveCursor( struct SIOInstance *sio, int lines )

/* Move cursor by specified number of lines, in light of current display mode */

{
    int dir = ( lines < 0 ) ? -1 : 1;

    while ( lines && ( ( ( dir == -1 ) && ( sio->opTextRline ) ) || ( ( dir == 1 ) && ( sio->opTextRline < sio->opTextWline - 1 ) ) ) )
    {
        sio->opTextRline += dir;

        if ( _onDisplay( sio, sio->opTextRline ) )
        {
            lines -= dir;
        }
    }
}
// ====================================================================================================
static enum SIOEvent _processRegularKeys( struct SIOInstance *sio )

/* Handle keys in regular mode */

{
    enum SIOEvent op = SIO_EV_NONE;

    switch ( sio->Key )
    {
        case ERR:
            op = SIO_EV_CONSUMED;
            break;

        case 6:  /* ----------------------------- CTRL-F Search Forwards ------------------------- */
            sio->searchMode = SRCH_FORWARDS;
            *sio->searchString = 0;
            sio->searchOK = true;
            sio->searchStartPos = sio->opTextRline;
            curs_set( 1 );
            op = SIO_EV_CONSUMED;
            break;

        case 18: /* ----------------------------- CTRL-R Search Reverse -------------------------- */
            sio->searchMode = SRCH_BACKWARDS;
            *sio->searchString = 0;
            sio->searchOK = true;
            sio->searchStartPos = sio->opTextRline;
            curs_set( 1 );
            op = SIO_EV_CONSUMED;
            break;

        case 3: /* ------------------------------ CTRL-C Exit ------------------------------------ */
            op = SIO_EV_QUIT;
            break;

        case '?': /* ---------------------------- Help ------------------------------------------- */
            sio->outputtingHelp = !sio->outputtingHelp;
            SIOrequestRefresh( sio );
            op = SIO_EV_CONSUMED;
            break;

        case 'd': /* ---------------------------- Display Mode ----------------------------------- */
            if ( !sio->amDiving )
            {
                sio->displayMode = ( sio->displayMode + 1 ) % DISP_MAX_OPTIONS;
                op = SIO_EV_CONSUMED;
                SIOrequestRefresh( sio );
            }

            break;

        case 'm':
        case 'M': /* ---------------------------- Enter Mark ------------------------------------- */
            if ( !sio->amDiving )
            {
                sio->enteringMark = !sio->enteringMark;
                op = SIO_EV_CONSUMED;
            }

            break;

        case 's':
        case 'S': /* ---------------------------- Enter save filename ---------------------------- */
            if ( ( sio->opTextWline ) && ( !sio->amDiving ) )
            {
                sio->saveFilename = ( char * )realloc( sio->saveFilename, 1 );
                *sio->saveFilename = 0;
                curs_set( 1 );
                sio->enteringSaveFilename = true;
            }
            else
            {
                beep();
            }

            op = SIO_EV_CONSUMED;
            break;

        case '0' ... '0'+MAX_TAGS: /* ----------- Tagged Location -------------------------------- */
            if ( sio->amDiving )
            {
                beep();
            }
            else
            {
                if ( sio->enteringMark )
                {
                    sio->tag[sio->Key - '0'] = sio->opTextRline + 1;
                    sio->enteringMark = false;
                }
                else
                {
                    if ( ( sio->tag[sio->Key - '0'] ) && ( sio->tag[sio->Key - '0'] < sio->opTextWline ) )
                    {
                        sio->opTextRline = sio->tag[sio->Key - '0'] - 1;
                    }
                    else
                    {
                        beep();
                    }
                }
            }

            op = SIO_EV_CONSUMED;
            break;

        case 259: /* ---------------------------- UP --------------------------------------------- */
            _moveCursor( sio, -1 );
            op = SIO_EV_CONSUMED;
            break;

        case 398: /* ---------------------------- Shift PgUp ------------------------------------- */
            _moveCursor( sio, -( 10 * OUTPUT_WINDOW_L ) );
            op = SIO_EV_CONSUMED;
            break;

        case 339: /* ---------------------------- PREV PAGE -------------------------------------- */
            _moveCursor( sio, -OUTPUT_WINDOW_L );
            op = SIO_EV_CONSUMED;
            break;

        case 338: /* ---------------------------- NEXT PAGE -------------------------------------- */
            _moveCursor( sio, OUTPUT_WINDOW_L );
            op = SIO_EV_CONSUMED;
            break;

        case 262: /* ---------------------------- HOME ------------------------------------------- */
            sio->opTextRline = 0;
            _moveCursor( sio, 1 );
            _moveCursor( sio, -1 );
            op = SIO_EV_CONSUMED;
            break;

        case 360: /* ---------------------------- END -------------------------------------------- */
            sio->opTextRline = sio->opTextWline - 1;
            _moveCursor( sio, -1 );
            _moveCursor( sio, 1 );
            op = SIO_EV_CONSUMED;
            break;

        case 258: /* ---------------------------- DOWN ------------------------------------------- */
            _moveCursor( sio, 1 );
            op = SIO_EV_CONSUMED;
            break;

        case 396: /* ---------------------------- With shift for added speeeeeed ----------------- */
            _moveCursor( sio, 10 * OUTPUT_WINDOW_L );
            op = SIO_EV_CONSUMED;
            break;

        default: /* ------------------------------------------------------------------------------ */
            break;
    }

    return op;
}
// ====================================================================================================
static bool _updateSearch( struct SIOInstance *sio )

/* Progress search to next element, or ping and return false if we can't */

{
    for ( int32_t l = sio->opTextRline;
            ( sio->searchMode == SRCH_FORWARDS ) ? ( l < sio->opTextWline - 1 ) : ( l > 0 );
            ( sio->searchMode == SRCH_FORWARDS ) ? l++ : l-- )
    {
        if ( strstr( ( *sio->opText )[l].buffer, sio->searchString ) )
        {
            /* This is a match */
            sio->opTextRline = l;
            sio->searchOK = true;
            return true;
        }
    }

    /* If we get here then we had no match */
    beep();
    sio->searchOK = false;
    return false;
}
// ====================================================================================================
static enum SIOEvent _processSearchKeys( struct SIOInstance *sio )

/* Handle keys in search mode */

{
    enum SIOEvent op = SIO_EV_NONE;

    switch ( sio->Key )
    {
        case ERR:
            op = SIO_EV_CONSUMED;
            break;

        case 10: /* ----------------------------- Newline Commit Search -------------------------- */
            /* Commit the search */
            sio->searchMode = SRCH_OFF;
            curs_set( 0 );
            sio->storedFirstSearch = *sio->searchString;
            *sio->searchString = 0;
            SIOrequestRefresh( sio );
            op = SIO_EV_CONSUMED;
            break;

        case 3: /* ------------------------------ CTRL-C Abort Search ---------------------------- */
            /* Abort the search */
            sio->searchMode = SRCH_OFF;
            sio->opTextRline = sio->searchStartPos;
            sio->storedFirstSearch = *sio->searchString;
            *sio->searchString = 0;
            SIOrequestRefresh( sio );
            curs_set( 0 );
            op = SIO_EV_CONSUMED;
            break;

        case 6:  /* ----------------------------- CTRL-F Search Forwards ------------------------- */
            if ( !*sio->searchString )
            {
                /* Try to re-instate old search */
                *sio->searchString = sio->storedFirstSearch;
            }

            sio->searchMode = SRCH_FORWARDS;

            /* Find next match */
            if ( ( sio->searchOK ) && ( sio->opTextRline < sio->opTextWline - 1 ) )
            {
                sio->opTextRline++;
            }

            _updateSearch( sio );
            SIOrequestRefresh( sio );
            op = SIO_EV_CONSUMED;
            break;

        case 18: /* ---------------------------- CTRL-R Search Backwards ------------------------- */
            if ( !*sio->searchString )
            {
                /* Try to re-instate old search */
                *sio->searchString = sio->storedFirstSearch;
            }

            /* Find prev match */
            sio->searchMode = SRCH_BACKWARDS;

            if ( ( sio->searchOK ) && ( sio->opTextRline > 0 ) )
            {
                sio->opTextRline--;
            }

            _updateSearch( sio );
            SIOrequestRefresh( sio );
            op = SIO_EV_CONSUMED;
            break;

        case 263: /* --------------------------- Del Remove char from search string -------------- */

            /* Delete last character in search string */
            if ( strlen( sio->searchString ) )
            {
                sio->searchString[strlen( sio->searchString ) - 1] = 0;
            }

            _updateSearch( sio );
            SIOrequestRefresh( sio );
            op = SIO_EV_CONSUMED;
            break;

        default: /* ---------------------------- Add valid chars to search string ---------------- */
            if ( ( sio->Key > 31 ) && ( sio->Key < 255 ) )
            {
                sio->searchString = ( char * )realloc( sio->searchString, strlen( sio->searchString ) + 1 );
                sio->searchString[strlen( sio->searchString ) + 1] = 0;
                sio->searchString[strlen( sio->searchString )] = sio->Key;
                _updateSearch( sio );
                SIOrequestRefresh( sio );
                op = SIO_EV_CONSUMED;
            }

            break;
    }

    return op;
}
// ====================================================================================================
static bool _displayLine( struct SIOInstance *sio, int32_t lineNum, int32_t screenline, bool highlight )

{
    int y, x;                   /* Current size of output window */
    attr_t attr;                /* On-screen attributes */
    short pair;
    char *ssp;                  /* Position in search match string */
    char *u;

    /* Make sure this line is valid */
    if ( ( lineNum < 0 ) || ( lineNum >= sio->opTextWline ) )
    {
        return true;
    }

    /* ...and only display it if it fits the current mode */
    if ( !_onDisplay( sio, lineNum ) )
    {
        return false;
    }

    u = ( *sio->opText )[lineNum].buffer;
    ssp = sio->searchString;

    wmove( sio->outputWindow, screenline, 0 );

    switch ( ( *sio->opText )[lineNum].lt )
    {
        case LT_EVENT:
            wattrset( sio->outputWindow, ( highlight ? A_STANDOUT : 0 ) | A_BOLD | COLOR_PAIR( CP_EVENT ) );
            break;

        case LT_FILE:
            wattrset( sio->outputWindow, ( highlight ? A_STANDOUT : 0 ) | A_BOLD | COLOR_PAIR( CP_FILEFUNCTION ) );
            break;

        case LT_MU_SOURCE:
        case LT_SOURCE:
            wattrset( sio->outputWindow, ( highlight ? A_STANDOUT : 0 ) | COLOR_PAIR( CP_LINENO ) );
            wprintw( sio->outputWindow, "%5d ", ( *sio->opText )[lineNum].line );
            wattrset( sio->outputWindow, ( highlight ? A_STANDOUT : 0 ) | A_BOLD | COLOR_PAIR( CP_NORMAL ) );
            break;

        case LT_ASSEMBLY:
            wattrset( sio->outputWindow, ( highlight ? A_STANDOUT : 0 ) | COLOR_PAIR( CP_EXECASSY ) );
            break;

        case LT_NASSEMBLY:
            wattrset( sio->outputWindow, ( highlight ? A_STANDOUT : 0 ) | COLOR_PAIR( CP_NEXECASSY ) );
            break;

        case LT_DEBUG:
            wattrset( sio->outputWindow, ( highlight ? A_STANDOUT : 0 ) | COLOR_PAIR( CP_DEBUG ) );
            break;

        default:
            wattrset( sio->outputWindow, ( highlight ? A_STANDOUT : 0 ) | COLOR_PAIR( CP_NORMAL ) );
            break;
    }

    /* Store the attributes in case we need them later (e.g. as a result of search changing things) */
    wattr_get( sio->outputWindow, &attr, &pair, NULL );

    /* Now output the text of the line */
    getyx( sio->outputWindow, y, x );
    ( void )y;

    while ( ( *u ) && ( *u != '\n' ) && ( *u != '\r' ) && ( x < OUTPUT_WINDOW_W ) )
    {
        /* Colour matches if we're in search mode, but whatever is happening, output the characters */
        if ( ( sio->searchMode != SRCH_OFF ) && ( *sio->searchString ) && ( !strncmp( u, ssp, strlen( ssp ) ) ) )
        {
            wattrset( sio->outputWindow, A_BOLD | COLOR_PAIR( CP_SEARCH ) );
            ssp++;

            if ( !*ssp )
            {
                ssp = sio->searchString;
            }
        }
        else
        {
            wattr_set( sio->outputWindow, attr, pair, NULL );
        }

        if ( x == OUTPUT_WINDOW_W - 1 )
        {
            waddch( sio->outputWindow, '>' );
            break;
        }
        else
        {
            waddch( sio->outputWindow, ( x == OUTPUT_WINDOW_W - 1 ) ? '>' : *u++ );
        }

        /* This is done like this so chars like tabs are accounted for correctly */
        getyx( sio->outputWindow, y, x );
    }

    /* Now pad out the rest of this line with spaces */
    if ( highlight )
    {
        while ( x < OUTPUT_WINDOW_W )
        {
            waddch( sio->outputWindow, ' ' );
            x++;
        }
    }

    return true;
}
// ====================================================================================================
static void _outputOutput( struct SIOInstance *sio )

{
    werase( sio->outputWindow );
    int32_t cp, cl;


    /* First, output lines _forward_ from current position */
    cp = sio->opTextRline;
    cl = ( OUTPUT_WINDOW_L / 2 );

    /* Firstly go forwards filling in each line of the screen */
    while ( ( cl < OUTPUT_WINDOW_L ) && ( cp < sio->opTextWline ) )
    {
        if ( _displayLine( sio, cp++, cl, ( cl == ( OUTPUT_WINDOW_L / 2 ) ) ) )
        {
            cl++;
        }
    }

    /* Now go backwards doing likewise */
    cp = sio->opTextRline - 1;
    cl = ( OUTPUT_WINDOW_L / 2 ) - 1;

    while ( ( cl >= 0 ) && ( cp >= 0 ) )
    {
        if ( _displayLine( sio, cp--, cl, false ) )
        {
            cl--;
        }
    }
}
// ====================================================================================================
static void _outputStatus( struct SIOInstance *sio, uint64_t oldintervalBytes )

{
    werase( sio->statusWindow );
    wattrset( sio->statusWindow, A_BOLD | COLOR_PAIR( CP_BASELINE ) );
    mvwhline( sio->statusWindow, 0, 0, ACS_HLINE, COLS );
    mvwprintw( sio->statusWindow, 0, COLS - 4 - ( strlen( sio->progName ) + strlen( genericsBasename( sio->elffile ) ) ),
               " %s:%s ", sio->progName, genericsBasename( sio->elffile ) );

    if ( sio->warnTimeout )
    {
        if ( sio->warnTimeout > genericsTimestampmS() )
        {

            mvwprintw( sio->statusWindow, 0, 10, " %s ", sio->warnText );
        }
        else
        {
            sio->warnTimeout = 0;
        }
    }

    if ( sio->opTextWline )
    {
        /* We have some opData stored, indicate where we are in it */
        mvwprintw( sio->statusWindow, 0, 2, " %d%% (%d/%d) ", ( sio->opTextRline * 100 ) / ( sio->opTextWline - 1 ), sio->opTextRline + 1, sio->opTextWline );
    }

    if ( !sio->amDiving )
    {
        mvwprintw( sio->statusWindow, 0, 44, " %s%s", ( char *[] )
        {"Mixed", "Source", "Assembly"
        }[sio->displayMode], sio->outputDebug ? "+Debug " : " " );
    }
    else
    {
        mvwprintw( sio->statusWindow, 0, 30,  " Diving Buffer " );
    }


    wattrset( sio->statusWindow, A_BOLD | COLOR_PAIR( CP_BASELINETEXT ) );

    if ( sio->isFile )
    {
        mvwprintw( sio->statusWindow, 1, COLS - 13 - ( ( sio->ttext ) ? strlen( sio->ttext ) : 0 ), "%s From file", ( sio->ttext ) ? sio->ttext : "" );
    }
    else
    {
        if ( !sio->held )
        {
            if ( oldintervalBytes )
            {
                if ( oldintervalBytes < 9999 )
                {
                    mvwprintw( sio->statusWindow, 1, COLS - 45, "%ld Bps (~%ld Ips)", oldintervalBytes, ( oldintervalBytes * 8 ) / 11 );
                }
                else if ( oldintervalBytes < 9999999 )
                {
                    mvwprintw( sio->statusWindow, 1, COLS - 45, "%ld KBps (~%ld KIps)", oldintervalBytes / 1000, oldintervalBytes * 8 / 1120 );
                }
                else
                {
                    mvwprintw( sio->statusWindow, 1, COLS - 45, "%ld MBps (~%ld MIps)", oldintervalBytes / 1000000, ( oldintervalBytes * 8 ) / 1120000 );
                }
            }

            mvwprintw( sio->statusWindow, 1, COLS - 11  - ( ( sio->ttext ) ? strlen( sio->ttext ) : 0 ), "%s %s",
                       ( sio->ttext ) ? sio->ttext : "",
                       oldintervalBytes ? "Capturing" : "  Waiting" );
        }
        else
        {
            mvwprintw( sio->statusWindow, 1, COLS - 6 - ( ( sio->ttext ) ? strlen( sio->ttext ) : 0 ), "%s Hold", ( ( sio->ttext ) ? sio->ttext : "" ) );
        }
    }

    if ( !sio->warnTimeout )
    {
        mvwprintw( sio->statusWindow, 0, 30, " " );
    }

    /* We only output the tags while not in a diving buffer */
    if ( ! sio->amDiving )
    {
        for ( uint32_t t = 0; t < MAX_TAGS; t++ )
        {
            if ( sio->tag[t] )
            {
                wattrset( sio->statusWindow, A_BOLD | COLOR_PAIR( CP_BASELINETEXT ) );

                if ( ( sio->tag[t] >= ( sio->opTextRline - OUTPUT_WINDOW_L / 2 ) ) &&
                        ( sio->tag[t] <= ( sio->opTextRline + ( OUTPUT_WINDOW_L + 1 ) / 2 ) ) )
                {
                    /* This tag is on the visible page */
                    wattrset( sio->outputWindow, A_BOLD | COLOR_PAIR( CP_BASELINETEXT ) );
                    mvwprintw( sio->outputWindow, ( OUTPUT_WINDOW_L ) / 2 + sio->tag[t] - sio->opTextRline - 1, OUTPUT_WINDOW_W - 1, "%d", t );
                }
            }
            else
            {
                wattrset( sio->statusWindow, A_BOLD | COLOR_PAIR( CP_BASELINE ) );
            }

            if ( !sio->warnTimeout )
            {
                /* We only print this if we're not outputting a warning message */
                wprintw( sio->statusWindow, "%d", t );
            }
        }

        if ( !sio->warnTimeout )
        {
            wprintw( sio->statusWindow, " " );
        }
    }

    /* Deal with the various modes */
    if ( sio->enteringSaveFilename )
    {
        wattrset( sio->statusWindow, A_BOLD | COLOR_PAIR( CP_SEARCH ) );
        mvwprintw( sio->statusWindow, 1, 2, "Save Filename :%s", sio->saveFilename );
    }

    if ( sio->searchMode )
    {
        wattrset( sio->statusWindow, A_BOLD | COLOR_PAIR( CP_SEARCH ) );
        mvwprintw( sio->statusWindow, 1, 2, "%sSearch %s :%s", sio->searchOK ? "" : "(Failing) ", ( sio->searchMode == SRCH_FORWARDS ) ? "Forwards" : "Backwards", sio->searchString );
    }

    if ( sio->enteringMark )
    {
        wattrset( sio->statusWindow, A_BOLD | COLOR_PAIR( CP_SEARCH ) );
        mvwprintw( sio->statusWindow, 1, 2, "Mark Number?" );
    }

    // Uncomment this line to get a track of latest key values
    // mvwprintw( sio->statusWindow, 1, 40, "Key %d", sio->Key );
}
// ====================================================================================================
static void _updateWindows( struct SIOInstance *sio, bool isTick, bool isKey, uint64_t oldintervalBytes )

/* Update all the visible outputs onscreen */

{
    bool refreshOutput = false; /* Flag indicating that output window needs updating */
    bool refreshStatus = false; /* Flag indicating that status window needs updating */

    /* First, work with the output window */
    if ( sio->outputtingHelp )
    {
        _outputHelp( sio );
        refreshOutput = true;
    }
    else
    {
        if ( ( sio->oldopTextRline != sio->opTextRline ) || ( sio->forceRefresh ) )
        {
            _outputOutput( sio );
            sio->oldopTextRline = sio->opTextRline;
            refreshOutput = true;
        }
    }

    /* Now update the status */
    if ( ( isTick ) || ( isKey ) || ( sio->forceRefresh ) || ( sio->warnTimeout ) )
    {
        _outputStatus( sio, oldintervalBytes );
        refreshOutput = refreshStatus = true;
    }

    sio->forceRefresh = false;

    /* Now do any refreshes that are needed */
    if ( refreshOutput )
    {
        wrefresh( sio->outputWindow );
    }

    if ( refreshStatus )
    {
        wrefresh( sio->statusWindow );
    }
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Publically accessible routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
struct SIOInstance *SIOsetup( const char *progname, const char *elffile, bool isFile )

{
    struct SIOInstance *sio;

    sio = ( struct SIOInstance * )calloc( sizeof( struct SIOInstance ), 1 );

    sio->searchString = ( char * )calloc( 2, sizeof( char ) );
    sio->progName = progname;
    sio->elffile = elffile;
    sio->isFile  = isFile;

    initscr();
    sio->lines = LINES;
    sio->cols = COLS;

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
        init_pair( CP_DEBUG, COLOR_MAGENTA, COLOR_BLACK );
    }

    sio->outputWindow = newwin( OUTPUT_WINDOW_L, OUTPUT_WINDOW_W, 0, 0 );
    sio->statusWindow = newwin( STATUS_WINDOW_L, STATUS_WINDOW_W, OUTPUT_WINDOW_L, 0 );
    wtimeout( sio->statusWindow, 0 );
    scrollok( sio->outputWindow, false );
    keypad( sio->statusWindow, true );

    /* This allows CTRL-C and CTRL-S to be used in-program */
#ifndef DEBUG
    raw();
#endif

    noecho();
    curs_set( 0 );

    return sio;
}
// ====================================================================================================
const char *SIOgetSaveFilename( struct SIOInstance *sio )

{
    return sio->saveFilename;
}
// ====================================================================================================
int32_t SIOgetCurrentLineno( struct SIOInstance *sio )

{
    return sio->opTextRline;
}
// ====================================================================================================
void SIOalert( struct SIOInstance *sio, const char *msg )

{
    sio->warnText = ( char * )realloc( sio->warnText, strlen( msg ) + 1 );
    strcpy( sio->warnText, msg );
    sio->warnTimeout = genericsTimestampmS() + WARN_TIMEOUT;
    SIOrequestRefresh( sio );
}
// ====================================================================================================
void SIOterminate( struct SIOInstance *sio )

{
    ( void )sio;
    noraw();
    endwin();
}
// ====================================================================================================
void SIOheld( struct SIOInstance *sio, bool isHeld )

{
    sio->held = isHeld;
}
// ====================================================================================================
void SIOrequestRefresh( struct SIOInstance *sio  )

/* Flag that output windows should be updated */

{
    sio->forceRefresh = true;
}
// ====================================================================================================
void SIOsetOutputBuffer( struct SIOInstance *sio, int32_t numLines, int32_t currentLine, struct line **opTextSet, bool amDiving )

{
    sio->opText      = opTextSet;

    /* If we're starting diving store the current cursor position, on surfacing restore it */
    if ( ( !sio->amDiving ) && ( amDiving ) )
    {
        sio->pushedopTextRline = sio->opTextRline;
    }

    if ( opTextSet )
    {
        sio->opTextWline = numLines;

        if ( ( sio->amDiving ) && ( !amDiving ) )
        {
            sio->opTextRline = sio->pushedopTextRline;
        }
        else
        {
            sio->opTextRline = currentLine;
        }
    }
    else
    {
        sio->opTextWline = sio->opTextRline = 0;
        _deleteTags( sio );
    }

    sio->amDiving    = amDiving;
    SIOrequestRefresh( sio );
}
// ====================================================================================================
void SIOtagText ( struct SIOInstance *sio, const char *ttext )

{
    assert( sio );
    sio->ttext = ttext;
    SIOrequestRefresh( sio );
}

// ====================================================================================================

enum SIOEvent SIOHandler( struct SIOInstance *sio, bool isTick, uint64_t oldintervalBytes )

/* Top level to deal with all UI aspects */

{
    enum SIOEvent op = SIO_EV_NONE;

    sio->Key = wgetch( sio->statusWindow );

    if ( sio->Key != ERR )
    {
        if ( sio->enteringSaveFilename )
        {
            op =  _processSaveFilename( sio );
        }
        else
        {
            op = ( sio->searchMode ) ? _processSearchKeys( sio ) : _processRegularKeys( sio );
        }

        if ( op != SIO_EV_CONSUMED )
        {
            switch ( sio->Key )
            {
                case KEY_RESIZE:
                case 12:  /* CTRL-L, refresh ----------------------------------------------------- */
                    {
#if defined(WIN32)
                        getmaxyx( sio->outputWindow, sio->lines, sio->cols );
#else
                        struct winsize sz;
                        ioctl( STDIN_FILENO, TIOCGWINSZ, &sz );
                        sio->lines = ( uint32_t )sz.ws_row;
                        sio->cols = ( uint32_t )sz.ws_col;
#endif
                        clearok( sio->statusWindow, true );
                        clearok( sio->outputWindow, true );
                        wresize( sio->statusWindow, STATUS_WINDOW_L, STATUS_WINDOW_W );
                        wresize( sio->outputWindow, OUTPUT_WINDOW_L, OUTPUT_WINDOW_W );
                        mvwin( sio->statusWindow, OUTPUT_WINDOW_L, 0 );
                        op = SIO_EV_CONSUMED;
                        isTick = true;
                        SIOrequestRefresh( sio );
                    }
                    break;

                case '^':
                    op = SIO_EV_CONSUMED;
                    isTick = true;
                    sio->outputDebug = !sio->outputDebug;
                    SIOrequestRefresh( sio );
                    break;

                case 'h':
                case 'H':
                    op = SIO_EV_HOLD;
                    break;

                case 261:
                    op = SIO_EV_DIVE;
                    break;

                case 402:
                    op = SIO_EV_FOPEN;
                    break;

                case 260:
                    op = SIO_EV_SURFACE;
                    break;

                case 'q':
                case 'Q':
                    op = SIO_EV_QUIT;
                    break;

                default: /* ---------------------------------------------------------------------- */
                    break;
            }
        }
    }

    /* Now deal with the output windows */
    _updateWindows( sio, isTick, sio->Key != ERR, oldintervalBytes );

    return op;
}
// ====================================================================================================
