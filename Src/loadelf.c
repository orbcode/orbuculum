#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <gelf.h>
#include <ctype.h>
#include <dwarf.h>
#include <libdwarf.h>

#include "loadelf.h"
#include "generics.h"
#include "readsource.h"

#define DP_MAX_LINE_LEN (4095)
#define IS_INFO (true)

static char _print_buffer[DP_MAX_LINE_LEN];

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Internal routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================

static int _compareMem( const void *a, const void *b )
{
    const symbolMemaddr as = ( ( struct symbolMemoryStore * )a )->start;
    const symbolMemaddr bs = ( ( struct symbolMemoryStore * )b )->start;

    if ( as < bs )
    {
        return -1;
    }

    if ( as > bs )
    {
        return 1;
    }

    return 0;
}

// ====================================================================================================

static int _compareFunc( const void *a, const void *b )
{
    const symbolMemaddr as = ( *( struct symbolFunctionStore ** )a )->lowaddr;
    const symbolMemaddr bs = ( *( struct symbolFunctionStore ** )b )->lowaddr;

    if ( as < bs )
    {
        return -1;
    }

    if ( as > bs )
    {
        return 1;
    }

    return 0;
}

// ====================================================================================================

static int _matchFunc( const void *a, const void *b )
{
    const unsigned int key = *( uint64_t * )a;
    const symbolMemaddr as = ( *( struct symbolFunctionStore ** )b )->lowaddr;
    const symbolMemaddr ae = ( *( struct symbolFunctionStore ** )b )->highaddr;

    if ( key < as )
    {
        return -1;
    }

    if ( key > ae )
    {
        return 1;
    }

    return 0;
}

// ====================================================================================================

static int _compareLineMem( const void *a, const void *b )

{
    const symbolMemaddr as = ( *( struct symbolLineStore ** )a )->lowaddr;
    const symbolMemaddr bs = ( *( struct symbolLineStore ** )b )->lowaddr;

    if ( as < bs )
    {
        return -1;
    }

    if ( as > bs )
    {
        return 1;
    }

    return 0;
}

// ====================================================================================================

static int _matchLine( const void *a, const void *b )
{
    const symbolMemaddr key = *( uint64_t * )a;
    const symbolMemaddr as = ( *( struct symbolLineStore ** )b )->lowaddr;
    const symbolMemaddr ae = ( *( struct symbolLineStore ** )b )->highaddr;

    if ( key < as )
    {
        return -1;
    }

    if ( key > ae )
    {
        return 1;
    }

    return 0;
}
// ====================================================================================================

static bool _readProg( struct symbol *p )

{
    Elf *e;
    Elf_Scn *scn = NULL;
    GElf_Shdr shdr;
    size_t shstrndx;
    char *name;

    if ( elf_version( EV_CURRENT ) == EV_NONE )
    {
        fprintf( stderr, "ELF library initialization failed : %s ", elf_errmsg ( -1 ) );
        return p;
    }

    if ( ( e = elf_begin( p->fd, ELF_C_READ, NULL ) ) == NULL )
    {
        fprintf( stderr, "ELF Begin failed\n" );
        return false;
    }

    if ( elf_getshdrstrndx( e, &shstrndx ) != 0 )
    {
        fprintf( stderr, "elf_getshdrstrndx failed: %s\n", elf_errmsg( -1 ) );
        return false;
    }

    /* Read this into memory by iterating over sections */
    while ( ( scn = elf_nextscn( e, scn ) ) != NULL )
    {

        if ( gelf_getshdr( scn, &shdr ) != &shdr )
        {
            fprintf( stderr, "getshdr () failed : %s\n", elf_errmsg( -1 ) );
            return false;
        }

        if ( ( name = elf_strptr ( e, shstrndx, shdr.sh_name ) ) == NULL )
        {
            fprintf( stderr, " elf_strptr () failed : %s\n", elf_errmsg ( -1 ) );
            return false;
        }

        if ( ( shdr.sh_flags & SHF_ALLOC ) && ( shdr.sh_type == SHT_PROGBITS ) )
        {
            Elf_Data *data = NULL;

            /* This is program code or data; Allocate a new section */
            if ( ( data = elf_rawdata ( scn, data ) ) != NULL )
            {
                p->mem = ( struct symbolMemoryStore * )realloc( p->mem, ( p->nsect_mem + 1 ) * sizeof( struct symbolMemoryStore ) );
                struct symbolMemoryStore *n = p->mem + p->nsect_mem;
                p->nsect_mem++;

                n->start = shdr.sh_addr;
                n->len   = shdr.sh_size;
                n->name  = strdup( name );
                n->data  = ( uint8_t * )malloc( n->len );
                memmove( n->data, data->d_buf, n->len );
            }
        }
    }

    /* Sort mem sections into order so it's straightforward to find matches */
    qsort( p->mem, p->nsect_mem, sizeof( struct symbolMemoryStore ), _compareMem );
    p->cachedSearchIndex = -1;
    elf_end( e );
    return p;
}

// ====================================================================================================

