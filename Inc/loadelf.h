
#ifndef _LOADELF_H_

#include <stdbool.h>
#include <capstone/capstone.h>

typedef unsigned long int symbolMemaddr;
typedef unsigned char *symbolMemptr;

#define MEMADDRF "%08lx"
#define NO_LINE        (-1)
#define NO_FILE        (-1)
#define NO_DESTADDRESS (-1)
#define NO_ADDRESS     (-1)

/* Structure for a memory segment */
struct symbolMemoryStore
{
    symbolMemaddr   start;                 /* Low address of the memory segment */
    symbolMemaddr   len;                   /* Length of the memory segment */
    char           *name;                  /* Name of the segment as defined by the linker */
    symbolMemptr    data;                  /* Contents of the segment */
};


/* Structure for a line memory region identified in the image */
struct symbolLineStore
{
    unsigned int               filename;   /* Filename index for this line */
    unsigned int               startline;  /* First line in source code covered by this line region */
    bool isinline;
    symbolMemaddr              lowaddr;    /* Minimum address this line covers */
    symbolMemaddr              highaddr;   /* Max address this line covers */
    struct symbolFunctionStore *function;  /* Function encompassing this line (or NULL if none is identified */
};

/* Structure for a Function identified in the image */
struct symbolFunctionStore
{
    char                      *funcname;   /* What is the name of the function */
    unsigned int               producer;   /* What code/options produced it? */
    unsigned int               filename;   /* What filename + path off the source root? */
    unsigned int               startline;  /* Start line in source file of function */
    unsigned int               startcol;   /* Start col in source file of function */
    unsigned int               endline;    /* End line in source file of function */
    symbolMemaddr              lowaddr;    /* Lowest address of function */
    symbolMemaddr              highaddr;   /* Highest address of function */
    bool                       isinline;   /* Is this an abstract template for a function? */
    struct symbolLineStore   **line;       /* Lines comprising this function */
    unsigned int               nlines;     /* Number of lines in line number storage */
};

struct symbolSourcecodeStore
{
    char                     **linetext;   /* Table of text lines in this file */


    unsigned int               nlines;     /* Number of text lines in this file */
};

enum symbolTables { PT_PRODUCER, PT_FILENAME, PT_NUMTABLES };

struct symbol
{
    char **stringTable[PT_NUMTABLES];      /* Strings that we don't want to duplicate, so we give them an index */
    unsigned int tableLen[PT_NUMTABLES];   /* Number of strings for each of the deduplication tables */

    struct symbolSourcecodeStore **source; /* Table for source code lines, indexed by file number */

    struct symbolMemoryStore *mem;         /* Table of memory regions, sorted according to start address */
    unsigned int nsect_mem;                /* Number of entries in memory region table */

    struct symbolFunctionStore **func;     /* Table of functions, sorted by start address */
    unsigned int nfunc;                    /* Number of entries in function table */

    struct symbolLineStore **line;         /* Table of source code address indexes, sorted by start address */
    unsigned int nlines;                   /* Number of lines in source code line table */

    unsigned int cachedSearchIndex;        /* Cached memory search region, to speed up memory fetches */


    csh caphandle;
};

enum instructionClass { LE_IC_NONE, LE_IC_JUMP = ( 1 << 0 ), LE_IC_4BYTE = ( 1 << 1 ), LE_IC_CALL = ( 1 << 2 ),  LE_IC_IMMEDIATE = ( 1 << 3 ), LE_IC_IRET = ( 1 << 4 ) };

// ====================================================================================================

/* Return pointer to source code for specified line in file index */
const char *symbolSource( struct symbol *p, unsigned int fileNumber, unsigned int lineNumber );

/* Return function that encloses specified address, or NULL */
struct symbolFunctionStore *symbolFunctionAt( struct symbol *p, symbolMemaddr addr );

/* Get indexed function, or NULL if out of range */
struct symbolFunctionStore *symbolFunctionIndex( struct symbol *p, unsigned int index );

/* Get indexed line, or NULL if out of range */
struct symbolLineStore *symbolLineIndex( struct symbol *p, unsigned int index );

/* Get indexed line with distinct memory address covered by function */
struct symbolLineStore *symbolFunctionLineIndex( struct symbolFunctionStore *f, unsigned int index );

/* Return line covered by specified memory address, or NULL */
struct symbolLineStore *symbolLineAt( struct symbol *p, symbolMemaddr addr );

/* Get command line that produced this file (compilation unit) */
const char *symbolGetProducer( struct symbol *p, unsigned int index );

/* Get filename string for specified index */
const char *symbolGetFilename( struct symbol *p, unsigned int index );

/* Get pointer to memory at specified address...can move backwards and forwards through the region */
symbolMemptr symbolCodeAt( struct symbol *p, symbolMemaddr addr, unsigned int *len );

/* Return assembly code representing this line, with annotations */
char *symbolDisassembleLine( struct symbol *p, enum instructionClass *ic, symbolMemaddr addr, symbolMemaddr *newaddr );

/* Delete symbol set */
void symbolDelete( struct symbol *p );

/* Collect symbol set with specified components */
struct symbol *symbolAquire( char *filename, bool loadlines, bool loadmem, bool loadsource );

/* Check if current symbols are valid */
bool symbolSetValid( struct symbol *p, char *filename );

// ====================================================================================================

#endif
