/*
 * Post mortem monitor for parallel trace : Screen I/O Routines
 * ============================================================
 *
 * Copyright (C) 2017, 2019, 2020, 2021  Dave Marples  <dave@marples.net>
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

#ifndef _SIO_
#define _SIO_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_TAGS            (10)               /* Maximum number of tagged positions in file */
#define WARN_TIMEOUT        (2000)             /* How long a warning or info message remains on the display */


/* The main handle-type */
struct SIOInstance;

/* Events that can be returned by the handler */
enum SIOEvent { SIO_EV_NONE, SIO_EV_HOLD, SIO_EV_QUIT, SIO_EV_SAVE, SIO_EV_CONSUMED, SIO_EV_SURFACE, SIO_EV_DIVE, SIO_EV_FOPEN };

/* Types of line (each with their own display mechanism & colours */
enum LineType { LT_SOURCE, LT_ASSEMBLY, LT_ASSEMBLYFLOW, LT_NASSEMBLY, LT_MU_SOURCE, LT_EVENT, LT_LABEL, LT_FILE  };

/* Definition for a single line...collections of these are what get displayed */
struct line
{
    enum LineType lt;
    bool isRef;
    char *buffer;
    int32_t line;
};

// ====================================================================================================
const char *SIOgetSaveFilename( struct SIOInstance *sio );
int32_t SIOgetCurrentLineno( struct SIOInstance *sio );
void SIOsetOutputBuffer( struct SIOInstance *sio, int32_t numLines, int32_t currentLine, struct line **opTextSet, bool amDiving );
void SIOalert( struct SIOInstance *sio, const char *msg );
void SIOrequestRefresh( struct SIOInstance *sio );
void SIOheld( struct SIOInstance *sio, bool isHeld );

enum SIOEvent SIOHandler( struct SIOInstance *sio, bool isTick, uint64_t oldintervalBytes );

void SIOterminate( struct SIOInstance *sio );
struct SIOInstance *SIOsetup( const char *progname, const char *elffile );
// ====================================================================================================

#ifdef __cplusplus
}
#endif
#endif