int _findOrAddString( const char *stringToFindorAdd, char ***table, unsigned int *elementCount )

{
    /* Find the string in the specified table (and return its index) or create a new string record and return that index */
    for ( int i = 0; i < *elementCount; i++ )
    {
        if ( !strcmp( ( *table )[i], stringToFindorAdd ) )
        {
            return i;
        }
    }

    /* This string doesn't exist...add it and then return the index */
    *table = ( char ** )realloc( *table, sizeof( char * ) * ( ( *elementCount ) + 1 ) );
    ( *table )[*elementCount] = strdup( stringToFindorAdd );

    ( *elementCount )++;
    return ( *elementCount ) - 1;
}

// ====================================================================================================

#ifdef TESTING_LOADELF
static void _dump_die_attribs( Dwarf_Debug dbg, Dwarf_Die die )

{
    Dwarf_Attribute *attrs;
    Dwarf_Signed attr_count;
    const char *name;

    if ( DW_DLV_OK != dwarf_attrlist( die, &attrs, &attr_count, 0 ) )
    {
        fprintf( stderr, "Requesting attributes failed\n" );
        return;
    }

    // Iterate through attributes
    for ( int i = 0; i < attr_count; ++i )
    {
        Dwarf_Half attr;
        Dwarf_Attribute attr_ptr = attrs[i];

        if ( DW_DLV_OK != dwarf_whatattr( attr_ptr, &attr, 0 ) )
        {
            fprintf( stderr, "Failed to itentify attribute\n" );
            goto terminate;
        }

        dwarf_get_AT_name( attr, &name );
        fprintf( stderr, "Attribute Name: %s\n", name );
    }

    // Free attribute list
terminate:
    dwarf_dealloc( dbg, attrs, DW_DLA_LIST );
}
#endif

// ====================================================================================================

static void _getSourceLines( struct symbol *p, Dwarf_Debug dbg, Dwarf_Die die )


{
    Dwarf_Unsigned version;
    Dwarf_Line_Context linecontext;
    Dwarf_Line *linebuf;
    Dwarf_Signed linecount;
    Dwarf_Small tc;
    Dwarf_Addr line_addr;
    Dwarf_Addr tracked_addr;
    bool zero_start_dont_store;
    char *file_name;
    Dwarf_Unsigned line_num;
    Dwarf_Bool begin;
    Dwarf_Bool isset;

    /* Now, for each source line, pull it into the line storage */
    if ( DW_DLV_OK == dwarf_srclines_b( die, &version, &tc, &linecontext, 0 ) )
    {
        dwarf_srclines_from_linecontext( linecontext, &linebuf, &linecount, 0 );
        tracked_addr = 0;
        zero_start_dont_store = false;

        /* If a line address starts at zero, or is a direct continuation of a line that started at zero, then we dispose of it */
        /* We consider any line that is within 16 bytes of the previous one to be a continuation, to allow for padding.        */
        for ( int i = 0; i < linecount; ++i )
        {
            dwarf_line_is_addr_set( linebuf[i], &isset, 0 );
            dwarf_lineaddr( linebuf[i], &line_addr, 0 );
            dwarf_linebeginstatement( linebuf[i], &begin, 0 );

            if ( ( isset ) && ( line_addr == 0 ) )
            {
                zero_start_dont_store = true;
            }

            if ( ( zero_start_dont_store && ( ( !begin ) || ( !line_addr ) || ( ( line_addr - tracked_addr ) < 16 ) ) ) )
            {
                zero_start_dont_store = true;
            }
            else
            {
                zero_start_dont_store = false;
                dwarf_lineno( linebuf[i], &line_num, 0 );
                dwarf_linesrc( linebuf[i], &file_name, 0 );

                p->line = ( struct symbolLineStore ** )realloc( p->line, sizeof( struct symbolLineStore * ) * ( p->nlines + 1 ) );
                struct symbolLineStore *newLine = p->line[p->nlines] = ( struct symbolLineStore * )calloc( 1, sizeof( struct symbolLineStore ) );
                p->nlines++;
                newLine->startline = line_num;
                newLine->lowaddr = line_addr;
                newLine->isinline = true;
                newLine->filename = _findOrAddString( file_name, &p->stringTable[PT_FILENAME],  &p->tableLen[PT_FILENAME] );
            }

            tracked_addr = line_addr;
        }

        dwarf_srclines_dealloc_b( linecontext );
    }
}

// ====================================================================================================

void  _dwarf_error( Dwarf_Error e, void *ptr )
{
    fprintf( stderr, "Reached error:%s\n", dwarf_errmsg( e ) );
    exit( -1 );
}

// ====================================================================================================

void _dwarf_print( void *p, const char *line )

{
    fprintf( stderr, "%s", line );
}

// ====================================================================================================

static void _processFunctionDie( struct symbol *p, Dwarf_Debug dbg, Dwarf_Die die, int filenameN, int producerN, Dwarf_Addr cu_base_addr )

