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

/*
 * Implementation of ITM/DWT decode according to the specification in Appendix E
 * of the ARMv7-M Architecture Refrence Manual document available
 * from https://web.eecs.umich.edu/~prabal/teaching/eecs373-f10/readings/ARMv7-M_ARM.pdf
 */
#include <stdio.h>
#include <sys/time.h>
#include <string.h>
#include "itmDecoder.h"
#define SYNCMASK              0xFFFFFFFFFFFF
#define SYNCPATTERN           0x000000000080
#define MAX_PACKET            (5)
#define DEFAULT_PAGE_REGISTER (0x07)

// Define this to get transitions printed out
//#define PRINT_TRANSITIONS
// ====================================================================================================
void ITMDecoderInit( struct ITMDecoder *i )

/* Reset a ITMDecoder instance */

{
    i->syncStat = SYNCMASK;
    i->p = ITM_UNSYNCED;
    i->currentCount = 0;
    i->pageRegister = DEFAULT_PAGE_REGISTER;
    ITMDecoderZeroStats( i );
}
// ====================================================================================================
void ITMDecoderZeroStats( struct ITMDecoder *i )

{
    memset( &i->stats, 0, sizeof( struct ITMDecoderStats ) );
}
// ====================================================================================================
void ITMDecoderForceSync( struct ITMDecoder *i, BOOL isSynced )

/* Force the decoder into a specific sync state */

{
    if ( ( i->p == ITM_UNSYNCED ) && ( isSynced ) )
    {
        i->p = ITM_IDLE;
        i->stats.syncCount++;
        i->currentCount = 0;
    }
    else
    {
        if ( i->p != ITM_UNSYNCED )
        {
            i->stats.lostSyncCount++;
        }

        i->p = ITM_UNSYNCED;
    }
}
// ====================================================================================================
BOOL ITMGetPacket( struct ITMDecoder *i, struct ITMPacket *p )

/* Copy received packet into transfer buffer, and reset receiver */

{
    /* This should have been reset in the call */
    if ( i->p != ITM_IDLE )
    {
        return FALSE;
    }

    p->srcAddr = i->srcAddr;
    p->len = i->currentCount;
    p->pageRegister = i->pageRegister;

    memcpy( p->d, i->rxPacket, p->len );
    //    printf("L=%d (%d)                      %02X %02X %02X %02X\n",p->len,i->targetCount,i->rxPacket[0],i->rxPacket[1],i->rxPacket[2],i->rxPacket[3]);
    memset( &p->d[p->len], 0, ITM_MAX_PACKET - p->len );
    return TRUE;
}
// ====================================================================================================
#ifdef PRINT_TRANSITIONS
static char *_protoNames[] = {PROTO_NAME_LIST};
#endif

enum ITMPumpEvent ITMPump( struct ITMDecoder *i, uint8_t c )

/* Pump next byte into the protocol decoder */

