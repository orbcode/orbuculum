/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * ITM Decoder Module
 * ==================
 *
 * Implementation of ITM/DWT decode according to the specification in Appendix D4
 * of the ARMv7-M Architecture Refrence Manual document available
 * from https://static.docs.arm.com/ddi0403/e/DDI0403E_B_armv7m_arm.pdf
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "itmDecoder.h"
#include "msgDecoder.h"

// Define this to get transitions printed out
#define DEBUG

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

// ====================================================================================================
struct ITMDecoder *ITMDecoderCreate( void )

{
    struct ITMDecoder *i = ( struct ITMDecoder * )calloc( 1, sizeof( struct ITMDecoder ) );
    i->selfAllocated = true;
    return i;
}
// ====================================================================================================
void ITMDecoderInit( struct ITMDecoder *i, bool startSynced )

/* Reset a ITMDecoder instance */

{
    i->syncStat = SYNCMASK;
    i->pk.len = 0;
    i->contextIDlen = 0;
    i->pk.pageRegister = DEFAULT_PAGE_REGISTER;
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
            i->pk.len = 0;
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

/* Copy raw received packet into transfer buffer */

{
    /* This should have been reset in the call */
    if ( i->p != ITM_IDLE )
    {
        return false;
    }

    memcpy( p->d, &i->pk, sizeof( struct ITMPacket ) );
    return true;
}
// ====================================================================================================
bool ITMGetDecodedPacket( struct ITMDecoder *i, struct msg *decoded )

/* Decode received packet into 'decoded' buffer */

{
    return msgDecoder( &i->pk, decoded );
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

    /* Just check for a TPIU sync ... this is incredibly unlikely in */
    /* ITM data, so it's an indicator that the TPIU should have been */
    /* switched in, and hasn't been. */
    i->syncStat = ( i->syncStat << 8 ) | c;

    if ( ( ( i->syncStat )&TPIU_SYNCMASK ) == TPIU_SYNCPATTERN )
    {
        i->stats.tpiuSyncCount++;
    }

    if ( ( ( i->syncStat )&SYNCMASK ) == SYNCPATTERN )
    {
        i->stats.syncCount++;

        /* Page register is reset on a sync */
        i->pk.pageRegister = 0;
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

                /* Start off by making sure entire received packet is 0'ed */
                memset( i->pk.d, 0, ITM_MAX_PACKET );

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

                    i->pk.len = 0;
                    i->pk.srcAddr = ( c & 0xF8 ) >> 3;

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
                    i->pk.len = 1; /* The '1' is deliberate. */
                    /* This is a timestamp packet */
                    i->pk.d[0] = c;
                    i->stats.TSPkt++;

                    if ( c & 0x80 )
                    {
                        /* This is TS packet format 1, so there's more to follow */
                        newState = ITM_TS;
                    }
                    else
                    {
                        /* This is TS packet format 2, no more to come, and no change of state */
                        i->pk.type = ITM_PT_TS;
                        retVal = ITM_EV_PACKET_RXED;
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
                if ( c == 0b00001000 )
                {
                    /* This is a normal I-sync packet */
                    newState = ITM_NISYNC;
                    i->pk.len = 0;
                    i->targetCount = MAX_PACKET + i->contextIDlen;
                    break;
                }

                // ***********************************************
                if ( ( c & 0b00001000 ) == 0b00001000 )
                {
                    /* Extension Packet */
                    i->pk.len = 1; /* The '1' is deliberate. */
                    i->stats.XTNPkt++;

                    i->pk.d[0] = c;

                    if ( !( c & 0x84 ) )
                    {
                        /* This is the Stimulus Port Page Register setting ... deal with it here */
                        i->stats.PagePkt++;
                        i->pk.pageRegister = ( c >> 4 ) & 0x07;
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
                    i->pk.len = 1;
                    i->stats.ReservedPkt++;
                    i->pk.d[0] = c;

                    if ( !( c & 0x80 ) )
                    {
                        i->pk.type = ITM_PT_RSRVD;
                        retVal = ITM_EV_PACKET_RXED;
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
                i->pk.d[i->pk.len++] = c;

                if ( i->pk.len >= i->targetCount )
                {
                    newState = ITM_IDLE;
                    i->pk.type = ITM_PT_SW;
                    retVal = ITM_EV_PACKET_RXED;
                }

                break;

            // -----------------------------------------------------
            case ITM_HW:
                i->pk.d[i->pk.len++] = c;

                if ( i->pk.len >= i->targetCount )
                {
                    newState = ITM_IDLE;
                    i->pk.type = ITM_PT_HW;
                    retVal = ITM_EV_PACKET_RXED;
                }

                break;

            // -----------------------------------------------------
            case ITM_TS:
                i->pk.d[i->pk.len++] = c;

                if ( ( !( c & 0x80 ) ) || ( i->pk.len >= MAX_PACKET ) )
                {
                    /* We are done */
                    newState = ITM_IDLE;
                    i->pk.type = ITM_PT_TS;
                    retVal = ITM_EV_PACKET_RXED;
                }

                break;

            // -----------------------------------------------------
            case ITM_RSVD:
                i->pk.d[i->pk.len++] = c;

                if ( ( !( c & 0x80 ) ) || ( i->pk.len >= MAX_PACKET ) )
                {
                    /* We are done */
                    newState = ITM_IDLE;
                    i->pk.type = ITM_PT_RSRVD;
                    retVal = ITM_EV_PACKET_RXED;
                }

                break;

            // -----------------------------------------------------
            case ITM_XTN:
                i->pk.d[i->pk.len++] = c;

                if ( ( !( c & 0x80 ) ) || ( i->pk.len >= MAX_PACKET ) )
                {
                    /* We are done */
                    newState = ITM_IDLE;
                    i->pk.type = ITM_PT_XTN;
                    retVal = ITM_EV_PACKET_RXED;
                }

                break;

            // -----------------------------------------------------
            case ITM_NISYNC:
                i->pk.d[i->pk.len++] = c;

                if ( i->pk.len > i->targetCount )
                {
                    newState = ITM_IDLE;
                    i->pk.type = ITM_PT_NISYNC;
                    retVal = ITM_EV_PACKET_RXED;
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
