/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * ITM Decoder Module
 * ==================
 *
 */

#ifndef _ETM_DECODER_
#define _ETM_DECODER_

#include <stdbool.h>
#include <stdint.h>
#include "generics.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Internal states of the protocol machine */
enum ETMprotoState
{
    ETM_UNSYNCED,
    ETM_WAIT_ISYNC,
    ETM_IDLE,
    ETM_COLLECT_BA_STD_FORMAT,
    ETM_COLLECT_BA_ALT_FORMAT,
    ETM_COLLECT_EXCEPTION,
    ETM_GET_CONTEXTBYTE,
    ETM_GET_INFOBYTE,
    ETM_GET_IADDRESS,
    ETM_GET_ICYCLECOUNT,
    ETM_GET_CYCLECOUNT,
    ETM_GET_VMID,
    ETM_GET_TSTAMP,
    ETM_GET_CONTEXTID,
};

/* Textual form of the above, for debugging */
#define ETM_PROTO_NAME_LIST "UNSYNCED", "WAIT_ISYNC", "IDLE", "COLLECT_BA_STD", "COLLECT_BA_ALT", \
    "COLLECT_EXCEPTION",  "WAIT_CONTEXTBYTE", "WAIT_INFOBYTE", "WAIT_IADDRESS", \
    "WAIT_ICYCLECOUNT", "WAIT_CYCLECOUNT", "GET_VMID", "GET_TSTAMP", "GET_CONTEXTID"

enum ETMchanges
{
    EV_CH_EX_ENTRY,
    EV_CH_EX_EXIT,
    EV_CH_CLOCKSPEED,
    EV_CH_ENATOMS,
    EV_CH_WATOMS,
    EV_CH_ADDRESS,
    EV_CH_EXCEPTION,
    EV_CH_CANCELLED,
    EV_CH_VMID,
    EV_CH_TSTAMP,
    EV_CH_CYCLECOUNT,
    EV_CH_CONTEXTID,
    EV_CH_TRIGGER,
    EV_CH_SECURE,
    EV_CH_ALTISA,
    EV_CH_HYP,
    EV_CH_RESUME,
    EV_CH_REASON,
    EV_CH_JAZELLE,
    EV_CH_THUMB,
    EV_CH_ISLSIP,
    EV_CH_NUM_CHANGES
};



// ============================================================================
// Messages out of the decoder
// ============================================================================
enum ETMDecoderMsgType { ETMDEC_MSG_NONE, ETM_BRANCH, ETMDEC_MSG_NUM_MSGS };
enum Mode { ETM_ADDRMODE_ARM, ETM_ADDRMODE_THUMB, ETM_ADDRMODE_JAZELLE };
enum Reason { ETM_REASON_PERIODIC, ETM_REASON_TRACEON, ETM_REASON_TRACEOVF, ETM_REASON_EXITDBG };

/* ETM Decoder statistics */
struct ETMDecoderStats

{
    uint32_t lostSyncCount;              /* Number of times sync has been lost */
    uint32_t syncCount;                  /* Number of times a sync event has been received */
};

struct ETMCPUState
{
    uint32_t changeRecord;               /* Record of what changed since last report */

    // Gross processor state...
    uint64_t ts;                         /* Latest timestamp */
    uint32_t addr;                       /* Latest fully computed address */
    enum Mode addrMode;                  /* What kind of addressing are we using at the moment? */
    uint32_t contextID;                  /* Currently executing context */
    uint8_t vmid;                        /* Current virtual machine ID */
    uint32_t cycleCount;                 /* Cycle Count for exact mode */
    uint16_t exception;                  /* Exception type being executed */
    uint16_t resume;                     /* Interrupt resume code */
    uint64_t instCount;                  /* Number of instructions executed */

    // I-Sync related
    enum Reason reason;                  /* Why this i-sync was generated */
    bool isLSiP;                         /* Flag indicating this is a LSiP startup ISYNC */
    uint8_t numInstructions;             /* Number of dispatched instructions in this call */
    uint8_t watoms;                      /* Number of watoms in this step (exact timing mode only) */
    uint8_t eatoms;                      /* Number of E (executed) atoms in this step */
    uint8_t natoms;                      /* Number of N (non-executed) atoms in this step */
    uint32_t disposition;                /* What happened to condition codes for each instruction? */

    // State flags
    bool jazelle;                        /* Executing jazelle mode instructions */
    bool nonSecure;                      /* CPU is operating in non-secure mode */
    bool altISA;                         /* CPU is using alt ISA */
    bool hyp;                            /* CPU is in hypervisor mode */
    bool thumb;                          /* CPU is in thumb mode */
    bool clockSpeedChanged;              /* CPU Clockspeed changed since last timestamp */
};

// ============================================================================
// The ETM decoder state
// ============================================================================
struct ETMDecoder

{
    struct ETMDecoderStats stats;        /* Record of the statistics */
    enum ETMprotoState p;                /* Current state of the receiver */
    bool rxedISYNC;                      /* Indicator that we're fully synced */

    /* Trace configuration */
    /* ------------------- */
    bool usingAltAddrEncode;             /* Set if the new (ETM 3.4 onwards) addr formatting is used */
    uint8_t contextBytes;                /* How many context bytes we're using */
    bool cycleAccurate;                  /* Using cycle accurate mode */
    bool dataOnlyMode;                   /* If we're only tracing data, not instructions */

    /* Purely internal matters.... */
    /* --------------------------- */
    uint32_t asyncCount;                 /* Count of 0's in preparation for ASYNC recognition */
    uint32_t addrConstruct;              /* Address under construction */
    uint64_t tsConstruct;                /* Timestamp under construction */
    uint32_t contextConstruct;           /* Context under construction */
    uint32_t cycleConstruct;             /* Cycle count under construction */
    uint32_t byteCount;                  /* How many bytes of this packet do we have? */


    /* External resulutions of current CPU state */
    /* ----------------------------------------- */
    struct ETMCPUState cpu;              /* Current state of the CPU */
};

// ====================================================================================================
typedef void ( *etmDecodeCB )( void *d );

void ETMDecoderForceSync( struct ETMDecoder *i, bool isSynced );
void ETMDecoderZeroStats( struct ETMDecoder *i );
bool ETMDecoderIsSynced( struct ETMDecoder *i );

inline struct ETMCPUState *ETMCPUState( struct ETMDecoder *i )
{
    return &i->cpu;
}
inline bool ETMStateChanged( struct ETMDecoder *i, enum ETMchanges c )
{
    bool r = ( i->cpu.changeRecord & ( 1 << c ) ) != 0;
    i->cpu.changeRecord &= ~( 1 << c );
    return r;
}
struct ETMDecoderStats *ETMDecoderGetStats( struct ETMDecoder *i );

void ETMDecodeUsingAltAddrEncode( struct ETMDecoder *i, bool usingAltAddrEncodeSet );

void ETMDecoderPump( struct ETMDecoder *i, uint8_t *buf, int len, etmDecodeCB cb, genericsReportCB report, void *d );

void ETMDecoderInit( struct ETMDecoder *i, bool usingAltAddrEncodeSet );
// ====================================================================================================
#ifdef __cplusplus
}
#endif
#endif
