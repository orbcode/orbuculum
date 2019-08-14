/*
 * Symbol Management
 * =================
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
