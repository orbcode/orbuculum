/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * COBS Encoder/Decoder Module
 * ===========================
 *
 * Routines based on code from http://www.stuartcheshire.org/papers/COBSforToN.pdf
 * IEEE/ACM TRANSACTIONS ON NETWORKING, VOL.7, NO. 2, APRIL 1999
 * Consistent Overhead Byte Stuffing, Stuart Cheshire and Mary Baker
 *
 */

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "cobs.h"

const uint8_t cobs_eop[COBS_EOP_LEN] = { COBS_SYNC_CHAR };

// ====================================================================================================
struct COBS *COBSInit( struct COBS *t )

/* Reset a COBS instance */

{
    if ( !t )
    {
        t = ( struct COBS * )calloc( 1, sizeof( struct COBS ) );
        t->selfAllocated = true;
    }

    return t;
}
// ====================================================================================================
void COBSDelete( struct COBS *t )

/* Destroy a COBS instance, but only if we created it */

{
    if ( t->selfAllocated )
    {
        free( t );
        t = NULL;
    }
}

// ====================================================================================================

void COBSEncode( const uint8_t *frontMsg, int lfront, const uint8_t *inputMsg, int len, struct Frame *o )

/* Encode frame and write into provided output Frame buffer */

{
    uint8_t *wp = o->d;
    o->len = 0;

    len += lfront;

    assert( len <= COBS_OVERALL_MAX_PACKET_LEN );

    if ( len )
    {
        uint8_t *cp = wp++;
        int seglen = 1;

        for ( int i = 0; len--; i++ )
        {
            /* Take byte either from frontmatter or main message */
            const uint8_t *rp = ( i < lfront ) ? &frontMsg[i] : &inputMsg[i - lfront];

            if ( COBS_SYNC_CHAR != *rp )
            {
                *wp++ = *rp;
                seglen++;
            }

            if ( ( COBS_SYNC_CHAR == *rp ) || ( 0xff == seglen ) )
            {
                *cp = seglen;
                seglen = 1;
                cp = wp;

                if ( ( COBS_SYNC_CHAR == *rp )  || len )
                {
                    wp++;
                }
            }
        }

        *cp = seglen;
    }

    /* Packet must end with a sync to define EOP */
    *wp++ = COBS_SYNC_CHAR;

    o->len = ( wp - o->d );
}

// ====================================================================================================

bool COBSisEOFRAME( const uint8_t *inputEnc )

{
    return ( COBS_SYNC_CHAR == *inputEnc );
}

// ====================================================================================================

void COBSPump( struct COBS *t, const uint8_t *incoming, int len,
               void ( *packetRxed )( struct Frame *p, void *param ),
               void *param )


/* Assemble this packet into a complete frame and call back */

{
    const uint8_t *fp = incoming;

    for ( unsigned int rlen = 0; rlen < len; rlen++, fp++ )
    {
        switch ( t->s )
        {
            case COBS_IDLE:  // -------------------------------------------------------------------
                if ( COBS_SYNC_CHAR != *fp )
                {
                    t->f.len = 0;
                    t->intervalCount = *fp;
                    t->maxCount = ( *fp == 255 );
                    t->s = COBS_RXING;
                }

                break;

            case COBS_RXING: // -------------------------------------------------------------------
                t->intervalCount--;

                if ( !t->intervalCount )
                {
                    if ( COBS_SYNC_CHAR == *fp )
                    {
                        /* This is the end of a packet */
                        packetRxed( &t->f, param );
                        t->s = COBS_IDLE;
                    }
                    else
                    {
                        if ( !t->maxCount )
                        {
                            t->f.d[t->f.len++] = COBS_SYNC_CHAR;
                        }

                        t->intervalCount = *fp;
                        t->maxCount = ( *fp == 255 );
                    }
                }
                else
                {
                    /* Check for frame overflow ... if it's max then error */
                    if ( ( t->f.len > COBS_MAX_PACKET_LEN  ) || ( COBS_SYNC_CHAR == *fp ) )
                    {
                        t->error++;
                        t->s = COBS_IDLE;
                    }
                    else
                    {
                        t->f.d[t->f.len++] = *fp;
                    }
                }

                break;
        }
    }
}

// ====================================================================================================
bool COBSSimpleDecode( const uint8_t *inputEnc, int len, struct Frame *o )

/* Decode frame and write decoded frame into provided Frame buffer           */
/* Returns pointer to first character after frame (should be COBS_SYNC_CHAR) */
/* or NULL if packet did not decode...and store the fragment.                */

{
    const uint8_t *fp = inputEnc;
    const uint8_t *efp = inputEnc + len;

    uint8_t *op = o->d;

    uint8_t interval;

    /* Deal with possibility of sync chars on the front */
    while  ( ( COBS_SYNC_CHAR == *fp ) && ( fp < efp ) )
    {
        fp++;
    }

    while ( fp < efp )
    {
        interval = *fp++;

        if ( COBS_SYNC_CHAR == interval )
        {
            /* We have finished...for better or worse */
            break;
        }

        for ( int i = 1; i < interval; i++ )
        {
            /* Deal with possibility of illegal sync chars in the flow */
            if ( *fp == COBS_SYNC_CHAR )
            {
                /* return false...no good packet here */
                o->len = 0;
                return false;
            }

            *op++ = *fp++;
        }

        if ( ( interval != 0xff ) && ( *fp != COBS_SYNC_CHAR ) )
        {
            *op++ = COBS_SYNC_CHAR;
        }
    }

    o->len = op - o->d;
    return op != o->d;
}

// ====================================================================================================

const uint8_t *COBSgetFrameExtent( const uint8_t *inputEnc, int len )

/* Look through memory until an end of frame marker is found, or memory is exhausted. */

{
    /* Go find the next sync */
    while ( !COBSisEOFRAME( inputEnc ) && --len )
    {
        inputEnc++;
    }

    return inputEnc;
}

// ====================================================================================================
