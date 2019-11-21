/*
 * Fifo Interface
 * ==============
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

#ifndef _FIFOS_
#define _FIFOS_

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

struct Channel                             /* Information for an individual channel */
{
  char *chanName;                          /* Filename to be used for the fifo */
  char *presFormat;                        /* Format of data presentation to be used */

  /* Runtime state */
  int handle;                              /* Handle to the fifo */
  pthread_t thread;                        /* Thread on which it's running */
  
  char *fifoName;                          /* Constructed fifo name (from chanPath and name) */
};

struct fifosHandle

{
    /* The decoders and the packets from them */
    struct ITMDecoder i;
    struct ITMPacket h;
    struct TPIUDecoder t;
    struct TPIUPacket p;

  /* Timestamp info */
    uint64_t lastHWExceptionTS;

  /* Configuration information */
  char *chanPath;                               /* Path to where to put the fifos */
  bool useTPIU;                                 /* Is the TPIU active? */
  bool forceITMSync;                            /* Is ITM to be forced into sync? */
  int tpiuITMChannel;                           /* TPIU channel on which ITM appears */

  struct Channel c[NUM_CHANNELS + 1];           /* Output for each channel */
};

  /* Fifos running */
  void fifoForceSync( struct fifosHandle *f, bool synced );                  /* Force sync status */
  void fifoProtocolPump( struct fifosHandle *f, uint8_t c );                 /* Send undecoded data to the fifo */

  /* Getters and setters */
  void fifoSetChannel( struct fifosHandle *f, int chan, char *n, char *s );
  void fifoSetChanPath( struct fifosHandle *f, char *s );
  void fifoSetUseTPIU(struct fifosHandle *f, bool s );
  void fifoSetForceITMSync(struct fifosHandle *f, bool s );
  void fifoSettpiuITMChannel(struct fifosHandle *f, int channel );
  char *fifoGetChannelName( struct fifosHandle *f, int chan );  
  char *fifoGetChannelFormat( struct fifosHandle *f, int chan );
  char *fifoGetChanPath( struct fifosHandle *f );
  bool fifoGetUseTPIU(struct fifosHandle *f );
  bool fifoGetForceITMSync(struct fifosHandle *f );
  int fifoGettpiuITMChannel(struct fifosHandle *f );

  /* Fifos management */
  bool fifoCreate( struct fifosHandle *f );                                  /* Create the fifo set */
  void fifoRemove( struct fifosHandle *f );                                  /* Destroy the fifo set */
  struct fifosHandle *fifoInit( void );                                      /* Create an instance */
// ====================================================================================================
#ifdef __cplusplus
}
#endif
#endif
