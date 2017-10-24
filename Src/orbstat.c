/*
 * Stats interface for Blackmagic Probe and TTL Serial Interfaces
 * ==============================================================
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

#include "git_version_info.h"
#include "uthash.h"
#include "generics.h"
#include "tpiuDecoder.h"
#include "itmDecoder.h"

#define TEXT_SEGMENT ".text"
#define TRACE_CHANNEL  1                     /* Channel that we expect trace data to arrive on */

#define INTERRUPT    0xFFFFFFF8              /* Special memory address - interrupt origin */
#define NOT_FOUND    0xFFFFFFFF              /* Special memory address - not found */

#define EOL           "\n\r"

#define SERVER_PORT 3443                     /* Server port definition */
#define TRANSFER_SIZE (4096)                 /* Maximum packet we might receive */
#define TOP_UPDATE_INTERVAL (1000LL)         /* Interval between each on screen update */

/* Interface to/from target */
#define COMMS_MASK (0xF0000000)
#define IN_EVENT   (0x40000000)
#define OUT_EVENT  (0x50000000)

/* An entry in the names table */
struct nameEntryHash
{
    const char *filename;
    const char *function;
    uint32_t index;
    uint32_t line;
    uint32_t addr;
    BOOL seen;

    UT_hash_handle hh;
};

/* A calling edge */
struct edge
{
    uint32_t src;
    uint32_t dst;
    uint64_t tstamp;
    BOOL in;
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
struct                                       /* Record for options, either defaults or from command line */
{
    BOOL verbose;                            /* Talk more.... */
    BOOL useTPIU;                            /* Are we decoding via the TPIU? */
    uint32_t tpiuITMChannel;                 /* What channel? */
    BOOL forceITMSync;                       /* Do we assume ITM starts synced? */

    char *deleteMaterial;                    /* Material to strip off front of filenames for target */

    char *elffile;                           /* Target program config */

    char *dotfile;                           /* File to output dot information */
    char *profile;                           /* File to output profile information */
    char *outfile;                           /* File to output historic information */

    uint32_t maxRoutines;                    /* Historic information to emit */
    uint32_t maxHistory;

    BOOL lineDisaggregation;                 /* Aggregate per line or per function? */

    int port;                                /* Source information for where to connect to */
    char *server;

} options =
{
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

    /* Symbol table related info */
    asymbol **syms;                         /* Symbol table */
    uint32_t symcount;                      /* Number of symbols */
    bfd *abfd;                              /* BFD handle to file */
    asection *sect;                         /* Address data for the program section */

    /* Calls related info */
    enum CDState CDState;                   /* State of the call data machine */
    struct edge callsConstruct;             /* Call data entry under construction */
    struct edge *calls;                     /* Call data table */
    struct subcalls *sub;                   /* Construct data */

    uint32_t subPsn;                        /* Counter for sub calls */
    uint32_t psn;                           /* Current position in assessment of data */
    uint32_t cdCount;                       /* Call data count */

    FILE *c;                                /* Writable file */

    /* Turn addresses into files and routines tags */
    uint32_t nameCount;
    struct nameEntryHash *name;

    /* Used for stretching number of bits in target timer */
    uint32_t oldt;
    uint64_t highOrdert;
} _r;

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Internally available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
BOOL _loadSymbols( void )

/* Load symbols from bfd library compatible file */

{
    uint32_t storage;
    BOOL dynamic = FALSE;
    char **matching;

    bfd_init();

    _r.abfd = bfd_openr( options.elffile, NULL );

    if ( !_r.abfd )
    {
        fprintf( stderr, "Couldn't open ELF file" EOL );
        return FALSE;
    }

    _r.abfd->flags |= BFD_DECOMPRESS;

    if ( bfd_check_format( _r.abfd, bfd_archive ) )
    {
        fprintf( stderr, "Cannot get addresses from archive %s" EOL, options.elffile );
        return FALSE;
    }

    if ( ! bfd_check_format_matches ( _r.abfd, bfd_object, &matching ) )
    {
        fprintf( stderr, "Ambigious format for file" EOL );
        return FALSE;
    }

    if ( ( bfd_get_file_flags ( _r.abfd ) & HAS_SYMS ) == 0 )
    {
        fprintf( stderr, "No symbols found" EOL );
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
                case CD_waitinout:
                    if ( ( d & COMMS_MASK ) == IN_EVENT )
                    {
                        _r.callsConstruct.in = TRUE;
                        _r.CDState = CD_waitsrc;
                    }

                    if ( ( d & COMMS_MASK ) == OUT_EVENT )
                    {
                        _r.callsConstruct.in = FALSE;
                        _r.CDState = CD_waitsrc;
                    }

                    /* Time is encoded in lowest two octets ...accomodate rollover */
                    uint32_t t = d & 0xFFFF;

                    if ( t < _r.oldt )
                    {
                        _r.highOrdert++;
                    }

                    _r.oldt = t;
                    _r.callsConstruct.tstamp = ( _r.highOrdert << 16 ) | t;
                    break;

                // --------------------
                case CD_waitsrc:
                    /* Source address is the address of the _return_, so subtract 4 */
                    _r.callsConstruct.src = ( d - 4 );
                    _r.CDState = CD_waitdst;
                    break;

                // --------------------
                case CD_waitdst:
                    _r.callsConstruct.dst = d;
                    _r.CDState = CD_waitinout;

                    /* Now store this for later processing */
                    _r.calls = ( struct edge * )realloc( _r.calls, sizeof( struct edge ) * ( _r.cdCount + 1 ) );
                    memcpy( &_r.calls[_r.cdCount], &_r.callsConstruct, sizeof( struct edge ) );
                    _r.cdCount++;
                    break;
                    // --------------------
            }
        }
    }
}
// ====================================================================================================
BOOL _lookup( struct nameEntryHash **n, uint32_t addr, asection *section )

