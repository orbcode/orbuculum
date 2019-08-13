/*
 * TPIU Decoder Module
 * ===================
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

#ifndef _TPIU_DECODER_
#define _TPIU_DECODER_

#include "generics.h"

enum TPIUPumpEvent
{
    TPIU_EV_NONE,
    TPIU_EV_UNSYNCED,
    TPIU_EV_SYNCED,
    TPIU_EV_NEWSYNC,
    TPIU_EV_RXING,
    TPIU_EV_RXEDPACKET,
    TPIU_EV_ERROR
};

enum TPIUPumpState
{
    TPIU_UNSYNCED,
    TPIU_SYNCED,
    TPIU_RXING,
    TPIU_ERROR
};

#define TPIU_PACKET_LEN (16)

struct TPIUDecoderStats
{
    uint32_t lostSync;                     /* Number of times sync has been lost */
    uint32_t syncCount;                    /* Number of times a sync event has been received */
    uint32_t packets;                      /* Number of packets received */
    uint32_t error;                        /* Number of times an error has been received */
};

struct TPIUDecoder
{
    enum TPIUPumpState state;              /* Current state of TPIU decoder */
    uint8_t byteCount;                     /* Current byte number in reception */
    uint8_t currentStream;                 /* Currently selected stream */
    uint32_t syncMonitor;                  /* State of sync reception ... in case we loose sync */
    struct timeval lastPacket;             /* Timestamp for last packet arrival */
    uint8_t rxedPacket[TPIU_PACKET_LEN];   /* Packet currently under construction */

    struct TPIUDecoderStats stats;         /* Record of comms stats */
};

struct TPIUPacket
{
    uint8_t len;                           /* Received length (after pre-processing) */
    struct
    {
        int8_t s;                          /* Stream to which this byte relates */
        int8_t d;                          /* ...the byte itself */
    } packet[TPIU_PACKET_LEN];
};

// ====================================================================================================
void TPIUDecoderForceSync( struct TPIUDecoder *t, uint8_t offset );
bool TPIUGetPacket( struct TPIUDecoder *t, struct TPIUPacket *p );
void TPIUDecoderZeroStats( struct TPIUDecoder *t );
bool TPIUDecoderSynced( struct TPIUDecoder *t );
struct TPIUDecoderStats *TPIUDecoderGetStats( struct TPIUDecoder *t );
enum TPIUPumpEvent TPIUPump( struct TPIUDecoder *t, uint8_t d );

void TPIUDecoderInit( struct TPIUDecoder *t );
// ====================================================================================================
#endif
