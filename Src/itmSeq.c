/*
 * ITM Sequencer Module
 * ====================
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
 * Sequencer for re-ordering messages from the ITM according to timestamp
 * information in the flow.
 *
 * from https://static.docs.arm.com/ddi0403/e/DDI0403E_B_armv7m_arm.pdf
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "generics.h"
#include "itmSeq.h"

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Internal routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================

static bool _bufferPacket( struct ITMSeq *d, enum ITMPacketType label )

{
    ITMGetPacket( d->i, &d->pbuffer[d->wp] );
    d->pbuffer[d->wp].type = label;

    d->wp = ( d->wp + 1 ) % d->pbl;

    assert( d->wp != d->rp );

    /* If the next message would cause overflow, then empty regardless */
    return ( ( ( d->wp + 1 ) % d->pbl ) == d->rp );
}

// ====================================================================================================

static void _handleTS( struct ITMSeq *d )

/* ... a timestamp */

{
    struct ITMPacket p;
    uint32_t stamp = 0;

    if ( ITMGetPacket( d->i, &p ) )
    {
        if ( !( p.d[0] & 0x80 ) )
        {
            /* This is packet format 2 ... just a simple increment */
            stamp = p.d[0] >> 4;
        }
        else
        {
            /* This is packet format 1 ... full decode needed */
            d->i->timeStatus = ( p.d[0] & 0x30 ) >> 4;
            stamp = ( p.d[1] ) & 0x7f;

            if ( p.len > 2 )
            {
                stamp |= ( p.d[2] ) << 7;

                if ( p.len > 3 )
                {
                    stamp |= ( p.d[3] & 0x7F ) << 14;

                    if ( p.len > 4 )
                    {
                        stamp |= ( p.d[4] & 0x7f ) << 21;
                    }
                }
            }
        }

        d->i->timeStamp += stamp;
    }
}

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Externally available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================

void ITMSeqInit( struct ITMSeq *d, struct ITMDecoder *i, uint32_t maxEntries )

/* Reset and initialise an ITMSequencer instance */

{
    memset( d, 0, sizeof( struct ITMSeq ) );
    d->i = i;
    d->pbl = maxEntries;
    d->pbuffer = calloc( maxEntries, sizeof( struct ITMPacket ) );
}

// ====================================================================================================

struct ITMPacket *ITMSeqGetPacket( struct ITMSeq *d )

{
    uint32_t trp = d->rp;

    if ( d->wp == d->rp )
    {
        return NULL;
    }

    /* Roll to next entry */
    d->rp = ( d->rp + 1 ) % d->pbl;

    return &d->pbuffer[trp];
}

// ====================================================================================================

bool ITMSeqPump( struct ITMSeq *d, uint8_t c )

/* Handle individual characters into the itm decoder */

{
    bool r = false;

    switch ( ITMPump( d->i, c ) )
    {
        // ------------------------------------
        case ITM_EV_NONE:
            break;

        // ------------------------------------
        case ITM_EV_UNSYNCED:
            genericsReport( V_WARN, "ITM Lost Sync (%d)" EOL, ITMDecoderGetStats( d->i )->lostSyncCount );
            break;

        // ------------------------------------
        case ITM_EV_SYNCED:
            genericsReport( V_INFO, "ITM In Sync (%d)" EOL, ITMDecoderGetStats( d->i )->syncCount );
            break;

        // ------------------------------------
        case ITM_EV_OVERFLOW:
            genericsReport( V_WARN, "ITM Overflow (%d)" EOL, ITMDecoderGetStats( d->i )->overflow );
            break;

        // ------------------------------------
        case ITM_EV_RESERVED_PACKET_RXED:
            genericsReport( V_INFO, "Reserved Packet Received" EOL );
            break;

        // ------------------------------------
        case ITM_EV_XTN_PACKET_RXED:
            genericsReport( V_INFO, "Unknown Extension Packet Received" EOL );
            break;

        // ------------------------------------
        case ITM_EV_ERROR:
            genericsReport( V_WARN, "ITM Error" EOL );
            break;

        // ------------------------------------
        case ITM_EV_TS_PACKET_RXED:
            _handleTS( d );
            r = true;
            break;

        // ------------------------------------
        case ITM_EV_SW_PACKET_RXED:
            r = _bufferPacket(  d, ITM_PT_SW );
            break;

        // ------------------------------------
        case ITM_EV_HW_PACKET_RXED:
            r = _bufferPacket( d, ITM_PT_HW );
            break;

            // ------------------------------------
    }

    return r;
}

// ====================================================================================================