/* Lookup function for address to line, and hence to function, and cache in case we need it later */

{
    const char *function = NULL;
    const char *filename = NULL;

    uint32_t line;
    BOOL found = FALSE;

    HASH_FIND_INT( _r.name, &addr, *n );

    if ( *n )
    {
        found = TRUE;
    }
    else
    {
        uint32_t workingAddr = addr - bfd_get_section_vma( _r.abfd, section );

        if ( workingAddr <= bfd_section_size( _r.abfd, section ) )
        {
            if ( bfd_find_nearest_line( _r.abfd, section, _r.syms, workingAddr, &filename, &function, &line ) )
            {

                /* Remove any frontmatter off filename string that matches */
                if ( options.deleteMaterial )
                {
                    char *m = options.deleteMaterial;

                    while ( ( *m ) && ( *filename ) && ( *filename == *m ) )
                    {
                        m++;
                        filename++;
                    }
                }

                /* Was found, so create new hash entry for this */
                ( *n ) = ( struct nameEntryHash * )malloc( sizeof( struct nameEntryHash ) );
                ( *n )->filename = filename;
                ( *n )->function = function;
                ( *n )->addr = addr;
                ( *n )->index = _r.nameCount++;
                ( *n )->line = line;
                HASH_ADD_INT( _r.name, addr, ( *n ) );
                found = TRUE;
            }
        }
    }

    if ( !found )
    {
        if ( addr == INTERRUPT )
        {
            ( *n ) = ( struct nameEntryHash * )malloc( sizeof( struct nameEntryHash ) );
            ( *n )->filename = "None";
            ( *n )->function = "INTERRUPT";
            ( *n )->addr = addr;
            ( *n )->index = _r.nameCount++;
            ( *n )->line = 0;
            HASH_ADD_INT( _r.name, addr, ( *n ) );
        }
        else
        {
            uint32_t foundTag = NOT_FOUND;
            /* Create the not found entry if it's not already present */
            HASH_FIND_INT( _r.name, &foundTag, ( *n ) );

            if ( !( *n ) )
            {
                ( *n ) = ( struct nameEntryHash * )malloc( sizeof( struct nameEntryHash ) );
                ( *n )->filename = "Unknown";
                ( *n )->function = "Unknown";
                ( *n )->addr = foundTag;
                ( *n )->index = _r.nameCount++;
                ( *n )->line = 0;
                HASH_ADD_INT( _r.name, addr, ( *n ) );
            }
            else
            {
                found = TRUE;
            }
        }
    }

    /* However we got here, we can refer the requested line entry to a valid name tag */
    return found;
}
// ====================================================================================================
uint64_t _timestamp( void )

