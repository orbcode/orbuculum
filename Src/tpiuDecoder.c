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

#include <stdio.h>
#include <sys/time.h>
#include <string.h>
#include "generics.h"
#include "tpiuDecoder.h"

#define SYNCPATTERN 0xFFFFFF7F
#define NO_CHANNEL_CHANGE (0xFF)
#define TIMEOUT (3)

// ====================================================================================================
void TPIUDecoderInit( struct TPIUDecoder *t )

/* Reset a TPIUDecoder instance */

{
    t->state = TPIU_UNSYNCED;
    t->syncMonitor = 0;
    TPIUDecoderZeroStats( t );
}
// ====================================================================================================
void TPIUDecoderZeroStats( struct TPIUDecoder *t )

{
    memset( &t->stats, 0, sizeof( struct TPIUDecoderStats ) );
}
// ====================================================================================================
bool TPIUDecoderSynced( struct TPIUDecoder *t )
{
    return t->state != TPIU_UNSYNCED;
}
// ====================================================================================================
struct TPIUDecoderStats *TPIUDecoderGetStats( struct TPIUDecoder *t )
{
    return &t->stats;
}
// ====================================================================================================
void TPIUDecoderForceSync( struct TPIUDecoder *t, uint8_t offset )

/* Force the decoder into a specific sync state */

{
    if ( t->state == TPIU_UNSYNCED )
    {
        t->stats.syncCount++;
    }

    t->state = TPIU_RXING;
    t->byteCount = offset;

    /* Consider this a valid timestamp */
    gettimeofday( &t->lastPacket, NULL );
}
// ====================================================================================================
bool TPIUGetPacket( struct TPIUDecoder *t, struct TPIUPacket *p )

/* Copy received packet into transfer buffer, and reset receiver */

{
    uint8_t delayedStreamChange = NO_CHANNEL_CHANGE;

    /* This should have been reset in the call */
    if ( ( t->byteCount ) || ( !p ) )
    {
        return false;
    }

    p->len = 0;
    uint8_t lowbits = t->rxedPacket[TPIU_PACKET_LEN - 1];

    for ( uint32_t i = 0; i < TPIU_PACKET_LEN; i += 2 )
    {
        if ( ( t->rxedPacket[i] ) & 1 )
        {
            /* This is a stream change - either before or after the data byte */
            if ( lowbits & 1 )
            {
                delayedStreamChange = t->rxedPacket[i] >> 1;
            }
            else
            {
                t->currentStream = t->rxedPacket[i] >> 1;
            }
        }
        else
        {
            /* This is a data byte - store it */
            p->packet[p->len].d = t->rxedPacket[i] | ( lowbits & 1 );
            p->packet[p->len].s = t->currentStream;
            p->len++;
        }

        /* Now deal with the second byte of the pair */
        if ( i < 14 )
        {
            /* Now deal with the other byte of the pair ... this is always data */
            p->packet[p->len].d = t->rxedPacket[i + 1];
            p->packet[p->len].s = t->currentStream;
            p->len++;
        }

        /* ... and finally, if there's a delayed channel change, deal with it */
        if ( delayedStreamChange != NO_CHANNEL_CHANGE )
        {
            t->currentStream = delayedStreamChange;
            delayedStreamChange = NO_CHANNEL_CHANGE;
        }

        /* Make sure we accomodate the lowbit for the next two bytes */
        lowbits >>= 1;
    }

    return true;
}
// ====================================================================================================
enum TPIUPumpEvent TPIUPump( struct TPIUDecoder *t, uint8_t d )

/* Pump next byte into the protocol decoder */

{
    struct timeval nowTime, diffTime;

    t->syncMonitor = ( t->syncMonitor << 8 ) | d;

    if ( t->syncMonitor == SYNCPATTERN )
    {

        enum TPIUPumpEvent r;

        if ( t->state != TPIU_UNSYNCED )
        {
            r = TPIU_EV_SYNCED;
        }
        else
        {
            r = TPIU_EV_NEWSYNC;
        }

        t->state = TPIU_RXING;
        t->stats.syncCount++;
        t->byteCount = 0;

        /* Consider this a valid timestamp */
        gettimeofday( &t->lastPacket, NULL );

        return r;
    }

    switch ( t->state )
    {
        // -----------------------------------
        case TPIU_UNSYNCED:
            return TPIU_EV_NONE;

        // -----------------------------------
        case TPIU_RXING:
            t->rxedPacket[t->byteCount++] = d;

            if ( t->byteCount != TPIU_PACKET_LEN )
            {
                return TPIU_EV_RXING;
            }

            /* Check if this packet arrived a sensible time since the last one */
            gettimeofday( &nowTime, NULL );
            timersub( &nowTime, &t->lastPacket, &diffTime );
            memcpy( &t->lastPacket, &nowTime, sizeof( struct timeval ) );
            t->byteCount = 0;

            /* If it was less than a second since the last packet then it's valid */
            if ( diffTime.tv_sec < TIMEOUT )
            {
                t->stats.packets++;
                return TPIU_EV_RXEDPACKET;
            }
            else
            {
                genericsReport( V_WARN, ">>>>>>>>> PACKET INTERVAL TOO LONG <<<<<<<<<<<<<<" EOL );
                t->state = TPIU_UNSYNCED;
                t->stats.lostSync++;
                return TPIU_EV_UNSYNCED;
            }

        // -----------------------------------
        default:
            genericsReport( V_WARN, "In illegal state %d" EOL, t->state );
            t->stats.error++;
            return TPIU_EV_ERROR;
            // -----------------------------------
    }
}
// ====================================================================================================