{
    char *name = NULL;
    char *manglename = NULL;
    Dwarf_Addr h = 0;
    Dwarf_Addr l = 0;
    enum Dwarf_Form_Class formclass = DW_FORM_CLASS_UNKNOWN;

    Dwarf_Attribute attr_data;
    Dwarf_Half attr_tag;
    bool isinline = false;
    struct symbolFunctionStore *newFunc;

    Dwarf_Off specification_offset;
    Dwarf_Die specification_die;

    /* See if this is an inline die usage */
    attr_tag = DW_AT_abstract_origin;

    if ( DW_DLV_OK == dwarf_attr( die, attr_tag, &attr_data, 0 ) )
    {
        /* It is, so track back to the real one */
        Dwarf_Off abstract_origin_offset;
        Dwarf_Die abstract_origin_die;
        attr_tag = DW_AT_abstract_origin;
        dwarf_attr( die, attr_tag, &attr_data, 0 );
        dwarf_global_formref( attr_data, &abstract_origin_offset, 0 );
        if (DW_DLV_OK == dwarf_offdie_b( dbg, abstract_origin_offset, IS_INFO, &abstract_origin_die, 0 ))
        {
            isinline = true;
            name_die = abstract_origin_die;
        }
    }

    dwarf_highpc_b ( die, &h, 0, &formclass, 0 );
    dwarf_lowpc ( die, &l, 0 );

    if ( formclass == DW_FORM_CLASS_CONSTANT )
    {
        h += l;
    }

    specification_die = die;

    /* Get the possibly mangled linkage name if it exists */
    if ( DW_DLV_OK == dwarf_attr( die, DW_AT_linkage_name, &attr_data, 0 ) )
    {
        dwarf_formstring( attr_data, &manglename, 0 );
    }

    if ( DW_DLV_OK != dwarf_diename( die, &name, 0 ) )
    {
        /* Name will be hidden in a specification reference */
        attr_tag = DW_AT_specification;

        if ( dwarf_attr( die, attr_tag, &attr_data, 0 ) == DW_DLV_OK )
        {
            dwarf_attr( die, attr_tag, &attr_data, 0 );

            if ( DW_DLV_OK == dwarf_global_formref( attr_data, &specification_offset, 0 ) )
            {
                dwarf_offdie_b( dbg, specification_offset, IS_INFO, &specification_die, 0 );
                dwarf_diename( specification_die, &name, 0 );
            }
        }
    }

    if ( name && l && h )
    {
        p->func = ( struct symbolFunctionStore ** )realloc( p->func, sizeof( struct symbolFunctionStore * ) * ( p->nfunc + 1 ) );
        newFunc = p->func[p->nfunc] = ( struct symbolFunctionStore * )calloc( 1, sizeof( struct symbolFunctionStore ) );
        newFunc->isinline = isinline;
        p->nfunc++;

        newFunc->funcname  = strdup( name );
        newFunc->producer  = producerN;
        newFunc->filename  = filenameN;
        newFunc->lowaddr   = l;
        newFunc->highaddr  = h - 1;

        if ( manglename )
        {
            newFunc->manglename = strdup( manglename );
        }

        /* Collect start of function line and column */
        attr_tag = DW_AT_decl_line;

        if ( dwarf_attr( specification_die, attr_tag, &attr_data, 0 ) == DW_DLV_OK )
        {
            Dwarf_Unsigned no;
            dwarf_formudata( attr_data, &no, 0 );
            newFunc->startline = no;
        }

        attr_tag = DW_AT_decl_column;

        if ( dwarf_attr( specification_die, attr_tag, &attr_data, 0 ) == DW_DLV_OK )
        {
            Dwarf_Unsigned no;
            dwarf_formudata( attr_data, &no, 0 );
            newFunc->startcol = no;
        }
    }
}

// ====================================================================================================

static void _processDie( struct symbol *p, Dwarf_Debug dbg, Dwarf_Die die, int level, int filenameN, int producerN, Dwarf_Addr cu_base_addr )

{
    Dwarf_Half tag;
    Dwarf_Die child;

    Dwarf_Die sib = die;

    while ( DW_DLV_OK == dwarf_siblingof_b( dbg, sib, IS_INFO, &sib, 0 ) )
    {
        dwarf_tag( sib, &tag, 0 );
        const char *n;
        dwarf_get_TAG_name( tag, &n );

        if ( ( tag == DW_TAG_subprogram ) || ( tag == DW_TAG_inlined_subroutine ) )
        {
            _processFunctionDie( p, dbg, sib, filenameN, producerN, cu_base_addr );
        }
    }

    if ( DW_DLV_OK == dwarf_child( die, &child, 0 ) )
    {
        _processDie( p, dbg, child, level + 1, filenameN, producerN, cu_base_addr );
        dwarf_dealloc( dbg, child, DW_DLA_DIE );
    }
}

// ====================================================================================================

static bool _isAbsPath( const char *p )

{
    /* Path is absolute if it starts with / or x: where x is in the range A..Z. */
    return (
                       ( p[0] == '/' ) ||
                       ( ( toupper( p[0] ) >= 'A' ) && ( toupper( p[0] ) <= 'Z' ) && ( p[1] == ':' ) )
           );
}