/* Return a timestamp */

{
    struct timeval te;
    gettimeofday( &te, NULL ); // get current time
    uint64_t milliseconds = te.tv_sec * 1000LL + te.tv_usec / 1000; // caculate milliseconds
    return milliseconds;
}
// ====================================================================================================
int _addresses_sort_fn( const void *a, const void *b )

/* Sort addresses first by src, then by dst */

{
    int32_t c = ( ( ( struct subcalls * )a )->src ) - ( ( ( struct subcalls * )b )->src );

    if ( c )
    {
        return c;
    }

    return ( ( ( struct subcalls * )a )->dst ) - ( ( ( struct subcalls * )b )->dst );
}
// ====================================================================================================
int _addresses_sort_dest_fn( const void *a, const void *b )

/* Sort addresses first by dst, then by src */

{
    int32_t c = ( ( ( struct subcalls * )a )->dst ) - ( ( ( struct subcalls * )b )->dst );

    if ( c )
    {
        return c;
    }

    return ( ( ( struct subcalls * )a )->src ) - ( ( ( struct subcalls * )b )->src );
}
// ====================================================================================================
void _dumpProfile( void )

/* Dump profile to Valgrind (KCacheGrind compatible) file format */

{
    struct nameEntryHash *f, *t;

    uint64_t myCost;
    uint64_t totalCost;
    uint32_t totalCalls;

    /* Empty the 'seen' field of the name cache */
    HASH_ITER( hh, _r.name, f, t )
    {
        f->seen = FALSE;
    }

    /* Record any destination routine and the time it's taken */
    qsort( _r.sub, _r.subPsn, sizeof( struct subcalls ), _addresses_sort_dest_fn );

    for ( uint32_t i = 0; i < _r.subPsn - 1; i++ )
    {
        /* Collect total cost and sub costs for this dest routine */
        myCost = _r.sub[i].myCost;

        while ( ( i < _r.subPsn - 1 ) && ( _r.sub[i].dst == _r.sub[i + 1].dst ) )
        {
            myCost += _r.sub[i++].myCost;
        }

        _lookup( &t, _r.sub[i].dst, _r.sect );

        if ( !t->seen )
        {
            /* Haven't seen it before, so announce it */
            fprintf( _r.c, "fl=(%d) %s\nfn=(%d) %s\n0x%08x %d %ld\n", t->index, t->filename, t->index, t->function, t->addr, t->line, myCost );
            t->seen = TRUE;
        }
    }


    /* OK, now proceed to report the calls */

    fprintf( _r.c, "\n\n## ------------------- Calls Follow ------------------------\n" );

    for ( uint32_t i = 0; i < _r.subPsn - 2; i++ )
    {
        myCost = _r.sub[i].myCost;
        totalCost = _r.sub[i].total;
        totalCalls = 1;

        while ( ( i < _r.subPsn - 2 ) && ( _r.sub[i].dst == _r.sub[i + 1].dst ) && ( _r.sub[i].src == _r.sub[i + 1].src ) )
        {
            i++;
            totalCost += _r.sub[i].total;
            myCost += _r.sub[i].myCost;
            totalCalls++;
        }

        _lookup( &t, _r.sub[i].dst, _r.sect );

        if ( !t->seen )
        {
            /* This is a previously unseen dest, announce it */
            fprintf( _r.c, "fl=(%d) %s\nfn=(%d) %s\n0x%08x %d %ld\n", t->index, t->filename, t->index, t->function, t->addr, t->line, myCost );
            t->seen = TRUE;
        }

        _lookup( &f, _r.sub[i].src, _r.sect );

        if ( !f->seen )
        {
            /* Add this in, but cost of the caller is not visible here...we need to put 1 else no code is visible */
            fprintf( _r.c, "fl=(%d) %s\nfn=(%d) %s\n0x%08x %d 1\n", f->index, f->filename, f->index, f->function, f->addr, f->line );
            f->seen = TRUE;
        }
        else
        {
            fprintf( _r.c, "fl=(%d)\nfn=(%d)\n", f->index, f->index );
        }

        /* Now publish the call destination. By definition is is known, so can be shortformed */
        fprintf( _r.c, "cfi=(%d)\ncfn=(%d)\ncalls=%d 0x%08x %d\n", t->index, t->index, totalCalls, _r.sub[i].dst, t->line );
        fprintf( _r.c, "0x%08x %d %ld\n", _r.sub[i].src, f->line, totalCost );
    }
}
// ====================================================================================================
uint64_t _traverse( uint32_t layer )

