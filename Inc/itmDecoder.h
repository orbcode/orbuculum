/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * ITM Decoder Module
 * ==================
 *
 */

#ifndef _ITM_DECODER_
#define _ITM_DECODER_

#include <stdbool.h>
#include <stdint.h>

#define ITM_MAX_PACKET  (14) // This length can only happen for a timestamp or some SYNC packets
#define ITM_DATA_PACKET (4)  // This is the maximum length of everything else

#ifdef __cplusplus
extern "C" {
#endif

/* Hardware event numbers (used for the event fifo) */
enum hwEvents
{
    HWEVENT_TS,
    HWEVENT_EXCEPTION,
    HWEVENT_PCSample,
    HWEVENT_DWT,
    HWEVENT_RWWT,
    HWEVENT_AWP,
    HWEVENT_OFS,
    HWEVENT_UNUSED,
    HWEVENT_NISYNC
};

/* Exception events */
enum ExceptionEvents
{
    EXEVENT_UNKNOWN,
    EXEVENT_ENTER,
    EXEVENT_EXIT,
    EXEVENT_RESUME
};

/* The different packet types that can be identified */
enum ITMPacketType

{
    ITM_PT_NONE,
    ITM_PT_TS,
    ITM_PT_SW,
    ITM_PT_HW,
    ITM_PT_XTN,
    ITM_PT_RSRVD,
    ITM_PT_NISYNC
};

/* Events from the process of pumping bytes through the ITM decoder */
enum ITMPumpEvent
{
    ITM_EV_NONE,
    ITM_EV_PACKET_RXED,
    ITM_EV_UNSYNCED,
    ITM_EV_SYNCED,
    ITM_EV_OVERFLOW,
    ITM_EV_ERROR
};

/* Internal states of the protocol machine */
enum _protoState
{
    ITM_UNSYNCED,
    ITM_IDLE,
    ITM_TS,
    ITM_SW,
    ITM_HW,
    ITM_GTS1,
    ITM_GTS2,
    ITM_RSVD,
    ITM_XTN,
    ITM_NISYNC
};
/* Textual form of the above, for debugging */
#define PROTO_NAME_LIST "UNSYNCED", "IDLE", "TS", "SW", "HW", "GTS1", "GTS2", "RSVD", "XTN", "ISYNC"

/* Type of the packet received over the link */
struct ITMPacket

{
    enum ITMPacketType type;
    uint8_t srcAddr;

    uint8_t len;
    uint8_t pageRegister; /* The current stimulus page register value */
    uint8_t d[ITM_MAX_PACKET];
};

/* Time conditions of a TS message */
enum timeDelay {TIME_CURRENT, TIME_DELAYED, EVENT_DELAYED, EVENT_AND_TIME_DELAYED};

/* ITM Decoder statistics */
struct ITMDecoderStats

{
    uint32_t lostSyncCount;              /* Number of times sync has been lost */
    uint32_t syncCount;                  /* Number of times a sync event has been received */
    uint32_t tpiuSyncCount;              /* Number of times a tpiu sync event has been received (shouldn't happen) */
    uint32_t overflow;                   /* Number of times an overflow occured */
    uint32_t SWPkt;                      /* Number of SW Packets received */
    uint32_t TSPkt;                      /* Number of TS Packets received */
    uint32_t HWPkt;                      /* Number of HW Packets received */
    uint32_t XTNPkt;                     /* Number of XTN Packets received */
    uint32_t ReservedPkt;                /* Number of Reserved Packets received */
    uint32_t ErrorPkt;                   /* Number of Packets received we don't know how to handle */
    uint32_t PagePkt;                    /* Number of Packets received containing page sets */
};

/* The ITM decoder state */
struct ITMDecoder

{
    uint8_t contextIDlen;                /* Number of octets in a contextID (zero for no contextID) */
    int targetCount;                     /* Number of bytes to be collected */
    uint64_t syncStat;                   /* Sync monitor status */
    bool selfAllocated;                  /* Flag indicating that memory was allocated by the library */

    struct ITMPacket pk;                 /* Packet under construction */
    struct ITMDecoderStats stats;        /* Record of the statistics */
    enum _protoState p;                  /* Current state of the receiver */
};

/* A fully decoded message (expanded in msgDecoder.h) */
struct msg;

// ====================================================================================================
void ITMDecoderForceSync( struct ITMDecoder *i, bool isSynced );
void ITMDecoderZeroStats( struct ITMDecoder *i );
bool ITMDecoderIsSynced( struct ITMDecoder *i );
struct ITMDecoderStats *ITMDecoderGetStats( struct ITMDecoder *i );
bool ITMGetPacket( struct ITMDecoder *i, struct ITMPacket *p );
bool ITMGetDecodedPacket( struct ITMDecoder *i, struct msg *decoded );

enum ITMPumpEvent ITMPump( struct ITMDecoder *i, uint8_t c );

struct ITMDecoder *ITMDecoderCreate( void );
void ITMDecoderInit( struct ITMDecoder *i, bool startSynced );
// ====================================================================================================
#ifdef __cplusplus
}
#endif
#endif
