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

#include <stdio.h>
#include <sys/time.h>
#include <string.h>
#include "tpiuDecoder.h"

#define SYNCPATTERN 0xFFFFFF7F
#define NO_CHANNEL_CHANGE (0xFF)

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
void TPIUDecoderForceSync( struct TPIUDecoder *t, uint8_t offset )

/* Force the decoder into a specific sync state */

{
    if ( t->state == TPIU_UNSYNCED )
    {
        t->stats.syncCount++;
    }

    t->state = TPIU_RXING;
    t->byteCount = offset;
}
// ====================================================================================================
BOOL TPIUGetPacket( struct TPIUDecoder *t, struct TPIUPacket *p )

/* Copy received packet into transfer buffer, and reset receiver */

{
    uint8_t delayedStreamChange = NO_CHANNEL_CHANGE;

    /* This should have been reset in the call */
    if ( ( t->byteCount ) || ( !p ) )
    {
        return FALSE;
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

    return TRUE;
}
// ====================================================================================================
enum TPIUPumpEvent TPIUPump( struct TPIUDecoder *t, uint8_t d )

/* Pump next byte into the protocol decoder */

{
    struct timeval nowTime, diffTime;

    t->syncMonitor = ( t->syncMonitor << 8 ) | d;

    if ( t->syncMonitor == SYNCPATTERN )
    {
        t->state = TPIU_RXING;
        t->stats.syncCount++;
        t->byteCount = 0;

        /* Consider this a valid timestamp */
        gettimeofday( &t->lastPacket, NULL );
        return TPIU_EV_SYNCED;
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
            if ( !diffTime.tv_sec )
            {
                t->stats.packets++;
                return TPIU_EV_RXEDPACKET;
            }
            else
            {
                fprintf( stderr, ">>>>>>>>> PACKET INTERVAL TOO LONG <<<<<<<<<<<<<<\n" );
                t->state = TPIU_UNSYNCED;
                t->stats.lostSync++;
                return TPIU_EV_UNSYNCED;
            }

        // -----------------------------------
        default:
            fprintf( stderr, "In illegal state %d\n", t->state );
            t->stats.error++;
            return TPIU_EV_ERROR;
            // -----------------------------------
    }
}
// ====================================================================================================
