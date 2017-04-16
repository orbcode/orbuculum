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

#define ITM_MAX_PACKET_DATA (4)
#define ITM_MAX_PACKET      (ITM_MAX_PACKET_DATA+1)

enum ITMPumpEvent {ITM_EV_NONE, 
		   ITM_EV_UNSYNCED, 
		   ITM_EV_SYNCED, 
		   ITM_EV_TS_PACKET_RXED,
		   ITM_EV_SW_PACKET_RXED,
		   ITM_EV_HW_PACKET_RXED, 
		   ITM_EV_OVERFLOW, 
		   ITM_EV_ERROR
		   };

enum _protoState {ITM_UNSYNCED, ITM_IDLE, ITM_TS, ITM_SW, ITM_HW};
#define PROTO_NAME_LIST "UNSYNCED", "IDLE", "TS", "SW", "HW"

/* Type of the packet received over the link */
struct ITMPacket

{
  uint8_t srcAddr;
  int len;
  union
  {
    uint8_t d[ITM_MAX_PACKET_DATA];
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    int8_t s8;
    int16_t s16;
    int32_t s32;    
  };
};

struct ITMDecoder

{
  int targetCount;   /* Number of bytes to be collected */
  int currentCount;  /* Number of bytes that have been collected */
  union
  {
    uint8_t rxPacket[ITM_MAX_PACKET_DATA]; /* Packet in reception */
    uint8_t d[ITM_MAX_PACKET_DATA];
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    int8_t s8;
    int16_t s16;
    int32_t s32;    
  };
    
  uint32_t syncStat; /* Sync monitor status */
  int srcAddr;       /* Source address for this packet */

  enum _protoState p; /* Current state of the receiver */
};

// ====================================================================================================
void ITMDecoderInit(struct ITMDecoder *i, BOOL isLiveSet);
void ITMDecoderForceSync(struct ITMDecoder *i, BOOL isSynced);
BOOL ITMGetPacket(struct ITMDecoder *i, struct ITMPacket *p);
enum ITMPumpEvent ITMPump(struct ITMDecoder *i, uint8_t c);
// ====================================================================================================
#endif
