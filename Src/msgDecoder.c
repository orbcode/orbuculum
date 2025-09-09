/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Msg Decoder Module
 * ==================
 *
 * Decoding of individual messages from the decoded stream according to the
 * specification in Appendix D4 of the ARMv7-M Architecture Refrence Manual
 * document available from https://static.docs.arm.com/ddi0403/e/DDI0403E_B_armv7m_arm.pdf
 */

#include <string.h>
#include <assert.h>
#include "itmDecoder.h"
#include "msgDecoder.h"
#include "generics.h"

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Internal routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
static bool _handleException( struct ITMPacket *packet, struct excMsg *decoded )

{
    decoded->msgtype = MSG_EXCEPTION;
    decoded->exceptionNumber = ( ( packet->d[1] & 0x01 ) << 8 ) | packet->d[0];
    decoded->eventType = packet->d[1] >> 4;

    return true;
}
// ====================================================================================================
static bool _handleDWTEvent( struct ITMPacket *packet, struct dwtMsg *decoded )

{
    decoded->msgtype = MSG_DWT_EVENT;
    decoded->event = packet->d[0] & 0x2F;
    return true;
}
// ====================================================================================================
static bool _handlePCSample( struct ITMPacket *packet, struct pcSampleMsg *decoded )

/* We got a sample of the PC */

{
    decoded->msgtype = MSG_PC_SAMPLE;

    if ( packet->len == 1 )
    {
        decoded->sleep = true;
    }
    else
    {
        decoded->sleep = false;
        decoded->pc = ( packet->d[3] << 24 ) | ( packet->d[2] << 16 ) | ( packet->d[1] << 8 ) | ( packet->d[0] );
    }

    return true;
}
// ====================================================================================================
static bool _handleDataRWWP( struct ITMPacket *packet, struct watchMsg *decoded )

/* We got an alert due to a watch pointer */

{
    decoded->msgtype = MSG_DATA_RWWP;
    decoded->comp = ( packet->srcAddr >> 1 ) & 0x3;
    decoded->isWrite = ( ( packet->srcAddr & 0x01 ) != 0 );

    switch ( packet->len )
    {
        case 1:
            decoded->data = packet->d[0];
            break;

        case 2:
            decoded->data = ( packet->d[0] ) | ( ( packet->d[1] ) << 8 );
            break;

        default:
            decoded->data = ( packet->d[0] ) | ( ( packet->d[1] ) << 8 ) | ( ( packet->d[2] ) << 16 ) | ( ( packet->d[3] ) << 24 );
            break;
    }

    return true;
}
// ====================================================================================================
static bool _handleDataAccessWP( struct ITMPacket *packet, struct wptMsg *decoded )

/* We got an alert due to a watchpoint */

{
    decoded->msgtype = MSG_DATA_ACCESS_WP;
    decoded->comp = ( packet->srcAddr >> 1 ) & 0x3;
    decoded->data = ( packet->d[0] ) | ( ( packet->d[1] ) << 8 ) | ( ( packet->d[2] ) << 16 ) | ( ( packet->d[3] ) << 24 );
    return true;
}
// ====================================================================================================
static bool _handleDataOffsetWP(  struct ITMPacket *packet, struct oswMsg *decoded )

/* We got an alert due to an offset write event */

{
    decoded->msgtype = MSG_OSW;
    decoded->comp = ( packet->srcAddr >> 1 ) & 0x3;
    decoded->offset = ( packet->d[0] ) | ( ( packet->d[1] ) << 8 );
    return true;
}
// ====================================================================================================
static bool _handleHW( struct ITMPacket *packet, struct msg *decoded )

/* ... a hardware event has been received, dispatch it */