/* Recursively traverse the calls tree, recording each subroutine call as we go along */

{
    uint32_t startPoint = _r.psn; /* Record where we came in on this iteration */
    uint64_t childCost = 0;      /* ...and keep a record of any children visited */

    /* If this is an out and we're already at the top level then it's to be ignored */
    if ( ( layer == 0 ) && ( !_r.calls[_r.psn].in ) )
    {
        _r.psn++;
        return 0;
    }

    _r.psn++; /* Move past my node... */

    /* Two cases...either an in node, in which case there is more to be covered */
    /* or an out node, in which case we're done and we can just record what we've got */

    /* ...of course there might be a whole sequence if in calls if we call several routines from ours */
    while ( _r.calls[_r.psn].in )
    {
        if ( _r.psn >= _r.cdCount - 2 )
        {
            return 0;
        }

        childCost += _traverse( layer + 1 );
    }


    /* This is my out node....they may have been others below, but this one matches my in node */
    /* At this point startPoint is the in node, and r_psn is the exit node, so store this entry */

    _r.sub = ( struct subcalls * )realloc( _r.sub, ( ++_r.subPsn ) * ( sizeof( struct subcalls ) ) );
    _r.sub[_r.subPsn - 1].dst = _r.calls[_r.psn].dst;
    _r.sub[_r.subPsn - 1].src = _r.calls[_r.psn].src;
    _r.sub[_r.subPsn - 1].total = _r.calls[_r.psn].tstamp - _r.calls[startPoint].tstamp;
    _r.sub[_r.subPsn - 1].myCost = _r.sub[_r.subPsn - 1].total - childCost;

    _r.psn++;

    /* ...and float to level above any cost we've got */
    return _r.sub[_r.subPsn - 1].total;
}
// ====================================================================================================
void _outputProfile( void )

/* Output a KCacheGrind compatible profile */

{
    _r.c = fopen( options.profile, "w" );
    fprintf( _r.c, "# callgrind format\n" );
    fprintf( _r.c, "positions: line instr\nevent: Cyc : Processor Clock Cycles\nevents: Cyc\n" );
    /* Samples are in time order, so we can determine the extent of time.... */
    fprintf( _r.c, "summary: %ld\n", _r.calls[_r.cdCount - 1].tstamp - _r.calls[0].tstamp );
    fprintf( _r.c, "ob=%s\n", options.elffile );

    /* If we have a set of sub-calls from a previous run then delete them */
    if ( _r.sub )
    {
        free( _r.sub );
        _r.sub = NULL;
    }

    _r.subPsn = 0;

    _r.psn = 0;

    while ( _r.psn < _r.cdCount - 2 )
    {
        _traverse( 0 );
    }

    _dumpProfile();
    fclose( _r.c );
}
// ====================================================================================================
void _outputDot( void )

/* Output call graph to dot file */

