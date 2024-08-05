/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * OTAG Encoder/Decoder Module
 * ===========================
 *
 *
 */

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include "cobs.h"
#include "otag.h"

// ====================================================================================================
struct OTAG *OTAGInit( struct OTAG *t )

/* Reset a OTAG instance */

{
    if ( !t )
    {
        t = ( struct OTAG * )calloc( 1, sizeof( struct OTAG ) );
        t->selfAllocated = true;
    }

    /* Initialise the containing COBS instance */
    COBSInit( &t->c );

    return t;
}
// ====================================================================================================
void OTAGDelete( struct OTAG *t )

/* Destroy a OTAG instance, but only if we created it */

{
    /* Need to remove the containing COBS instance */
    COBSDelete( &t->c );

    if ( t->selfAllocated )
    {
        free( t );
        t = NULL;
    }
}

// ====================================================================================================

void OTAGEncode( const uint8_t channel, const uint64_t tstamp, const uint8_t *inputMsg, int len, struct Frame *o )

/* Encode frame and write into provided output Frame buffer */

{
    const uint8_t frontMatter[1] = { channel };
    uint8_t sum = channel;

    /* Calculate packet sum for last byte */
    for ( int i = 0; i < len; i++ )
    {
        sum += inputMsg[i];
    }

    /* Ensure total sums to 0 */
    uint8_t backMatter[1] = { 256 - sum };

    COBSEncode( frontMatter, 1, backMatter, 1, inputMsg, len, o );
}

// ====================================================================================================

bool OTAGisEOFRAME( const uint8_t *inputEnc )

{
    return ( COBS_SYNC_CHAR == *inputEnc );
}

// ====================================================================================================
static void _pumpcb( struct Frame *p, void *param )

{
    /* Callback function when a COBS packet is complete */
    struct OTAG *t = ( struct OTAG * )param;

    t->f.len  = p->len - 2;       /* OTAG frames have the first element representing the tag and last element the checksum */
    t->f.tag  = p->d[0];          /* First byte of an OTAG frame is the tag */
    t->f.sum  = p->d[p->len - 1]; /* Last byte of an OTAG frame is the sum */
    t->f.d    = &p->d[1];         /* This is the rest of the data */

    /* Calculate received packet sum and insert good status into packet */
    uint8_t sum  = t->f.tag;

    for ( int i = 0; i < t->f.len; i++ )
    {
        sum += t->f.d[i];
    }

    sum += t->f.sum;
    t->f.good = ( sum == 0 );

    /* Timestamp was already set for this cluster */
    ( t->cb )( &t->f, t->param );
}

void OTAGPump( struct OTAG *t, const uint8_t *incoming, int len,
               void ( *packetRxed )( struct OTAGFrame *p, void *param ),
               void *param )


/* Assemble this packet into a complete frame and call back */

{
    struct timespec ts;
    t->cb = packetRxed;
    clock_gettime( CLOCK_REALTIME, &ts );
    t->f.tstamp = ts.tv_sec * OTAG_TS_RESOLUTION + ts.tv_nsec; /* For now, fake the timestamp */
    t->param = param;
    COBSPump( &t->c, incoming, len, _pumpcb, t );
}

// ====================================================================================================

const uint8_t *OTAGgetFrameExtent( const uint8_t *inputEnc, int len )

/* Look through memory until an end of frame marker is found, or memory is exhausted. */

{
    /* Go find the next sync */
    while ( !OTAGisEOFRAME( inputEnc ) && --len )
    {
        inputEnc++;
    }

    return inputEnc;
}

// ====================================================================================================
