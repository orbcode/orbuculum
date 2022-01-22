/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * External file format writing module for Orbuculum
 * =================================================
 *
 */

#include <stdio.h>
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
static int _calls_src_sort_fn( const void *a, const void *b )

/* Sort instructions by called from address */

{
    int i;

    if ( ( i = ( int )( ( ( struct subcall * )a )->srch->functionindex ) - ( int )( ( ( struct subcall * )b )->srch->functionindex ) ) )
    {
        return i;
    }

    return ( int )( ( ( struct subcall * )a )->dsth->functionindex ) - ( int )( ( ( struct subcall * )b )->dsth->functionindex );
}
// ====================================================================================================
static int _calls_dst_sort_fn( const void *a, const void *b )

/* Sort instructions by called to address */

{
    int i;

    if ( ( i = ( int )( ( ( struct subcall * )a )->dsth->functionindex ) - ( int )( ( ( struct subcall * )b )->dsth->functionindex ) ) )
    {
        return i;
    }

    return ( int )( ( ( struct subcall * )a )->srch->functionindex ) - ( int )( ( ( struct subcall * )b )->srch->functionindex );
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
    uint32_t functionidx, dfunctionidx, fileidx;
    uint64_t cnt;
    struct subcall *s;

    if ( !dotfile )
    {
        return false;
    }

    /* Sort according to addresses visited. */

    c = fopen( dotfile, "w" );
    fprintf( c, "graph calls\n{\n  overlap=true; splines=true; size=\"7.75,10.25\"; orientation=portrait; sep=0.1; nodesep=1;\n" );

    HASH_SORT( subcallList, _calls_src_sort_fn );
    s = subcallList;

    /* Now go through and label the arrows... */

    s = subcallList;

    while ( s )
    {
        functionidx = s->srch->functionindex;
        fileidx = s->srch->fileindex;

        dfunctionidx = s->dsth->functionindex;
        cnt = s->count;
        s = s->hh.next;

        while ( ( s ) && ( functionidx == s->srch->functionindex ) && ( dfunctionidx == s->dsth->functionindex ) )
        {
            cnt += s->count;
            s = s->hh.next;
        }

        fprintf( c, "    \"\n(%s)\n%s\n\n\" -- ", SymbolFilename( ss, fileidx ), SymbolFunction( ss, functionidx ) );
        fprintf( c, "\"\n(%s)\n%s\n\n\" [label=%" PRIu64 ", weight=0.1 ];\n", SymbolFilename( ss, fileidx ), SymbolFunction( ss, dfunctionidx ), cnt );
    }

    fprintf( c, "}\n" );
    fclose( c );
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
    char *e = elffile;
    char *d = deleteMaterial;
    FILE *c;

    if ( !profile )
    {
        return false;
    }


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
        if ( prevfile != s->srch->fileindex )
        {
            fprintf( c, "fl=(%u)\n", s->srch->fileindex & HANDLE_MASK );
            prevfile = s->srch->fileindex;
        }

        if ( prevfn != s->srch->functionindex )
        {
            fprintf( c, "fn=(%u)\n", s->srch->functionindex & HANDLE_MASK );
            prevfn = s->srch->functionindex;
        }

        fprintf( c, "cfl=(%d)\ncfn=(%d)\n", s->dsth->fileindex, s->dsth->functionindex );
        fprintf( c, "calls=%" PRIu64 " 0x%08x %d\n", s->count, s->sig.dst, s->dsth->line );

        if ( includeVisits )
        {
            fprintf( c, "0x%08x %d %" PRIu64 " %" PRIu64 "\n", s->sig.src, s->srch->line, s->myCost, s->count );
        }
        else
        {
            fprintf( c, "0x%08x %d %" PRIu64 "\n", s->sig.src, s->srch->line, s->myCost );
        }

        s = s->hh.next;
    }

    fclose( c );

    return true;
}
// ====================================================================================================