{
    bool wasDecoded = false;
    decoded->genericMsg.msgtype = MSG_NONE;

    switch ( packet->srcAddr )
    {
        // --------------
        case 0: /* DWT Event */
            wasDecoded = _handleDWTEvent( packet, ( struct dwtMsg * )decoded );
            break;

        // --------------
        case 1: /* Exception */
            wasDecoded = _handleException( packet, ( struct excMsg * )decoded );
            break;

        // --------------
        case 2: /* PC Counter Sample */
            wasDecoded = _handlePCSample( packet, ( struct pcSampleMsg * )decoded );
            break;

        // --------------
        default:
            /* Special case for srcAddr 0x13 from DWT comparator 1 */
            if ( packet->srcAddr == 0x13 )
            {
                wasDecoded = _handleDataAccessWP( packet, ( struct wptMsg * )decoded );
            }
            else if ( ( ( packet->srcAddr & 0x19 ) == 0x10 ) || ( ( packet->srcAddr & 0x19 ) == 0x11 ) )
            {
                wasDecoded = _handleDataRWWP( packet, ( struct watchMsg * )decoded );
            }
            else if ( ( packet->srcAddr & 0x19 ) == 0x08 )
            {
                wasDecoded = _handleDataAccessWP( packet, ( struct wptMsg * )decoded );
            }
            else if ( ( packet->srcAddr & 0x19 ) == 0x09 )
            {
                wasDecoded = _handleDataOffsetWP( packet, ( struct oswMsg * )decoded );
            }

            break;
            // --------------
    }

    return wasDecoded;
}
// ====================================================================================================
static bool _handleSW( struct ITMPacket *packet, struct swMsg *decoded )

{
    decoded->msgtype = MSG_SOFTWARE;

    decoded->srcAddr = packet->srcAddr;
    decoded->len = packet->len;

    /* Build 32 bit value the long way around to avoid type-punning issues */
    decoded->value =
                ( packet->d[3] << 24 ) |
                ( packet->d[2] << 16 ) |
                ( packet->d[1] << 8 ) |
                ( packet->d[0] );
    return true;
}
// ====================================================================================================
static bool _handleNISYNC( struct ITMPacket *packet, struct nisyncMsg *decoded )

{
    decoded->msgtype = MSG_NISYNC;
    decoded->type = packet->d[0];
    decoded->addr =
                ( packet->d[1] & 0xFE ) |
                ( packet->d[2] << 8 ) |
                ( packet->d[3] << 16 ) |
                ( packet->d[4] << 24 );
    return true;
}
// ====================================================================================================
static bool _handleTS( struct ITMPacket *packet, struct TSMsg *decoded )

/* ... a timestamp */

{
    decoded->msgtype = MSG_TS;
    uint32_t stamp = 0;

    if ( !( ( packet->d[0] ) & 0x80 ) )
    {
        /* This is packet format 2 ... just a simple increment */
        stamp = packet->d[0] >> 4;
    }
    else
    {
        /* This is packet format 1 ... full decode needed */
        decoded->timeStatus = ( packet->d[0] & 0x30 ) >> 4;
        stamp = ( packet->d[1] ) & 0x7f;

        if ( packet->len > 2 )
        {
            stamp |= ( packet->d[2] & 0x7F ) << 7;
        }

        if ( packet->len > 3 )
        {
            stamp |= ( packet->d[3] & 0x7F ) << 14;
        }

        if ( packet->len > 4 )
        {
            stamp |= ( packet->d[4] & 0x7f ) << 21;
        }
    }

    decoded->timeInc = stamp;
    return true;
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Publically available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
bool msgDecoder( struct ITMPacket *packet, struct msg *decoded )

{
    bool wasDecoded = false;
    decoded->genericMsg.msgtype = MSG_NONE;
    decoded->genericMsg.ts = genericsTimestampuS(); /* Stamp as early as possible, even if its not real */

    switch ( packet->type )
    {
        case ITM_PT_NONE:
            decoded->genericMsg.msgtype = MSG_NONE;
            break;

        case ITM_PT_TS:
            wasDecoded = _handleTS( packet, ( struct TSMsg * )decoded );
            break;

        case ITM_PT_SW:
            wasDecoded = _handleSW( packet, ( struct swMsg * )decoded );
            break;

        case ITM_PT_HW:
            wasDecoded = _handleHW( packet, decoded );
            break;

        case ITM_PT_NISYNC:
            wasDecoded = _handleNISYNC( packet, ( struct nisyncMsg * )decoded );
            break;

        case ITM_PT_XTN:
            genericsReport( V_INFO, "Unknown Extension Packet Received" EOL );
            decoded->genericMsg.msgtype = MSG_UNKNOWN;
            break;

        case ITM_PT_RSRVD:
            genericsReport( V_INFO, "Reserved Packet Received" EOL );
            decoded->genericMsg.msgtype = MSG_RESERVED;
            break;

        default:
            decoded->genericMsg.msgtype = MSG_ERROR;
            break;
    }

    return wasDecoded;
}
// ====================================================================================================
