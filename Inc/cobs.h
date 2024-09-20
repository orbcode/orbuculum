/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * COBS Module
 * ===========
 *
 */

#ifndef _COBS_
#define _COBS_

#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COBS_FRONTMATTER            (10)
#define COBS_MAX_PACKET_LEN         (4096)
#define COBS_SYNC_CHAR              (0)
#define COBS_OVERALL_MAX_PACKET_LEN (COBS_MAX_PACKET_LEN+COBS_FRONTMATTER)
#define COBS_MAX_ENC_PACKET_LEN     (COBS_OVERALL_MAX_PACKET_LEN + COBS_OVERALL_MAX_PACKET_LEN / 254)

enum COBSPumpState
{
    COBS_IDLE,
    COBS_RXING,
    COBS_DRAINING
};

struct Frame
{
    unsigned int len;                       /* Received length (after pre-processing) */
    uint8_t d[COBS_MAX_ENC_PACKET_LEN];     /* ...the data itself + room for worst case overhead */
};

struct COBS
{
    struct Frame f;                        /* Decoded frame currently under construction */
    enum COBSPumpState s;
    int intervalCount;
    bool maxCount;
    int error;
    struct Frame partf;                    /* Partial frame that is being collected */
    bool selfAllocated;                    /* Flag indicating that memory was allocated by the library */
};

#define COBS_EOP_LEN (1)
extern const uint8_t cobs_eop[COBS_EOP_LEN];

// ====================================================================================================

const uint8_t *COBSgetFrameExtent( const uint8_t *inputEnc, int len );
bool COBSSimpleDecode( const uint8_t *inputEnc, int len, struct Frame *o );
bool COBSisEOFRAME( const uint8_t *inputEnc );

void COBSEncode( const uint8_t *frontMsg, int lfront, const uint8_t *backMsg, int lback, const uint8_t *inputMsg, int lmsg, struct Frame *o );

/* Context free functions */
void COBSPump( struct COBS *t, const uint8_t *incoming, int len,
               void ( *packetRxed )( struct Frame *p, void *param ),
               void *param );
void COBSDelete( struct COBS *t );
static inline int COBSGetErrors( struct COBS *t )
{
    return t ? t->error : 0;
}
struct COBS *COBSInit( struct COBS *t );
// ====================================================================================================
#ifdef __cplusplus
}
#endif
#endif
