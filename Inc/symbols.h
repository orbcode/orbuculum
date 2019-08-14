#ifndef _SYMBOLS_H_
#define _SYMBOLS_H_

#include <stdbool.h>
#include "bfd_wrapper.h"
#include "uthash.h"

#define INT_ORIGIN   0xFFFFFFF8              /* Special memory address - interrupt origin */


#define NOT_FOUND    0x1                     /* Special address flag - not found */
#define INTERRUPT    0x3                     /* Special address flag - interrupt */
#define SLEEPING     0x5                     /* Special address flag sleeping */

/* An entry in the names table */
struct nameEntry
{
    const char *filename;
    const char *function;
    uint32_t index;
    uint32_t line;
    uint32_t addr;
};

struct SymbolSet
{
    /* Symbol table related info */
    asymbol **syms;                         /* Symbol table */
    struct stat st;  /* Stat of the file that was accessed for the symbols */
    uint32_t symcount;
    bfd *abfd;                              /* BFD handle to file */
    asection *sect;                         /* Address data for the program section */
    char *elfFile;                           /* File containing structure info */
};

// ====================================================================================================
struct SymbolSet *SymbolSetCreate( char *filename );
void SymbolSetDelete( struct SymbolSet **s );
bool SymbolSetCheckValidity( struct SymbolSet **s, char *filename );
bool SymbolLookup( struct SymbolSet *s, uint32_t addr, struct nameEntry *n, char *deleteMaterial ) ;
// ====================================================================================================
#endif
