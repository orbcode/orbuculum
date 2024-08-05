/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * OTAG Module
 * ===========
 *
 */

#ifndef _OTAG_
#define _OTAG_

#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>
#include "cobs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct OTAGFrame
{
    unsigned int len;                       /* Received length (after pre-processing) */
    uint8_t      tag;                       /* Tag (packet type) */
    uint8_t      sum;                       /* Checksum byte */
    bool         good;                      /* Is the checksum valid? */
    uint64_t     tstamp;                    /* Timestamp for the packet */

    uint8_t *d;                             /* ...pointer to the data itself */
};

struct OTAG
{
    bool selfAllocated;                    /* Flag indicating that memory was allocated by the library */
    struct COBS c;
    struct OTAGFrame f;

    /* Materials for callback */
    void ( *cb )( struct OTAGFrame *p, void *param );
    void *param;
};

#define OTAG_MAX_PACKET_LEN     (COBS_MAX_PACKET_LEN-2)
#define OTAG_MAX_ENC_PACKET_LEN (COBS_MAX_ENC_PACKET_LEN)
#define OTAG_EOP_LEN            (COBS_EOP_LEN)
#define OTAG_TS_RESOLUTION      (1000000000L)

// ====================================================================================================

static inline uint64_t OTAGResolution( struct OTAG *t )
{
    return OTAG_TS_RESOLUTION;
}

const uint8_t *OTAGgetFrameExtent( const uint8_t *inputEnc, int len );
bool OTAGisEOFRAME( const uint8_t *inputEnc );

void OTAGEncode( const uint8_t channel, const uint64_t tstamp, const uint8_t *inputMsg, int len, struct Frame *o );

/* Context free functions */
void OTAGPump( struct OTAG *t, const uint8_t *incoming, int len,
               void ( *packetRxed )( struct OTAGFrame *p, void *param ),
               void *param );

void OTAGDelete( struct OTAG *t );
struct OTAG *OTAGInit( struct OTAG *t );
// ====================================================================================================
#ifdef __cplusplus
}
#endif
#endif