{
    FILE *c;
    struct nameEntryHash *f = NULL, *t = NULL;

    struct callVector
    {
        struct nameEntryHash *fromName;
        struct nameEntryHash *toName;
        uint32_t count;
    };

    struct callVector *call = NULL;
    uint32_t callCount = 0;


    int _addr_sort_dst_fn( const void *a, const void *b )

    {
        int32_t c = ( ( ( struct edge * )a )->dst ) - ( ( ( struct edge * )b )->dst );

        if ( c )
        {
            return c;
        }

        return ( ( ( struct edge * )a )->src ) - ( ( ( struct edge * )b )->src );
    }


    int _calls_sort_src_fn( const void *a, const void *b )

    {
        int32_t c = ( ( ( struct callVector * )a )->fromName ) - ( ( ( struct callVector * )b )->fromName );

        if ( c )
        {
            return c;
        }

        return ( ( ( struct callVector * )a )->toName ) - ( ( ( struct callVector * )b )->toName );
    }

    int _calls_sort_dst_fn( const void *a, const void *b )

    {
        int32_t c = ( ( ( struct callVector * )a )->toName ) - ( ( ( struct callVector * )b )->toName );

        if ( c )
        {
            return c;
        }

        return ( ( ( struct callVector * )a )->fromName ) - ( ( ( struct callVector * )b )->fromName );
    }

    if ( !options.dotfile )
    {
        return;
    }

    /* Sort according to addresses visited. */
    qsort( _r.calls, _r.cdCount, sizeof( struct edge ), _addr_sort_dst_fn );

    /* Put in all the names of functions. There may be multiple addresses that match to a function, so crush them up */
    _lookup( &f, _r.calls[0].src, _r.sect );
    _lookup( &t, _r.calls[0].dst, _r.sect );

    for ( uint32_t i = 0; i < _r.cdCount; i++ )
    {
        call = ( struct callVector * )realloc( call, sizeof( struct callVector ) * ( callCount + 1 ) );
        call[callCount].fromName = f;
        call[callCount].toName = t;
        call[callCount].count = 0;

        do
        {
            call[callCount].count++;
            _lookup( &f, _r.calls[i].src, _r.sect );
            _lookup( &t, _r.calls[i].dst, _r.sect );
            i++;
        }
        while ( ( !strcmp( call[callCount].fromName->function, f->function ) ) && ( !strcmp( call[callCount].toName->function, t->function ) ) && ( i < _r.cdCount ) );
        callCount++;
    }

    c = fopen( options.dotfile, "w" );
    fprintf( c, "digraph calls\n{\n  overlap=false; splines=true; size=\"7.75,10.25\"; orientation=portrait; sep=0.1; nodesep=0.1;\n" );

    /* firstly write out the nodes in each subgraph - dest side clustered */
    qsort( call, callCount, sizeof( struct callVector ), _calls_sort_dst_fn );

    for ( uint32_t x = 1; x < callCount; x++ )
    {
        fprintf( c, "  subgraph \"cluster_%s\"\n  {\n    label=\"%s\";\n    bgcolor=lightgrey;\n", call[x - 1].toName->filename, call[x - 1].toName->filename );

        while ( x < callCount )
        {
            /* Now output each function in the subgraph */
            fprintf( c, "    %s [style=filled, fillcolor=white];\n", call[x - 1].toName->function );

            /* Spin forwards until the function name _or_ filename changes */
            while ( ( x < callCount ) && ( call[x - 1].toName == call[x].toName ) )
            {
                x++;
            }

            if ( ( x >= callCount ) || ( strcmp( call[x - 1].toName->filename, call[x].toName->filename ) ) )
            {
                break;
            }

            x++;
        }

        fprintf( c, "  }\n\n" );
    }

    /* now write out the nodes in each subgraph - source side clustered */
    qsort( call, callCount, sizeof( struct callVector ), _calls_sort_src_fn );

    for ( uint32_t x = 1; x < callCount; x++ )
    {
        fprintf( c, "  subgraph \"cluster_%s\"\n  {\n    label=\"%s\";\n    bgcolor=lightgrey;\n", call[x - 1].fromName->filename, call[x - 1].fromName->filename );

        while ( x < callCount )
        {
            /* Now output each function in the subgraph */
            fprintf( c, "    %s [style=filled, fillcolor=white];\n", call[x - 1].fromName->function );

            /* Spin forwards until the function name _or_ filename changes */
            while ( ( x < callCount ) && ( call[x - 1].fromName == call[x].fromName ) )
            {
                x++;
            }

            if ( ( x >= callCount ) || ( strcmp( call[x - 1].fromName->filename, call[x].fromName->filename ) ) )
            {
                break;
            }

            x++;
        }

        fprintf( c, "  }\n\n" );
    }

    /* Now go through and label the arrows... */
    for ( uint32_t x = 0; x < callCount; x++ )
    {
        fprintf( c, "    %s -> ", call[x].fromName->function );
        fprintf( c, "%s [label=%d , weight=0.1;];\n", call[x].toName->function, call[x].count );
    }

    fprintf( c, "}\n" );
    fclose( c );
}
// ====================================================================================================
void _handlePCSample( struct ITMDecoder *i, struct ITMPacket *p )

