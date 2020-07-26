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
#include <elf.h>
#include <demangle.h>
#include <assert.h>
#include <inttypes.h>

#include "bfd_wrapper.h"

#if defined OSX
    #include <sys/ioctl.h>
    #include <libusb.h>
    #include <termios.h>
#else
    #if defined LINUX
        #include <libusb-1.0/libusb.h>
        #include <asm/ioctls.h>
        #if defined TCGETS2
            #include <asm/termios.h>
            /* Manual declaration to avoid conflict. */
            extern int ioctl ( int __fd, unsigned long int __request, ... ) __THROW;
        #else
            #include <sys/ioctl.h>
            #include <termios.h>
        #endif
    #else
        #error "Unknown OS"
    #endif
#endif
#include <stdint.h>
#include <limits.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "cJSON.h"
#include "generics.h"
#include "uthash.h"
#include "git_version_info.h"
#include "generics.h"
#include "tpiuDecoder.h"
#include "itmDecoder.h"
#include "symbols.h"
#include "itmSeq.h"

#define CUTOFF              (10)             /* Default cutoff at 0.1% */
#define SERVER_PORT         (3443)           /* Server port definition */
#define TRANSFER_SIZE       (4096)           /* Maximum packet we might receive */
#define TOP_UPDATE_INTERVAL (1000)           /* Interval between each on screen update */

#define MAX_EXCEPTIONS      (512)            /* Maximum number of exceptions to be considered */
#define NO_EXCEPTION        (0xFFFFFFFF)     /* Flag indicating no exception is being processed */

#define ITM_REORDER_BUFLEN  (10)             /* Maximum number of samples to re-order for timekeeping */

#define CLEAR_SCREEN        "\033[2J\033[;H" /* ASCII Sequence for clear screen */

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
    int speed;                               /* Speed (for case of a serial link) */
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
    struct ITMSeq    d;
    struct ITMPacket h;
    struct TPIUDecoder t;
    struct TPIUPacket p;

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
#if defined(LINUX) && defined (TCGETS2)
static int _setSerialConfig ( int f, speed_t speed )
{
    // Use Linux specific termios2.
    struct termios2 settings;
    int ret = ioctl( f, TCGETS2, &settings );

    if ( ret < 0 )
    {
        return ( -3 );
    }

    settings.c_iflag &= ~( ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF );
    settings.c_lflag &= ~( ICANON | ECHO | ECHOE | ISIG );
    settings.c_cflag &= ~PARENB; /* no parity */
    settings.c_cflag &= ~CSTOPB; /* 1 stop bit */
    settings.c_cflag &= ~CSIZE;
    settings.c_cflag &= ~( CBAUD | CIBAUD );
    settings.c_cflag |= CS8 | CLOCAL; /* 8 bits */
    settings.c_oflag &= ~OPOST; /* raw output */


    const unsigned int speed1[] =
    {
        B115200, B230400, 0, B460800, B576000,
        0, 0, B921600, 0, B1152000
    };
    const unsigned int speed2[] =
    {
        B500000,  B1000000, B1500000, B2000000,
        B2500000, B3000000, B3500000, B4000000
    };
    int speed_ok = 0;

    if ( ( speed % 500000 ) == 0 )
    {
        // speed is multiple of 500000, use speed2 table.
        int i = speed / 500000;

        if ( i <= 8 )
        {
            speed_ok = speed2[i - 1];
        }
    }
    else if ( ( speed % 115200 ) == 0 )
    {
        int i = speed / 115200;

        if ( i <= 10 && speed1[i - 1] )
        {
            speed_ok = speed2[i - 1];
        }
    }

    if ( speed_ok )
    {
        settings.c_cflag |= speed_ok;
    }
    else
    {
        settings.c_cflag |= BOTHER;
        settings.c_ispeed = speed;
        settings.c_ospeed = speed;
    }

    // Ensure input baud is same than output.
    settings.c_cflag |= ( settings.c_cflag & CBAUD ) << IBSHIFT;
    // Now configure port.
    ret = ioctl( f, TCSETS2, &settings );

    if ( ret < 0 )
    {
        genericsReport( V_ERROR, "Unsupported baudrate" EOL );
        return ( -3 );
    }

    // Check configuration is ok.
    ret = ioctl( f, TCGETS2, &settings );

    if ( ret < 0 )
    {
        return ( -3 );
    }

    if ( speed_ok )
    {
        if ( ( settings.c_cflag & CBAUD ) != speed_ok )
        {
            genericsReport( V_WARN, "Fail to set baudrate" EOL );
        }
    }
    else
    {
        if ( ( settings.c_ispeed != speed ) || ( settings.c_ospeed != speed ) )
        {
            genericsReport( V_WARN, "Fail to set baudrate" EOL );
        }
    }

    // Flush port.
    ioctl( f, TCFLSH, TCIOFLUSH );
    return 0;
}
#else
static int _setSerialConfig ( int f, speed_t speed )
{
    struct termios settings;

    if ( tcgetattr( f, &settings ) < 0 )
    {
        perror( "tcgetattr" );
        return ( -3 );
    }

    if ( cfsetspeed( &settings, speed ) < 0 )
    {
        genericsReport( V_ERROR, "Error Setting input speed" EOL );
        return ( -3 );
    }

    settings.c_iflag &= ~( ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF );
    settings.c_lflag &= ~( ICANON | ECHO | ECHOE | ISIG );
    settings.c_cflag &= ~PARENB; /* no parity */
    settings.c_cflag &= ~CSTOPB; /* 1 stop bit */
    settings.c_cflag &= ~CSIZE;
    settings.c_cflag |= CS8 | CLOCAL; /* 8 bits */
    settings.c_oflag &= ~OPOST; /* raw output */

    if ( tcsetattr( f, TCSANOW, &settings ) < 0 )
    {
        genericsReport( V_ERROR, "Unsupported baudrate" EOL );
        return ( -3 );
    }

    tcflush( f, TCOFLUSH );
    return 0;
}
#endif
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
void _handleException( struct ITMDecoder *i, struct ITMPacket *p )

