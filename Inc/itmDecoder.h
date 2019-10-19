/*
 * ITM Decoder Module
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

#ifndef _ITM_DECODER_
#define _ITM_DECODER_

#include <stdbool.h>
#include <stdint.h>

#define ITM_MAX_PACKET  (5)  // This length can only happen for a timestamp
#define ITM_DATA_PACKET (4)  // This is the maximum length of everything else

#ifdef __cplusplus
extern "C" {
#endif
  
enum ITMPacketType

{
    ITM_PT_NONE,
    ITM_PT_TS,
    ITM_PT_SW,
    ITM_PT_HW,
    ITM_PT_XTN,
    ITM_PT_RSRVD
};

enum ITMPumpEvent
{
    ITM_EV_NONE = ITM_PT_NONE,
    ITM_EV_TS_PACKET_RXED = ITM_PT_TS,
    ITM_EV_SW_PACKET_RXED = ITM_PT_SW,
    ITM_EV_HW_PACKET_RXED = ITM_PT_HW,
    ITM_EV_XTN_PACKET_RXED = ITM_PT_XTN,
    ITM_EV_RESERVED_PACKET_RXED = ITM_PT_RSRVD,
    ITM_EV_UNSYNCED,
    ITM_EV_SYNCED,
    ITM_EV_OVERFLOW,
    ITM_EV_ERROR
};

enum hwEvents
{
    HWEVENT_TS,
    HWEVENT_EXCEPTION,
    HWEVENT_PCSample,
    HWEVENT_DWT,
    HWEVENT_RWWT,
    HWEVENT_AWP,
    HWEVENT_OFS,
    HWEVENT_TIMESTAMP
};

enum ExceptionEvents
{
    EXEVENT_UNKNOWN,
    EXEVENT_ENTER,
    EXEVENT_EXIT,
    EXEVENT_RESUME
};

enum _protoState
{
    ITM_UNSYNCED,
    ITM_IDLE,
    ITM_TS,
    ITM_SW,
    ITM_HW,
    ITM_GTS1,
    ITM_GTS2,
    ITM_RSVD,
    ITM_XTN
};
#define PROTO_NAME_LIST "UNSYNCED", "IDLE", "TS", "SW", "HW", "GTS1", "GTS2", "RSVD", "XTN"

/* Type of the packet received over the link */
struct ITMPacket

{
    enum ITMPacketType type;
    uint8_t srcAddr;

    uint8_t len;
    uint8_t pageRegister; /* The current stimulus page register value */
    uint8_t d[ITM_MAX_PACKET];
};

enum timeDelay {TIME_CURRENT, TIME_DELAYED, EVENT_DELAYED, EVENT_AND_TIME_DELAYED};

struct ITMDecoderStats

{
    uint32_t lostSyncCount;              /* Number of times sync has been lost */
    uint32_t syncCount;                  /* Number of times a sync event has been received */
    uint32_t tpiuSyncCount;              /* Number of times a tpiu sync event has been received (shouldn't happen) */
    uint32_t overflow;                   /* Number of times an overflow occured */
    uint32_t SWPkt;                      /* Number of SW Packets received */
    uint32_t TSPkt;                      /* Number of TS Packets received */
    uint32_t HWPkt;                      /* Number of HW Packets received */
    uint32_t XTNPkt;                     /* Number of XTN Packets received */
    uint32_t ReservedPkt;                /* Number of Reserved Packets received */
    uint32_t ErrorPkt;                   /* Number of Packets received we don't know how to handle */
    uint32_t PagePkt;                    /* Number of Packets received containing page sets */
};

struct ITMDecoder

{
    enum timeDelay timeStatus;           /* Indicator of if this time is exact */
    uint64_t timeStamp;                  /* Latest received time */

    int targetCount;                     /* Number of bytes to be collected */
    int currentCount;                    /* Number of bytes that have been collected */
    uint8_t rxPacket[ITM_MAX_PACKET];    /* Packet in reception */
    uint64_t syncStat;                   /* Sync monitor status */
    int srcAddr;                         /* Source address for this packet */

    struct ITMDecoderStats stats;        /* Record of the statistics */
    uint8_t pageRegister;                /* The current stimulus page register value */
    enum _protoState p;                  /* Current state of the receiver */
};

// ====================================================================================================
void ITMDecoderForceSync( struct ITMDecoder *i, bool isSynced );
void ITMDecoderZeroStats( struct ITMDecoder *i );
bool ITMDecoderIsSynced( struct ITMDecoder *i );
struct ITMDecoderStats *ITMDecoderGetStats( struct ITMDecoder *i );
bool ITMGetPacket( struct ITMDecoder *i, struct ITMPacket *p );
enum ITMPumpEvent ITMPump( struct ITMDecoder *i, uint8_t c );

void ITMDecoderInit( struct ITMDecoder *i, bool startSynced );
// ====================================================================================================
#ifdef __cplusplus
}
#endif
#endif
