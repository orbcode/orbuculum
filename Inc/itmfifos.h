/*
 * ITM Fifo Interface
 * ==================
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

#ifndef _ITMFIFOS_
#define _ITMFIFOS_

#include "tpiuDecoder.h"
#include "itmDecoder.h"

#include "generics.h"

#ifdef __cplusplus
extern "C" {
#endif

// ====================================================================================================
#define NUM_CHANNELS  32                     /* Number of channels defined */
#define HW_CHANNEL    (NUM_CHANNELS)         /* Make the hardware fifo on the end of the software ones */
#define HWFIFO_NAME "hwevent"                /* Name for the hardware channel */

struct Channel;
struct itmfifosHandle;

/* Fifos running */
void itmfifoForceSync( struct itmfifosHandle *f, bool synced );                  /* Force sync status */
void itmfifoProtocolPump( struct itmfifosHandle *f, uint8_t c );                 /* Send undecoded data to the fifo */

/* Getters and setters */
void itmfifoSetChannel( struct itmfifosHandle *f, int chan, char *n, char *s );
void itmfifoSetChanPath( struct itmfifosHandle *f, char *s );
void itmfifoSetUseTPIU( struct itmfifosHandle *f, bool s );
void itmfifoSetForceITMSync( struct itmfifosHandle *f, bool s );
void itmfifoSettpiuITMChannel( struct itmfifosHandle *f, int channel );
char *itmfifoGetChannelName( struct itmfifosHandle *f, int chan );
char *itmfifoGetChannelFormat( struct itmfifosHandle *f, int chan );
char *itmfifoGetChanPath( struct itmfifosHandle *f );
bool itmfifoGetUseTPIU( struct itmfifosHandle *f );
struct TPIUCommsStats *itmfifoGetCommsStats( struct itmfifosHandle *f );
struct ITMDecoderStats *itmfifoGetITMDecoderStats( struct itmfifosHandle *f );
bool itmfifoGetForceITMSync( struct itmfifosHandle *f );
int itmfifoGettpiuITMChannel( struct itmfifosHandle *f );
void itmfifoUsePermafiles( struct itmfifosHandle *f, bool usePermafilesSet );

/* Filewriting */
void itmfifoFilewriter( struct itmfifosHandle *f, bool useFilewriter, char *workingPath );

/* Fifos management */
bool itmfifoCreate( struct itmfifosHandle *f );                                  /* Create the fifo set */
void itmfifoShutdown( struct itmfifosHandle *f );                                /* Destroy the fifo set */
struct itmfifosHandle *itmfifoInit( bool forceITMSyncSet,
                                    bool useTPIUSet,
                                    int TPIUchannelSet );                        /* Create an instance */
// ====================================================================================================
#ifdef __cplusplus
}
#endif
#endif