{
    uint64_t ts = i->timeStamp;
    uint32_t exceptionNumber = ( ( p->d[1] & 0x01 ) << 8 ) | p->d[0];
    uint32_t eventType = p->d[1] >> 4;

    assert( exceptionNumber < MAX_EXCEPTIONS );

    switch ( eventType )
    {
        case EXEVENT_ENTER:
            if ( _r.er[exceptionNumber].entryTime != 0 )
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
                _r.er[_r.currentException].thisTime += ts - _r.er[_r.currentException].entryTime;
            }

            /* Record however we got to this exception */
            _r.er[exceptionNumber].prev = _r.currentException;

            /* Now dip into this exception */
            _r.currentException = exceptionNumber;
            _r.er[exceptionNumber].entryTime = ts;
            _r.er[exceptionNumber].thisTime = 0;
            _r.erDepth++;
            break;

        case EXEVENT_RESUME: /* Unwind all levels of exception (deals with tail chaining) */
            while ( ( _r.currentException != NO_EXCEPTION ) && ( _r.erDepth ) )
            {
                _exitEx( ts );
            }

            _r.currentException = NO_EXCEPTION;
            break;

        case EXEVENT_EXIT: /* Exit single level of exception */
            _exitEx( ts );
            break;

        default:
        case EXEVENT_UNKNOWN:
            genericsReport( V_ERROR, "Unrecognised exception event" EOL );
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

                    fprintf( stdout, "%3d.%02d%% %8ld ", percentage / 100, percentage % 100, report[n].count );


                    if ( ( options.reportFilenames ) && ( report[n].n->filename ) )
                    {
                        fprintf( stdout, "%s::", report[n].n->filename );
                    }

                    if ( ( options.lineDisaggregation ) && ( report[n].n->line ) )
                    {
                        fprintf( stdout, "%s::%d" EOL, d ? d : report[n].n->function, report[n].n->line );
                    }
                    else
                    {
                        fprintf( stdout, "%s" EOL, d ? d : report[n].n->function );
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

    fprintf( stdout, "-----------------" EOL );

    fprintf( stdout, "%3d.%02d%% %8ld of %ld Samples" EOL, totPercent / 100, totPercent % 100, dispSamples, samples );

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
                fprintf( stdout, "%3" PRIu32 " | %8" PRIu64 " | %5" PRIu32 " |  %9" PRIu64 "  |  %9" PRIu64 " | %9" PRIu64 "  | %9" PRIu64 EOL,
                         e, _r.er[e].visits, _r.er[e].maxDepth, _r.er[e].totalTime, _r.er[e].totalTime / _r.er[e].visits, _r.er[e].minTime, _r.er[e].maxTime );
            }
        }
    }

    fprintf( stdout, EOL "[%c%c%c%c] ",
             ( _r.ITMoverflows != ITMDecoderGetStats( &_r.i )->overflow ) ? 'V' : '-',
             ( _r.SWPkt != ITMDecoderGetStats( &_r.i )->SWPkt ) ? 'S' : '-',
             ( _r.TSPkt != ITMDecoderGetStats( &_r.i )->TSPkt ) ? 'T' : '-',
             ( _r.HWPkt != ITMDecoderGetStats( &_r.i )->HWPkt ) ? 'H' : '-' );

    if ( _r.lastReportTicks )
        fprintf( stdout, "Interval = %" PRIu64 "mS / %" PRIu64 " (~%" PRIu64 " Ticks/mS)" EOL, lastTime - _r.lastReportmS, _r.i.timeStamp - _r.lastReportTicks,
                 ( _r.i.timeStamp - _r.lastReportTicks ) / ( lastTime - _r.lastReportmS ) );
    else
    {
        fprintf( stdout, "Interval = %" PRIu64 "mS" EOL, lastTime - _r.lastReportmS );
    }

    genericsReport( V_INFO, "         Ovf=%3d  ITMSync=%3d TPIUSync=%3d ITMErrors=%3d" EOL,
                    ITMDecoderGetStats( &_r.i )->overflow,
                    ITMDecoderGetStats( &_r.i )->syncCount,
                    TPIUDecoderGetStats( &_r.t )->syncCount,
                    ITMDecoderGetStats( &_r.i )->ErrorPkt );

}

