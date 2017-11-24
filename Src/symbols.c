/*
 * Symbol recovery from elf file
 * =============================
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
 * go through the binutils source to find your way around it.
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
#include "generics.h"
#include "symbols.h"

#define TEXT_SEGMENT ".text"

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
        fprintf( stderr, "Couldn't open ELF file" EOL );
        return false;
    }

    s->abfd->flags |= BFD_DECOMPRESS;

    if ( bfd_check_format( s->abfd, bfd_archive ) )
    {
        fprintf( stderr, "Cannot get addresses from archive %s" EOL, s->elfFile );
        return false;
    }

    if ( ! bfd_check_format_matches ( s->abfd, bfd_object, &matching ) )
    {
        fprintf( stderr, "Ambigious format for file" EOL );
        return false;
    }

    if ( ( bfd_get_file_flags ( s->abfd ) & HAS_SYMS ) == 0 )
    {
        fprintf( stderr, "No symbols found" EOL );
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

    uint32_t workingAddr = addr - bfd_get_section_vma( s->abfd, s->sect );

    if ( workingAddr <= bfd_section_size( s->abfd, s->sect ) )
    {
        if ( bfd_find_nearest_line( s->abfd, s->sect, s->syms, workingAddr, &filename, &function, &line ) )
        {

            /* Remove any frontmatter off filename string that matches */
            if ( deleteMaterial )
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
void SymbolSetDelete( struct SymbolSet *s )

{
    if ( s->abfd )
    {
        bfd_close( s->abfd );
        free( s->elfFile );
        free( s );
        s = NULL;
    }
}
// ====================================================================================================
bool SymbolSetCheckValidity( struct SymbolSet **s, char *filename )

{
    struct stat n;
    stat( filename, &n );

    /* We check filesize, modification time and status change time for any differences */
    if ( ( memcmp( &n.st_size, &( ( *s )->st.st_size ), sizeof( off_t ) ) ) ||
            ( memcmp( &n.st_mtim, &( ( *s )->st.st_mtim ), sizeof( struct timespec ) ) ) ||
            ( memcmp( &n.st_ctim, &( ( *s )->st.st_ctim ), sizeof( struct timespec ) ) )
       )
    {
        /* There was a difference, re-create the symbol set */
        SymbolSetDelete( *s );
        *s = SymbolSetCreate( filename );
        return false;
    }

    return true;
}
// ====================================================================================================