// ====================================================================================================

static char *_joinPaths( const char *p1, const char *p2 )

{
    if ( _isAbsPath( p2 ) )
    {
        return strdup( p2 );
    }
    else
    {
        char *res = ( char * )malloc( strlen( p1 ) + strlen( p2 ) + 2 );
        strcpy( res, p1 );
        strcat( res, "/" );
        strcat( res, p2 );
        return res;
    }
}

// ====================================================================================================

static bool _readLines( struct symbol *p )
{
    Dwarf_Debug dbg;
    Dwarf_Error err;
    Dwarf_Unsigned cu_header_length = 0;
    Dwarf_Half     version_stamp = 0;
    Dwarf_Off      abbrev_offset = 0;
    Dwarf_Addr     cu_low_addr;
    Dwarf_Half     address_size = 0;
    Dwarf_Unsigned next_cu_header = 0;
    Dwarf_Die      cu_die = NULL;

    bool retval = false;
    Dwarf_Half dw_length_size = 0;
    Dwarf_Half dw_extension_size = 0;
    Dwarf_Sig8 dw_type_signature;
    Dwarf_Unsigned dw_typeoffset = 0;
    Dwarf_Half dw_header_cu_type = DW_UT_compile;

    char *name;
    char *producer;
    char *compdir;

    unsigned int filenameN;
    unsigned int producerN;

    if ( 0 != dwarf_init_b( p->fd, DW_GROUPNUMBER_ANY, NULL, NULL, &dbg, &err ) )
    {
        return false;
    }

    struct Dwarf_Printf_Callback_Info_s print_setup =
    {
        .dp_user_pointer = p,
        .dp_fptr = &_dwarf_print,
        .dp_buffer = _print_buffer,
        .dp_buffer_len = DP_MAX_LINE_LEN,
        .dp_buffer_user_provided = true,
        .dp_reserved = NULL

    };

    dwarf_register_printf_callback( dbg, &print_setup );

    /* Add an empty string to each string table, so the 0th element is the empty string in all cases */
    for ( enum symbolTables pt = 0; pt < PT_NUMTABLES; pt++ )
    {
        _findOrAddString( "", &p->stringTable[pt],  &p->tableLen[pt] );
    }

    /* 1: Collect the functions and lines */
    /* ---------------------------------- */
    while ( true )
    {
        memset( &dw_type_signature, 0, sizeof( dw_type_signature ) );

        if ( DW_DLV_OK != dwarf_next_cu_header_d( dbg, true, &cu_header_length,
                &version_stamp, &abbrev_offset, &address_size,
                &dw_length_size, &dw_extension_size, &dw_type_signature,
                &dw_typeoffset, &next_cu_header, &dw_header_cu_type, 0 ) )
        {
            break;
        }

        dwarf_siblingof_b( dbg, NULL, IS_INFO, &cu_die, 0 );

        dwarf_diename( cu_die, &name, 0 );
        dwarf_die_text( cu_die, DW_AT_producer, &producer, 0 );
        dwarf_die_text( cu_die, DW_AT_comp_dir, &compdir, 0 );

        /* Need to construct the fully qualified filename from the directory + filename */
        char *s = _joinPaths( compdir, name );
        filenameN  = _findOrAddString( s, &p->stringTable[PT_FILENAME],  &p->tableLen[PT_FILENAME] );
        free( s );
        producerN =  _findOrAddString( producer, &p->stringTable[PT_PRODUCER],  &p->tableLen[PT_PRODUCER] );

        /* Kickoff the process for the DIE and its children to get the functions in this cu */

        dwarf_lowpc( cu_die, &cu_low_addr, 0 );
        _processDie( p, dbg, cu_die, 0, filenameN, producerN, cu_low_addr );

        /* ...and the source lines */
        _getSourceLines( p, dbg, cu_die );

        dwarf_dealloc( dbg, cu_die, DW_DLA_DIE );
    }

    if ( p->nlines && p->nfunc )
    {
        /* 2: We have the lines and functions. Clean them up and interlink them so they're useful to applications */
        /* ------------------------------------------------------------------------------------------------------ */
        /* Sort tables into address order, just in case they're not ... no gaurantees from the DWARF */
        qsort( p->line, p->nlines, sizeof( struct symbolLineStore * ), _compareLineMem );
        qsort( p->func, p->nfunc, sizeof( struct symbolFunctionStore * ), _compareFunc );

        /* Combine addresses in the lines table which have the same memory location...those aren't too useful for us      */
        int nlines = 0;
        struct symbolLineStore **nls = NULL;

        for ( int i = 0; i < p->nlines - 1; i++ )
        {
            nls = ( struct symbolLineStore ** )realloc( nls, sizeof( struct symbolLineStore * ) * ( nlines + 1 ) );

            if ( !nls )
            {
                genericsExit( -1, "Memory allocation failure" EOL );
            }

            nls[nlines] = p->line[i];

            /* Roll forward through all lines which have the same start address */
            while ( ( ++i < p->nlines - 1 ) &&
                    ( ( nls[nlines]->filename == p->line[i]->filename ) ) &&
                    ( ( nls[nlines]->lowaddr == p->line[i]->lowaddr ) ) )
            {
                /* This line needs to be freed in memory 'cos otherwise there is no reference to it anywhere */
                free( p->line[i] );
            }

            nlines++;
        }

        free( p->line );
        p->line = nls;
        p->nlines = nlines;

        nlines = 0;
        nls = NULL;

        /* Now do the same for lines with the same line number and file */
        /* We can also set the high memory extent for each line here */
        for ( int i = 0; i < p->nlines - 1; i++ )
        {
            nls = ( struct symbolLineStore ** )realloc( nls, sizeof( struct symbolLineStore * ) * ( nlines + 1 ) );
            nls[nlines] = p->line[i];

            while ( ( ++i < p->nlines - 1 ) &&
                    ( nls[nlines]->startline == p->line[i]->startline ) &&
                    ( nls[nlines]->filename == p->line[i]->filename ) )
            {
                free( p->line[i] );
            }

            nls[nlines]->highaddr = p->line[i]->lowaddr - 1;
            nlines++;
        }

        free( p->line );
        p->line = nls;
        p->nlines = nlines;

        if ( !p->nlines )
        {
            fprintf( stderr, "No lines found in file\n" );
        }
        else
        {
            p->line[p->nlines - 1]->highaddr = p->line[p->nlines - 1]->lowaddr + 2;
            p->line[0]->lowaddr = p->line[0]->highaddr - 2;

            /* Allocate lines to functions ... these will be in address order 'cos the lines already are */
            for ( int i = 0; i < p->nlines; i++ )
            {
                struct symbolFunctionStore *f = symbolFunctionAt( p, p->line[i]->lowaddr );
                p->line[i]->function = f;
                p->line[i]->isinline = false;

                if ( f )
                {
                    f->line = ( struct symbolLineStore ** )realloc( f->line, sizeof( struct symbolLineStore * ) * ( f->nlines + 1 ) );
                    f->line[f->nlines] = p->line[i];
                    f->nlines++;
                }
            }

            retval = true;
        }
    }

    dwarf_finish( dbg );

    return retval;
}