{
    enum _protoState newState = i->p;
    enum ITMPumpEvent retVal = ITM_EV_NONE;

    i->syncStat = ( i->syncStat << 8 ) | c;

    if ( ( ( i->syncStat )&SYNCMASK ) == SYNCPATTERN )
    {
        i->stats.syncCount++;
        retVal = ITM_EV_SYNCED;
        newState = ITM_IDLE;
    }
    else
    {

        switch ( i->p )
        {
            // -----------------------------------------------------
            case ITM_UNSYNCED:
                break;

            // -----------------------------------------------------
            case ITM_IDLE:

                // **********
                if ( !c )
                {
                    break;
                }

                // **********
                if ( c == 0b01110000 )
                {
                    /* This is an overflow packet */
                    i->stats.overflow++;
                    retVal = ITM_EV_OVERFLOW;
                    break;
                }

                // **********
                if ( !( c & 0x0F ) )
                {
                    i->currentCount = 1; /* The '1' is deliberate. */
                    /* This is a timestamp packet */
                    i->rxPacket[0] = c;
                    i->stats.TSPkt++;

                    if ( c & 0x80 )
                    {
                        /* This is TS packet format 1, so there's more to follow */
                        newState = ITM_TS;
                    }
                    else
                    {
                        /* This is TS packet format 2, no more to come, and no change of state */
                        retVal = ITM_EV_TS_PACKET_RXED;
                    }

                    break;
                }

                // **********
                if ( ( c & 0x0B ) == 0x08 )
                {
                    /* Extension Packet */
                    i->currentCount = 1; /* The '1' is deliberate. */
                    i->stats.XTNPkt++;

                    i->rxPacket[0] = c;

                    if ( !( c & 0x80 ) )
                    {
                        /* A one byte output */
                        retVal = ITM_EV_XTN_PACKET_RXED;
                        break;
                    }

                    newState = ITM_EXTENSION;
                    break;
                }

                // **********
                if ( ( c & 0x0F ) == 0x04 )
                {
                    /* This is a reserved packet */
                    if ( c & 0x80 )
                    {
                        /* This is a reserved encoding we don't know how to handle */
                        /* ...assume it's line noise and wait for sync again */
                        i->stats.ErrorPkt++;
                        retVal = ITM_EV_ERROR;
                    }
                    else
                    {
                        /* This is the Stimulus Port Page Register setting */
                        i->stats.PagePkt++;
                        i->pageRegister = ( c >> 4 ) & 0x07;
                    }

                    break;
                }

                // **********
                if ( !( c & 0x04 ) )
                {
                    /* This is a SW packet */
                    i->stats.SWPkt++;
		    i->targetCount = (c & 0x03);
                    if ( i->targetCount == 3 )
                    {
                        i->targetCount = 4;
                    }

                    i->srcAddr = ( c & 0xF8 ) >> 3;
                    i->currentCount = 0;
                    newState = ITM_SW;
                    break;
                }

                // **********
                if ( c & 0x04 )
                {
                    /* This is a HW packet */
                    i->stats.HWPkt++;
		    //printf("H [%d] ",c&0x03);
		    i->targetCount = (c & 0x03);
                    if ( i->targetCount == 3 )
                    {
                        i->targetCount = 4;
                    }

                    i->srcAddr = ( c & 0xF8 ) >> 3;
                    i->currentCount = 0;
                    newState = ITM_HW;
                    break;
                }

                // **********
#ifdef PRINT_TRANSITIONS
                fprintf( stderr, "General error for packet type %02x\n", c );
#endif
                retVal = ITM_EV_ERROR;
                break;

            // -----------------------------------------------------
            case ITM_SW:
                i->rxPacket[i->currentCount++] = c;

                if ( i->currentCount >= i->targetCount )
                {
                    newState = ITM_IDLE;
                    retVal = ITM_EV_SW_PACKET_RXED;
                    break;
                }

                retVal = ITM_EV_NONE;
                break;

            // -----------------------------------------------------
            case ITM_HW:
                i->rxPacket[i->currentCount] = c;
                i->currentCount++;

                if ( i->currentCount >= i->targetCount )
                {
                    newState = ITM_IDLE;
                    retVal = ITM_EV_HW_PACKET_RXED;
                    break;
                }

                retVal = ITM_EV_NONE;
                break;

            // -----------------------------------------------------
            case ITM_TS:
                i->rxPacket[i->currentCount++] = c;

                if ( ( !( c & 0x80 ) ) || ( i->currentCount >= MAX_PACKET ) )
                {
                    /* We are done */
                    newState = ITM_IDLE;
                    retVal = ITM_EV_TS_PACKET_RXED;
                    break;
                }

                break;

            // -----------------------------------------------------
            case ITM_EXTENSION:
                i->rxPacket[i->currentCount++] = c;

                if ( ( !( c & 0x80 ) ) || ( i->currentCount >= MAX_PACKET ) )
                {
                    /* We are done */
                    newState = ITM_IDLE;
                    retVal = ITM_EV_XTN_PACKET_RXED;
                    break;
                }

                break;
                // -----------------------------------------------------
        }
    }

#ifdef PRINT_TRANSITIONS

    if ( ( i->p != ITM_UNSYNCED ) || ( newState != ITM_UNSYNCED ) )
    {
        printf( "%02x %s --> %s(%d)\n", c, _protoNames[i->p], _protoNames[newState], i->targetCount );
    }

#endif
    i->p = newState;
    return retVal;
}
// ====================================================================================================
