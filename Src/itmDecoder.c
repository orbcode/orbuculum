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

/*
 * Implementation of ITM/DWT decode according to the specification in Appendix D4
 * of the ARMv7-M Architecture Refrence Manual document available
 * from https://static.docs.arm.com/ddi0403/e/DDI0403E_B_armv7m_arm.pdf
 */
#include <sys/time.h>
#include <string.h>
#include "itmDecoder.h"

#ifdef DEBUG
    #include <stdio.h>
    #include "generics.h"
#else
    #define genericsReport(x...)
#endif

#define SYNCMASK              0xFFFFFFFFFFFF
#define SYNCPATTERN           0x000000000080
#define TPIU_SYNCMASK         0xFFFFFFFF
#define TPIU_SYNCPATTERN      0xFFFFFF7F      /* This should not be seen in ITM data */
#define MAX_PACKET            (5)
#define DEFAULT_PAGE_REGISTER (0x07)

// Define this to get transitions printed out
// ====================================================================================================
void ITMDecoderInit( struct ITMDecoder *i, bool startSynced )

/* Reset a ITMDecoder instance */

{
    i->syncStat = SYNCMASK;
    i->currentCount = 0;
    i->pageRegister = DEFAULT_PAGE_REGISTER;
    ITMDecoderForceSync( i, startSynced );
    ITMDecoderZeroStats( i );
}
// ====================================================================================================
void ITMDecoderZeroStats( struct ITMDecoder *i )

{
    memset( &i->stats, 0, sizeof( struct ITMDecoderStats ) );
}
// ====================================================================================================
bool ITMDecoderIsSynced( struct ITMDecoder *i )

{
    return i->p != ITM_UNSYNCED;
}
// ====================================================================================================
struct ITMDecoderStats *ITMDecoderGetStats( struct ITMDecoder *i )
{
    return &i->stats;
}
// ====================================================================================================
void ITMDecoderForceSync( struct ITMDecoder *i, bool isSynced )

/* Force the decoder into a specific sync state */

{
    if ( i->p == ITM_UNSYNCED )
    {
        if ( isSynced )
        {
            i->p = ITM_IDLE;
            i->stats.syncCount++;
            i->currentCount = 0;
        }
    }
    else
    {
        if ( !isSynced )
        {
            i->stats.lostSyncCount++;
            i->p = ITM_UNSYNCED;
        }
    }
}
// ====================================================================================================
bool ITMGetPacket( struct ITMDecoder *i, struct ITMPacket *p )

/* Copy received packet into transfer buffer, and reset receiver */

{
    /* This should have been reset in the call */
    if ( i->p != ITM_IDLE )
    {
        return false;
    }

    p->srcAddr = i->srcAddr;
    p->len = i->currentCount;
    p->pageRegister = i->pageRegister;

    memcpy( p->d, i->rxPacket, p->len );
    memset( &p->d[p->len], 0, ITM_MAX_PACKET - p->len );
    return true;
}
// ====================================================================================================

#ifdef DEBUG
static char *_protoNames[] = {PROTO_NAME_LIST};
#endif

enum ITMPumpEvent ITMPump( struct ITMDecoder *i, uint8_t c )

/* Pump next byte into the protocol decoder */

