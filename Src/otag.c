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
    COBSEncode( frontMatter, 1, inputMsg, len, o );
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
    struct timespec ts;
    struct OTAG *t = ( struct OTAG * )param;

    t->f.len = p->len - 1; /* OTAG frames have the first element representing the tag */
    clock_gettime( CLOCK_REALTIME, &ts );
    t->f.tstamp = ts.tv_sec * OTAG_TS_RESOLUTION + ts.tv_nsec; /* For now, fake the timestamp */
    t->f.d = &p->d[1];

    ( t->cb )( &t->f, t->param );
}

void OTAGPump( struct OTAG *t, uint8_t *incoming, int len,
               void ( *packetRxed )( struct OTAGFrame *p, void *param ),
               void *param )


/* Assemble this packet into a complete frame and call back */

{
    t->cb = packetRxed;
    t->param = param;
    COBSPump( &t->c, incoming, len, _pumpcb, t );
}

// ====================================================================================================

bool OTAGSimpleDecode( const uint8_t *inputEnc, int len, struct Frame *o )

/* Decode frame and write decoded frame into provided Frame buffer           */
/* Returns FALSE if packet did not decode...and store the fragment.          */

{
    return COBSSimpleDecode( inputEnc, len, o );
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
