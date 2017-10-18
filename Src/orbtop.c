/*
 * SWO Top for Blackmagic Probe and TTL Serial Interfaces
 * ======================================================
 *
 * Copyright (C) 2017  Dave Marples  <dave@marples.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * This code uses the bdf library. It originally used libdwarf, but that
 * was painful. Unfortunately bdf isn't well documented so you've got to
 * go through the binutils source to find your way around it.  The bdf
 * dependencies are well partitioned into the _lookup routine and it's
 * supporting cast.
 *
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
#include <elf.h>
#include <bfd.h>
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
#include <netdb.h>

#include "uthash.h"
#include "git_version_info.h"
#include "generics.h"
#include "tpiuDecoder.h"
#include "itmDecoder.h"

#define TEXT_SEGMENT ".text"
#define TRACE_CHANNEL  1                     /* Channel that we expect trace data to arrive on */
#define INTERRUPT 0xFFFFFFFD                 /* Special memory address interrupt origin */
#define CUTOFF      10                       /* Default cutoff at 0.1% */
#define SERVER_PORT 3443                     /* Server port definition */
#define MAX_IP_PACKET_LEN (1500)             /* Maximum packet we might receive */
#define TOP_UPDATE_INTERVAL (1000LL)         /* Interval between each on screen update */

struct lineInfo
{
    uint32_t addr;
    char *filename;
    char *function;
    uint32_t line;
};

struct visitedAddr                           /* Structure for Hashmap of visited/observed addresses */
{
    uint32_t addr;
    uint32_t visits;
    UT_hash_handle hh;
};

/* Call data */
struct callData
{
    uint32_t src;
    uint32_t dst;
    uint32_t count;
};

struct edge
{
    struct lineInfo srcl;
    struct lineInfo dstl;
    uint32_t count;
};

struct reportLine

{
    const char *filename;
    const char *function;
    uint32_t addr;
    uint32_t line;
    uint64_t count;
};

enum CDState { CD_waitcount, CD_waitsrc, CD_waitdst };

/* ---------- CONFIGURATION ----------------- */
struct                                       /* Record for options, either defaults or from command line */
{
    BOOL verbose;                            /* Talk more.... */
    BOOL useTPIU;                            /* Are we decoding via the TPIU? */
    uint32_t tpiuITMChannel;                 /* What channel? */

    uint32_t hwOutputs;                      /* What hardware outputs are enabled */

    char *deleteMaterial;                    /* Material to delete off filenames for target */

    char *elffile;                           /* Target program config */

    char *profile;                           /* File to output profile information */
    char *outfile;                           /* File to output historic information */

    uint32_t maxRoutines;                    /* Historic information to emit */
    uint32_t maxHistory;
    BOOL lineDisaggregation;                 /* Aggregate per line or per function? */

    int port;                                /* Source information */
    char *server;

} options =
{
    .useTPIU = FALSE,
    .tpiuITMChannel = 1,
    .outfile = NULL,
    .lineDisaggregation = FALSE,
    .maxRoutines = 8,
    .maxHistory = 30,
    .port = SERVER_PORT,
    .server = "localhost"
};

/* ----------- LIVE STATE ----------------- */
struct
{
    struct ITMDecoder i;                    /* The decoders and the packets from them */
    struct ITMPacket h;
    struct TPIUDecoder t;
    struct TPIUPacket p;

    asymbol **syms;                         /* Symbol table */
    uint32_t symcount;                      /* Number of symbols */
    bfd *abfd;                              /* BFD handle to file */
    asection *sect;                         /* Address data for the program section */

    struct visitedAddr *addresses;          /* Addresses we received in the SWV */
    uint32_t sleeps;

    enum CDState CDState;                   /* State of the call data machine */
    struct callData callsConstruct;         /* Call data entry under construction */
    struct callData *calls;                 /* Call data table */
    uint32_t cdCount;                       /* Call data count */
} _r;

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Internally available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
BOOL _loadSymbols( void )