// ====================================================================================================
void _handlePCSample( struct ITMDecoder *i, struct ITMPacket *p )

{
    struct visitedAddr *a;
    uint32_t pc;

    if ( p->len == 1 )
    {
        /* This is a sleep packet */
        _r.sleeps++;
    }
    else
    {
        pc = ( p->d[3] << 24 ) | ( p->d[2] << 16 ) | ( p->d[1] << 8 ) | ( p->d[0] );

        HASH_FIND_INT( _r.addresses, &pc, a );

        if ( a )
        {
            a->visits++;
        }
        else
        {
            struct nameEntry n;

            /* Find a matching name record if there is one */
            SymbolLookup( _r.s, pc, &n, options.deleteMaterial );

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
void _handleHW( struct ITMDecoder *i, struct ITMPacket *p )

{
    switch ( p->srcAddr )
    {
        // --------------
        case 0: /* DWT Event */
            break;

        // --------------
        case 1: /* Exception */
            _handleException( i, p );
            break;

        // --------------
        case 2: /* PC Counter Sample */
            _handlePCSample( i, p );
            break;

        // --------------
        default:
            break;
            // --------------
    }
}
// ====================================================================================================
// Pump characters into the itm decoder
// ====================================================================================================
void _itmPumpProcess( uint8_t c )

{
    struct ITMPacket *p;

    if ( !ITMSeqPump( &_r.d, c ) )
    {
        return;
    }

    /* We are synced timewise, so empty anything that has been waiting */
    while ( 1 )
    {
        p = ITMSeqGetPacket( &_r.d );

        if ( !p )
        {
            break;
        }

        switch ( p->type )
        {
            case ITM_PT_SW:
                _handleSW( &_r.i, p );
                break;

            case ITM_PT_HW:
                _handleHW( &_r.i, p );
                break;

            default:
                genericsReport( V_WARN, "Unrecognised packet in buffer type %d" EOL, p->type );
                break;
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
    fprintf( stdout, "Usage: %s <htv> <-e ElfFile> <-g filename> <-o filename> -r <routines> <-i channel> <-p port> <-s server>" EOL, progName );
    fprintf( stdout, "        a: <serialSpeed> to use" EOL );
    fprintf( stdout, "        c: <num> Cut screen output after number of lines" EOL );
    fprintf( stdout, "        d: <DeleteMaterial> to take off front of filenames" EOL );
    fprintf( stdout, "        D: Switch off C++ symbol demangling" EOL );
    fprintf( stdout, "        e: <ElfFile> to use for symbols" EOL );
    fprintf( stdout, "        E: Include exceptions in output report" EOL );
    fprintf( stdout, "        f: <filename> Take input from specified file" EOL );
    fprintf( stdout, "        g: <LogFile> append historic records to specified file" EOL );
    fprintf( stdout, "        h: This help" EOL );
    fprintf( stdout, "        i: <channel> Set ITM Channel in TPIU decode (defaults to 1)" EOL );
    fprintf( stdout, "        I: <interval> Display interval in milliseconds (defaults to %d mS)" EOL, TOP_UPDATE_INTERVAL );
    fprintf( stdout, "        j: <filename> Output to file in JSON format (or screen if <filename> is '-')" EOL );
    fprintf( stdout, "        l: Aggregate per line rather than per function" EOL );
    fprintf( stdout, "        n: Enforce sync requirement for ITM (i.e. ITM needs to issue syncs)" EOL );
    fprintf( stdout, "        o: <filename> to be used for output live file" EOL );
    fprintf( stdout, "        r: <routines> to record in live file (default %d routines)" EOL, options.maxRoutines );
    fprintf( stdout, "        s: <Server>:<Port> to use" EOL );
    fprintf( stdout, "        t: Use TPIU decoder" EOL );
    fprintf( stdout, "        v: <level> Verbose mode 0(errors)..3(debug)" EOL );
}
// ====================================================================================================
int _processOptions( int argc, char *argv[] )

{
    int c;

    while ( ( c = getopt ( argc, argv, "a:c:d:DEe:f:g:hi:I:j:lm:no:r:s:tv:" ) ) != -1 )
        switch ( c )
        {
            // ------------------------------------
            case 'a':
                options.speed = atoi( optarg );
                break;

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
                break;

            // ------------------------------------
            case 'i':
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
                return false;

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

    if ( ( options.useTPIU ) && ( !options.tpiuITMChannel ) )
    {
        genericsReport( V_ERROR, "TPIU set for use but no channel set for ITM output" EOL );
        return false;
    }

    if ( !options.elffile )
    {
        genericsReport( V_ERROR, "Elf File not specified" EOL );
        exit( -2 );
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

    return true;
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
    int ret;
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

    if ( !_processOptions( argc, argv ) )
    {
        exit( -1 );
    }

    /* Reset the TPIU handler before we start */
    TPIUDecoderInit( &_r.t );
    ITMDecoderInit( &_r.i, options.forceITMSync );
    ITMSeqInit( &_r.d, &_r.i, ITM_REORDER_BUFLEN );

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
                return -1;
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
                return -1;
            }

            if ( setsockopt( sourcefd, SOL_SOCKET, SO_REUSEADDR, &( int )
        {
            1
        }, sizeof( int ) ) < 0 )
            {
                perror( "setsockopt(SO_REUSEADDR) failed" );
                return -1;
            }

            /* Now open the network connection */
            bzero( ( char * ) &serv_addr, sizeof( serv_addr ) );
            server = gethostbyname( options.server );

            if ( !server )
            {
                perror( "Cannot find host" );
                return -1;
            }

            serv_addr.sin_family = AF_INET;
            bcopy( ( char * )server->h_addr,
                   ( char * )&serv_addr.sin_addr.s_addr,
                   server->h_length );
            serv_addr.sin_port = htons( options.port );

            while ( connect( sourcefd, ( struct sockaddr * ) &serv_addr, sizeof( serv_addr ) ) < 0 )
            {
                if ( ( !options.json ) || ( options.json[0] != '-' ) )
                {
                    fprintf( stdout, CLEAR_SCREEN EOL );
                }

                perror( "Could not connect" );
                usleep( 1000000 );
            }
        }
        else
        {
            if ( ( sourcefd = open( options.file, O_RDONLY ) ) < 0 )
            {
                genericsExit( sourcefd, "Can't open file %s" EOL, options.file );
            }

            if ( ( ret = _setSerialConfig ( sourcefd, options.speed ) ) < 0 )
            {
                genericsExit( ret, "setSerialConfig failed" EOL );
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

            if ( r > 0 )
            {
                t = read( sourcefd, cbw, TRANSFER_SIZE );

                if ( t <= 0 )
                {
                    /* We are at EOF (Probably the descriptor closed) */
                    break;
                }
            }

            if ( !SymbolSetCheckValidity( &_r.s, options.elffile ) )
            {
                /* Make sure old references are invalidated */
                _flushHash();

                if ( _r.s )
                {
                    genericsReport( V_WARN, "Loaded %s" EOL, options.elffile );
                }
                else
                {
                    /* Its possible the file was in the process of being written, *
                     *so wait before testing again */
                    usleep( 1000000 );
                    genericsReport( V_WARN, "Attempt second reload of %s" EOL, options.elffile );

                    if ( !SymbolSetCheckValidity( &_r.s, options.elffile ) )
                    {
                        genericsReport( V_ERROR, "Elf file or symbols in it not found" EOL );
                        usleep( 1000000 );
                        break;
                    }
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
                _r.lastReportTicks = _r.i.timeStamp;

            }

            /* Check to make sure there's not an unexpected TPIU in here */
            if ( ITMDecoderGetStats( &_r.i )->tpiuSyncCount )
            {
                genericsReport( V_WARN, "Got a TPIU sync while decoding ITM...did you miss a -t option?" EOL );
                break;
            }
        }

        close( sourcefd );
    }

    if ( ( !ITMDecoderGetStats( &_r.i )->tpiuSyncCount ) )
    {
        genericsReport( V_ERROR, "Read failed" EOL );
    }

    return -2;
}
// ====================================================================================================