// ====================================================================================================

static bool _loadSource( struct symbol *p )

{
    char *r;
    size_t l;

    /* We need to aqquire source code for all of the files that we have an entry in the stringtable, so let's start by making room */
    p->source = ( struct symbolSourcecodeStore ** )calloc( 1, sizeof( struct symbolSourcecodeStore * )*p->tableLen[PT_FILENAME] );

    for ( int i = 0; i < p->tableLen[PT_FILENAME]; i++ )
    {
        r = readsourcefile( p->stringTable[PT_FILENAME][i], &l );

        /* Create an entry for this file. It will remain zero (NULL) if there are no lines in it, because r was NULL */
        struct symbolSourcecodeStore *store = p->source[i] = ( struct symbolSourcecodeStore * )calloc( 1, sizeof( struct symbolSourcecodeStore ) );

        /* Lines in sio.c are demarked by \n, \r or \0 ... so we just need to find the indicies to one after each of those */
        while ( l )
        {
            /* Add this line to the storage. */
            store->linetext = ( char ** )realloc( store->linetext, sizeof( char * ) * ( store->nlines + 1 ) );
            store->linetext[store->nlines++] = r;

            /* Spin forwards for next newline or eof */
            while ( ( --l > 0 ) && ( *r++ != '\n' ) ) {};

            if ( l )
            {
                *r++ = 0;
                l--;
            }
        }
    }

    return true;
}

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Publically available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================

const char *symbolSource( struct symbol *p, unsigned int fileNumber, unsigned int lineNumber )

/* Return pointer to source code for specified line in file index */

{
    assert( p );

    if ( ( fileNumber < p->tableLen[PT_FILENAME] ) && p->source && ( lineNumber < p->source[fileNumber]->nlines ) )
    {
        return ( const char * )p->source[fileNumber]->linetext[lineNumber];
    }
    else
    {
        return NULL;
    }
}
// ====================================================================================================

struct symbolFunctionStore *symbolFunctionAt( struct symbol *p, symbolMemaddr addr )

/* Return function that encloses specified address, or NULL */

{
    assert( p );
    struct symbolFunctionStore **fp = ( struct symbolFunctionStore ** )bsearch( &addr, p->func, p->nfunc, sizeof( struct symbolFunctionStore * ), _matchFunc );

    return fp ? *fp : NULL;
}

// ====================================================================================================

struct symbolFunctionStore *symbolFunctionIndex( struct symbol *p, unsigned int index )

/* Get indexed function, or NULL if out of range */

{
    assert( p );
    return ( index < p->nfunc ) ? p->func[index] : NULL;
}

// ====================================================================================================
struct symbolLineStore *symbolLineIndex( struct symbol *p, unsigned int index )

/* Get indexed line, or NULL if out of range */

{
    assert( p );
    return ( index < p->nlines ) ? p->line[index] : NULL;
}
// ====================================================================================================

