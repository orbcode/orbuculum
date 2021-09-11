/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Symbol Management
 * =================
 *
 */

#ifndef _SYMBOLS_H_
#define _SYMBOLS_H_

#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "uthash.h"

#define ASSY_NOT_FOUND    0xffffffff        /* Assembly line not found */
#define NO_LINE           0xffffffff        /* No line number defined */
#define NO_FILE           0xffffffff        /* No file defined */
#define NO_FUNCTION       0xffffffff        /* No function defined */

#define SPECIALS_MASK     0xfffffff0
#define FN_SLEEPING       (SPECIALS_MASK|0xb)         /* Marker for sleeping case */
#define FN_SLEEPING_STR   "** Sleeping **"            /* String for sleeping case */

#define INTERRUPT         (SPECIALS_MASK|0xd)
#define FN_INTERRUPT_STR  "INTERRUPT"

/* Mapping of lines numbers to indicies */
struct assyLineEntry

{
    uint32_t addr;                          /* Address of this assembly */
    char *label;                            /* Any associated label */
    char *lineText;                         /* Text of the line */
    char *assy;                             /* Pointer to the start of the assembly in the lineText above */
    uint32_t codes;                         /* Binary code for the line */
    bool is4Byte;                           /* Indicate if this is a 4 byte entry */
    bool isJump;                            /* This is a potential jump */
    bool isSubCall;                         /* this is a subrouine call (BL/BLX) */
    bool isReturn;                          /* this is a return instruction (i.e. branch to LR or pop into PC) */
    uint32_t jumpdest;                      /* If this is an absolute jump, the destination */
};

/* Full string name for a file */
struct fileEntry

{
    char *name;                             /* Path to file */
};

/* Full details for a function */
struct functionEntry

{
    char *name;                             /* Name of function */
    uint32_t startAddr;                     /* Start address */
    uint32_t endAddr;                       /* End address */
    uint32_t fileEntryIdx;                  /* Link back to containing file */
};

/* Details for a source line */
struct sourceLineEntry

{
    uint32_t startAddr;                     /* Start of this line in memory */
    uint32_t endAddr;                       /* End of this line in memory */
    uint32_t lineNo;                        /* Line number in source file */
    char *lineText;                         /* All source text relating to this memory range */
    uint16_t linesInBlock;                  /* How many lines in the source file correspond to this line? */
    uint32_t assyLines;                     /* Number of lines of assembly for this memory range */
    struct assyLineEntry *assy;             /* Assembly entries for this memory range */

    uint32_t functionIdx;                   /* Index back to function this line is part of */
    uint32_t fileIdx;                       /* Index back to file this line is part of */

};

/* The full set of symbols */
struct SymbolSet
{
    char *elfFile;                         /* File containing structure info */
    char *deleteMaterial;                  /* Material to strip off filenames */

    struct stat st;

    /* For memory saving and speedup... */
    bool recordSource;                     /* Keep a record of source code */
    bool recordAssy;                       /* Keep a record of assembly code */
    bool demanglecpp;                      /* If we want C++ names demangling */

    /* For file mapping... */
    uint32_t sourceCount;                  /* Number of source lines we have loaded */


    uint32_t fileCount;                    /* Number of files we have loaded */
    struct fileEntry *files;               /* Table of files */
    uint32_t functionCount;                /* Number of functions we have loaded */
    struct functionEntry *functions;       /* Table of functions */
    struct sourceLineEntry *sources;       /* Table of sources */
};

/* An entry in the names table ... what we return to our caller */
struct nameEntry
{
    uint32_t fileindex;                    /* Index of filename */
    uint32_t functionindex;                /* Index of functionname */
    uint32_t line;                         /* Source line containing the address */
    uint16_t linesInBlock;                 /* Number of lines in this text block */
    const char *source;                    /* Corresponding source text */
    const struct assyLineEntry *assy;      /* Corresponding assembly text */
    uint32_t assyLine;                     /* Line of assembly text */
    uint32_t addr;                         /* Matched address */
    uint32_t index;
};

// ====================================================================================================
struct SymbolSet *SymbolSetCreate( const char *filename, const char *deleteMaterial, bool demanglecpp, bool recordSource, bool recordAssy );

void SymbolSetDelete( struct SymbolSet **s );
bool SymbolSetValid( struct SymbolSet **s, char *filename );
const char *SymbolFilename( struct SymbolSet *s, uint32_t index );
const char *SymbolFunction( struct SymbolSet *s, uint32_t index );
bool SymbolLookup( struct SymbolSet *s, uint32_t addr, struct nameEntry *n );
// ====================================================================================================
#endif
