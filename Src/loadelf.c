#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <gelf.h>
#include <dwarf.h>
#include <elfutils/libdw.h>


#include "loadelf.h"
#include "generics.h"

#define MAX_LINE_LEN (4095)
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

static bool _readProg( int fd, struct symbol *p )

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

    if ( ( e = elf_begin( fd, ELF_C_READ, NULL ) ) == NULL )
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

        //            printf("%c ADDR=%08lx Type=%8x Flags=%04lx Size=%08lx Name=%s\n",((shdr.sh_flags & SHF_ALLOC) && (shdr.sh_type==SHT_PROGBITS))?'L':' ', shdr.sh_addr, shdr.sh_type, shdr.sh_flags, shdr.sh_size, name);

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

static int _processDie( Dwarf_Die *d, void *pp )

{
    /* This is the callback set in dwarf_getfuncs below to actually populate the function record */
    struct symbol *p = ( struct symbol * )pp;

    Dwarf_Attribute dirattr;
    Dwarf_Attribute producerattr;
    Dwarf_Attribute typeattr;
    Dwarf_Addr h = -1;
    Dwarf_Addr l = -1;
    Dwarf_Addr e = -1;

    struct symbolFunctionStore *newFunc;

    Dwarf_Die cudie;

    dwarf_highpc ( d, &h );
    dwarf_lowpc ( d, &l );
    dwarf_entrypc ( d, &e );

    if ( l != h )
    {
        dwarf_diecu ( d, &cudie, NULL, NULL );
        dwarf_attr( &cudie, DW_AT_comp_dir, &dirattr );
        dwarf_attr( &cudie, DW_AT_producer, &producerattr );
        dwarf_attr( d, DW_AT_type, &typeattr );

        /* This has length, so it's valid ... create a new function entry */
        p->func = ( struct symbolFunctionStore ** )realloc( p->func, sizeof( struct symbolFunctionStore * ) * ( p->nfunc + 1 ) );
        newFunc = p->func[p->nfunc] = ( struct symbolFunctionStore * )calloc( 1, sizeof( struct symbolFunctionStore ) );
        p->nfunc++;

        newFunc->funcname  = strdup( dwarf_diename( d ) );
        newFunc->producer  = _findOrAddString( dwarf_formstring( &producerattr ), &p->stringTable[PT_PRODUCER],  &p->tableLen[PT_PRODUCER] );
        const char *directory    = dwarf_formstring( &dirattr );
        const char *filename     = dwarf_decl_file( d );

        if ( filename )
        {
            if ( filename[0] != '/' )
            {
                /* Need to construct the fully qualified filename from the directory + filename */
                char *s = ( char * )malloc( strlen( filename ) + strlen( directory ) + 2 );
                strcpy( s, directory );
                strcat( s, "/" );
                strcat( s, filename );
                newFunc->filename  = _findOrAddString( s, &p->stringTable[PT_FILENAME],  &p->tableLen[PT_FILENAME] );
                free( s );
            }
            else
            {
                /* This string is already fully qualified, so just use it */
                newFunc->filename  = _findOrAddString( filename, &p->stringTable[PT_FILENAME],  &p->tableLen[PT_FILENAME] );
            }
        }

        dwarf_decl_line( d, ( int * )&newFunc->startline );
        dwarf_decl_column( d, ( int * )&newFunc->startcol );
        newFunc->lowaddr = l;
        newFunc->highaddr = h - 1;
        newFunc->entryaddr = e;
        newFunc->isInline = dwarf_func_inline( d );
    }

    return DWARF_CB_OK;
}

// ====================================================================================================

