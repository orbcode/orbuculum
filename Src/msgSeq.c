/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Message Sequencer Module
 * ========================
 *
 * Sequencer for re-ordering messages from the ITM according to timestamp
 * information in the flow.
 *
 * from https://static.docs.arm.com/ddi0403/e/DDI0403E_B_armv7m_arm.pdf
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "generics.h"
#include "msgSeq.h"
#include "msgDecoder.h"

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Internal routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
static bool _bufferPacket( struct MSGSeq *d )

{
    struct msg p;


    if ( !ITMGetDecodedPacket( d->i, &p )  )
    {
        /* There wasn't a decodable message in there */
        return false;
    }

    /* Make a copy of it for later dispatch */
    memcpy( &d->pbuffer[d->wp], &p, sizeof( struct msg ) );

    /* If this is a timestamp then we put it on the front to be released first */
    if ( d->pbuffer[d->wp].genericMsg.msgtype == MSG_TS )
    {
        d->releaseTimeMsg = true;
        return true;
    }
    else
    {
        d->wp = ( d->wp + 1 ) % d->pbl;

        assert( d->wp != d->rp );

        /* If the next message would cause overflow, then empty regardless */
        return ( ( ( d->wp + 1 ) % d->pbl ) == d->rp );
    }
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Externally available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
void MSGSeqInit( struct MSGSeq *d, struct ITMDecoder *i, uint32_t maxEntries )

/* Reset and initialise an Message Sequencer instance */

{
    memset( d, 0, sizeof( struct MSGSeq ) );
    d->i = i;
    d->pbl = maxEntries;
    d->pbuffer = ( struct msg * )calloc( maxEntries, sizeof( struct msg ) );
}
// ====================================================================================================
struct msg *MSGSeqGetPacket( struct MSGSeq *d )

{
    uint32_t trp = d->rp;

    /* Roll the timestamp off the front if it's present */
    if ( d->releaseTimeMsg )
    {
        d->releaseTimeMsg = false;
        return &d->pbuffer[d->wp];
    }

    if ( d->wp == d->rp )
    {
        return NULL;
    }

    /* Roll to next entry */
    d->rp = ( d->rp + 1 ) % d->pbl;

    return &d->pbuffer[trp];
}
// ====================================================================================================
bool MSGSeqPump( struct MSGSeq *d, uint8_t c )

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
            genericsReport( V_DEBUG, "ITM Overflow (%d)" EOL, ITMDecoderGetStats( d->i )->overflow );
            break;

        // ------------------------------------
        case ITM_EV_ERROR:
            genericsReport( V_WARN, "ITM Error" EOL );
            break;

        // ------------------------------------
        case ITM_EV_PACKET_RXED:
            r = _bufferPacket(  d );
            break;

            // ------------------------------------
    }

    return r;
}
// ====================================================================================================
