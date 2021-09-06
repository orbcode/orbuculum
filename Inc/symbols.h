/*
 * Symbol Management
 * =================
 *
 * Copyright (C) 2017, 2019, 2021  Dave Marples  <dave@marples.net>
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

#ifndef _SYMBOLS_H_
#define _SYMBOLS_H_

#include <stdbool.h>
#include "uthash.h"

#define EXC_RETURN      0xF0000000        /* interrupt origin */
#define EXC_RETURN_MASK 0xF0000000
#define INT_ORIGIN_MASK 0x0000000F
#define INT_ORIGIN_HANDLER     0x1
#define INT_ORIGIN_MAIN_STACK  0x9
#define INT_ORIGIN_PROC_STACK  0xD

#define NOT_FOUND         0x1             /* Special address flag - not found */
#define INTERRUPT_HANDLER 0x3             /* Call from interrupt handler */
#define INTERRUPT_MAIN    0x5             /* Called from main stack */
#define INTERRUPT_PROC    0x7             /* Called from process stack */
#define INTERRUPT_UNKNOWN 0x9             /* umm...we don't really know */
#define SLEEPING          0xB             /* Special address flag sleeping */

#define ASSY_NOT_FOUND    0xffffffff      /* Assembly line not found */
#define NO_LINE           0xffffffff      /* No line number defined */

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


struct fileEntry

{
    char *name;                             /* Path to file */
};

struct functionEntry

{
    char *name;                             /* Name of function */
    uint32_t startAddr;                     /* Start address */
    uint32_t endAddr;                       /* End address */
    uint32_t fileEntryIdx;                  /* Link back to containing file */
};

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


struct SymbolSet
{
    char *elfFile;                         /* File containing structure info */
    struct stat st;

    /* For memory saving and speedup... */
    bool recordSource;                     /* Keep a record of source code */
    bool recordAssy;                       /* Keep a record of assembly code */
    bool demanglecpp;                      /* If we want C++ names demangling */

    /* For file mapping... */
    uint32_t fileCount;                    /* Number of files we have loaded */
    uint32_t sourceCount;                  /* Number of source lines we have loaded */
    uint32_t functionCount;                /* Number of functions we have loaded */

    struct fileEntry *files;               /* Table of files */
    struct functionEntry *functions;       /* Table of functions */
    struct sourceLineEntry *sources;       /* Table of sources */
};

/* An entry in the names table ... what we return to our caller */
struct nameEntry
{
    const char *filename;                /* Filename containing the address */
    const char *function;                /* Function containing the address */
    uint32_t fileindex;                  /* Index of filename */
    uint32_t functionindex;              /* Index of functionname */
    uint32_t line;                       /* Source line containing the address */
    uint16_t linesInBlock;               /* Number of lines in this text block */
    const char *source;                  /* Corresponding source text */
    const struct assyLineEntry *assy;    /* Corresponding assembly text */
    uint32_t assyLine;                   /* Line of assembly text */
    uint32_t addr;                       /* Matched address */
    uint32_t index;
};

// ====================================================================================================
struct SymbolSet *SymbolSetCreate( char *filename, bool demanglecpp, bool recordSource, bool recordAssy );
void SymbolSetDelete( struct SymbolSet **s );
bool SymbolSetValid( struct SymbolSet **s, char *filename );
bool SymbolLookup( struct SymbolSet *s, uint32_t addr, struct nameEntry *n, char *deleteMaterial );
// ====================================================================================================
#endif
