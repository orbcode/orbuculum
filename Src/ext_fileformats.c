/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * External file format writing module for Orbuculum
 * =================================================
 *
 */

#include <stdio.h>
#include <assert.h>

#include "ext_fileformats.h"

#define HANDLE_MASK         (0xFFFFFF)   /* cachegrind cannot cope with large file handle numbers */

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Internally available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
static int _inst_sort_fn( const void *a, const void *b )

/* Sort instructions by address */

{
    return ( int )( ( ( struct execEntryHash * )a )->addr ) - ( int )( ( ( struct execEntryHash * )b )->addr );
}
// ====================================================================================================
static int _calls_src_sort_addr( const void *a, const void *b )

/* Sort instructions by called from address */

{
    int i;

    if ( ( i = ( int )( ( ( struct subcall * )a )->sig.src ) - ( int )( ( ( struct subcall * )b )->sig.src ) ) )
    {
        return i;
    }

    return ( int )( ( ( struct subcall * )a )->sig.dst ) - ( int )( ( ( struct subcall * )b )->sig.dst );
}
// ====================================================================================================
static int _calls_dst_sort_addr( const void *a, const void *b )

/* Sort instructions by called to address */

{
    int i;

    if ( ( i = ( int )( ( ( struct subcall * )a )->sig.dst ) - ( int )( ( ( struct subcall * )b )->sig.dst ) ) )
    {
        return i;
    }

    return ( int )( ( ( struct subcall * )a )->sig.src ) - ( int )( ( ( struct subcall * )b )->sig.src );
}
// ====================================================================================================
static int _calls_src_sort_fn( const void *a, const void *b )

/* Sort instructions by called from address */

{
    int i;

    if ( ( i = ( int )( ( ( struct subcall * )a )->fn->functionindex ) - ( int )( ( ( struct subcall * )b )->fn->functionindex ) ) )
    {
        return i;
    }

    return ( int )( ( ( struct subcall * )a )->tn->functionindex ) - ( int )( ( ( struct subcall * )b )->tn->functionindex );
}
// ====================================================================================================
static int _calls_dst_sort_fn( const void *a, const void *b )

/* Sort instructions by called to address */

{
    int i;

    if ( ( i = ( int )( ( ( struct subcall * )a )->tn->functionindex ) - ( int )( ( ( struct subcall * )b )->tn->functionindex ) ) )
    {
        return i;
    }

    return ( int )( ( ( struct subcall * )a )->fn->functionindex ) - ( int )( ( ( struct subcall * )b )->fn->functionindex );
}
// ====================================================================================================
static void _annotateGraph( struct subcall **subcallList, struct SymbolSet *ss, struct nameEntry **nel )

{
    uint32_t nameCount = 0;
    struct subcall *s, *p;
    struct nameEntry n;

    /* Shouldn't enter here with any names already allocated */
    assert( *nel == 0 );

    /* First handle from side */
    HASH_SORT( ( *subcallList ), _calls_src_sort_addr );
    s = ( *subcallList );
    p = NULL;

    while ( s )
    {
        SymbolLookup( ss, s->sig.src, &n );

        if ( ( !p ) || ( n.fileindex != p->fn->fileindex ) || ( n.functionindex != p->fn->functionindex ) )
        {
            /* Either we have no names, or it changed...in either case, store this name */
            *nel = ( struct nameEntry * )realloc( *nel, sizeof( struct nameEntry ) * ( nameCount + 1 ) );
            memcpy( &( ( *nel )[nameCount++] ), &n, sizeof( struct nameEntry ) );
        }

        s->fn = &( ( *nel )[nameCount - 1] );
        p = s;
        s = s->hh.next;
    }

    /* Now do destination side */
    HASH_SORT( ( *subcallList ), _calls_dst_sort_addr );
    s = ( *subcallList );
    p = NULL;

    while ( s )
    {
        SymbolLookup( ss, s->sig.dst, &n );

        if ( ( !p ) || ( n.fileindex != p->tn->fileindex ) || ( n.functionindex != p->tn->functionindex ) )
        {
            /* Either we have no names, or it changed...in either case, store this name */
            *nel = ( struct nameEntry * )realloc( *nel, sizeof( struct nameEntry ) * ( nameCount + 1 ) );
            memcpy( &( ( *nel )[nameCount++] ), &n, sizeof( struct nameEntry ) );
        }

        s->tn = &( ( *nel )[nameCount - 1] );
        p = s;
        s = s->hh.next;
    }
}
// ====================================================================================================
static void _clearAnnotation( struct nameEntry **nel )

