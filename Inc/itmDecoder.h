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
struct ITMSWPacket

{
  uint8_t srcAddr;
  
  uint8_t len;
  uint8_t d[ITM_MAX_PACKET];
};

struct ITMHWPacket

{
  uint8_t srcAddr;
  
  uint8_t len;
  uint8_t d[ITM_MAX_PACKET];
};

struct ITMDecoder

{
  int targetCount;   /* Number of bytes to be collected */
  int currentCount;  /* Number of bytes that have been collected */
  uint8_t rxPacket[ITM_MAX_PACKET]; /* Packet in reception */
  uint32_t syncStat; /* Sync monitor status */
  int srcAddr;       /* Source address for this packet */

  enum _protoState p; /* Current state of the receiver */
};

// ====================================================================================================
void ITMDecoderInit(struct ITMDecoder *i, BOOL isLiveSet);
void ITMDecoderForceSync(struct ITMDecoder *i, BOOL isSynced);
BOOL ITMGetSWPacket(struct ITMDecoder *i, struct ITMSWPacket *p);
BOOL ITMGetHWPacket(struct ITMDecoder *i, struct ITMHWPacket *p);
enum ITMPumpEvent ITMPump(struct ITMDecoder *i, uint8_t c);
// ====================================================================================================
#endif
