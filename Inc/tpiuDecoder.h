/*
 * TPIU Decoder Module
 * ===================
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

#ifndef _TPIU_DECODER_
#define _TPIU_DECODER_

#include <stdint.h>

#ifndef BOOL
#define BOOL  int
#define FALSE (0)
#define TRUE  (!FALSE)
#endif

enum TPIUPumpEvent {TPIU_EV_NONE, TPIU_EV_UNSYNCED, TPIU_EV_SYNCED, TPIU_EV_RXING, TPIU_EV_RXEDPACKET, TPIU_EV_ERROR};
enum TPIUPumpState {TPIU_UNSYNCED, TPIU_SYNCED, TPIU_RXING, TPIU_ERROR };

#define TPIU_PACKET_LEN (16)

struct TPIUDecoder {
  enum TPIUPumpState state;
  uint8_t byteCount;
  uint8_t currentStream;
  uint32_t syncMonitor;
  struct timeval lastPacket;
  BOOL isLive;
  uint8_t rxedPacket[TPIU_PACKET_LEN];
};

struct TPIUPacket {
  uint8_t len;
  struct
  {
    int8_t s;
    int8_t d;
  } packet[TPIU_PACKET_LEN];
};

// ====================================================================================================
void TPIUDecoderInit(struct TPIUDecoder *t, BOOL isLiveSet);
void TPIUDecoderForceSync(struct TPIUDecoder *t, uint8_t offset);
BOOL TPIUGetPacket(struct TPIUDecoder *t, struct TPIUPacket *p);
enum TPIUPumpEvent TPIUPump(struct TPIUDecoder *t, uint8_t d);
// ====================================================================================================
#endif