{
    free( *nel );
    *nel = NULL;
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Externally available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================

// ====================================================================================================
// ====================================================================================================
// Dot support
// ====================================================================================================
// ====================================================================================================
bool ext_ff_outputDot( char *dotfile, struct subcall *subcallList, struct SymbolSet *ss )

/* Output call graph to dot file */

{
    FILE *c;
    uint64_t cnt;
    struct subcall *s;
    struct subcall *p;
    struct nameEntry *nel = NULL;

    if ( !dotfile )
    {
        return false;
    }

    _annotateGraph( &subcallList, ss, &nel );

    /* Sort according to addresses visited. */

    c = fopen( dotfile, "w" );
    fprintf( c, "graph calls\n{\n  overlap=scale; splines=true; size=\"7.75,10.25\"; orientation=portrait; sep=0.1; nodesep=1;\n" );

    HASH_SORT( subcallList, _calls_src_sort_fn );
    s = subcallList;

    /* Now go through and label the arrows... */

    s = subcallList;

    while ( s )
    {
        /* Cluster all calls within the same from/to pair */
        cnt = 0;
        p = s;

        while ( ( s ) && ( p->fn->functionindex == s->fn->functionindex ) && ( p->tn->functionindex == s->tn->functionindex ) )
        {
            cnt += s->count;
            s = s->hh.next;
        }

        fprintf( c, "    \"\n(%s)\n%s\n%08x\n\" -- ", SymbolFilename( ss, p->fn->fileindex ), SymbolFunction( ss, p->fn->functionindex ), p->fn->addr );
        fprintf( c, "\"\n(%s)\n%s\n%08x\n\" [label=%" PRIu64 ", weight=%" PRIu64 " ];\n", SymbolFilename( ss, p->tn->fileindex ), SymbolFunction( ss, p->tn->functionindex ), p->tn->addr, cnt, cnt );
    }

    fprintf( c, "}\n" );
    fclose( c );

    _clearAnnotation( &nel );
    return true;
}
// ====================================================================================================
// ====================================================================================================
// KCacheGrind support
// ====================================================================================================
// ====================================================================================================
bool ext_ff_outputProfile( char *profile, char *elffile, char *deleteMaterial, bool includeVisits, uint64_t timelen,
                           struct execEntryHash *insthead, struct subcall *subcallList, struct SymbolSet *ss )

/* Output a KCacheGrind compatible profile, with instruction coverage in insthead, calls in subcallList */

{
    struct nameEntry n;
    uint32_t prevfile = NO_FILE;
    uint32_t prevfn   = NO_FUNCTION;
    uint32_t prevaddr = NO_FUNCTION;
    uint32_t prevline = NO_LINE;
    struct nameEntry *nel = NULL;
    char *e = elffile;
    char *d = deleteMaterial;
    FILE *c;

    if ( !profile )
    {
        return false;
    }

    _annotateGraph( &subcallList, ss, &nel );

    c = fopen( profile, "w" );
    fprintf( c, "# callgrind format\n" );

    if ( includeVisits )
    {
        fprintf( c, "creator: orbprofile\npositions: instr line\nevent: Inst : CPU Instructions\nevent: Visits : Visits to source line\nevents: Inst Visits\n" );
    }
    else
    {
        fprintf( c, "creator: orbprofile\npositions: instr line\nevent: Inst : CPU Instructions\nevents: Inst\n" );
    }

    /* Samples are in time order, so we can determine the extent of time.... */
    fprintf( c, "summary: %" PRIu64 "\n", timelen );

    /* Try to remove frontmatter off the elfile if nessessary and possible */
    if ( deleteMaterial )
    {
        while ( ( *d ) && ( *d == *e ) )
        {
            d++;
            e++;
        }

        if ( e - elffile != strlen( deleteMaterial ) )
        {
            /* Strings don't match, give up and use the file elffile name */
            e = elffile;
        }
    }

    /* ...and record whatever elffilename we ended up with */
    fprintf( c, "ob=%s\n", e );

    HASH_SORT( insthead, _inst_sort_fn );
    struct execEntryHash *f = insthead;

    while ( f )
    {
        SymbolLookup( ss, f->addr, &n );

        if ( prevfile != n.fileindex )
        {
            fprintf( c, "fl=(%u) %s%s\n", n.fileindex & HANDLE_MASK, deleteMaterial  ? deleteMaterial : "", SymbolFilename( ss, n.fileindex ) );
        }

        if ( prevfn != n.functionindex )
        {
            fprintf( c, "fn=(%u) %s\n", n.functionindex & HANDLE_MASK, SymbolFunction( ss, n.functionindex ) );
        }

        if ( ( prevline == NO_LINE ) || ( prevaddr == NO_FUNCTION ) )
        {
            fprintf( c, "0x%08x %d ", f->addr, n.line );
        }
        else
        {
            if ( prevaddr == f->addr )
            {
                fprintf( c, "* " );
            }
            else
            {
                fprintf( c, "%s%d ", f->addr > prevaddr ? "+" : "", ( int )f->addr - prevaddr );
            }

            if ( prevline == n.line )
            {
                fprintf( c, "* " );
            }
            else
            {
                fprintf( c, "%s%d ", n.line > prevline ? "+" : "", ( int )n.line - prevline );
            }
        }

        if ( includeVisits )
        {
            fprintf( c, "%" PRIu64 " %" PRIu64 "\n", f->count, f->scount );
        }
        else
        {
            fprintf( c, "%" PRIu64 "\n", f->count );
        }


        prevline = n.line;
        prevaddr = f->addr;
        prevfile = n.fileindex;
        prevfn = n.functionindex;
        f = f->hh.next;
    }

    fprintf( c, "\n\n## ------------------- Calls Follow ------------------------\n" );
    HASH_SORT( subcallList, _calls_src_sort_fn );
    struct subcall *s = subcallList;

    while ( s )
    {
        /* Now publish the call destination. By definition is is known, so can be shortformed */
        if ( prevfile != s->fn->fileindex )
        {
            fprintf( c, "fl=(%u)\n", s->fn->fileindex & HANDLE_MASK );
            prevfile = s->fn->fileindex;
        }

        if ( prevfn != s->fn->functionindex )
        {
            fprintf( c, "fn=(%u)\n", s->fn->functionindex & HANDLE_MASK );
            prevfn = s->fn->functionindex;
        }

        fprintf( c, "cfl=(%d)\ncfn=(%d)\n", s->tn->fileindex, s->tn->functionindex );
        fprintf( c, "calls=%" PRIu64 " 0x%08x %d\n", s->count, s->sig.dst, s->tn->line );

        if ( includeVisits )
        {
            fprintf( c, "0x%08x %d %" PRIu64 " %" PRIu64 "\n", s->sig.src, s->fn->line, s->myCost, s->count );
        }
        else
        {
            fprintf( c, "0x%08x %d %" PRIu64 "\n", s->sig.src, s->fn->line, s->myCost );
        }

        s = s->hh.next;
    }

    fclose( c );

    _clearAnnotation( &nel );
    return true;
}
// ====================================================================================================
