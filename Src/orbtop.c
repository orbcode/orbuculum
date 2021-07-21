/*
 * SWO Top for Blackmagic Probe and TTL Serial Interfaces
 * ======================================================
 *
 * Copyright (C) 2017, 2019  Dave Marples  <dave@marples.net>
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
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#ifdef OSX
    #include "osxelf.h"
#else
    #include <elf.h>
#endif

#include <demangle.h>
#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <limits.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "bfd_wrapper.h"

#include "cJSON.h"
#include "generics.h"
#include "uthash.h"
#include "git_version_info.h"
#include "generics.h"
#include "tpiuDecoder.h"
#include "itmDecoder.h"
#include "symbols.h"
#include "msgSeq.h"

#define CUTOFF              (10)             /* Default cutoff at 0.1% */
#define SERVER_PORT         (3443)           /* Server port definition */
#define TRANSFER_SIZE       (4096)           /* Maximum packet we might receive */
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
    uint32_t maxDepth;

    /* Elements used in calcuation */
    int64_t entryTime;
    int64_t thisTime;
    uint32_t prev;
};


/* ---------- CONFIGURATION ----------------- */
struct                                       /* Record for options, either defaults or from command line */
{
    bool useTPIU;                            /* Are we decoding via the TPIU? */
    bool reportFilenames;                    /* Report filenames for each routine? -- not presented via UI, intended for debug */
    bool outputExceptions;                   /* Set to include exceptions in output flow */
    uint32_t tpiuITMChannel;                 /* What channel? */
    bool forceITMSync;                       /* Must ITM start synced? */
    char *file;                              /* File host connection */

    uint32_t hwOutputs;                      /* What hardware outputs are enabled */

    char *deleteMaterial;                    /* Material to delete off filenames for target */

    char *elffile;                           /* Target program config */

    char *json;                              /* Output in JSON format rather than human readable, either '-' for screen or filename */
    char *outfile;                           /* File to output current information */
    char *logfile;                           /* File to output historic information */

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
    .displayInterval = TOP_UPDATE_INTERVAL,
    .port = SERVER_PORT,
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

    int64_t lastReportmS;                              /* Last time an output report was generated, in milliseconds */
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
    int64_t milliseconds = te.tv_sec * 1000LL + ( te.tv_usec + 500 ) / 1000; // caculate milliseconds
    return milliseconds;
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

    if ( ( ( ( struct visitedAddr * )a )->n->filename ) &&   ( ( ( struct visitedAddr * )b )->n->filename ) )
    {
        r = strcmp( ( ( struct visitedAddr * )a )->n->filename, ( ( struct visitedAddr * )b )->n->filename );

        if ( r )
        {
            return r;
        }
    }


    r = strcmp( ( ( struct visitedAddr * )a )->n->function, ( ( struct visitedAddr * )b )->n->function ) ;

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
    _r.er[_r.currentException].thisTime += ts - _r.er[_r.currentException].entryTime;
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

    if ( _r.erDepth > _r.er[_r.currentException].maxDepth )
    {
        _r.er[_r.currentException].maxDepth = _r.erDepth;
    }

    /* Step out of this exception */
    _r.currentException = _r.er[_r.currentException].prev;
    _r.erDepth--;

