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

#include <stdio.h>
#include <sys/time.h>
#include <string.h>
#include "itmDecoder.h"
#define SYNCPATTERN 0x00000080

// Define this to get transitions printed out
// #define PRINT_TRANSITIONS
// ====================================================================================================
void ITMDecoderInit(struct ITMDecoder *i, BOOL isLiveSet)

/* Reset a ITMDecoder instance */

{
  i->syncStat=0xFFFFFFFF;
  i->p=ITM_UNSYNCED;
}
// ====================================================================================================
void ITMDecoderForceSync(struct ITMDecoder *i, BOOL isSynced)

/* Force the decoder into a specific sync state */

{
  if (isSynced)
    {
      i->p=ITM_IDLE;
    }
  else
    {
      i->p=ITM_UNSYNCED;
    }
}
// ====================================================================================================
BOOL ITMGetSWPacket(struct ITMDecoder *i, struct ITMSWPacket *p)

/* Copy received packet into transfer buffer, and reset receiver */

{
  /* This should have been reset in the call */
  if (i->p!=ITM_IDLE)
    return FALSE;

  p->srcAddr=i->srcAddr;
  p->len=i->targetCount;
  memcpy(p->d,i->rxPacket,p->len);
  memset(&p->d[p->len],0,ITM_MAX_PACKET-p->len);
  return TRUE;
}
// ====================================================================================================
#ifdef PRINT_TRANSITIONS
static char *_protoNames[]={PROTO_NAME_LIST};
#endif

enum ITMPumpEvent ITMPump(struct ITMDecoder *i, uint8_t c)

/* Pump next byte into the protocol decoder */

{
  enum _protoState newState = i->p;
  enum ITMPumpEvent retVal = ITM_EV_NONE;

  i->syncStat=(i->syncStat<<8)|c;
  if (i->syncStat==SYNCPATTERN)
    {
      i->p=ITM_IDLE;
      return ITM_EV_SYNCED;
    }

  switch (i->p)
    {
      // -----------------------------------------------------
    case ITM_UNSYNCED:
      break;
      // -----------------------------------------------------
    case ITM_IDLE:
      if (c==0b01110000)
	{
	  /* This is an overflow packet */
	  retVal= ITM_EV_OVERFLOW;
	  break;
	}
      // **********
      if (!(c&0x0F))
	{
	  i->currentCount=1; /* The '1' is deliberate. */
	  /* This is a timestamp packet */
	  i->rxPacket[0]=c;

	  if (!(c&0x80))
	    {
	      /* A one byte output */
	      return ITM_EV_TS_PACKET_RXED;
	    }

	  newState=ITM_TS;
	  retVal=ITM_EV_NONE;
	  break;
	}
      // **********
      if ((c&0x0F) == 0x04)
	{
	  /* This is a reserved packet */
	  retVal=ITM_EV_ERROR;
	  break;
	}
      // **********
      if (!(c&0x04))
	{
	  /* This is a SW packet */
	  if ((i->targetCount=c&0x03)==3)
	    i->targetCount=4;
	  i->srcAddr=(c&0xF8)>>3;
	  i->currentCount=0;
	  newState=ITM_SW;
	  retVal=ITM_EV_NONE;
	  break;
	}
      // **********
      if (c&0x04)
	{
	  /* This is a HW packet */
	  if ((i->targetCount=c&0x03)==3)
	    i->targetCount=4;
	  i->srcAddr=(c&0xF8)>>3;
	  i->currentCount=0;
	  newState=ITM_HW;
	  retVal=ITM_EV_NONE;
	  break;
	}
      // **********
      retVal=ITM_EV_ERROR;
      break;
      // -----------------------------------------------------
    case ITM_SW:
	  i->rxPacket[i->currentCount]=c;
	  i->currentCount++;

	  if (i->currentCount>=i->targetCount)
	    {
	      newState=ITM_IDLE;
	      retVal=ITM_EV_SW_PACKET_RXED;
	      break;
	    }

	  retVal=ITM_EV_NONE;
	  break;
      // -----------------------------------------------------
    case ITM_HW:
	  i->rxPacket[i->currentCount]=c;
	  i->currentCount++;

	  if (i->currentCount>=i->targetCount)
	    {
	      newState=ITM_IDLE;
	      retVal=ITM_EV_HW_PACKET_RXED;
	      break;
	    }
	  retVal=ITM_EV_NONE;
	  break;
      // -----------------------------------------------------
    case ITM_TS:
      i->rxPacket[i->currentCount++]=c;
      if (!(c&0x80))
	{
	  /* We are done */
	  retVal=ITM_EV_TS_PACKET_RXED;
	  break;
	}

      if (i->currentCount>4)
	{
	  /* Something went badly wrong */
	  newState=ITM_UNSYNCED;
	  retVal=ITM_EV_UNSYNCED;
	  break;
	}
      retVal=ITM_EV_NONE;	  
      break;
      // -----------------------------------------------------
    }
#ifdef PRINT_TRANSITIONS
  printf("%02x %s --> %s\n",c,_protoNames[i->p],_protoNames[newState]);
#endif
  i->p=newState;
  return retVal;
}
// ====================================================================================================