struct symbolLineStore *symbolFunctionLineIndex( struct symbolFunctionStore *f, unsigned int index )

/* Get indexed line with distinct memory address covered by function */

{
    assert( f );
    return ( index < f->nlines ) ? f->line[index] : NULL;
}
// ====================================================================================================

struct symbolLineStore *symbolLineAt( struct symbol *p, symbolMemaddr addr )

/* Return line covered by specified memory address, or NULL */

{
    assert( p );
    struct symbolLineStore **l = ( struct symbolLineStore ** )bsearch( &addr, p->line, p->nlines, sizeof( struct symbolLineStore * ), _matchLine );
    return l ? *l : NULL;
}

// ====================================================================================================

const char *symbolGetProducer( struct symbol *p, unsigned int index )

/* Get command line that produced this code */

{
    assert( p );
    return ( index < p->tableLen[PT_PRODUCER] ) ? p->stringTable[PT_PRODUCER][index] : NULL;
}

// ====================================================================================================

const char *symbolGetFilename( struct symbol *p, unsigned int index )

/* Get filename string for specified index */

{
    assert( p );
    return ( index < p->tableLen[PT_FILENAME] ) ? p->stringTable[PT_FILENAME][index] : NULL;
}

// ====================================================================================================

symbolMemptr symbolCodeAt( struct symbol *p, symbolMemaddr addr, unsigned int *len )

/* Get pointer to memory at specified address...can move backwards and forwards through the region */

{
    assert( p );
    int i;

    /* A speedup in case we're looking in the same region as previously */
    if ( ( p->cachedSearchIndex != -1 ) &&
            ( p->mem[p->cachedSearchIndex].start < addr ) &&
            ( addr - p->mem[p->cachedSearchIndex].start < p->mem[p->cachedSearchIndex].len ) )
    {
        if ( len )
        {
            *len = p->mem[p->cachedSearchIndex].len - ( addr - p->mem[p->cachedSearchIndex].start );
        }

        return &( p->mem[p->cachedSearchIndex].data[addr - p->mem[p->cachedSearchIndex].start] );
    }

    /* Search backwards for candidate section for memory to be in. This could be       */
    /* slightly more efficient as a binary search, but setup would offset any benefits */
    for ( i = p->nsect_mem - 1; i && p->mem[i].start > addr; i-- );

    /* Are we within this code? */
    if ( addr - p->mem[i].start < p->mem[i].len )
    {
        p->cachedSearchIndex = i;

        if ( len )
        {
            *len = p->mem[i].len - ( addr - p->mem[i].start );
        }

        return &( p->mem[i].data[addr - p->mem[i].start] );
    }
    else
    {
        p->cachedSearchIndex = -1;
        return NULL;
    }
}

// ====================================================================================================

void symbolDelete( struct symbol *p )

/* Delete symbol set */

{
    if ( p )
    {
        /* We are done with any elf that might have been open */
        if ( p->fd >= 0 )
        {
            /* There is no point in nulling p->fd cos we will delete p anyway */
            close( p->fd );
        }

        /* Close the disassembler if it's in use */
        if ( !p->caphandle )
        {
            cs_close( &p->caphandle );
        }

        if ( p->nsect_mem )
        {
            for ( int i = p->nsect_mem - 1; i; i-- )
            {
                if ( p->mem[i].name )
                {
                    free( p->mem[i].name );

                    if ( p->mem[i].data )
                    {
                        free( p->mem[i].data );
                    }
                }
            }

            free( p->mem );
        }

        while ( p->nfunc )
        {
            struct symbolFunctionStore *f = p->func[--p->nfunc];

            if ( f->funcname )
            {
                /* Remove the functionName, assuming we have one */
                free( f->funcname );
            }

            if ( f->manglename )
            {
                /* Remove the mangled name, assuming we have one */
                free( f->manglename );
            }

            if ( f->line )
            {
                /* ...and any source code cross-references */
                free( f->line );
            }

            free( f );
        }

        /* Flush the string tables */
        for ( enum symbolTables pt = 0; pt < PT_NUMTABLES; pt++ )
        {
            while ( p->tableLen[pt] )
            {
                free( p->stringTable[pt][--p->tableLen[pt]] );
            }
        }

        /* Flush the source code line records */
        for ( int i = 0; i < p->nlines; i++ )
        {
            free( p->line[i] );
        }

        if ( p->line )
        {
            free( p->line );
        }

        /* Remove any source code we might be holding */
        for ( int i = 0; i < p->tableLen[PT_FILENAME]; i++ )
        {
            /* Text is all allocated in one block by readsource, so just deleting the firt element is enough */
            free( p->source[i]->linetext[0] );

            /* ...and the block of pointers to lines in that text */
            free( p->source[i] );
        }

        free( p );
    }

    p = NULL;
}

// ====================================================================================================

char *symbolDisassembleLine( struct symbol *p, enum instructionClass *ic, symbolMemaddr addr, symbolMemaddr *newaddr )

/* Return assembly code representing this line */