static bool _readLines( int fd, struct symbol *p )
{
    Dwarf *dw;

    if ( ( dw = dwarf_begin( fd, DWARF_C_READ ) ) == NULL )
    {
        fprintf( stderr, "Couldn't start DWARF\n" );
        return false;
    }

    /* Add an empty string to each string table, so the 0th element is the empty string in all cases */
    for ( enum symbolTables pt = 0; pt < PT_NUMTABLES; pt++ )
    {
        _findOrAddString( "", &p->stringTable[pt],  &p->tableLen[pt] );
    }

    /* Now let's iterate over all the cu's. Iterator starts with cu=NULL and off=0 */
    Dwarf_CU *cu = NULL;
    Dwarf_Off off = 0;
    Dwarf_Files *srcfiles = NULL;
    size_t nfiles = 0;
    Dwarf_Lines *srclines = NULL;
    size_t nlines = 0;

    /* Collect the functions and lines */
    /* ------------------------------- */
    while ( 0 == dwarf_next_lines( dw, off, &off, &cu, &srcfiles, &nfiles, &srclines, &nlines ) )
    {
        Dwarf_Die die;
        Dwarf_Die subdie;
        uint8_t unitId;
        Dwarf_Half version;
        ptrdiff_t offset = 0;

        /* Start off by populating the functions */
        if ( dwarf_cu_info ( cu, &version, &unitId, &die, &subdie, NULL, NULL, NULL ) == 0 )
        {
            while ( ( offset = dwarf_getfuncs ( &die, _processDie, p, offset ) ) > 0 );
        }

        /* Now read in the lines */
        for ( int t = 0; t < nlines; t++ )
        {
            Dwarf_Line *l = dwarf_onesrcline ( srclines, t );
            Dwarf_Addr a;
            dwarf_lineaddr( l, &a );

            /* Only proceed if this line has a non-zero address ...this heuristic does mean we'll miss code starting at 0 */
            if ( ( a ) )
            {
                const char *const *dirtable;
                size_t ndirs;
                int line;
                Dwarf_Word mtime;
                Dwarf_Word length;
                const char *filesrc;

                dwarf_lineno( l, &line );
                dwarf_getsrcdirs ( srcfiles, &dirtable, &ndirs );
                filesrc = dwarf_filesrc ( srcfiles, 1, &mtime, &length );

                /* Create a new line and populate it */
                p->line = ( struct symbolLineStore ** )realloc( p->line, sizeof( struct symbolLineStore * ) * ( p->nlines + 1 ) );
                struct symbolLineStore *newLine = p->line[p->nlines] = ( struct symbolLineStore * )calloc( 1, sizeof( struct symbolLineStore ) );
                p->nlines++;
                newLine->startline = line;
                newLine->lowaddr = a;

                if ( filesrc && ( filesrc[0] != '/' ) )
                {
                    /* Need to construct the fully qualified filename from the directory + filename */
                    char *s = ( char * )malloc( strlen( filesrc ) + strlen( dirtable[0] ) + 2 );
                    strcpy( s, dirtable[0] );
                    strcat( s, "/" );
                    strcat( s, filesrc );
                    newLine->filename  = _findOrAddString( s, &p->stringTable[PT_FILENAME],  &p->tableLen[PT_FILENAME] );
                    free( s );
                }
                else
                {
                    /* This string is already fully qualified, so just use it */
                    newLine->filename  = _findOrAddString( filesrc, &p->stringTable[PT_FILENAME],  &p->tableLen[PT_FILENAME] );
                }
            }
        }
    }

    /* We have the lines and functions. Clean them up and interlink them so they're useful to applications */
    /* --------------------------------------------------------------------------------------------------- */
    /* Sort tables into address order, just in case they're not ... no gaurantees from the DWARF */
    qsort( p->func, p->nfunc, sizeof( struct symbolFunctionStore * ), _compareFunc );
    qsort( p->line, p->nlines, sizeof( struct symbolLineStore * ), _compareLineMem );

    /* Combine lines in the lines table which have the same address...that isn't too useful for us      */
    /* and set the ranges too, in order to make searching easier. Note that this will set the startline */
    /* parameter to the _last_ line of the shared-address block, which seems to work best.              */
    for ( int i = 1; i < p->nlines; i++ )
    {
        while ( ( i < p->nlines ) && ( ( p->line[i]->lowaddr == p->line[i - 1]->lowaddr ) ) )
        {
            /* This line needs to be freed in memory 'cos otherwise there is no reference to it anywhere */
            free( p->line[i - 1] );

            /* ...and move the following lines down */
            for ( int j = i; j < p->nlines; j++ )
            {
                p->line[j - 1] = p->line[j];
            }

            p->nlines--;
        }
    }

    /* Now do the same for lines with the same line number and file */
    /* We can also set the high memory extent for each line here */
    for ( int i = 1; i < p->nlines; i++ )
    {
        while ( ( i < p->nlines ) &&
                ( p->line[i]->startline == p->line[i - 1]->startline ) &&
                ( p->line[i]->filename == p->line[i - 1]->filename ) )
        {
            free( p->line[i - 1] );

            for ( int j = i; j < p->nlines; j++ )
            {
                p->line[j - 1] = p->line[j];
            }

            p->nlines--;
        }

        p->line[i - 1]->highaddr = p->line[i]->lowaddr - 1;
    }

    p->line[p->nlines - 1]->highaddr = -1;

    /* Finally, allocate lines to functions ... these will be in address order 'cos the lines already are */
    for ( int i = 0; i < p->nlines; i++ )
    {
        struct symbolFunctionStore *f = symbolFunctionAt( p, p->line[i]->lowaddr );
        p->line[i]->function = f;

        if ( f )
        {
            f->line = ( struct symbolLineStore ** )realloc( f->line, sizeof( struct symbolLineStore * ) * ( f->nlines + 1 ) );
            f->line[f->nlines] = p->line[i];
            f->nlines++;
        }
    }

    dwarf_end( dw );

    return true;
}

