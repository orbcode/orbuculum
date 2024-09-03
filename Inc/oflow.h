/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * ORBFLOW Module
 * ==============
 *
 */

#ifndef _ORBFLOW_
#define _ORBFLOW_

#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>
#include "cobs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct OFLOWFrame
{
    unsigned int len;                       /* Received length (after pre-processing) */
    uint8_t      tag;                       /* Tag (packet type) */
    uint8_t      sum;                       /* Checksum byte */
    bool         good;                      /* Is the checksum valid? */
    uint64_t     tstamp;                    /* Timestamp for the packet */

    uint8_t *d;                             /* ...pointer to the data itself */
};

struct OFLOW
{
    bool selfAllocated;                    /* Flag indicating that memory was allocated by the library */
    struct COBS c;
    struct OFLOWFrame f;
    uint64_t perror;

    /* Materials for callback */
    void ( *cb )( struct OFLOWFrame *p, void *param );
    void *param;
};

#define OFLOW_MAX_PACKET_LEN     (COBS_MAX_PACKET_LEN-2)
#define OFLOW_MAX_ENC_PACKET_LEN (COBS_MAX_ENC_PACKET_LEN)
#define OFLOW_EOP_LEN            (COBS_EOP_LEN)
#define OFLOW_TS_RESOLUTION      (1000000000L)

// ====================================================================================================

static inline uint64_t OFLOWResolution( struct OFLOW *t )
{
    return OFLOW_TS_RESOLUTION;
}

const uint8_t *OFLOWgetFrameExtent( const uint8_t *inputEnc, int len );
bool OFLOWisEOFRAME( const uint8_t *inputEnc );

void OFLOWEncode( const uint8_t channel, const uint64_t tstamp, const uint8_t *inputMsg, int len, struct Frame *o );

/* Context free functions */
void OFLOWPump( struct OFLOW *t, const uint8_t *incoming, int len,
                void ( *packetRxed )( struct OFLOWFrame *p, void *param ),
                void *param );
static inline uint64_t OFLOWGetErrors( struct OFLOW *t )
{
    return t ? t->perror : ( uint64_t ) -1;
}
static inline uint64_t OFLOWGetCOBSErrors( struct OFLOW *t )
{
    return t ? COBSGetErrors( &t->c ) : -1;
}
void OFLOWDelete( struct OFLOW *t );
struct OFLOW *OFLOWInit( struct OFLOW *t );
// ====================================================================================================
#ifdef __cplusplus
}
#endif
#endif