{
    return;
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
                fprintf( stdout, "ITM Lost Sync (%d)" EOL, ITMDecoderGetStats( &_r.i )->lostSyncCount );
            }
            break;
        // ------------------------------------
        case ITM_EV_SYNCED:
            if ( options.verbose )
            {
                fprintf( stdout, "ITM In Sync (%d)" EOL, ITMDecoderGetStats( &_r.i )->syncCount );
            }
            break;
        // ------------------------------------
        case ITM_EV_OVERFLOW:
            if ( options.verbose )
            {
                fprintf( stdout, "ITM Overflow (%d)" EOL, ITMDecoderGetStats( &_r.i )->overflow );
            }
            break;
        // ------------------------------------
        case ITM_EV_ERROR:
            if ( options.verbose )
            {
                fprintf( stdout, "ITM Error" EOL );
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
                    fprintf( stdout, "TPIU In Sync (%d)" EOL, TPIUDecoderGetStats( &_r.t )->syncCount );
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
                fprintf( stdout, "TPIU Lost Sync (%d)" EOL, TPIUDecoderGetStats( &_r.t )->lostSync );
                ITMDecoderForceSync( &_r.i, FALSE );
                break;

            // ------------------------------------
            case TPIU_EV_RXEDPACKET:
                if ( !TPIUGetPacket( &_r.t, &_r.p ) )
                {
                    fprintf( stderr, "TPIUGetPacket fell over" EOL );
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
                            fprintf( stdout, "Unknown TPIU channel %02x" EOL, _r.p.packet[g].s );
                        }
                    }
                }

                break;

            // ------------------------------------
            case TPIU_EV_ERROR:
                fprintf( stderr, "****ERROR****" EOL );
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
    fprintf( stdout, "Usage: %s <htv> <-e ElfFile> <-m MaxHistory> <-o filename> -r <routines> <-i channel> <-p port> <-s server>" EOL, progName );
    fprintf( stdout, "       d: <DeleteMaterial> to take off front of filenames" EOL );
    fprintf( stdout, "       e: <ElfFile> to use for symbols" EOL );
    fprintf( stdout, "       h: This help" EOL );
    fprintf( stdout, "       i: <channel> Set ITM Channel in TPIU decode (defaults to 1)" EOL );
    fprintf( stdout, "       l: Aggregate per line rather than per function" EOL );
    fprintf( stdout, "       m: <MaxHistory> to record in history file (default %d intervals)" EOL, options.maxHistory );
    fprintf( stdout, "       n: No sync requirement for ITM (i.e. ITM does not need to issue syncs)" EOL );
    fprintf( stdout, "       o: <filename> to be used for output history file" EOL );
    fprintf( stdout, "       p: <Port> to use" EOL );
    fprintf( stdout, "       r: <routines> to record in history file (default %d routines)" EOL, options.maxRoutines );
    fprintf( stdout, "       s: <Server> to use" EOL );
    fprintf( stdout, "       t: Use TPIU decoder" EOL );
    fprintf( stdout, "       v: Verbose mode (this will intermingle state info with the output flow)" EOL );
    fprintf( stdout, "       y: <Filename> dotty filename for structured callgraph output" EOL );
    fprintf( stdout, "       z: <Filename> profile filename for kcachegrind output" EOL );
}
// ====================================================================================================
int _processOptions( int argc, char *argv[] )

