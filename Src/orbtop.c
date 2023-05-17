/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * ITM Top for Orbuculum
 * =====================
 *
 */

#include <fcntl.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/stat.h>
#include <stdio.h>
#include <assert.h>
#include <inttypes.h>
#include <getopt.h>

#include "cJSON.h"
#include "generics.h"
#include "uthash.h"
#include "git_version_info.h"
#include "generics.h"
#include "tpiuDecoder.h"
#include "itmDecoder.h"
#include "symbols.h"
#include "msgSeq.h"
#include "nw.h"
#include "stream.h"

#define CUTOFF              (10)             /* Default cutoff at 0.1% */
#define TOP_UPDATE_INTERVAL (1000)           /* Interval between each on screen update */

#define MAX_EXCEPTIONS      (512)            /* Maximum number of exceptions to be considered */
#define NO_EXCEPTION        (0xFFFFFFFF)     /* Flag indicating no exception is being processed */

#define MSG_REORDER_BUFLEN  (10)             /* Maximum number of samples to re-order for timekeeping */

struct visitedAddr                           /* Structure for Hashmap of visited/observed addresses */
{
    uint64_t visits;
    struct nameEntry *n;

    UT_hash_handle hh;
};

struct reportLine

{
    uint64_t count;
    struct nameEntry *n;
};

struct exceptionRecord                       /* Record of exception activity */

{
    uint64_t visits;
    int64_t totalTime;
    int64_t minTime;
    int64_t maxTime;
    int64_t maxWallTime;
    uint32_t maxDepth;

    /* Elements used in calcuation */
    int64_t entryTime;
    int64_t thisTime;
    int64_t stealTime;
    uint32_t prev;
};


/* ---------- CONFIGURATION ----------------- */
struct                                       /* Record for options, either defaults or from command line */
{
    bool useTPIU;                            /* Are we decoding via the TPIU? */
    bool reportFilenames;                    /* Report filenames for each routine? */
    bool outputExceptions;                   /* Set to include exceptions in output flow */
    uint32_t tpiuITMChannel;                 /* What channel? */
    bool forceITMSync;                       /* Must ITM start synced? */
    char *file;                              /* File host connection */

    uint32_t hwOutputs;                      /* What hardware outputs are enabled */

    char *deleteMaterial;                    /* Material to delete off filenames for target */

    char *elffile;                           /* Target program config */
    char *odoptions;                         /* Options to pass directly to objdump */

    char *json;                              /* Output in JSON format rather than human readable, either '-' for screen or filename */
    char *outfile;                           /* File to output current information */
    char *logfile;                           /* File to output historic information */
    bool mono;                               /* Supress colour in output */
    uint32_t cutscreen;                      /* Cut screen output after specified number of lines */
    uint32_t maxRoutines;                    /* Historic information to emit */
    bool lineDisaggregation;                 /* Aggregate per line or per function? */
    bool demangle;                           /* Do we want to demangle any C++ we come across? */
    int64_t displayInterval;                 /* What is the display interval? */

    int port;                                /* Source information */
    char *server;

} options =
{
    .forceITMSync = true,
    .useTPIU = false,
    .tpiuITMChannel = 1,
    .outfile = NULL,
    .logfile = NULL,
    .lineDisaggregation = false,
    .maxRoutines = 8,
    .demangle = true,
    .displayInterval = TOP_UPDATE_INTERVAL * 1000,
    .port = NWCLIENT_SERVER_PORT,
    .server = "localhost"
};

/* ----------- LIVE STATE ----------------- */
struct
{
    struct ITMDecoder i;                               /* The decoders and the packets from them */
    struct MSGSeq    d;                                   /* Message (re-)sequencer */
    struct ITMPacket h;
    struct TPIUDecoder t;
    struct TPIUPacket p;
    enum timeDelay timeStatus;                         /* Indicator of if this time is exact */
    uint64_t timeStamp;                                /* Latest received time */

    struct SymbolSet *s;                               /* Symbols read from elf */
    struct nameEntry *n;                               /* Current table of recognised names */

    struct visitedAddr *addresses;                     /* Addresses we received in the SWV */

    struct exceptionRecord er[MAX_EXCEPTIONS];         /* Exceptions we received on this interval */
    uint32_t currentException;                         /* Exception we are currently embedded in */
    uint32_t erDepth;                                  /* Current depth of exception stack */
    char *depthList;                                   /* Record of maximum depth of exceptions */

    int64_t lastReportus;                              /* Last time an output report was generated, in microseconds */
    int64_t lastReportTicks;                           /* Last time an output report was generated, in ticks */
    uint32_t ITMoverflows;                             /* Has an ITM overflow been detected? */
    uint32_t SWPkt;                                    /* Number of SW Packets received */
    uint32_t TSPkt;                                    /* Number of TS Packets received */
    uint32_t HWPkt;                                    /* Number of HW Packets received */

    FILE *jsonfile;                                    /* File where json output is being dumped */
    uint32_t interrupts;
    uint32_t sleeps;
    uint32_t notFound;
} _r;

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Internally available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
int64_t _timestamp( void )

{
    struct timeval te;
    gettimeofday( &te, NULL ); // get current time
    int64_t microseconds = te.tv_sec * 1000000LL + ( te.tv_usec ); // accumulate microseconds
    return microseconds;
}
// ====================================================================================================
int _addresses_sort_fn( void *a, void *b )

{
    if ( ( ( ( struct visitedAddr * )a )->n->addr ) < ( ( ( struct visitedAddr * )b )->n->addr ) )
    {
        return -1;
    }

    if ( ( ( ( struct visitedAddr * )a )->n->addr ) > ( ( ( struct visitedAddr * )b )->n->addr ) )
    {
        return 1;
    }

    return 0;
}
// ====================================================================================================
int _routines_sort_fn( void *a, void *b )