{
    enum _protoState newState = i->p;
    enum ITMPumpEvent retVal = ITM_EV_NONE;

    i->syncStat = ( i->syncStat << 8 ) | c;

    /* Just check for a TPIU sync ... this is incredibly unlikely in */
    /* ITM data, so it's an indicator that the TPIU should have been */
    /* switched in, and hasn't been. */
    if ( ( ( i->syncStat )&TPIU_SYNCMASK ) == TPIU_SYNCPATTERN )
    {
        i->stats.tpiuSyncCount++;
    }


    if ( ( ( i->syncStat )&SYNCMASK ) == SYNCPATTERN )
    {
        i->stats.syncCount++;

        /* Page register is reset on a sync */
        i->pageRegister = 0;
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

                // *************************************************
                // ************** SYNC PACKET **********************
                // *************************************************
                if ( c == 0b00000000 )
                {
                    break;
                }

                // *************************************************
                // ************** SOURCE PACKET ********************
                // *************************************************
                if ( c & 0b00000011 )
                {
                    i->targetCount = ( c & 0x03 );

                    if ( i->targetCount == 3 )
                    {
                        i->targetCount = 4;
                    }

                    i->currentCount = 0;
                    i->srcAddr = ( c & 0xF8 ) >> 3;

                    if ( !( c & 0x04 ) )
                    {
                        /* This is a Instrumentation (SW) packet */
                        i->stats.SWPkt++;
                        newState = ITM_SW;
                    }
                    else
                    {
                        /* This is a HW packet */
                        i->stats.HWPkt++;
                        newState = ITM_HW;
                    }

                    break;
                }

                // *************************************************
                // ************** PROTOCOL PACKET ******************
                // *************************************************

                if ( c == 0b01110000 )
                {
                    /* This is an overflow packet */
                    i->stats.overflow++;
                    retVal = ITM_EV_OVERFLOW;
                    break;
                }

                // ***********************************************
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

                // ***********************************************

                if ( ( c & 0b11011111 ) == 0b10010100 )
                {
                    /* This is a global timestamp packet */
                    if ( ( c & 0b00100000 ) == 0 )
                    {
                        newState = ITM_GTS1;
                    }
                    else
                    {
                        newState = ITM_GTS2;
                    }

                    break;
                }

                // ***********************************************
                if ( ( c & 0b00001000 ) == 0b00001000 )
                {
                    /* Extension Packet */
                    i->currentCount = 1; /* The '1' is deliberate. */
                    i->stats.XTNPkt++;

                    i->rxPacket[0] = c;

                    if ( !( c & 0x84 ) )
                    {
                        /* This is the Stimulus Port Page Register setting ... deal with it here */
                        i->stats.PagePkt++;
                        i->pageRegister = ( c >> 4 ) & 0x07;
                    }
                    else
                    {
                        newState = ITM_XTN;
                    }

                    break;
                }

                // ***********************************************
                if ( ( ( c & 0b11000100 ) == 0b11000100 ) ||
                        ( ( c & 0b10000100 ) == 0b10000100 ) ||
                        ( ( c & 0b11110000 ) == 0b11110000 ) ||
                        ( ( c & 0b00000100 ) == 0b00000100 ) )
                {
                    /* Reserved packets - we have no idea what these are */
                    i->currentCount = 1;
                    i->stats.ReservedPkt++;
                    i->rxPacket[0] = c;

                    if ( !( c & 0x80 ) )
                    {
                        retVal = ITM_EV_RESERVED_PACKET_RXED;
                    }
                    else
                    {
                        /* According to protocol, this is multi-byte, so report it */
                        newState = ITM_RSVD;
                    }

                    break;
                }

                // Any other value here isn't valid - so fall through to illegal packet case


                // *************************************************
                // ************** ILLEGAL PACKET *******************
                // *************************************************
                /* This is a reserved encoding we don't know how to handle */
                /* ...assume it's line noise and wait for sync again */
                i->stats.ErrorPkt++;
#ifdef DEBUG
                fprintf( stderr, EOL "%02X " EOL, c );
#endif
                retVal = ITM_EV_ERROR;
                genericsReport( V_DEBUG, "General error for packet type %02x" EOL, c );
                break;

            // -----------------------------------------------------
            case ITM_GTS1:  // Collecting GTS1 timestamp - wait for a zero continuation bit
                if ( ( c & 0x80 ) == 0 )
                {
                    newState = ITM_IDLE;
                }

                break;

            // -----------------------------------------------------
            case ITM_GTS2: // Collecting GTS2 timestamp - wait for a zero continuation bit
                if ( ( c & 0x80 ) == 0 )
                {
                    newState = ITM_IDLE;
                }

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
            case ITM_RSVD:
                i->rxPacket[i->currentCount++] = c;

                if ( ( !( c & 0x80 ) ) || ( i->currentCount >= MAX_PACKET ) )
                {
                    /* We are done */
                    newState = ITM_IDLE;
                    retVal = ITM_EV_RESERVED_PACKET_RXED;
                    break;
                }

                break;

            // -----------------------------------------------------
            case ITM_XTN:
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

    if ( ( i->p != ITM_UNSYNCED ) || ( newState != ITM_UNSYNCED ) )
    {
        genericsReport( V_DEBUG, "%02x %s --> %s(%d)%s", c, _protoNames[i->p], _protoNames[newState], i->targetCount, ( ( newState == ITM_IDLE ) ? EOL : " : " ) );
    }

    i->p = newState;
    return retVal;
}
// ====================================================================================================