{
    uint32_t storage;
    BOOL dynamic = FALSE;
    char **matching;

    bfd_init();

    _r.abfd = bfd_openr( options.elffile, NULL );

    if ( !_r.abfd )
    {
        fprintf( stderr, "Couldn't open ELF file\n" );
        return FALSE;
    }

    _r.abfd->flags |= BFD_DECOMPRESS;

    if ( bfd_check_format( _r.abfd, bfd_archive ) )
    {
        fprintf( stderr, "Cannot get addresses from archive %s\n", options.elffile );
        return FALSE;
    }

    if ( ! bfd_check_format_matches ( _r.abfd, bfd_object, &matching ) )
    {
        fprintf( stderr, "Ambigious format for file\n" );
        return FALSE;
    }

    if ( ( bfd_get_file_flags ( _r.abfd ) & HAS_SYMS ) == 0 )
    {
        printf( "No symbols found\n" );
        return FALSE;
    }

    storage = bfd_get_symtab_upper_bound ( _r.abfd ); /* This is returned in bytes */

    if ( storage == 0 )
    {
        storage = bfd_get_dynamic_symtab_upper_bound ( _r.abfd );
        dynamic = TRUE;
    }

    _r.syms = ( asymbol ** )malloc( storage );

    if ( dynamic )
    {
        _r.symcount = bfd_canonicalize_dynamic_symtab ( _r.abfd, _r.syms );
    }
    else
    {
        _r.symcount = bfd_canonicalize_symtab ( _r.abfd, _r.syms );
    }

    _r.sect = bfd_get_section_by_name( _r.abfd, TEXT_SEGMENT );
    return TRUE;
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Handler for individual message types from SWO
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
void _handleException( struct ITMDecoder *i, struct ITMPacket *p )

{
    if ( !( options.hwOutputs & ( 1 << HWEVENT_EXCEPTION ) ) )
    {
        return;
    }

    uint32_t exceptionNumber = ( ( p->d[1] & 0x01 ) << 8 ) | p->d[0];
    uint32_t eventType = p->d[1] >> 4;

    const char *exNames[] = {"Thread", "Reset", "NMI", "HardFault", "MemManage", "BusFault", "UsageFault", "UNKNOWN_7",
                             "UNKNOWN_8", "UNKNOWN_9", "UNKNOWN_10", "SVCall", "Debug Monitor", "UNKNOWN_13", "PendSV", "SysTick"
                            };
    const char *exEvent[] = {"Unknown", "Enter", "Exit", "Resume"};
    fprintf( stdout, "%d,%s,%s\n", HWEVENT_EXCEPTION, exEvent[eventType], exNames[exceptionNumber] );
}
// ====================================================================================================
void _handleDWTEvent( struct ITMDecoder *i, struct ITMPacket *p )

{
    if ( !( options.hwOutputs & ( 1 << HWEVENT_DWT ) ) )
    {
        return;
    }

    uint32_t event = p->d[1] & 0x2F;
    const char *evName[] = {"CPI", "Exc", "Sleep", "LSU", "Fold", "Cyc"};

    for ( uint32_t i = 0; i < 6; i++ )
    {
        if ( event & ( 1 << i ) )
        {
            fprintf( stdout, "%d,%s\n", HWEVENT_DWT, evName[event] );
        }
    }
}
// ====================================================================================================
void _handleSW( struct ITMDecoder *i )

{
    struct ITMPacket p;
    uint32_t d;

    if ( ITMGetPacket( i, &p ) )
    {
        if ( p.srcAddr == TRACE_CHANNEL )
        {
            d = ( p.d[3] << 24 ) | ( p.d[2] << 16 ) | ( p.d[1] << 8 ) | p.d[0];

            switch ( _r.CDState )
            {
                // --------------------
                case CD_waitcount:
                    if ( ( d & 0xFFFF0000 ) == 0x40000000 )
                    {
                        _r.callsConstruct.count = d & 0xFFFF;
                        _r.CDState = CD_waitsrc;
                    }

                    break;

                // --------------------
                case CD_waitsrc:
                    _r.callsConstruct.src = d;
                    _r.CDState = CD_waitdst;
                    break;

                // --------------------
                case CD_waitdst:
                    _r.callsConstruct.dst = d;
                    _r.CDState = CD_waitcount;

                    /* Now store this for later processing */
                    _r.calls = ( struct callData * )realloc( _r.calls, sizeof( struct callData ) * ( _r.cdCount + 1 ) );
                    memcpy( &_r.calls[_r.cdCount], &_r.callsConstruct, sizeof( struct callData ) );
                    _r.cdCount++;
                    break;
                    // --------------------
            }
        }
    }
}
// ====================================================================================================
BOOL _lookup( struct lineInfo *l, uint32_t addr, asection *section )

/* Lookup function for address to line, and hence to function */

{
    /* Dummy line for case that lookup returns no data */
    static struct lineInfo dummyLine = {.filename = "Unknown", .function = "Unknown", .line = 0};

    if ( addr == INTERRUPT )
    {
        l->filename = "None";
        l->function = "INTERRUPT";
        l->line = 0;
        return TRUE;
    }
    else
    {
        addr -= bfd_get_section_vma( _r.abfd, section );

        if ( addr > bfd_section_size( _r.abfd, section ) )
        {
            memcpy( l, &dummyLine, sizeof( struct lineInfo ) );
            return FALSE;
        }

        if ( !bfd_find_nearest_line( _r.abfd, section, _r.syms, addr, ( const char ** )&l->filename, ( const char ** )&l->function, &l->line ) )
        {
            memcpy( l, &dummyLine, sizeof( struct lineInfo ) );
            return FALSE;
        }
    }

    /* Deal with case that filename wasn't recorded */
    if ( !l->filename )
    {
        l->filename = "Unknown";
    }

    /* Remove any frontmatter off filename string that matches */
    if ( options.deleteMaterial )
    {
        char *m = options.deleteMaterial;

        while ( ( *m ) && ( *l->filename ) && ( *l->filename == *m ) )
        {
            m++;
            l->filename++;
        }
    }

    l->addr = addr;
    return TRUE;
}
// ====================================================================================================
uint64_t _timestamp( void )

{
    struct timeval te;
    gettimeofday( &te, NULL ); // get current time
    uint64_t milliseconds = te.tv_sec * 1000LL + te.tv_usec / 1000; // caculate milliseconds
    return milliseconds;
}
// ====================================================================================================
int _addresses_sort_fn( void *a, void *b )

{
    if ( ( ( ( struct visitedAddr * )a )->addr ) < ( ( ( struct visitedAddr * )b )->addr ) )
    {
        return -1;
    }

    if ( ( ( ( struct visitedAddr * )a )->addr ) > ( ( ( struct visitedAddr * )b )->addr ) )
    {
        return 1;
    }

    return 0;
}
// ====================================================================================================
int _report_sort_fn( const void *a, const void *b )

{
    return ( ( struct reportLine * )b )->count - ( ( struct reportLine * )a )->count;
}
// ====================================================================================================
int _calls_sort_src_fn( const void *a, const void *b )

{
    struct edge *ae = ( struct edge * )a;
    struct edge *be = ( struct edge * )b;
    int r;

    r = strcmp( ae->srcl.filename, be->srcl.filename );

    if ( r )
    {
        return r;
    }

    r = strcmp( ae->srcl.function, be->srcl.function );

    if ( r )
    {
        return r;
    }

    r = strcmp( ae->dstl.filename, be->dstl.filename );

    if ( r )
    {
        return r;
    }

    return strcmp( ae->dstl.function, be->dstl.function );
}
// ====================================================================================================
int _calls_sort_dst_fn( const void *a, const void *b )

{
    struct edge *ae = ( struct edge * )a;
    struct edge *be = ( struct edge * )b;
    int r;

    r = strcmp( ae->dstl.filename, be->dstl.filename );

    if ( r )
    {
        return r;
    }

    r = strcmp( ae->dstl.function, be->dstl.function );

    if ( r )
    {
        return r;
    }

    r = strcmp( ae->srcl.filename, be->srcl.filename );

    if ( r )
    {
        return r;
    }

    return strcmp( ae->srcl.function, be->srcl.function );
}
// ====================================================================================================
void outputTop( void )

/* Produce the output */

{
    struct reportLine *report = NULL;
    uint32_t reportLines = 0;
    struct lineInfo l;

    uint32_t total = 0;
    uint32_t unknown = 0;
    uint64_t samples = 0;
    uint32_t percentage;
    uint32_t totPercent = 0;

    FILE *p = NULL;

    /* Put the address into order */
    HASH_SORT( _r.addresses, _addresses_sort_fn );

    /* Now merge them together */
    for ( struct visitedAddr *a = _r.addresses; a != NULL; a = a->hh.next )
    {
        if ( !a->visits )
        {
            continue;
        }

        if ( !_lookup( &l, a->addr, _r.sect ) )
        {
            printf( "%x\n", a->addr );
            unknown += a->visits;
            a->visits = 0;
            continue;
        }

        if ( ( reportLines == 0 ) ||
                ( strcmp( report[reportLines - 1].filename, l.filename ) ) ||
                ( strcmp( report[reportLines - 1].function, l.function ) ) ||
                ( ( report[reportLines - 1].line != l.line ) && ( options.lineDisaggregation ) ) )
        {
            /* Make room for a report line */
            reportLines++;
            report = ( struct reportLine * )realloc( report, sizeof( struct reportLine ) * ( reportLines ) );
            report[reportLines - 1].addr = l.addr;
            report[reportLines - 1].filename = l.filename;
            report[reportLines - 1].function = l.function;
            report[reportLines - 1].line = l.line;
            report[reportLines - 1].count = 0;
        }

        report[reportLines - 1].count += a->visits;
        total += a->visits;
        a->visits = 0;
    }

    /* Add entries for Sleeping and unknown, if there are any of either */
    if ( _r.sleeps )
    {
        report = ( struct reportLine * )realloc( report, sizeof( struct reportLine ) * ( reportLines + 1 ) );
        report[reportLines].filename = "";
        report[reportLines].function = "** SLEEPING **";
        report[reportLines].line = 0;
        report[reportLines].count = _r.sleeps;
        reportLines++;
        total += _r.sleeps;
        _r.sleeps = 0;
    }

    if ( unknown )
    {
        report = ( struct reportLine * )realloc( report, sizeof( struct reportLine ) * ( reportLines + 1 ) );
        report[reportLines].filename = "";
        report[reportLines].function = "** Unknown **";
        report[reportLines].line = 0;
        report[reportLines].count = unknown;
        reportLines++;
        total += unknown;
    }

    /* Now put the whole thing into order of number of samples */
    qsort( report, reportLines, sizeof( struct reportLine ), _report_sort_fn );

    if ( options.outfile )
    {
        p = fopen( options.outfile, "w" );
    }

    fprintf( stdout, "\033[2J\033[;H" );

    for ( uint32_t n = 0; n < reportLines; n++ )
    {
        percentage = ( report[n].count * 10000 ) / total;
        samples += report[n].count;

        if ( report[n].count )
        {
            if ( percentage >= CUTOFF )
            {
                fprintf( stdout, "%3d.%02d%% %8ld ", percentage / 100, percentage % 100, report[n].count );

                if ( options.lineDisaggregation )
                {
                    fprintf( stdout, "%s::%d\n", report[n].function, report[n].line );
                }
                else
                {
                    fprintf( stdout, "%s\n", report[n].function );
                }

                totPercent += percentage;
            }

            if ( ( p )  && ( n < options.maxRoutines ) )
            {
                if ( !options.lineDisaggregation )
                {
                    fprintf( p, "%s,%3d.%02d\n", report[n].function, percentage / 100, percentage % 100 );
                }
                else
                {
                    fprintf( p, "%s::%d,%3d.%02d\n", report[n].function, report[n].line, percentage / 100, percentage % 100 );
                }
            }
        }
    }

    fprintf( stdout, "-----------------\n" );
    fprintf( stdout, "%3d.%02d%% %8ld Samples\n", totPercent / 100, totPercent % 100, samples );

    if ( p )
    {
        fclose( p );
        p = NULL;
    }

    if ( options.verbose )
    {
        fprintf( stdout, "         Ovf=%3d  ITMSync=%3d TPIUSync=%3d ITMErrors=%3d\n",
                 ITMDecoderGetStats( &_r.i )->overflow,
                 ITMDecoderGetStats( &_r.i )->syncCount,
                 TPIUDecoderGetStats( &_r.t )->syncCount,
                 ITMDecoderGetStats( &_r.i )->ErrorPkt );
    }

    /* ... and we are done with the report now, get rid of it */
    free( report );
}
// ====================================================================================================
void _handlePCSample( struct ITMDecoder *i, struct ITMPacket *p )

{
    uint32_t pc;

    if ( p->len == 1 )
    {
        /* This is a sleep packet */
        _r.sleeps++;
    }
    else
    {
        pc = ( p->d[3] << 24 ) | ( p->d[2] << 16 ) | ( p->d[1] << 8 ) | ( p->d[0] );

        struct visitedAddr *a;
        HASH_FIND_INT( _r.addresses, &pc, a );

        if ( a )
        {
            a->visits++;
        }
        else
        {
            /* Create a new record for this address */
            a = ( struct visitedAddr * )malloc( sizeof( struct visitedAddr ) );
            a->visits = 1;
            a->addr = pc;
            HASH_ADD_INT( _r.addresses, addr, a );
        }
    }
}
// ====================================================================================================
void _handleHW( struct ITMDecoder *i )

{
    struct ITMPacket p;
    ITMGetPacket( i, &p );

    switch ( p.srcAddr )
    {
        // --------------
        case 0: /* DWT Event */
            break;

        // --------------
        case 1: /* Exception */
            _handleException( i, &p );
            break;

        // --------------
        case 2: /* PC Counter Sample */
            _handlePCSample( i, &p );
            break;

        // --------------
        default:
            break;
            // --------------
    }
}
// ====================================================================================================
void _itmPumpProcess( char c )

/* Handle individual characters into the itm decoder */

{
    switch ( ITMPump( &_r.i, c ) )
    {
        // ------------------------------------
        case ITM_EV_NONE:
            break;

        // ------------------------------------
        case ITM_EV_UNSYNCED:
            if ( options.verbose )
            {
                fprintf( stdout, "ITM Lost Sync (%d)\n", ITMDecoderGetStats( &_r.i )->lostSyncCount );
            }

            break;

        // ------------------------------------
        case ITM_EV_SYNCED:
            if ( options.verbose )
            {
                fprintf( stdout, "ITM In Sync (%d)\n", ITMDecoderGetStats( &_r.i )->syncCount );
            }

            break;

        // ------------------------------------
        case ITM_EV_OVERFLOW:
            if ( options.verbose )
            {
                fprintf( stdout, "ITM Overflow (%d)\n", ITMDecoderGetStats( &_r.i )->overflow );
            }

            break;

        // ------------------------------------
        case ITM_EV_ERROR:
            if ( options.verbose )
            {
                fprintf( stdout, "ITM Error\n" );
            }

            break;

        // ------------------------------------
        case ITM_EV_TS_PACKET_RXED:
            break;

        // ------------------------------------
        case ITM_EV_SW_PACKET_RXED:
            _handleSW( &_r.i );
            break;

        // ------------------------------------
        case ITM_EV_HW_PACKET_RXED:
            _handleHW( &_r.i );
            break;

        // ------------------------------------
        case ITM_EV_XTN_PACKET_RXED:
            break;
            // ------------------------------------
    }
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Protocol pump for decoding messages
// ====================================================================================================
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
                if ( options.verbose )
                {
                    printf( "TPIU In Sync (%d)\n", TPIUDecoderGetStats( &_r.t )->syncCount );
                }
            case TPIU_EV_SYNCED:
                ITMDecoderForceSync( &_r.i, TRUE );
                break;

            // ------------------------------------
            case TPIU_EV_RXING:
            case TPIU_EV_NONE:
                break;

            // ------------------------------------
            case TPIU_EV_UNSYNCED:
                printf( "TPIU Lost Sync (%d)\n", TPIUDecoderGetStats( &_r.t )->lostSync );
                ITMDecoderForceSync( &_r.i, FALSE );
                break;

            // ------------------------------------
            case TPIU_EV_RXEDPACKET:
                if ( !TPIUGetPacket( &_r.t, &_r.p ) )
                {
                    fprintf( stderr, "TPIUGetPacket fell over\n" );
                }

                for ( uint32_t g = 0; g < _r.p.len; g++ )
                {
                    if ( _r.p.packet[g].s == options.tpiuITMChannel )
                    {
                        _itmPumpProcess( _r.p.packet[g].d );
                        continue;
                    }

                    if ( ( _r.p.packet[g].s != 0 ) && ( options.verbose ) )
                    {
                        if ( options.verbose )
                        {
                            printf( "Unknown TPIU channel %02x\n", _r.p.packet[g].s );
                        }
                    }
                }

                break;

            // ------------------------------------
            case TPIU_EV_ERROR:
                fprintf( stderr, "****ERROR****\n" );
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
    printf( "Useage: %s <htv> <-e ElfFile> <-m MaxHistory> <-o filename> -r <routines> <-i channel> <-p port> <-s server>\n", progName );
    printf( "        d: <DeleteMaterial> to take off front of filenames\n" );
    printf( "        e: <ElfFile> to use for symbols\n" );
    printf( "        h: This help\n" );
    printf( "        i: <channel> Set ITM Channel in TPIU decode (defaults to 1)\n" );
    printf( "        l: Aggregate per line rather than per function\n" );
    printf( "        m: <MaxHistory> to record in history file (default %d intervals)\n", options.maxHistory );
    printf( "        o: <filename> to be used for output history file\n" );
    printf( "        p: <Port> to use\n" );
    printf( "        r: <routines> to record in history file (default %d routines)\n", options.maxRoutines );
    printf( "        s: <Server> to use\n" );
    printf( "        t: Use TPIU decoder\n" );
    printf( "        v: Verbose mode (this will intermingle state info with the output flow)\n" );
}
// ====================================================================================================
int _processOptions( int argc, char *argv[] )

{
    int c;

    while ( ( c = getopt ( argc, argv, "d:e:hi:lm:o:p:r:s:v" ) ) != -1 )
        switch ( c )
        {
            case 'e':
                options.elffile = optarg;
                break;

            case 'd':
                options.deleteMaterial = optarg;
                break;

            case 'l':
                options.lineDisaggregation = TRUE;
                break;

            case 'r':
                options.maxRoutines = atoi( optarg );
                break;

            case 'm':
                options.maxHistory = atoi( optarg );
                break;

            case 'o':
                options.outfile = optarg;
                break;

            case 'v':
                options.verbose = 1;
                break;

            case 't':
                options.useTPIU = TRUE;
                break;

            case 'i':
                options.tpiuITMChannel = atoi( optarg );
                break;

            /* Source information */
            case 'p':
                options.port = atoi( optarg );
                break;

            case 's':
                options.server = optarg;
                break;

            case 'h':
                _printHelp( argv[0] );
                return FALSE;

            case '?':
                if ( optopt == 'b' )
                {
                    fprintf ( stderr, "Option '%c' requires an argument.\n", optopt );
                }
                else if ( !isprint ( optopt ) )
                {
                    fprintf ( stderr, "Unknown option character `\\x%x'.\n", optopt );
                }

                return FALSE;

            default:
                fprintf( stderr, "Unknown option %c\n", optopt );
                return FALSE;
        }

    if ( ( options.useTPIU ) && ( !options.tpiuITMChannel ) )
    {
        fprintf( stderr, "TPIU set for use but no channel set for ITM output\n" );
        return FALSE;
    }

    if ( !options.elffile )
    {
        fprintf( stderr, "Elf File not specified\n" );
        exit( -2 );
    }

    if ( options.verbose )
    {
        fprintf( stdout, "orbtop V" VERSION " (Git %08X %s, Built " BUILD_DATE ")\n", GIT_HASH, ( GIT_DIRTY ? "Dirty" : "Clean" ) );

        fprintf( stdout, "Verbose     : TRUE\n" );
        fprintf( stdout, "Server      : %s:%d\n", options.server, options.port );
        fprintf( stdout, "Delete Mat  : %s\n", options.deleteMaterial ? options.deleteMaterial : "None" );
        fprintf( stdout, "Elf File    : %s\n", options.elffile );

        if ( options.useTPIU )
        {
            fprintf( stdout, "Using TPIU  : TRUE (ITM on channel %d)\n", options.tpiuITMChannel );
        }
    }

    return TRUE;
}
// ====================================================================================================
int main( int argc, char *argv[] )

{
    int sockfd;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    uint8_t cbw[MAX_IP_PACKET_LEN];
    uint64_t lastTime;

    ssize_t t;
    int flag = 1;

    /* Fill in a time to start from */
    lastTime = _timestamp();

    if ( !_processOptions( argc, argv ) )
    {
        exit( -1 );
    }

    if ( !_loadSymbols() )
    {
        exit( -3 );
    }

    /* Reset the TPIU handler before we start */
    TPIUDecoderInit( &_r.t );
    ITMDecoderInit( &_r.i );

    sockfd = socket( AF_INET, SOCK_STREAM, 0 );
    setsockopt( sockfd, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof( flag ) );

    if ( sockfd < 0 )
    {
        perror( "Error creating socket\n" );
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

    if ( connect( sockfd, ( struct sockaddr * ) &serv_addr, sizeof( serv_addr ) ) < 0 )
    {
        perror( "Could not connect" );
        return -1;
    }

    while ( ( t = read( sockfd, cbw, MAX_IP_PACKET_LEN ) ) > 0 )
    {
        uint8_t *c = cbw;

        while ( t-- )
        {
            _protocolPump( *c++ );
        }

        if ( _timestamp() - lastTime > TOP_UPDATE_INTERVAL )
        {
            lastTime = _timestamp();
            outputTop();
        }

        /* Check to make sure there's not an unexpected TPIU in here */
        if ( ITMDecoderGetStats( &_r.i )->tpiuSyncCount )
        {
            fprintf( stderr, "Got a TPIU sync while decoding ITM...did you miss a -t option?\n" );
            break;
        }
    }

    if ( ( options.verbose ) && ( !ITMDecoderGetStats( &_r.i )->tpiuSyncCount ) )
    {
        fprintf( stderr, "Read failed\n" );
    }

    close( sockfd );
    return -2;
}
// ====================================================================================================