{
    cs_insn *insn;
    size_t count;
    static char op[255];

    if ( newaddr )
    {
        *newaddr = NO_ADDRESS;
    }

    *ic = 0;

    if ( !p->caphandle )
    {
        /* Disassembler isn't initialised yet */
        if ( cs_open( CS_ARCH_ARM, CS_MODE_THUMB + CS_MODE_LITTLE_ENDIAN + CS_MODE_MCLASS, &p->caphandle ) != CS_ERR_OK )
        {
            return NULL;
        }

        cs_option( p->caphandle, CS_OPT_DETAIL, CS_OPT_ON );
    }

    symbolMemptr m = symbolCodeAt( p, addr, NULL );

    if ( !m )
    {
        /* If we don't have memory then we can't decode */
        return NULL;
    }

    count = cs_disasm( p->caphandle, m, 4, addr, 0, &insn );
    *ic = LE_IC_NONE;


    if ( count > 0 )
    {
        /* Characterise the instruction using rules from F1.3 of ARM IHI0064H.a */

        /* Check instruction size */
        *ic |= ( insn->size == 4 ) ? LE_IC_4BYTE : 0;

        /* Was it a subroutine call? */
        *ic |= ( ( insn->id == ARM_INS_BL ) || ( insn->id == ARM_INS_BLX ) ) ? LE_IC_JUMP | LE_IC_CALL : 0;

        /* Was it a regular call? */
        *ic |= ( ( insn->id == ARM_INS_B )    || ( insn->id == ARM_INS_BX )  || ( insn->id == ARM_INS_ISB ) ||
                 ( insn->id == ARM_INS_WFI )  || ( insn->id == ARM_INS_WFE ) || ( insn->id == ARM_INS_TBB ) ||
                 ( insn->id == ARM_INS_TBH )  || ( insn->id == ARM_INS_BXJ ) || ( insn->id == ARM_INS_CBZ ) ||
                 ( insn->id == ARM_INS_CBNZ ) || ( insn->id == ARM_INS_WFI ) || ( insn->id == ARM_INS_WFE )
               ) ? LE_IC_JUMP : 0;

        *ic |=  (
                            ( ( ( insn->id == ARM_INS_SUB ) || ( insn->id == ARM_INS_MOV ) ||
                                ( insn->id == ARM_INS_LDM ) || ( insn->id == ARM_INS_POP ) )
                              && strstr( insn->op_str, "pc" ) )
                ) ? LE_IC_JUMP : 0;

        /* create a copy to check if load in pc */
        char *copy = strdup(insn->op_str);
        *ic |=  (
                            ( ( ( insn->id == ARM_INS_LDR ) )
                              && strstr(strtok(copy,","), "pc" ) )
                ) ? LE_IC_JUMP : 0;
        free(copy);
        
        /* Was it an exception return? */
        *ic |=  ( ( insn->id == ARM_INS_ERET ) ) ? LE_IC_JUMP | LE_IC_IRET : 0;


        /* Add text describing instruction */
        if ( *ic & LE_IC_4BYTE )
        {
            sprintf( op, "%8"PRIx64":   %02x%02x %02x%02x   %s %s", insn->address, insn->bytes[1], insn->bytes[0], insn->bytes[3], insn->bytes[2], insn->mnemonic, insn->op_str );
        }
        else
        {
            sprintf( op, "%8"PRIx64":   %02x%02x        %s  %s", insn->address, insn->bytes[1], insn->bytes[0], insn->mnemonic, insn->op_str  );
        }

        /* Check to see if operands are immediate */
        cs_detail *detail = insn->detail;

        if ( detail->arm.op_count )
        {

            for ( int n = 0; n <  insn->detail->arm.op_count; n++ )
            {
                if ( insn->detail->arm.operands[n].type == ARM_OP_IMM )
                {
                    *ic |= LE_IC_IMMEDIATE;

                    if ( newaddr )
                    {
                            *newaddr = detail->arm.operands[n].imm;
                    }

                    break;
                }
            }
        }


        /* Add classifications ( for debug ) */
        //if ( *ic )
        //            {
        //                fsprintf ( stderr, &op[strlen( op )], " ; %s%s%s%s",  *ic & LE_IC_JUMP ? "JUMP " : "", *ic & LE_IC_CALL ? "CALL " : "", *ic & LE_IC_IMMEDIATE ? "IMM " : "", *ic & LE_IC_IRET ? "IRET " : "" );
        //            }
    }
    else
    {
        sprintf( op, "No disassembly" );
    }

    cs_free( insn, count );

    return op;
}

// ====================================================================================================

bool symbolSetValid( struct symbol *p )

/* Check if current symbols are valid. True if we've read them and the file we read from hasn't */
/* evaporated in the meantime. Not a perfect heuristic, but pretty reasonable.                  */

{
#define ELF_MAGIC (0x464c457f)
    uint32_t magicMatch;

    if ( ( p ) && ( p->fd >= 0 ) )
    {
        /* See if we can read from this file */
        lseek( p->fd, 0, SEEK_SET );

        if ( sizeof( magicMatch ) == read( p->fd, &magicMatch, sizeof( magicMatch ) ) )
        {
            if ( ELF_MAGIC == magicMatch )
            {
                return true;
            }
        }
    }

    return false;
}