{
    int r;

    if ( ( options.reportFilenames ) && ( ( ( ( struct visitedAddr * )a )->n->fileindex ) && ( ( ( struct visitedAddr * )b )->n->fileindex ) ) )
    {
        r = ( ( int )( ( struct visitedAddr * )a )->n->fileindex ) - ( ( int )( ( struct visitedAddr * )b )->n->fileindex );

        if ( r )
        {
            return r;
        }
    }

    r = ( ( int )( ( struct visitedAddr * )a )->n->functionindex ) - ( ( int )( ( struct visitedAddr * )b )->n->functionindex );

    if ( r )
    {
        return r;
    }

    return ( ( int )( ( struct visitedAddr * )a )->n->line ) - ( ( int )( ( struct visitedAddr * )b )->n->line );
}
// ====================================================================================================
int _report_sort_fn( const void *a, const void *b )

{
    return ( ( struct reportLine * )b )->count - ( ( struct reportLine * )a )->count;
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Handler for individual message types from SWO
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
void _exitEx( int64_t ts )

{
    if ( _r.currentException == NO_EXCEPTION )
    {
        /* This can happen under startup and overflow conditions */
        return;
    }

    /* Calculate total time for this exception as we're leaving it */
    int64_t thisTime = ts - _r.er[_r.currentException].entryTime;
    int64_t thisStealTime = _r.er[_r.currentException].stealTime;

    _r.er[_r.currentException].thisTime += thisTime;
    _r.er[_r.currentException].visits++;
    _r.er[_r.currentException].totalTime += _r.er[_r.currentException].thisTime;

    /* Zero the entryTime as it's used to show when an exception is 'live' */
    _r.er[_r.currentException].entryTime = 0;

    /* ...and account for this time */
    if ( ( !_r.er[_r.currentException].minTime ) || ( _r.er[_r.currentException].thisTime < _r.er[_r.currentException].minTime ) )
    {
        _r.er[_r.currentException].minTime = _r.er[_r.currentException].thisTime;
    }

    if ( _r.er[_r.currentException].thisTime > _r.er[_r.currentException].maxTime )
    {
        _r.er[_r.currentException].maxTime = _r.er[_r.currentException].thisTime;
    }

    const int64_t walltime = _r.er[_r.currentException].thisTime + _r.er[_r.currentException].stealTime;

    if ( walltime > _r.er[_r.currentException].maxWallTime )
    {
        _r.er[_r.currentException].maxWallTime = walltime;
    }

    if ( _r.erDepth > _r.er[_r.currentException].maxDepth )
    {
        _r.er[_r.currentException].maxDepth = _r.erDepth;
    }

    /* Step out of this exception */
    _r.currentException = _r.er[_r.currentException].prev;

    if ( _r.erDepth )
    {
        _r.erDepth--;
    }

    /* If we are still in an exception then carry on accounting */
    if ( _r.currentException != NO_EXCEPTION )
    {
        _r.er[_r.currentException].entryTime = ts;
        _r.er[_r.currentException].stealTime += thisTime + thisStealTime;
    }
}
// ====================================================================================================
void _handleTS( struct TSMsg *m, struct ITMDecoder *i )

{
    assert( m->msgtype == MSG_TS );

    _r.timeStatus = m->timeStatus;
    _r.timeStamp += m->timeInc;
}
// ====================================================================================================
void _handleException( struct excMsg *m, struct ITMDecoder *i )

{
    assert( m->msgtype == MSG_EXCEPTION );
    assert( m->exceptionNumber < MAX_EXCEPTIONS );

    switch ( m->eventType )
    {
        case EXEVENT_ENTER:
            if ( _r.er[m->exceptionNumber].entryTime != 0 )
            {
                /* We beleive we are already in this exception. This can happen when we've lost
                 * messages due to ITM overflow. Don't process the enter. Everything will get
                 * fixed up in the next EXEXIT_RESUME which will reset everything.
                 */
                break;
            }

            if ( _r.currentException != NO_EXCEPTION )
            {
                /* Already in an exception ... account for time until now */
                _r.er[_r.currentException].thisTime += _r.timeStamp - _r.er[_r.currentException].entryTime;
            }

            /* Record however we got to this exception */
            _r.er[m->exceptionNumber].prev = _r.currentException;

            /* Now dip into this exception */
            _r.currentException = m->exceptionNumber;
            _r.er[m->exceptionNumber].entryTime = _r.timeStamp;
            _r.er[m->exceptionNumber].thisTime = 0;
            _r.er[m->exceptionNumber].stealTime = 0;
            _r.erDepth++;
            break;

        case EXEVENT_RESUME: /* Unwind all levels of exception (deals with tail chaining) */
            while ( ( _r.currentException != m->exceptionNumber ) && ( _r.erDepth ) )
            {
                _exitEx( _r.timeStamp );
            }

            break;

        case EXEVENT_EXIT: /* Exit single level of exception */
            _exitEx( _r.timeStamp );
            break;

        default:
        case EXEVENT_UNKNOWN:
            genericsReport( V_INFO, "Unrecognised exception event (%d,%d)" EOL, m->eventType, m->exceptionNumber );
            break;
    };
}
// ====================================================================================================
void _handleDWTEvent( struct ITMDecoder *i, struct ITMPacket *p )

{

}
// ====================================================================================================
void _handleSW( struct ITMDecoder *i, struct ITMPacket *p )

{

}
// ====================================================================================================
// ====================================================================================================
// Outputter routines
// ====================================================================================================
// ====================================================================================================
uint32_t _consolodateReport( struct reportLine **returnReport, uint32_t *returnReportLines )

{
    struct nameEntry *n;
    struct visitedAddr *a;

    uint32_t reportLines = 0;
    struct reportLine *report = NULL;
    uint32_t total = 0;


    /* Put the address into order of the file and function names */
    HASH_SORT( _r.addresses, _routines_sort_fn );

    /* Now merge them together */
    for ( a = _r.addresses; a != NULL; a = a->hh.next )
    {
        if ( !a->visits )
        {
            continue;
        }

        if ( ( reportLines == 0 ) ||
                ( ( options.reportFilenames ) &&  ( report[reportLines - 1].n->fileindex != a->n->fileindex ) ) ||
                ( report[reportLines - 1].n->functionindex != a->n->functionindex ) ||
                ( ( report[reportLines - 1].n->line != a->n->line ) && ( options.lineDisaggregation ) ) )
        {
            /* Make room for a report line */
            reportLines++;
            report = ( struct reportLine * )realloc( report, sizeof( struct reportLine ) * ( reportLines ) );
            report[reportLines - 1].n = a->n;
            report[reportLines - 1].count = 0;
        }

        report[reportLines - 1].count += a->visits;
        total += a->visits;
        a->visits = 0;
    }


    /* Now fold in any sleeping entries */
    report = ( struct reportLine * )realloc( report, sizeof( struct reportLine ) * ( reportLines + 1 ) );

    uint32_t addr = FN_SLEEPING;
    HASH_FIND_INT( _r.addresses, &addr, a );

    if ( a )
    {
        n = a->n;
    }
    else
    {
        n = ( struct nameEntry * )malloc( sizeof( struct nameEntry ) );
    }

    n->fileindex = NO_FILE;
    n->functionindex = FN_SLEEPING;
    n->addr = 0;
    n->line = 0;

    report[reportLines].n = n;
    report[reportLines].count = _r.sleeps;
    reportLines++;
    total += _r.sleeps;
    _r.sleeps = 0;

    /* Now put the whole thing into order of number of samples */
    qsort( report, reportLines, sizeof( struct reportLine ), _report_sort_fn );

    *returnReport = report;
    *returnReportLines = reportLines;

    return total;
}
// ====================================================================================================
static void _outputJson( FILE *f, uint32_t total, uint32_t reportLines, struct reportLine *report, int64_t timeStamp )

/* Produce the output to JSON */

{
    /* JSON output constructor */
    cJSON *jsonStore;
    cJSON *jsonElement;
    cJSON *jsonTopTable;
    cJSON *jsonStatsTable;
    cJSON *jsonTableEntry;
    cJSON *jsonIntTable;
    char *opString;

    /* Start of frame  ====================================================== */
    jsonStore = cJSON_CreateObject();
    assert( jsonStore );
    jsonElement = cJSON_CreateNumber( timeStamp );
    assert( jsonElement );
    cJSON_AddItemToObject( jsonStore, "timestamp", jsonElement );
    jsonElement = cJSON_CreateNumber( total );
    assert( jsonElement );
    cJSON_AddItemToObject( jsonStore, "elements", jsonElement );
    jsonElement = cJSON_CreateNumber( timeStamp - _r.lastReportus );
    assert( jsonElement );
    cJSON_AddItemToObject( jsonStore, "interval", jsonElement );

    /* Create stats ========================================================= */
    jsonStatsTable = cJSON_CreateObject();
    assert( jsonStatsTable );
    cJSON_AddItemToObject( jsonStore, "stats", jsonStatsTable );

    jsonElement = cJSON_CreateNumber( ITMDecoderGetStats( &_r.i )->overflow );
    assert( jsonElement );
    cJSON_AddItemToObject( jsonStatsTable, "overflow", jsonElement );

    jsonElement = cJSON_CreateNumber( ITMDecoderGetStats( &_r.i )->syncCount );
    assert( jsonElement );
    cJSON_AddItemToObject( jsonStatsTable, "itmsync", jsonElement );
    jsonElement = cJSON_CreateNumber( TPIUDecoderGetStats( &_r.t )->syncCount );
    assert( jsonElement );
    cJSON_AddItemToObject( jsonStatsTable, "tpiusync", jsonElement );
    jsonElement = cJSON_CreateNumber( ITMDecoderGetStats( &_r.i )->ErrorPkt );
    assert( jsonElement );
    cJSON_AddItemToObject( jsonStatsTable, "error", jsonElement );


    /* Create top table ====================================================== */
    jsonTopTable = cJSON_CreateArray();
    assert( jsonTopTable );
    cJSON_AddItemToObject( jsonStore, "toptable", jsonTopTable );

    for ( uint32_t n = 0; n < reportLines; n++ )
    {
        if ( report[n].count )
        {
            char *d = NULL;

            /* Output in JSON Format */
            jsonTableEntry = cJSON_CreateObject();
            assert( jsonTableEntry );

            jsonElement = cJSON_CreateNumber( report[n].count );
            assert( jsonElement );
            cJSON_AddItemToObject( jsonTableEntry, "count", jsonElement );
            jsonElement = cJSON_CreateString( SymbolFilename( _r.s, report[n].n->fileindex ) );
            assert( jsonElement );
            cJSON_AddItemToObject( jsonTableEntry, "filename", jsonElement );

            jsonElement = cJSON_CreateString(  d ? d : SymbolFunction( _r.s, report[n].n->functionindex ) );
            assert( jsonElement );
            cJSON_AddItemToObject( jsonTableEntry, "function", jsonElement );

            if ( options.lineDisaggregation )
            {
                jsonElement = cJSON_CreateNumber( report[n].n->line ? report[n].n->line : 0 );
                assert( jsonElement );
                cJSON_AddItemToObject( jsonTableEntry, "line", jsonElement );
            }

            cJSON_AddItemToObject( jsonTopTable, "top", jsonTableEntry );
        }
    }

    /* Now add the interrupt metrics ================================================ */
    jsonIntTable = cJSON_CreateArray();
    assert( jsonIntTable );
    cJSON_AddItemToObject( jsonStore, "exceptions", jsonIntTable );

    for ( uint32_t e = 0; e < MAX_EXCEPTIONS; e++ )
    {
        if ( _r.er[e].visits )
        {
            jsonTableEntry = cJSON_CreateObject();
            assert( jsonTableEntry );
            cJSON_AddItemToObject( jsonIntTable, "exceptions", jsonTableEntry );
            jsonElement = cJSON_CreateNumber( e );
            assert( jsonElement );
            cJSON_AddItemToObject( jsonTableEntry, "ex", jsonElement );
            jsonElement = cJSON_CreateNumber( _r.er[e].visits );
            assert( jsonElement );
            cJSON_AddItemToObject( jsonTableEntry, "count", jsonElement );
            jsonElement = cJSON_CreateNumber( _r.er[e].maxDepth );
            assert( jsonElement );
            cJSON_AddItemToObject( jsonTableEntry, "maxd", jsonElement );
            jsonElement = cJSON_CreateNumber( _r.er[e].totalTime );
            assert( jsonElement );
            cJSON_AddItemToObject( jsonTableEntry, "totalt", jsonElement );
            jsonElement = cJSON_CreateNumber( _r.er[e].minTime );
            assert( jsonElement );
            cJSON_AddItemToObject( jsonTableEntry, "mint", jsonElement );
            jsonElement = cJSON_CreateNumber( _r.er[e].maxTime );
            assert( jsonElement );
            cJSON_AddItemToObject( jsonTableEntry, "maxt", jsonElement );
            jsonElement = cJSON_CreateNumber( _r.er[e].maxWallTime );
            assert( jsonElement );
            cJSON_AddItemToObject( jsonTableEntry, "maxwt", jsonElement );
        }
    }

    /* Close off JSON report - if you want your printing pretty then use the first line */
    //opString=cJSON_Print(jsonStore);

    opString = cJSON_PrintUnformatted( jsonStore );
    cJSON_Delete( jsonStore );
    fprintf( f, "%s" EOL, opString );
    free( opString );
}

static const char *ExceptionNames[] =
{
    [0] = "None",
    [1] = "Reset",
    [2] = "NMI",
    [3] = "HardFault",
    [4] = "MemManage",
    [5] = "BusFault",
    [6] = "UsageFault",
    [7] = "Reserved",
    [8] = "Reserved",
    [9] = "Reserved",
    [10] = "Reserved",
    [11] = "SVCall",
    [12] = "DebugMonitor",
    [13] = "Reserved",
    [14] = "PendSV",
    [15] = "SysTick",
};

// ====================================================================================================
static void _outputTop( uint32_t total, uint32_t reportLines, struct reportLine *report, int64_t lastTime )

/* Produce the output */

{
    uint64_t samples = 0;
    uint64_t dispSamples = 0;
    uint32_t percentage;
    uint32_t totPercent = 0;

    FILE *p = NULL;
    FILE *q = NULL;
    uint32_t printed = 0;

    /* This is the file retaining the current samples */
    if ( options.outfile )
    {
        p = fopen( options.outfile, "w" );
    }

    /* This is the file containing the historic samples */
    if ( options.logfile )
    {
        q = fopen( options.logfile, "a" );
    }

    genericsPrintf( CLEAR_SCREEN );

    if ( total )
    {
        for ( uint32_t n = 0; n < reportLines; n++ )
        {
            percentage = ( report[n].count * 10000 ) / total;
            samples += report[n].count;

            if ( report[n].count )
            {
                char *d = NULL;

                if ( ( percentage >= CUTOFF ) && ( ( !options.cutscreen ) || ( n < options.cutscreen ) ) )
                {
                    dispSamples += report[n].count;
                    totPercent += percentage;

                    genericsPrintf( C_DATA "%3d.%02d%% " C_SUPPORT " %7" PRIu64 " ", percentage / 100, percentage % 100, report[n].count );


                    if ( ( options.reportFilenames ) && ( report[n].n->fileindex != NO_FILE ) )
                    {
                        genericsPrintf( C_CONTEXT "%s" C_RESET "::", SymbolFilename( _r.s, report[n].n->fileindex ) );
                    }

                    if ( ( options.lineDisaggregation ) && ( report[n].n->line ) )
                    {
                        genericsPrintf( C_SUPPORT2 "%s" C_RESET "::" C_CONTEXT "%d" EOL, d ? d : SymbolFunction( _r.s, report[n].n->functionindex ), report[n].n->line );
                    }
                    else
                    {
                        genericsPrintf( C_SUPPORT2 "%s" C_RESET EOL, d ? d : SymbolFunction( _r.s, report[n].n->functionindex ) );
                    }

                    printed++;
                }

                /* Write to current and historical data files if appropriate */
                if ( percentage >= CUTOFF )
                {
                    if ( !options.lineDisaggregation )
                    {
                        if ( ( p ) && ( n < options.maxRoutines ) )
                        {
                            fprintf( p, "%s,%3d.%02d" EOL, d ? d : SymbolFunction( _r.s, report[n].n->functionindex ), percentage / 100, percentage % 100 );
                        }

                        if ( q )
                        {
                            fprintf( q, "%s,%3d.%02d" EOL, d ? d : SymbolFunction( _r.s, report[n].n->functionindex ), percentage / 100, percentage % 100 );
                        }
                    }
                    else
                    {
                        if ( ( p ) && ( n < options.maxRoutines ) )
                        {
                            fprintf( p, "%s::%d,%3d.%02d" EOL, d ? d : SymbolFunction( _r.s, report[n].n->functionindex ), report[n].n->line, percentage / 100, percentage % 100 );
                        }

                        if ( q )
                        {
                            fprintf( q, "%s::%d,%3d.%02d" EOL, d ? d : SymbolFunction( _r.s, report[n].n->functionindex ), report[n].n->line, percentage / 100, percentage % 100 );
                        }
                    }


                }
            }
        }
    }

    genericsPrintf( C_RESET "-----------------" EOL );

    genericsPrintf( C_DATA "%3d.%02d%% " C_SUPPORT " %7" PRIu64 " " C_RESET "of "C_DATA" %" PRIu64 " "C_RESET" Samples" EOL, totPercent / 100, totPercent % 100, dispSamples, samples );

    if ( p )
    {
        fclose( p );
    }

    if ( q )
    {
        fprintf( q, "===================================" EOL );
        fclose( q );
    }


    if ( options.outputExceptions )
    {
        /* Tidy up screen output */
        while ( printed++ <= options.cutscreen )
        {
            genericsPrintf( EOL );
        }

        genericsPrintf( EOL " Exception         |   Count  |  MaxD | TotalTicks  |   %%   |  AveTicks  |  minTicks  |  maxTicks  |  maxWall " EOL );
        genericsPrintf( /**/"-------------------+----------+-------+-------------+-------+------------+------------+------------+----------" EOL );

        for ( uint32_t e = 0; e < MAX_EXCEPTIONS; e++ )
        {

            if ( _r.er[e].visits )
            {
                char exceptionName[30] = { 0 };

                if ( e < 16 )
                {
                    snprintf( exceptionName, sizeof( exceptionName ), "(%s)", ExceptionNames[e] );
                }
                else
                {
                    snprintf( exceptionName, sizeof( exceptionName ), "(IRQ %d)", e - 16 );
                }

                const float util_percent = ( float )_r.er[e].totalTime / ( _r.timeStamp - _r.lastReportTicks ) * 100.0f;
                genericsPrintf( C_DATA "%3" PRId32 " %-14s" C_RESET " | " C_DATA "%8" PRIu64 C_RESET " |" C_DATA " %5"
                                PRIu32 C_RESET " | "C_DATA " %9" PRIu64 C_RESET "  |" C_DATA "%6.1f" C_RESET " |  " C_DATA "%9" PRIu64 C_RESET " | " C_DATA "%9" PRIu64 C_RESET "  | " C_DATA" %9" PRIu64 C_RESET " | " C_DATA "%9"
                                PRIu64 C_RESET EOL,
                                e, exceptionName, _r.er[e].visits, _r.er[e].maxDepth, _r.er[e].totalTime, util_percent, _r.er[e].totalTime / _r.er[e].visits, _r.er[e].minTime, _r.er[e].maxTime, _r.er[e].maxWallTime );
            }
        }
    }

    genericsPrintf( EOL C_RESET "[%s%s%s%s" C_RESET "] ",
                    ( _r.ITMoverflows != ITMDecoderGetStats( &_r.i )->overflow ) ? C_OVF_IND "V" : C_RESET "-",
                    ( _r.SWPkt != ITMDecoderGetStats( &_r.i )->SWPkt ) ? C_SOFT_IND "S" : C_RESET "-",
                    ( _r.TSPkt != ITMDecoderGetStats( &_r.i )->TSPkt ) ? C_TSTAMP_IND "T" : C_RESET "-",
                    ( _r.HWPkt != ITMDecoderGetStats( &_r.i )->HWPkt ) ? C_HW_IND "H" : C_RESET "-" );

    if ( _r.lastReportTicks )
        genericsPrintf( "Interval = " C_DATA "%" PRIu64 "mS " C_RESET "/ "C_DATA "%" PRIu64 C_RESET " (~" C_DATA "%" PRIu64 C_RESET " Ticks/mS)" EOL,
                        ( ( lastTime - _r.lastReportus ) + 500 ) / 1000, _r.timeStamp - _r.lastReportTicks, ( ( _r.timeStamp - _r.lastReportTicks ) * 1000 ) / ( lastTime - _r.lastReportus ) );
    else
    {
        genericsPrintf( C_RESET "Interval = " C_DATA "%" PRIu64 C_RESET "mS" EOL, ( ( lastTime - _r.lastReportus ) + 500 ) / 1000 );
    }

    genericsReport( V_INFO, "         Ovf=%3d  ITMSync=%3d TPIUSync=%3d ITMErrors=%3d" EOL,
                    ITMDecoderGetStats( &_r.i )->overflow,
                    ITMDecoderGetStats( &_r.i )->syncCount,
                    TPIUDecoderGetStats( &_r.t )->syncCount,
                    ITMDecoderGetStats( &_r.i )->ErrorPkt );

}

// ====================================================================================================
void _handlePCSample( struct pcSampleMsg *m, struct ITMDecoder *i )

{
    assert( m->msgtype == MSG_PC_SAMPLE );

    struct visitedAddr *a;

    if ( m->sleep )
    {
        /* This is a sleep packet */
        _r.sleeps++;
    }
    else
    {
        HASH_FIND_INT( _r.addresses, &m->pc, a );

        if ( a )
        {
            a->visits++;
        }
        else
        {
            struct nameEntry n;

            /* Find a matching name record if there is one */
            SymbolLookup( _r.s, m->pc, &n );

            /* This is a new entry - record it */

            a = ( struct visitedAddr * )calloc( 1, sizeof( struct visitedAddr ) );
            MEMCHECK( a, );
            a->visits = 1;

            a->n = ( struct nameEntry * )malloc( sizeof( struct nameEntry ) );
            MEMCHECK( a->n, )
            memcpy( a->n, &n, sizeof( struct nameEntry ) );
            HASH_ADD_INT( _r.addresses, n->addr, a );
        }
    }
}
// ====================================================================================================
void _flushHash( void )

{
    struct visitedAddr *a;
    UT_hash_handle hh;

    for ( a = _r.addresses; a != NULL; a = hh.next )
    {
        hh = a->hh;
        free( a );
    }

    _r.addresses = NULL;
}
// ====================================================================================================
// Pump characters into the itm decoder
// ====================================================================================================
void _itmPumpProcess( uint8_t c )

{
    typedef void ( *handlers )( void *decoded, struct ITMDecoder * i );

    /* Handlers for each complete message received */
    static const handlers h[MSG_NUM_MSGS] =
    {
        /* MSG_UNKNOWN */         NULL,
        /* MSG_RESERVED */        NULL,
        /* MSG_ERROR */           NULL,
        /* MSG_NONE */            NULL,
        /* MSG_SOFTWARE */        NULL,
        /* MSG_NISYNC */          NULL,
        /* MSG_OSW */             NULL,
        /* MSG_DATA_ACCESS_WP */  NULL,
        /* MSG_DATA_RWWP */       NULL,
        /* MSG_PC_SAMPLE */       ( handlers )_handlePCSample,
        /* MSG_DWT_EVENT */       NULL,
        /* MSG_EXCEPTION */       ( handlers )_handleException,
        /* MSG_TS */              ( handlers )_handleTS
    };

    struct msg *p;

    if ( !MSGSeqPump( &_r.d, c ) )
    {
        return;
    }

    /* We are synced timewise, so empty anything that has been waiting */
    while ( 1 )
    {
        p = MSGSeqGetPacket( &_r.d );

        if ( !p )
        {
            /* all read */
            break;
        }

        assert( p->genericMsg.msgtype < MSG_NUM_MSGS );

        if ( h[p->genericMsg.msgtype] )
        {
            ( h[p->genericMsg.msgtype] )( p, &_r.i );
        }
    }

    return;
}
// ====================================================================================================
// ====================================================================================================
// Protocol pump for decoding messages
// ====================================================================================================
// ====================================================================================================
void _protocolPump( uint8_t c )

/* Top level protocol pump */

{
    if ( options.useTPIU )
    {
        switch ( TPIUPump( &_r.t, c ) )
        {
            // ------------------------------------
            case TPIU_EV_NEWSYNC:
                genericsReport( V_INFO, "TPIU In Sync (%d)" EOL, TPIUDecoderGetStats( &_r.t )->syncCount );

            case TPIU_EV_SYNCED:
                ITMDecoderForceSync( &_r.i, true );
                break;

            // ------------------------------------
            case TPIU_EV_RXING:
            case TPIU_EV_NONE:
                break;

            // ------------------------------------
            case TPIU_EV_UNSYNCED:
                genericsReport( V_WARN, "TPIU Lost Sync (%d)" EOL, TPIUDecoderGetStats( &_r.t )->lostSync );
                ITMDecoderForceSync( &_r.i, false );
                break;

            // ------------------------------------
            case TPIU_EV_RXEDPACKET:
                if ( !TPIUGetPacket( &_r.t, &_r.p ) )
                {
                    genericsReport( V_WARN, "TPIUGetPacket fell over" EOL );
                }

                for ( uint32_t g = 0; g < _r.p.len; g++ )
                {
                    if ( _r.p.packet[g].s == options.tpiuITMChannel )
                    {
                        _itmPumpProcess( _r.p.packet[g].d );
                        continue;
                    }

                    if ( _r.p.packet[g].s != 0 )
                    {
                        genericsReport( V_DEBUG, "Unknown TPIU channel %02x" EOL, _r.p.packet[g].s );
                    }
                }

                break;

            // ------------------------------------
            case TPIU_EV_ERROR:
                genericsReport( V_WARN, "****ERROR****" EOL );
                break;
                // ------------------------------------
        }
    }
    else
    {
        /* There's no TPIU in use, so this goes straight to the ITM layer */
        _itmPumpProcess( c );
    }
}
// ====================================================================================================
void _printHelp( const char *const progName )

{
    genericsPrintf( "Usage: %s [options]" EOL, progName );
    genericsPrintf( "    -c, --cut-after:    <num> Cut screen output after number of lines" EOL );
    genericsPrintf( "    -D, --no-demangle:  Switch off C++ symbol demangling" EOL );
    genericsPrintf( "    -d, --del-prefix:   <DeleteMaterial> to take off front of filenames" EOL );
    genericsPrintf( "    -e, --elf-file:     <ElfFile> to use for symbols" EOL );
    genericsPrintf( "    -E, --exceptions:   Include exceptions in output report" EOL );
    genericsPrintf( "    -f, --input-file:   <filename> Take input from specified file" EOL );
    genericsPrintf( "    -g, --record-file:  <LogFile> append historic records to specified file" EOL );
    genericsPrintf( "    -h, --help:         This help" EOL );
    genericsPrintf( "    -I, --interval:     <interval> Display interval in milliseconds (defaults to %dms)" EOL, TOP_UPDATE_INTERVAL );
    genericsPrintf( "    -j, --json-file:    <filename> Output to file in JSON format (or screen if <filename> is '-')" EOL );
    genericsPrintf( "    -l, --agg-lines:    Aggregate per line rather than per function" EOL );
    genericsPrintf( "    -M, --no-colour:    Supress colour in output" EOL );
    genericsPrintf( "    -n, --itm-sync:     Enforce sync requirement for ITM (i.e. ITM needs to issue syncs)" EOL );
    genericsPrintf( "    -o, --output-file:  <filename> to be used for output live file" EOL );
    genericsPrintf( "    -O, --objdump-opts: <options> Options to pass directly to objdump" EOL );
    genericsPrintf( "    -r, --routines:     <routines> to record in live file (default %d routines)" EOL, options.maxRoutines );
    genericsPrintf( "    -R, --report-files: Report filenames as part of function discriminator" EOL );
    genericsPrintf( "    -s, --server:       <Server>:<Port> to use" EOL );
    genericsPrintf( "    -t, --tpiu:         <channel> Use TPIU decoder on specified channel" EOL );
    genericsPrintf( "    -v, --verbose:      <level> Verbose mode 0(errors)..3(debug)" EOL );
    genericsPrintf( "    -V, --version:      Print version and exit" EOL );
    genericsPrintf( EOL "Environment Variables;" EOL );
    genericsPrintf( "  OBJDUMP: to use non-standard obbdump binary" EOL );
}
// ====================================================================================================
void _printVersion( void )

{
    genericsPrintf( "orbtop version " GIT_DESCRIBE EOL );
}
// ====================================================================================================
static struct option _longOptions[] =
{
    {"cut-after", required_argument, NULL, 'c'},
    {"no-demangle", required_argument, NULL, 'D'},
    {"del-prefix", required_argument, NULL, 'd'},
    {"elf-file", required_argument, NULL, 'e'},
    {"exceptions", no_argument, NULL, 'E'},
    {"input-file", required_argument, NULL, 'f'},
    {"record-file", required_argument, NULL, 'g'},
    {"help", no_argument, NULL, 'h'},
    {"interval", required_argument, NULL, 'I'},
    {"json-file", required_argument, NULL, 'j'},
    {"agg-lines", no_argument, NULL, 'l'},
    {"itm-sync", no_argument, NULL, 'n'},
    {"no-colour", no_argument, NULL, 'M'},
    {"no-color", no_argument, NULL, 'M'},
    {"output-file", required_argument, NULL, 'o'},
    {"objdump-opts", required_argument, NULL, 'O'},
    {"routines", required_argument, NULL, 'r'},
    {"report-files", no_argument, NULL, 'R'},
    {"server", required_argument, NULL, 's'},
    {"tpiu", required_argument, NULL, 't'},
    {"verbose", required_argument, NULL, 'v'},
    {"version", no_argument, NULL, 'V'},
    {NULL, no_argument, NULL, 0}
};
// ====================================================================================================
bool _processOptions( int argc, char *argv[] )

{
    int c, optionIndex = 0;

    while ( ( c = getopt_long ( argc, argv, "c:d:DEe:f:g:hVI:j:lMnO:o:r:Rs:t:v:", _longOptions, &optionIndex ) ) != -1 )
        switch ( c )
        {
            // ------------------------------------
            case 'c':
                options.cutscreen = atoi( optarg );
                break;

            // ------------------------------------
            case 'e':
                options.elffile = optarg;
                break;

            // ------------------------------------
            case 'E':
                options.outputExceptions = true;
                break;

            // ------------------------------------
            case 'f':
                options.file = optarg;
                break;

            // ------------------------------------
            case 'g':
                options.logfile = optarg;
                break;

            // ------------------------------------
            case 'd':
                options.deleteMaterial = optarg;
                break;

            // ------------------------------------
            case 'D':
                options.demangle = false;
                break;

            // ------------------------------------
            case 'I':
                options.displayInterval = ( int64_t ) ( atof( optarg ) ) * 1000;
                break;

            // ------------------------------------
            case 'j':
                options.json = optarg;
                break;

            // ------------------------------------
            case 'l':
                options.lineDisaggregation = true;
                break;

            // ------------------------------------
            case 'r':
                options.maxRoutines = atoi( optarg );
                break;

            // ------------------------------------

            case 'M':
                options.mono = true;
                break;

            // ------------------------------------

            case 'n':
                options.forceITMSync = false;
                break;

            // ------------------------------------
            case 'o':
                options.outfile = optarg;
                break;

            // ------------------------------------

            case 'O':
                options.odoptions = optarg;
                break;

            // ------------------------------------
            case 'v':
                if ( !isdigit( *optarg ) )
                {
                    genericsReport( V_ERROR, "-v requires a numeric argument." EOL );
                    return false;
                }

                genericsSetReportLevel( atoi( optarg ) );
                break;

            // ------------------------------------
            case 't':
                options.useTPIU = true;
                options.tpiuITMChannel = atoi( optarg );
                break;

            // ------------------------------------
            case 'R':
                options.reportFilenames = true;
                break;

            // ------------------------------------
            case 's':
                options.server = optarg;

                // See if we have an optional port number too
                char *a = optarg;

                while ( ( *a ) && ( *a != ':' ) )
                {
                    a++;
                }

                if ( *a == ':' )
                {
                    *a = 0;
                    options.port = atoi( ++a );
                }

                if ( !options.port )
                {
                    options.port = NWCLIENT_SERVER_PORT;
                }

                break;

            // ------------------------------------
            case 'h':
                _printHelp( argv[0] );
                return ERR;

            // ------------------------------------
            case 'V':
                _printVersion();
                return -EINVAL;

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

                return -EINVAL;

            // ------------------------------------
            default:
                genericsReport( V_ERROR, "Unknown option %c" EOL, optopt );
                return -EINVAL;
                // ------------------------------------
        }

    if ( ( options.useTPIU ) && ( !options.tpiuITMChannel ) )
    {
        genericsReport( V_ERROR, "TPIU set for use but no channel set for ITM output" EOL );
        return -EINVAL;
    }

    if ( !options.elffile )
    {
        genericsReport( V_ERROR, "Elf File not specified" EOL );
        exit( -EBADF );
    }

    genericsReport( V_INFO, "orbtop version " GIT_DESCRIBE EOL );

    if ( options.file )
    {
        genericsReport( V_INFO, "Input File       : %s", options.file );
    }
    else
    {
        genericsReport( V_INFO, "Server           : %s:%d" EOL, options.server, options.port );
    }

    genericsReport( V_INFO, "Delete Mat       : %s" EOL, options.deleteMaterial ? options.deleteMaterial : "None" );
    genericsReport( V_INFO, "Elf File         : %s" EOL, options.elffile );
    genericsReport( V_INFO, "ForceSync        : %s" EOL, options.forceITMSync ? "true" : "false" );
    genericsReport( V_INFO, "C++ Demangle     : %s" EOL, options.demangle ? "true" : "false" );
    genericsReport( V_INFO, "Display Interval : %d mS" EOL, options.displayInterval / 1000 );
    genericsReport( V_INFO, "Log File         : %s" EOL, options.logfile ? options.logfile : "None" );
    genericsReport( V_INFO, "Objdump options  : %s" EOL, options.odoptions ? options.odoptions : "None" );

    if ( options.useTPIU )
    {
        genericsReport( V_INFO, "Using TPIU       : true (ITM on channel %d)" EOL, options.tpiuITMChannel );
    }
    else
    {
        genericsReport( V_INFO, "Using TPIU       : false" EOL );
    }

    return OK;
}
// ====================================================================================================

static struct Stream *_openStream()
{
    if ( options.file != NULL )
    {
        return streamCreateFile( options.file );
    }
    else
    {
        return streamCreateSocket( options.server, options.port );
    }
}

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Externally available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
int main( int argc, char *argv[] )

{
    uint8_t cbw[TRANSFER_SIZE];

    /* Output variables for interval report */
    uint32_t total;
    uint32_t reportLines = 0;
    struct reportLine *report;
    bool alreadyReported = false;

    int64_t remainTime;
    int64_t thisTime;
    struct timeval tv;
    enum ReceiveResult receiveResult = RECEIVE_RESULT_OK;
    size_t receivedSize = 0;
    enum symbolErr r;

    if ( OK != _processOptions( argc, argv ) )
    {
        exit( -EINVAL );
    }

    genericsScreenHandling( !options.mono );

    /* Check we've got _some_ symbols to start from */
    r = SymbolSetCreate( &_r.s, options.elffile, options.deleteMaterial, options.demangle, true, true, options.odoptions );

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

    genericsReport( V_WARN, "Loaded %s" EOL, options.elffile );

    /* Reset the TPIU handler before we start */
    TPIUDecoderInit( &_r.t );
    ITMDecoderInit( &_r.i, options.forceITMSync );
    MSGSeqInit( &_r.d, &_r.i, MSG_REORDER_BUFLEN );

    /* First interval will be from startup to first packet arriving */
    _r.lastReportus = _timestamp();
    _r.currentException = NO_EXCEPTION;

    /* Open file for JSON output if we have one */
    if ( options.json )
    {
        if ( options.json[0] == '-' )
        {
            _r.jsonfile = stdout;
        }
        else
        {
            _r.jsonfile = fopen( options.json, "w" );

            if ( !_r.jsonfile )
            {
                perror( "Couldn't open json output file" );
                return -ENOENT;
            }
        }
    }

    while ( 1 )
    {
        struct Stream *stream = _openStream();

        if ( stream == NULL )
        {
            if ( !alreadyReported )
            {
                genericsReport( V_ERROR, "No connection" EOL );
                alreadyReported = true;
            }

            usleep( 500 * 1000 );
            continue;
        }

        alreadyReported = false;

        if ( ( !options.json ) || ( options.json[0] != '-' ) )
        {
            genericsPrintf( CLEAR_SCREEN "Connected..." EOL );
        }

        /* ...just in case we have any readings from a previous incantation */
        _flushHash( );

        _r.lastReportus = _timestamp();

        while ( 1 )
        {
            thisTime = _timestamp();
            remainTime = ( ( _r.lastReportus + options.displayInterval - thisTime ) ) + 500;

            if ( remainTime > 0 )
            {
                tv.tv_sec = remainTime / 1000000;
                tv.tv_usec  = remainTime % 1000000;
                receiveResult = stream->receive( stream, cbw, TRANSFER_SIZE, &tv, &receivedSize );
            }
            else
            {
                receiveResult = RECEIVE_RESULT_OK;
                receivedSize = 0;
            }

            if ( receiveResult == RECEIVE_RESULT_ERROR )
            {
                /* Something went wrong in the receive */
                break;
            }

            if ( receiveResult == RECEIVE_RESULT_EOF )
            {
                /* We are at EOF, hopefully next loop will get more data. */
            }

            /* Check to make sure our symbols are still appropriate */
            if ( !SymbolSetValid( &_r.s, options.elffile ) )
            {
                /* Make sure old references are invalidated */
                _flushHash();

                r = SymbolSetCreate( &_r.s, options.elffile, options.deleteMaterial, options.demangle, true, true, options.odoptions );

                switch ( r )
                {
                    case SYMBOL_NOELF:
                        genericsReport( V_WARN, "Elf file or symbols in it not found" EOL );
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

                if ( SYMBOL_NOELF == r )
                {
                    usleep( 1000000L );
                    continue;
                }

                genericsReport( V_WARN, "Loaded %s" EOL, options.elffile );
            }

            /* Pump all of the data through the protocol handler */
            uint8_t *c = cbw;

            while ( receivedSize > 0 )
            {
                _protocolPump( *c++ );
                receivedSize--;
            }

            /* See if its time to post-process it */
            if ( receiveResult == RECEIVE_RESULT_TIMEOUT || remainTime <= 0 )
            {
                /* Create the report that we will output */
                total = _consolodateReport( &report, &reportLines );

                if ( options.json )
                {
                    _outputJson( _r.jsonfile, total, reportLines, report, thisTime );
                }

                if ( ( !options.json ) || ( options.json[0] != '-' ) )
                {
                    _outputTop( total, reportLines, report, thisTime );
                }

                /* ... and we are done with the report now, get rid of it */
                free( report );

                /* ...and zero the exception records */
                for ( uint32_t e = 0; e < MAX_EXCEPTIONS; e++ )
                {
                    _r.er[e].visits = _r.er[e].maxDepth = _r.er[e].totalTime = _r.er[e].minTime = _r.er[e].maxTime = _r.er[e].maxWallTime = 0;
                }

                /* It's safe to update these here because the ticks won't be updated until more
                 * records arrive. */
                if ( _r.ITMoverflows != ITMDecoderGetStats( &_r.i )->overflow )
                {
                    /* We had an overflow, so can't safely track max depth ... reset it */
                    _r.erDepth = 0;
                }

                _r.ITMoverflows = ITMDecoderGetStats( &_r.i )->overflow;
                _r.SWPkt = ITMDecoderGetStats( &_r.i )->SWPkt;
                _r.TSPkt = ITMDecoderGetStats( &_r.i )->TSPkt;
                _r.HWPkt = ITMDecoderGetStats( &_r.i )->HWPkt;
                _r.lastReportus =  thisTime;
                _r.lastReportTicks = _r.timeStamp;

                /* Check to make sure there's not an unexpected TPIU in here */
                if ( ITMDecoderGetStats( &_r.i )->tpiuSyncCount )
                {
                    genericsReport( V_WARN, "Got a TPIU sync while decoding ITM...did you miss a -t option?" EOL );
                    ITMDecoderGetStats( &_r.i )->tpiuSyncCount = 0;
                }
            }
        }

        stream->close( stream );
        free( stream );
    }

    if ( ( !ITMDecoderGetStats( &_r.i )->tpiuSyncCount ) )
    {
        genericsReport( V_ERROR, "Read failed" EOL );
    }

    return -ESRCH;
}
// ====================================================================================================