    /* If we are still in an exception then carry on accounting */
    if ( _r.currentException != NO_EXCEPTION )
    {
        _r.er[_r.currentException].entryTime = ts;
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
            _r.erDepth++;
            break;

        case EXEVENT_RESUME: /* Unwind all levels of exception (deals with tail chaining) */
            while ( ( _r.currentException != NO_EXCEPTION ) && ( _r.erDepth ) )
            {
                _exitEx( _r.timeStamp );
            }

            _r.currentException = NO_EXCEPTION;
            break;

        case EXEVENT_EXIT: /* Exit single level of exception */
            _exitEx( _r.timeStamp );
            break;

        default:
        case EXEVENT_UNKNOWN:
            genericsReport( V_ERROR, "Unrecognised exception event (%d,%d)" EOL, m->eventType, m->exceptionNumber );
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
                ( strcmp( report[reportLines - 1].n->filename, a->n->filename ) ) ||
                ( strcmp( report[reportLines - 1].n->function, a->n->function ) ) ||
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

    uint32_t addr = SLEEPING;
    HASH_FIND_INT( _r.addresses, &addr, a );

    if ( a )
    {
        n = a->n;
    }
    else
    {
        n = ( struct nameEntry * )malloc( sizeof( struct nameEntry ) );
    }

    n->filename = "";
    n->function = "** SLEEPING **";
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
    jsonElement = cJSON_CreateNumber( timeStamp - _r.lastReportmS );
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

            if ( ( options.demangle ) && ( !options.reportFilenames ) )
            {
                d = cplus_demangle( report[n].n->function, DMGL_AUTO );
            }

            /* Output in JSON Format */
            jsonTableEntry = cJSON_CreateObject();
            assert( jsonTableEntry );

            jsonElement = cJSON_CreateNumber( report[n].count );
            assert( jsonElement );
            cJSON_AddItemToObject( jsonTableEntry, "count", jsonElement );
            jsonElement = cJSON_CreateString( report[n].n->filename ? report[n].n->filename : "" );
            assert( jsonElement );
            cJSON_AddItemToObject( jsonTableEntry, "filename", jsonElement );
            jsonElement = cJSON_CreateString(  d ? d : report[n].n->function );
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
        }
    }

    /* Close off JSON report - if you want your printing pretty then use the first line */
    //opString=cJSON_Print(jsonStore);

    opString = cJSON_PrintUnformatted( jsonStore );
    cJSON_Delete( jsonStore );
    fprintf( f, "%s" EOL, opString );
    free( opString );
}

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

    fprintf( stdout, CLEAR_SCREEN );

    if ( total )
    {
        for ( uint32_t n = 0; n < reportLines; n++ )
        {
            percentage = ( report[n].count * 10000 ) / total;
            samples += report[n].count;

            if ( report[n].count )
            {
                char *d = NULL;

                if ( ( options.demangle ) && ( !options.reportFilenames ) )
                {
                    d = cplus_demangle( report[n].n->function, DMGL_AUTO );
                }

                if ( ( percentage >= CUTOFF ) && ( ( !options.cutscreen ) || ( n < options.cutscreen ) ) )
                {
                    dispSamples += report[n].count;
                    totPercent += percentage;

                    fprintf( stdout, C_DATA "%3d.%02d%% " C_SUPPORT " %7" PRIu64 " ", percentage / 100, percentage % 100, report[n].count );


                    if ( ( options.reportFilenames ) && ( report[n].n->filename ) )
                    {
                        fprintf( stdout, C_CONTEXT "%s" C_RESET "::", report[n].n->filename );
                    }

                    if ( ( options.lineDisaggregation ) && ( report[n].n->line ) )
                    {
                        fprintf( stdout, C_SUPPORT2 "%s" C_RESET "::" C_CONTEXT "%d" EOL, d ? d : report[n].n->function, report[n].n->line );
                    }
                    else
                    {
                        fprintf( stdout, C_SUPPORT2 "%s" C_RESET EOL, d ? d : report[n].n->function );
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
                            fprintf( p, "%s,%3d.%02d" EOL, d ? d : report[n].n->function, percentage / 100, percentage % 100 );
                        }

                        if ( q )
                        {
                            fprintf( q, "%s,%3d.%02d" EOL, d ? d : report[n].n->function, percentage / 100, percentage % 100 );
                        }
                    }
                    else
                    {
                        if ( ( p ) && ( n < options.maxRoutines ) )
                        {
                            fprintf( p, "%s::%d,%3d.%02d" EOL, d ? d : report[n].n->function, report[n].n->line, percentage / 100, percentage % 100 );
                        }

                        if ( q )
                        {
                            fprintf( q, "%s::%d,%3d.%02d" EOL, d ? d : report[n].n->function, report[n].n->line, percentage / 100, percentage % 100 );
                        }
                    }


                }
            }
        }
    }

    fprintf( stdout, C_RESET "-----------------" EOL );

    fprintf( stdout, C_DATA "%3d.%02d%% " C_SUPPORT " %7" PRIu64 " " C_RESET "of "C_DATA" %" PRIu64 " "C_RESET" Samples" EOL, totPercent / 100, totPercent % 100, dispSamples, samples );

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
            fprintf( stdout, EOL );
        }

        fprintf( stdout, EOL " Ex |   Count  |  MaxD | TotalTicks  |  AveTicks  |  minTicks  |  maxTicks " EOL );
        fprintf( stdout, "----+----------+-------+-------------+------------+------------+------------" EOL );

        for ( uint32_t e = 0; e < MAX_EXCEPTIONS; e++ )
        {

            if ( _r.er[e].visits )
            {
                fprintf( stdout, C_DATA "%3" PRIu32 C_RESET " | " C_DATA "%8" PRIu64 C_RESET " |" C_DATA " %5"
                         PRIu32 C_RESET " | "C_DATA " %9" PRIu64 C_RESET "  |  " C_DATA "%9" PRIu64 C_RESET " | " C_DATA "%9" PRIu64 C_RESET "  | " C_DATA" %9" PRIu64 C_RESET EOL,
                         e, _r.er[e].visits, _r.er[e].maxDepth, _r.er[e].totalTime, _r.er[e].totalTime / _r.er[e].visits, _r.er[e].minTime, _r.er[e].maxTime );
            }
        }
    }

    fprintf( stdout, EOL C_RESET "[%s%s%s%s" C_RESET "] ",
             ( _r.ITMoverflows != ITMDecoderGetStats( &_r.i )->overflow ) ? C_OVF_IND "V" : C_RESET "-",
             ( _r.SWPkt != ITMDecoderGetStats( &_r.i )->SWPkt ) ? C_SOFT_IND "S" : C_RESET "-",
             ( _r.TSPkt != ITMDecoderGetStats( &_r.i )->TSPkt ) ? C_TSTAMP_IND "T" : C_RESET "-",
             ( _r.HWPkt != ITMDecoderGetStats( &_r.i )->HWPkt ) ? C_HW_IND "H" : C_RESET "-" );

    if ( _r.lastReportTicks )
        fprintf( stdout, "Interval = " C_DATA "%" PRIu64 "mS " C_RESET "/ "C_DATA "%" PRIu64 C_RESET " (~" C_DATA "%" PRIu64 C_RESET " Ticks/mS)" EOL,
                 lastTime - _r.lastReportmS, _r.timeStamp - _r.lastReportTicks, ( _r.timeStamp - _r.lastReportTicks ) / ( lastTime - _r.lastReportmS ) );
    else
    {
        fprintf( stdout, C_RESET "Interval = " C_DATA "%" PRIu64 C_RESET "mS" EOL, lastTime - _r.lastReportmS );
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
            SymbolLookup( _r.s, m->pc, &n, options.deleteMaterial );

            /* This is a new entry - record it */

            a = ( struct visitedAddr * )calloc( 1, sizeof( struct visitedAddr ) );
            a->visits = 1;

            a->n = ( struct nameEntry * )malloc( sizeof( struct nameEntry ) );
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
                        genericsReport( V_WARN, "Unknown TPIU channel %02x" EOL, _r.p.packet[g].s );
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
void _printHelp( char *progName )

{
    fprintf( stdout, "Usage: %s [options]" EOL, progName );
    fprintf( stdout, "       -c: <num> Cut screen output after number of lines" EOL );
    fprintf( stdout, "       -d: <DeleteMaterial> to take off front of filenames" EOL );
    fprintf( stdout, "       -D: Switch off C++ symbol demangling" EOL );
    fprintf( stdout, "       -e: <ElfFile> to use for symbols" EOL );
    fprintf( stdout, "       -E: Include exceptions in output report" EOL );
    fprintf( stdout, "       -f: <filename> Take input from specified file" EOL );
    fprintf( stdout, "       -g: <LogFile> append historic records to specified file" EOL );
    fprintf( stdout, "       -h: This help" EOL );
    fprintf( stdout, "       -I: <interval> Display interval in milliseconds (defaults to %d mS)" EOL, TOP_UPDATE_INTERVAL );
    fprintf( stdout, "       -j: <filename> Output to file in JSON format (or screen if <filename> is '-')" EOL );
    fprintf( stdout, "       -l: Aggregate per line rather than per function" EOL );
    fprintf( stdout, "       -n: Enforce sync requirement for ITM (i.e. ITM needs to issue syncs)" EOL );
    fprintf( stdout, "       -o: <filename> to be used for output live file" EOL );
    fprintf( stdout, "       -r: <routines> to record in live file (default %d routines)" EOL, options.maxRoutines );
    fprintf( stdout, "       -s: <Server>:<Port> to use" EOL );
    fprintf( stdout, "       -t: <channel> Use TPIU decoder on specified channel" EOL );
    fprintf( stdout, "       -v: <level> Verbose mode 0(errors)..3(debug)" EOL );
}
// ====================================================================================================
int _processOptions( int argc, char *argv[] )

{
    int c;

    while ( ( c = getopt ( argc, argv, "c:d:DEe:f:g:hI:j:lm:no:r:s:t:v:" ) ) != -1 )
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
                options.displayInterval = ( int64_t ) ( atof( optarg ) );
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

            case 'n':
                options.forceITMSync = false;
                break;

            // ------------------------------------
            case 'o':
                options.outfile = optarg;
                break;

            // ------------------------------------
            case 'v':
                genericsSetReportLevel( atoi( optarg ) );
                break;

            // ------------------------------------
            case 't':
                options.useTPIU = true;
                options.tpiuITMChannel = atoi( optarg );
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
                    options.port = SERVER_PORT;
                }

                break;

            // ------------------------------------
            case 'h':
                _printHelp( argv[0] );
                return ERR;

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

    genericsReport( V_INFO, "orbtop V" VERSION " (Git %08X %s, Built " BUILD_DATE ")" EOL, GIT_HASH, ( GIT_DIRTY ? "Dirty" : "Clean" ) );

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
    genericsReport( V_INFO, "Display Interval : %d mS" EOL, options.displayInterval );
    genericsReport( V_INFO, "Log File         : %s" EOL, options.logfile ? options.logfile : "None" );

    if ( options.useTPIU )
    {
        genericsReport( V_INFO, "Using TPIU  : true (ITM on channel %d)" EOL, options.tpiuITMChannel );
    }

    return OK;
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
    int sourcefd;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    uint8_t cbw[TRANSFER_SIZE];
    int64_t lastTime;

    /* Output variables for interval report */
    uint32_t total;
    uint32_t reportLines = 0;
    struct reportLine *report;

    ssize_t t;
    int flag = 1;
    int r;
    int64_t remainTime;
    struct timeval tv;
    fd_set readfds;

    /* Fill in a time to start from */
    lastTime = _timestamp();

    if ( OK != _processOptions( argc, argv ) )
    {
        exit( -EINVAL );
    }

    /* Reset the TPIU handler before we start */
    TPIUDecoderInit( &_r.t );
    ITMDecoderInit( &_r.i, options.forceITMSync );
    MSGSeqInit( &_r.d, &_r.i, MSG_REORDER_BUFLEN );

    /* First interval will be from startup to first packet arriving */
    _r.lastReportmS = _timestamp();
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
        if ( !options.file )
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
            server = gethostbyname( options.server );

            if ( !server )
            {
                perror( "Cannot find host" );
                return -EIO;
            }

            serv_addr.sin_family = AF_INET;
            bcopy( ( char * )server->h_addr,
                   ( char * )&serv_addr.sin_addr.s_addr,
                   server->h_length );
            serv_addr.sin_port = htons( options.port );

            if ( connect( sourcefd, ( struct sockaddr * ) &serv_addr, sizeof( serv_addr ) ) < 0 )
            {
                if ( ( !options.json ) || ( options.json[0] != '-' ) )
                {
                    fprintf( stdout, CLEAR_SCREEN EOL );
                }

                perror( "Could not connect" );
                close( sourcefd );
                usleep( 1000000 );
                continue;
            }
        }
        else
        {
            if ( ( sourcefd = open( options.file, O_RDONLY ) ) < 0 )
            {
                genericsExit( sourcefd, "Can't open file %s" EOL, options.file );
            }

        }

        if ( ( !options.json ) || ( options.json[0] != '-' ) )
        {
            fprintf( stdout, CLEAR_SCREEN "Connected..." EOL );
        }

        /* ...just in case we have any readings from a previous incantation */
        _flushHash( );

        lastTime = _timestamp();

        while ( 1 )
        {
            remainTime = ( ( lastTime + options.displayInterval - _timestamp() ) * 1000 ) - 500;
            r = t = 0;

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
                t = read( sourcefd, cbw, TRANSFER_SIZE );

                if ( t <= 0 )
                {
                    /* We are at EOF (Probably the descriptor closed) */
                    break;
                }
            }

            if ( !SymbolSetValid( &_r.s, options.elffile ) )
            {
                /* Make sure old references are invalidated */
                _flushHash();

                if ( !SymbolSetLoad( &_r.s, options.elffile ) )
                {
                    genericsReport( V_ERROR, "Elf file or symbols in it not found" EOL );
                    usleep( 1000000 );
                    break;
                }
                else
                {
                    genericsReport( V_WARN, "Loaded %s" EOL, options.elffile );
                }
            }

            /* Pump all of the data through the protocol handler */
            uint8_t *c = cbw;

            while ( t-- )
            {
                _protocolPump( *c++ );
            }

            /* See if its time to post-process it */
            if ( r <= 0 )
            {
                /* Create the report that we will output */
                total = _consolodateReport( &report, &reportLines );

                lastTime = _timestamp();

                if ( options.json )
                {
                    _outputJson( _r.jsonfile, total, reportLines, report, lastTime );
                }

                if ( ( !options.json ) || ( options.json[0] != '-' ) )
                {
                    _outputTop( total, reportLines, report, lastTime );
                }

                /* ... and we are done with the report now, get rid of it */
                free( report );

                /* ...and zero the exception records */
                for ( uint32_t e = 0; e < MAX_EXCEPTIONS; e++ )
                {
                    _r.er[e].visits = _r.er[e].maxDepth = _r.er[e].totalTime = _r.er[e].minTime = _r.er[e].maxTime = 0;
                }

                /* It's safe to update these here because the ticks won't be updated until more
                 * records arrive. */
                _r.ITMoverflows = ITMDecoderGetStats( &_r.i )->overflow;
                _r.SWPkt = ITMDecoderGetStats( &_r.i )->SWPkt;
                _r.TSPkt = ITMDecoderGetStats( &_r.i )->TSPkt;
                _r.HWPkt = ITMDecoderGetStats( &_r.i )->HWPkt;
                _r.lastReportmS = lastTime;
                _r.lastReportTicks = _r.timeStamp;

                /* Check to make sure there's not an unexpected TPIU in here */
                if ( ITMDecoderGetStats( &_r.i )->tpiuSyncCount )
                {
                    genericsReport( V_WARN, "Got a TPIU sync while decoding ITM...did you miss a -t option?" EOL );
                }
            }
        }

        close( sourcefd );
    }

    if ( ( !ITMDecoderGetStats( &_r.i )->tpiuSyncCount ) )
    {
        genericsReport( V_ERROR, "Read failed" EOL );
    }

    return -ESRCH;
}
// ====================================================================================================
