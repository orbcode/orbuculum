/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * TPIU Decoder Module
 * ===================
 *
 */

#ifndef _COBS_DECODER_
#define _COBS_DECODER_

#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

enum COBSPumpEvent
{
    COBS_EV_NONE,
    COBS_EV_UNSYNCED,
    COBS_EV_NEWSYNC,
    COBS_EV_RXING,
    COBS_EV_RXEDFRAME,
    COBS_EV_ERROR
};

enum COBSPumpState
{
    COBS_UNSYNCED,
    COBS_IDLE,
    COBS_RXING,
    COBS_ERROR
};

#define COBS_MAX_PACKET_LEN (4096)

struct COBSDecoderStats
{
    uint32_t lostSync;                     /* Number of times sync has been lost */
    uint32_t syncCount;                    /* Number of times a sync event has been received */
    uint32_t halfSyncCount;                /* Number of times a half sync event has been received */
    uint32_t packets;                      /* Number of packets received */
    uint32_t error;                        /* Number of times an error has been received */
};

struct Frame
{
    unsigned int len;                      /* Received length (after pre-processing) */
    int8_t d[COBS_MAX_PACKET_LEN];         /* ...the data itself */
};

struct COBSDecoder
{
    enum COBSPumpState s;                  /* Current state of TPIU decoder */
    uint8_t intervalCount;                 /* Current byte number in reception */
    bool maxCount;                         /* Is this interval maxed out? (So don't expect a SYNC at the end) */
    uint8_t currentStream;                 /* Currently selected stream */
    uint32_t syncMonitor;                  /* State of sync reception ... in case we loose sync */
    struct Frame f;                        /* Receive frame currently under construction */
    bool selfAllocated;                    /* Flag indicating that memory was allocated by the library */

    struct COBSDecoderStats stats;         /* Record of decoder stats */
};

// ====================================================================================================
void COBSDecoderForceSync( struct COBSDecoder *t );
void COBSDecoderZeroStats( struct COBSDecoder *t );
bool COBSDecoderSynced( struct COBSDecoder *t );
struct COBSDecoderStats *COBSDecoderGetStats( struct COBSDecoder *t );

void COBSPump( struct COBSDecoder *t, uint8_t *incoming, int len,
               void ( *packetRxed )( enum COBSPumpEvent e, struct Frame *p, void *param ),
               void *param );

void COBSDecoderDelete( struct COBSDecoder *t );
struct COBSDecoder *COBSDecoderInit( struct COBSDecoder *t );
// ====================================================================================================
#ifdef __cplusplus
}
#endif
#endif
