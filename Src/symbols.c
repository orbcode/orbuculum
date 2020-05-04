/*
 * Symbol recovery from elf file
 * =============================
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
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <elf.h>
#include <stdint.h>
#include <assert.h>
#include "generics.h"
#include "symbols.h"

#define TEXT_SEGMENT ".text"

#define ELF_RELOAD_DELAY_TIME 1000000   /* Time before elf reload will be attempted when its been lost */

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Internal Routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
bool _symbolsLoad( struct SymbolSet *s )

/* Load symbols from bfd library compatible file */

{
    uint32_t storage;
    bool dynamic = false;
    char **matching;

    bfd_init();

    /* Get information about the file being used */
    stat ( s->elfFile, &( s->st ) );
    s->abfd = bfd_openr( s->elfFile, NULL );

    if ( !s->abfd )
    {
        genericsReport( V_ERROR, "Couldn't open ELF file" EOL );
        return false;
    }

    s->abfd->flags |= BFD_DECOMPRESS;

    if ( bfd_check_format( s->abfd, bfd_archive ) )
    {
        genericsReport( V_ERROR, "Cannot get addresses from archive %s" EOL, s->elfFile );
        return false;
    }

    if ( ! bfd_check_format_matches ( s->abfd, bfd_object, &matching ) )
    {
        genericsReport( V_ERROR, "Ambigious format for file" EOL );
        return false;
    }

    if ( ( bfd_get_file_flags ( s->abfd ) & HAS_SYMS ) == 0 )
    {
        genericsReport( V_ERROR, "No symbols found" EOL );
        return false;
    }

    storage = bfd_get_symtab_upper_bound ( s->abfd ); /* This is returned in bytes */

    if ( storage == 0 )
    {
        storage = bfd_get_dynamic_symtab_upper_bound ( s->abfd );
        dynamic = true;
    }

    s->syms = ( asymbol ** )malloc( storage );

    if ( dynamic )
    {
        s->symcount = bfd_canonicalize_dynamic_symtab ( s->abfd, s->syms );
    }
    else
    {
        s->symcount = bfd_canonicalize_symtab ( s->abfd, s->syms );
    }

    s->sect = bfd_get_section_by_name( s->abfd, TEXT_SEGMENT );
    return true;
}
// ====================================================================================================
bool SymbolLookup( struct SymbolSet *s, uint32_t addr, struct nameEntry *n, char *deleteMaterial )

/* Lookup function for address to line, and hence to function */

{
    const char *function = NULL;
    const char *filename = NULL;

    uint32_t line;

    assert( s );

    // Work around for changes in binutils 2.34
#ifdef bfd_get_section_vma
    uint32_t workingAddr = addr - bfd_get_section_vma( s->abfd, s->sect );
#else
    uint32_t workingAddr = addr - bfd_section_vma( s->sect );
#endif
    
    if ( workingAddr <= bfd_section_size( s->abfd, s->sect ) )
    {
        if ( bfd_find_nearest_line( s->abfd, s->sect, s->syms, workingAddr, &filename, &function, &line ) )
        {

            /* Remove any frontmatter off filename string that matches */
	  if (( deleteMaterial ) && ( filename ))
            {
                char *m = deleteMaterial;

                while ( ( *m ) && ( *filename ) && ( *filename == *m ) )
                {
                    m++;
                    filename++;
                }
            }

            n->filename = filename ? filename : "";
            n->function = function ? function : "";
            n->addr = addr;
            n->line = line;
            return true;
        }
    }

    if ( addr >= INT_ORIGIN )
    {
        n->filename = "";
        n->function = "INTERRUPT";
        n->addr = INTERRUPT;
        n->line = 0;
        return false;
    }

    n->filename = "Unknown";
    n->function = "Unknown";
    n->addr = NOT_FOUND;
    n->line = 0;
    return false;
}
// ====================================================================================================
struct SymbolSet *SymbolSetCreate( char *filename )

{
    struct SymbolSet *s = ( struct SymbolSet * )calloc( sizeof( struct SymbolSet ), 1 );
    s->elfFile = strdup( filename );

    if ( !_symbolsLoad( s ) )
    {
        free( s->elfFile );
        free( s );
        s = NULL;
    }

    return s;
}
// ====================================================================================================
void SymbolSetDelete( struct SymbolSet **s )

{
    if ( ( *s ) && ( ( *s )->abfd ) )
    {
        bfd_close( ( *s )->abfd );
        free( ( *s )->elfFile );
        free( *s );
        *s = NULL;
    }
}
// ====================================================================================================
bool SymbolSetCheckValidity( struct SymbolSet **s, char *filename )

{
    struct stat n;
    stat( filename, &n );

    /* We check filesize, modification time and status change time for any differences */
    if ( ( !( *s ) ) ||
            ( memcmp( &n.st_size, &( ( *s )->st.st_size ), sizeof( off_t ) ) ) ||
            ( memcmp( &n.st_mtim, &( ( *s )->st.st_mtim ), sizeof( struct timespec ) ) ) ||
            ( memcmp( &n.st_ctim, &( ( *s )->st.st_ctim ), sizeof( struct timespec ) ) )
       )
    {
        /* There was either no file, or a difference, re-create the symbol set */
        SymbolSetDelete( s );

        /* Since something changed, let's wait a while before trying to reload */
        usleep( ELF_RELOAD_DELAY_TIME );
        *s = SymbolSetCreate( filename );
    }

    return ( ( *s ) != NULL );
}
// ====================================================================================================
