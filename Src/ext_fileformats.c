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

    if ( ( i = ( int )( ( ( struct subcall * )a )->sig.src ) - ( int )( ( ( struct subcall * )b )->sig.src ) ) )
    {
        return i;
    }

    return ( int )( ( ( struct subcall * )a )->sig.dst ) - ( int )( ( ( struct subcall * )b )->sig.dst );
}
// ====================================================================================================
static int _calls_dst_sort_fn( const void *a, const void *b )

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
    fprintf( c, "digraph calls\n{\n  overlap=false; splines=true; size=\"7.75,10.25\"; orientation=portrait; sep=0.1; nodesep=0.1;\n" );

    /* firstly write out the nodes in each subgraph - dest side clustered */
    HASH_SORT( subcallList, _calls_dst_sort_fn );
    s = subcallList;

    while ( s )
    {
        if ( s->dsth->fileindex != INTERRUPT )
        {
            fprintf( c, "  subgraph \"cluster_%s\"\n  {\n    label=\"%s\";\n    bgcolor=lightgrey;\n", SymbolFilename( ss, s->dsth->fileindex ), SymbolFilename( ss, s->dsth->fileindex ) );
            fileidx = s->dsth->fileindex;

            while ( s && ( fileidx == s->dsth->fileindex ) )
            {
                /* Now output each function in the subgraph */
                fprintf( c, "    %s [style=filled, fillcolor=white];\n", SymbolFunction( ss, s->dsth->functionindex )  );
                functionidx = s->dsth->functionindex;

                /* Spin forwards until the function name _or_ filename changes */
                while ( ( s ) && ( functionidx == s->dsth->functionindex ) && ( fileidx == s->dsth->fileindex ) )
                {
                    s = s->hh.next;
                }
            }
        }
        else
        {
            s = s->hh.next;
        }

        fprintf( c, "  }\n\n" );
    }

    /* now write out the nodes in each subgraph - source side clustered */
    HASH_SORT( subcallList, _calls_src_sort_fn );
    s = subcallList;

    while ( s )
    {
        fprintf( c, "  subgraph \"cluster_%s\"\n  {\n    label=\"%s\";\n    bgcolor=lightgrey;\n", SymbolFilename( ss, s->srch->fileindex ), SymbolFilename( ss, s->srch->fileindex ) );
        fileidx = s->srch->fileindex;

        while ( s && ( fileidx == s->srch->fileindex ) )
        {
            if ( s->srch->fileindex != INTERRUPT )
            {
                /* Now output each function in the subgraph */
                fprintf( c, "    %s [style=filled, fillcolor=white];\n", SymbolFunction( ss, s->srch->functionindex )  );
                functionidx = s->srch->functionindex;

                /* Spin forwards until the function name _or_ filename changes */
                while ( ( s ) && ( functionidx == s->srch->functionindex ) && ( fileidx == s->srch->fileindex ) )
                {
                    s = s->hh.next;
                }
            }
            else
            {
                s = s->hh.next;
            }
        }

        fprintf( c, "  }\n\n" );
    }

    /* Now go through and label the arrows... */

    s = subcallList;

    while ( s )
    {
        functionidx = s->srch->functionindex;
        dfunctionidx = s->dsth->functionindex;
        cnt = s->count;
        s = s->hh.next;

        while ( ( s ) && ( functionidx == s->srch->functionindex ) && ( dfunctionidx == s->dsth->functionindex ) )
        {
            cnt += s->count;
            s = s->hh.next;
        }

        fprintf( c, "    %s -> ", SymbolFunction( ss, functionidx ) );
        fprintf( c, "%s [label=%" PRIu64 ", weight=0.1];\n", SymbolFunction( ss, dfunctionidx ), cnt );
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
bool ext_ff_outputProfile( char *profile, char *elffile, char *deleteMaterial, uint64_t timelen,
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
    fprintf( c, "creator: orbprofile\npositions: instr line\nevent: Inst : CPU Instructions\nevent: Visits : Visits to source line\nevents: Inst Visits\n" );

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

        fprintf( c, "%" PRIu64 " %" PRIu64 "\n", f->count, f->scount );


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
        fprintf( c, "0x%08x %d %" PRIu64 " %" PRIu64 "\n", s->sig.src, s->srch->line, s->myCost, s->count );
        s = s->hh.next;
    }

    fclose( c );

    return true;
}
// ====================================================================================================