// ====================================================================================================

static bool _loadSource( struct symbol *p )

{
    char *w = NULL;
    size_t m = 0;
    FILE *fd;
    char commandLine[MAX_LINE_LEN];
    bool notProcess;

    /* We need to aqquire source code for all of the files that we have an entry in the stringtable, so let's start by making room */
    p->source = ( struct symbolSourcecodeStore ** )calloc( 1, sizeof( struct symbolSourcecodeStore * )*p->tableLen[PT_FILENAME] );

    for ( int i = 0; i < p->tableLen[PT_FILENAME]; i++ )
    {
        /* Try and grab the file via a prettyprinter. If that doesn't work, grab it via cat */
        if ( getenv( "ORB_PRETTYPRINTER" ) )
        {
            /* We have an environment variable containing the prettyprinter...lets use that */
            snprintf( commandLine, MAX_LINE_LEN, "%s %s", getenv( "ORB_PRETTYPRINTER" ), p->stringTable[PT_FILENAME][i] );
        }
        else
        {
            /* No environment variable, use the default */
            snprintf( commandLine, MAX_LINE_LEN, "source-highlight -f esc -o STDOUT -i %s 2>/dev/null", p->stringTable[PT_FILENAME][i] );
        }

        fd = popen( commandLine, "r" );
        /* Perform a single read...this will lead to eof if the command wasn't valid */
        getline( &w, &m, fd );

        if ( feof( fd ) )
        {
            pclose( fd );
            notProcess = true;

            if ( !( fd = fopen( p->stringTable[PT_FILENAME][i], "r" ) ) )
            {
                continue;
            }

            getline( &w, &m, fd );
        }

        /* Create an entry for this file. It will be zero (NULL) if there are no lines in it */
        struct symbolSourcecodeStore *store = p->source[i] = ( struct symbolSourcecodeStore * )calloc( 1, sizeof( struct symbolSourcecodeStore ) );

        while ( !feof( fd ) )
        {
            /* Add this line to the storage. We strdup the line storage because the getline call tends to be too generous */
            store->linetext = ( char ** )realloc( store->linetext, sizeof( char * ) * ( store->nlines + 1 ) );
            store->linetext[store->nlines++] = strdup( w );
            getline( &w, &m, fd );
        }

        /* Close the process or file, depending on what it was that actually got opened */
        notProcess ? fclose( fd ) : pclose( fd );
    }

    free( w );
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

    if ( ( fileNumber < p->tableLen[PT_FILENAME] ) && ( lineNumber < p->source[fileNumber]->nlines ) )
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
            for ( int j = 0; j < p->source[i]->nlines; j++ )
            {
                free( p->source[i]->linetext );
            }

            free( p->source[i] );
        }

        free( p );
    }
}

// ====================================================================================================

char *symbolDisssembleLine( struct symbol *p, enum instructionClass *ic, symbolMemaddr addr, symbolMemaddr *newaddr )

/* Return assembly code representing this line */

