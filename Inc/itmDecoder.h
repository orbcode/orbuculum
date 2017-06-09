/*
 * ITM Decoder Module
 * ==================
 *
 * Copyright (C) 2017  Dave Marples  <dave@marples.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _ITM_DECODER_
#define _ITM_DECODER_

#include <stdint.h>

#ifndef BOOL
    #define BOOL  int
    #define FALSE (0)
    #define TRUE  (!FALSE)
#endif

#define ITM_MAX_PACKET  (5)  // This length can only happen for a timestamp
#define ITM_DATA_PACKET (4)  // This is the maximum length of everything else

enum ITMPumpEvent
{
    ITM_EV_NONE,
    ITM_EV_UNSYNCED,
    ITM_EV_SYNCED,
    ITM_EV_TS_PACKET_RXED,
    ITM_EV_SW_PACKET_RXED,
    ITM_EV_HW_PACKET_RXED,
    ITM_EV_XTN_PACKET_RXED,
    ITM_EV_OVERFLOW,
    ITM_EV_ERROR
};

enum hwEvents
{
    HWEVENT_TS,
    HWEVENT_PCSample,
    HWEVENT_DWT,
    HWEVENT_EXCEPTION,
    HWEVENT_RWWT,
    HWEVENT_AWP,
    HWEVENT_OFS,
    HWEVENT_TIMESTAMP
};

enum _protoState
{
    ITM_UNSYNCED,
    ITM_IDLE,
    ITM_TS,
    ITM_SW,
    ITM_HW,
    ITM_EXTENSION

};
#define PROTO_NAME_LIST "UNSYNCED", "IDLE", "TS", "SW", "HW", "EXTENSION"

/* Type of the packet received over the link */
struct ITMPacket

{
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
    uint32_t overflow;                   /* Number of times an overflow occured */
    uint32_t SWPkt;                      /* Number of SW Packets received */
    uint32_t TSPkt;                      /* Number of TS Packets received */
    uint32_t HWPkt;                      /* Number of HW Packets received */
    uint32_t XTNPkt;                     /* Number of XTN Packets received */
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
void ITMDecoderForceSync( struct ITMDecoder *i, BOOL isSynced );
void ITMDecoderZeroStats( struct ITMDecoder *i );
inline struct ITMDecoderStats *ITMDecoderGetStats( struct ITMDecoder *i )
{
    return &i->stats;
}
BOOL ITMGetPacket( struct ITMDecoder *i, struct ITMPacket *p );
enum ITMPumpEvent ITMPump( struct ITMDecoder *i, uint8_t c );

void ITMDecoderInit( struct ITMDecoder *i );
// ====================================================================================================
#endif