// ====================================================================================================

struct symbol *symbolAcquire( char *filename, bool loadmem, bool loadsource )

/* Collect symbol set with specified components */

{
    struct symbol *p = ( struct symbol * )calloc( 1, sizeof( struct symbol ) );

    /* O_BINARY Only needed on platforms that differentiate between binary and text files */
#ifndef O_BINARY

    if ( ( p->fd = open( filename, O_RDONLY, 0 ) ) < 0 )
#else
    if ( ( p->fd = open( filename, O_RDONLY | O_BINARY, 0 ) ) < 0 )
#endif
    {
        free( p );
        return NULL;
    }

    /* Load the memory image if this was requested...if it fails then we fail */
    if ( loadmem && ( !_readProg( p ) ) )
    {
        symbolDelete( p );
        return NULL;
    }

    /* Load the functions and source code line mappings if requested */
    if ( !_readLines( p ) )
    {
        symbolDelete( p );
        return NULL;
    }

    /* ...finally, the source code if requested. This can only be done if mem or functions we requested */
    if ( ( loadsource && loadmem )  && !_loadSource( p ) )
    {
        symbolDelete( p );
        return NULL;
    }

    return p;
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Test routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================

/* Test routines can be built with;
 * gcc -DTESTING_LOADELF loadelf.c readsource.c -I../Inc -I../subprojects/libdwarf-0.7.0/src/lib/libdwarf -ggdb -lcapstone -lelf ../build/subprojects/libdwarf-0.7.0/src/lib/libdwarf/libdwarf.so.0
 */

#ifdef TESTING_LOADELF

bool _listLines( struct symbol *p )

{
    int index = 0;
    struct symbolLineStore *l;

    while ( ( l = symbolLineIndex( p, index++ ) ) )
    {
        fprintf( stderr, "   " MEMADDRF "..." MEMADDRF " %4d ( %s )" EOL, l->lowaddr, l->highaddr, l->startline, symbolGetFilename( p, l->filename ) );
    }
}

// ====================================================================================================

bool _listFunctions( struct symbol *p, bool includeLines )

{
    int iter = 0;
    struct symbolFunctionStore *f;
    struct symbolLineStore *l;

    while ( f = symbolFunctionIndex( p, iter++ ) )
    {
        fprintf( stderr, MEMADDRF "..." MEMADDRF " %s ( %s %d,%d ) %s" EOL, f->lowaddr, f->highaddr, f->funcname, symbolGetFilename( p, f->filename ), f->startline, f->startcol,
                 f->manglename ? f->manglename : "" );

        if ( includeLines )
        {
            int iter2 = 0;

            while ( l = symbolFunctionLineIndex( f, iter2++ ) )
            {
                fprintf( stderr, "   " MEMADDRF "..." MEMADDRF " %4d ( %s )" EOL, l->lowaddr, l->highaddr, l->startline, symbolGetFilename( p, l->filename ) );

                if ( ( l->function != f ) || ( l->filename != f->filename ) )
                {
                    fprintf( stderr, "*****DATA INCONSISTENCY" EOL );
                }
            }
        }
    }
}

// ====================================================================================================

bool _listFile( struct symbol *p, int fileNo )

{
    const char *t;
    int i = 0;

    while ( t = symbolSource( p, fileNo, i++ ) )
    {
        fprintf( stderr, "%s", t );
    }
}

// ====================================================================================================

bool _disassemble( struct symbol *p, symbolMemaddr a, unsigned int len )


{
    enum instructionClass ic;
    symbolMemaddr addr;
    symbolMemaddr newaddr;

    for ( addr = a; addr < a + len; addr += ic & LE_IC_4BYTE ? 4 : 2 )
    {
        char *u = symbolDisassembleLine( p, &ic, addr, &newaddr );
        fprintf( stderr, "%s\n", u );
    }

    return true;
}
// ====================================================================================================

void main( int argc, char *argv[] )

{
    enum instructionClass ic;
    struct symbol *p = symbolAcquire( argv[1], true, true );

    if ( !p )
    {
        fprintf( stderr, "Failed to aquire" EOL );
        exit( -1 );
    }

    struct symbolLineStore *s;

    for ( int i = 0; i < p->nlines; i++ )
    {
        s = symbolLineIndex( p, i );

        fprintf( stderr, "\n%08x ... %08x %s %s", ( uint32_t )s->lowaddr, ( uint32_t )s->highaddr, s->isinline ? "INLINE" : "", symbolSource( p, s->filename, s->startline - 1 ) );

        if ( ( s->lowaddr > 0x08000000 ) && ( s->highaddr != -1 ) )
            for ( symbolMemaddr b = s->lowaddr; b < s->highaddr; )
            {
                fprintf( stderr, "         %s\n", symbolDisassembleLine( p, &ic, b, NULL ) );
                b += ic & LE_IC_4BYTE ? 4 : 2;
            }
    }
}
// ====================================================================================================
#endif