{
    int c;

    while ( ( c = getopt ( argc, argv, "d:e:hi:lm:no:p:r:s:tvy:z:" ) ) != -1 )

        switch ( c )
        {
            // ------------------------------------
            case 'd':
                options.deleteMaterial = optarg;
                break;

            // ------------------------------------
            case 'e':
                options.elffile = optarg;
                break;

            // ------------------------------------
            case 'h':
                _printHelp( argv[0] );
                return FALSE;

            // ------------------------------------
            case 'i':
                options.tpiuITMChannel = atoi( optarg );
                break;

            // ------------------------------------
            case 'l':
                options.lineDisaggregation = TRUE;
                break;

            // ------------------------------------
            case 'm':
                options.maxHistory = atoi( optarg );
                break;

            // ------------------------------------
            case 'n':
                options.forceITMSync = TRUE;
                break;

            // ------------------------------------
            case 'o':
                options.outfile = optarg;
                break;

            // ------------------------------------
            case 'p':
                options.port = atoi( optarg );
                break;

            // ------------------------------------
            case 'r':
                options.maxRoutines = atoi( optarg );
                break;

            // ------------------------------------
            case 's':
                options.server = optarg;
                break;

            // ------------------------------------
            case 't':
                options.useTPIU = TRUE;
                break;

            // ------------------------------------
            case 'v':
                options.verbose = TRUE;
                break;

            // ------------------------------------
            case 'y':
                options.dotfile = optarg;
                break;

            // ------------------------------------
            case 'z':
                options.profile = optarg;
                break;

            // ------------------------------------
            case '?':
                if ( optopt == 'b' )
                {
                    fprintf ( stderr, "Option '%c' requires an argument." EOL, optopt );
                }
                else if ( !isprint ( optopt ) )
                {
                    fprintf ( stderr, "Unknown option character `\\x%x'." EOL, optopt );
                }

                return FALSE;

            // ------------------------------------
            default:
                fprintf( stderr, "Unknown option %c" EOL, optopt );
                return FALSE;
                // ------------------------------------
        }

    if ( ( options.useTPIU ) && ( !options.tpiuITMChannel ) )
    {
        fprintf( stderr, "TPIU set for use but no channel set for ITM output" EOL );
        return FALSE;
    }

    if ( !options.elffile )
    {
        fprintf( stderr, "Elf File not specified" EOL );
        exit( -2 );
    }

    if ( options.verbose )
    {
        fprintf( stdout, "orbtop V" VERSION " (Git %08X %s, Built " BUILD_DATE ")" EOL, GIT_HASH, ( GIT_DIRTY ? "Dirty" : "Clean" ) );

        fprintf( stdout, "Verbose     : TRUE" EOL );
        fprintf( stdout, "Server      : %s:%d" EOL, options.server, options.port );
        fprintf( stdout, "Delete Mat  : %s" EOL, options.deleteMaterial ? options.deleteMaterial : "None" );
        fprintf( stdout, "Elf File    : %s" EOL, options.elffile );
        fprintf( stdout, "DOT file    : %s" EOL, options.dotfile ? options.dotfile : "None" );
        fprintf( stdout, "ForceSync   : %s" EOL, options.forceITMSync ? "TRUE" : "FALSE" );

        if ( options.useTPIU )
        {
            fprintf( stdout, "Using TPIU  : TRUE (ITM on channel %d)" EOL, options.tpiuITMChannel );
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
    uint8_t cbw[TRANSFER_SIZE];
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
    ITMDecoderInit( &_r.i, options.forceITMSync );

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

    while ( ( t = read( sockfd, cbw, TRANSFER_SIZE ) ) > 0 )
    {
        uint8_t *c = cbw;

        while ( t-- )
        {
            _protocolPump( *c++ );
        }

        if ( _timestamp() - lastTime > TOP_UPDATE_INTERVAL )
        {
            lastTime = _timestamp();

            if (  _r.cdCount )
            {
                if ( options.dotfile )
                {
                    _outputDot();
                }

                if ( options.profile )
                {
                    _outputProfile();
                }
            }

            fprintf( stdout, "%d records processed" EOL, _r.cdCount );
            /* Now free up this seconds data */
            free( _r.calls );
            _r.calls = NULL;
            _r.cdCount = 0;
        }

        /* Check to make sure there's not an unexpected TPIU in here */
        if ( ITMDecoderGetStats( &_r.i )->tpiuSyncCount )
        {
            fprintf( stderr, "Got a TPIU sync while decoding ITM...did you miss a -t option?" EOL );
            break;
        }
    }

    if ( ( options.verbose ) && ( !ITMDecoderGetStats( &_r.i )->tpiuSyncCount ) )
    {
        fprintf( stderr, "Read failed" EOL );
    }

    close( sockfd );
    return -2;
}
// ====================================================================================================
