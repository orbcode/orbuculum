/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * External file format writing module for Orbuculum
 * =================================================
 *
 */

#ifndef _EXT_FF_H_
#define _EXT_FF_H_

#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>

#include "uthash.h"
#include "symbols.h"

#ifdef __cplusplus
extern "C" {
#endif


/* Execution of an instruction. We maintain a lot of information, but it's a PC, so we've got the room :-) */
struct execEntryHash
{
    /* The address in the memory map of the target */
    uint32_t addr;
    uint32_t codes;

    /* Counter at assembly and source line levels */
    uint64_t count;                      /* Instruction level count */
    uint64_t scount;                     /* Source level count (applied to first instruction of a new source line) */

    /* Details about this instruction */
    bool     isJump;                     /* Flag if this is a jump instruction */
    bool     is4Byte;                    /* Flag for 4 byte instruction */
    bool     isSubCall;                  /* Flag for subroutine call (BL/BLX) */
    bool     isReturn;                   /* Flag for return */
    uint32_t jumpdest;                   /* Destination for a jump, if it's taken */

    /* Location of this line in source code */
    uint32_t fileindex;                  /* File index (from symbols.c) */
    uint32_t functionindex;              /* Function index (from symbols.c) */
    uint32_t line;                       /* Line number in identified file */
    const char *assyText;                /* Assembly line text */

    /* Hash handle to make construct hashable */
    UT_hash_handle hh;
};


/* Signature for a source/dest calling pair */
struct subcallSig
{
    uint32_t src;                       /* Where the call is from */
    uint32_t dst;                       /* Where the call is to */
};

struct subcallAccount
{
    struct subcallSig sig;
    uint64_t inTicks;
    bool tailChained;
};

/* Processed subcalls from routine to routine */
struct subcall
{
    struct subcallSig sig;              /* Calling and called side record, forming an index entry */

    struct execEntryHash *srch;         /* Calling side */
    struct execEntryHash *dsth;         /* Called side */

    /* Housekeeping */
    uint64_t myCost;                   /* Inclusive cost of this call */
    uint64_t count;                    /* Number of executions of this call */

    /* Hash handle to make construct hashable */
    UT_hash_handle hh;
};


// ====================================================================================================
bool ext_ff_outputDot( char *dotfile, struct subcall *subcallList, struct SymbolSet *ss );
bool ext_ff_outputProfile( char *profile, char *elffile, char *deleteMaterial, bool includeVisits, uint64_t timelen,
                           struct execEntryHash *insthead, struct subcall *subcallList, struct SymbolSet *ss );
// ====================================================================================================

#ifdef __cplusplus
}
#endif
#endif
