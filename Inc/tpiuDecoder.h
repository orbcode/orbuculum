/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * TPIU Decoder Module
 * ===================
 *
 */

#ifndef _TPIU_DECODER_
#define _TPIU_DECODER_

#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

enum TPIUPumpEvent
{
    TPIU_EV_NONE,
    TPIU_EV_UNSYNCED,
    TPIU_EV_SYNCED,
    TPIU_EV_NEWSYNC,
    TPIU_EV_RXING,
    TPIU_EV_RXEDPACKET,
    TPIU_EV_ERROR
};

enum TPIUPumpState
{
    TPIU_UNSYNCED,
    TPIU_SYNCED,
    TPIU_RXING,
    TPIU_ERROR
};

#define TPIU_PACKET_LEN (16)

struct TPIUCommsStats

{
    uint16_t pendingCount;                   /* Number of frames pending at the start of this stat report */
    uint8_t leds;                            /* LED status bitfield */
    uint16_t lostFrames;                     /* Number of frames lost to overflow */
    uint32_t totalFrames;                    /* Total frames received */
};

struct TPIUDecoderStats
{
    uint32_t lostSync;                     /* Number of times sync has been lost */
    uint32_t syncCount;                    /* Number of times a sync event has been received */
    uint32_t halfSyncCount;                /* Number of times a half sync event has been received */
    uint32_t packets;                      /* Number of packets received */
    uint32_t error;                        /* Number of times an error has been received */
};

struct TPIUDecoder
{
    enum TPIUPumpState state;              /* Current state of TPIU decoder */
    uint8_t byteCount;                     /* Current byte number in reception */
    uint8_t currentStream;                 /* Currently selected stream */
    uint32_t syncMonitor;                  /* State of sync reception ... in case we loose sync */
    struct timeval lastPacket;             /* Timestamp for last packet arrival */
    bool got_lowbits;                      /* Indicator that we've already got the low bits */
    uint8_t rxedPacket[TPIU_PACKET_LEN];   /* Packet currently under construction */

    struct TPIUDecoderStats stats;         /* Record of decoder stats */
    struct TPIUCommsStats commsStats;      /* Record of Comms stats */
};

struct TPIUPacket
{
    uint8_t len;                           /* Received length (after pre-processing) */
    struct
    {
        int8_t s;                          /* Stream to which this byte relates */
        int8_t d;                          /* ...the byte itself */
    } packet[TPIU_PACKET_LEN];
};

// ====================================================================================================
/* LEGACY */ bool TPIUGetPacket( struct TPIUDecoder *t, struct TPIUPacket *p );
/* LEGACY */ enum TPIUPumpEvent TPIUPump( struct TPIUDecoder *t, uint8_t d );

void TPIUDecoderForceSync( struct TPIUDecoder *t, uint8_t offset );
void TPIUDecoderZeroStats( struct TPIUDecoder *t );
bool TPIUDecoderSynced( struct TPIUDecoder *t );
struct TPIUDecoderStats *TPIUDecoderGetStats( struct TPIUDecoder *t );
struct TPIUCommsStats *TPIUGetCommsStats( struct TPIUDecoder *t );

void TPIUPump2( struct TPIUDecoder *t, uint8_t *frame, int len,
                void ( *packetRxed )( enum TPIUPumpEvent e, struct TPIUPacket *p, void *param ),
                void *param );
void TPIUDecoderInit( struct TPIUDecoder *t );
// ====================================================================================================
#ifdef __cplusplus
}
#endif
#endif