{
    cs_insn *insn;
    size_t count;
    static char op[255];

    *newaddr = NO_ADDRESS;

    if ( !p->caphandle )
    {
        /* Disassembler isn't initialised yet */
        if ( cs_open( CS_ARCH_ARM, CS_MODE_THUMB + CS_MODE_LITTLE_ENDIAN, &p->caphandle ) != CS_ERR_OK )
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

    if ( count > 0 )
    {
        cs_detail *detail = insn[0].detail;

        if ( detail->arm.op_count )
        {
            *newaddr = detail->arm.operands[0].imm;
        }

        if ( insn[0].size == 2 )
        {
            sprintf( op, "%8"PRIx64":   %02x%02x        %s  %s", insn[0].address, insn[0].bytes[1], insn[0].bytes[0], insn[0].mnemonic, insn[0].op_str  );
            *ic = LE_IC_NONE;
        }
        else
        {
            sprintf( op, "%8"PRIx64":   %02x%02x %02x%02x   %s %s", insn[0].address, insn[0].bytes[1], insn[0].bytes[0], insn[0].bytes[3], insn[0].bytes[2], insn[0].mnemonic, insn[0].op_str );
            *ic = LE_IC_4BYTE;
        }

        /* Fill in other characteristics of this instruction */
        for ( int n = 0; n < detail->groups_count; n++ )
        {
            switch ( detail->groups[n] )
            {
                case CS_GRP_JUMP:
                case CS_GRP_BRANCH_RELATIVE:
                    *ic |= LE_IC_ISJUMP;
                    break;

                case CS_GRP_CALL:
                    *ic |= LE_IC_CALL;
                    break;

                case CS_GRP_RET:
                    *ic |= LE_IC_RET;
                    break;

                case CS_GRP_INT:
                    *ic |= LE_IC_INT;
                    break;

                case CS_GRP_IRET:
                    *ic |= LE_IC_INTRET;
                    break;

            }
        }
    }
    else
    {
        sprintf( op, "No disassembly" );
    }

    cs_free( insn, count );

    return op;
}

// ====================================================================================================

struct symbol *symbolAqquire( char *filename, bool loadlines, bool loadmem, bool loadsource )

/* Collect symbol set with specified components */

{
    int fd;
    struct symbol *p = NULL;

    if ( ( fd = open( filename, O_RDONLY, 0 ) ) < 0 )
    {
        return NULL;
    }

    p = ( struct symbol * )calloc( 1, sizeof( struct symbol ) );

    /* Load the memory image if this was requested...if it fails then we fail */
    if ( loadmem && ( !_readProg( fd, p ) ) )
    {
        symbolDelete( p );
        close( fd );
        return NULL;
    }

    /* Load the functions and source code line mappings if requested */
    if ( !_readLines( fd, p ) )
    {
        symbolDelete( p );
        close( fd );
        return NULL;
    }

    close( fd );

    /* ...finally, the source code if requested. This can only be done if mem or functions we requested */
    if ( ( loadsource && ( loadmem || loadlines ) )  && !_loadSource( p ) )
    {
        symbolDelete( p );
        return NULL;
    }

    return p;
}
// ====================================================================================================
bool symbolSetValid( struct symbol *p, char *filename )

/* Check if current symbols are valid */

{
    return p != NULL;
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Test routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================

#ifdef TESTING_LOADELF

bool _listLines( struct symbol *p )

{
    int index = 0;
    struct symbolLineStore *l;

    while ( ( l = symbolLineIndex( p, index++ ) ) )
    {
        printf( "   " MEMADDRF "..." MEMADDRF " %4d ( %s )" EOL, l->lowaddr, l->highaddr, l->startline, symbolGetFilename( p, l->filename ) );
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
        printf( MEMADDRF "..." MEMADDRF " %s ( %s %d,%d )" EOL, f->lowaddr, f->highaddr, f->funcname, symbolGetFilename( p, f->filename ), f->startline, f->startcol );

        if ( includeLines )
        {
            int iter2 = 0;

            while ( l = symbolFunctionLineIndex( f, iter2++ ) )
            {
                printf( "   " MEMADDRF "..." MEMADDRF " %4d ( %s )" EOL, l->lowaddr, l->highaddr, l->startline, symbolGetFilename( p, l->filename ) );

                if ( ( l->function != f ) || ( l->filename != f->filename ) )
                {
                    printf( "*****DATA INCONSISTENCY" EOL );
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
        printf( "%s", t );
    }
}

// ====================================================================================================

bool _disassemble( struct symbol *p, symbolMemaddr a, unsigned int len )


{
    bool isJump, is4byte;
    symbolMemaddr addr;

    for ( addr = a; addr < a + len; addr += is4byte ? 4 : 2 )
    {
        char *u = symbolAssemblyLine( p, &isJump, &is4byte, addr );
        printf( "%s   %s\n", u, isJump ? "JUMP" : "" );
    }

    return true;
}
// ====================================================================================================

void main( int argc, char *argv[] )

{
    struct symbol *p = symbolAqquire( argv[1], true, true, true );

    if ( !p )
    {
        printf( "Failed to aqquire" EOL );
        exit( -1 );
    }

    _listFunctions( p, true );
    _listFile( p, 9 );
    _disassemble( p, 0x08000214, 60 );
    symbolDelete( p );
}
// ====================================================================================================
#endif
