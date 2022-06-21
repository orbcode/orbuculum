/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * TRACE Decoder Module
 * ====================
 *
 */

#ifndef _TRACE_DECODER_
#define _TRACE_DECODER_

#include <stdbool.h>
#include <stdint.h>
#include "generics.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Protocol format */
enum TRACEprotocol
{
    TRACE_PROT_LIST_START = 0,
    TRACE_PROT_ETM35      = TRACE_PROT_LIST_START,
    TRACE_PROT_MTB,
    TRACE_PROT_LIST_END,
    TRACE_PROT_NONE       = TRACE_PROT_LIST_END
};

#define TRACEProtocolStringDEF "ETM3.5", "MTB", NULL
extern const char *TRACEprotocolString[];

enum TRACEchanges
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
    EV_CH_LINEAR,
    EV_CH_TRACESTART,
    EV_CH_TRACESTOP,
    EV_CH_NUM_CHANGES
};

// ============================================================================
// Messages out of the decoder
// ============================================================================
enum TRACEDecoderMsgType { TRACEDEC_MSG_NONE, TRACE_BRANCH, TRACEDEC_MSG_NUM_MSGS };
enum Mode { TRACE_ADDRMODE_THUMB, TRACE_ADDRMODE_ARM, TRACE_ADDRMODE_JAZELLE };
enum Reason { TRACE_REASON_PERIODIC, TRACE_REASON_TRACEON, TRACE_REASON_TRACEOVF, TRACE_REASON_EXITDBG };

/* TRACE Decoder statistics */
struct TRACEDecoderStats

{
    uint32_t lostSyncCount;              /* Number of times sync has been lost */
    uint32_t syncCount;                  /* Number of times a sync event has been received */
};

struct TRACECPUState
{
    uint32_t changeRecord;               /* Record of what changed since last report */

    // Gross processor state...
    uint64_t ts;                         /* Latest timestamp */
    uint32_t addr;                       /* Latest fully computed address */
    uint32_t toAddr;                     /* Address to run to in linear mode (MTB) */
    uint32_t nextAddr;                   /* Next address to run from in linear mode (MTB) */
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

/* Internal states of the protocol machine */
enum TRACEprotoState
{
    TRACE_UNSYNCED,
    TRACE_WAIT_ISYNC,
    TRACE_IDLE,
    TRACE_COLLECT_BA_STD_FORMAT,
    TRACE_COLLECT_BA_ALT_FORMAT,
    TRACE_COLLECT_EXCEPTION,
    TRACE_GET_CONTEXTBYTE,
    TRACE_GET_INFOBYTE,
    TRACE_GET_IADDRESS,
    TRACE_GET_ICYCLECOUNT,
    TRACE_GET_CYCLECOUNT,
    TRACE_GET_VMID,
    TRACE_GET_TSTAMP,
    TRACE_GET_CONTEXTID
};

/* Textual form of the above, for debugging */
#define TRACEprotoStateNamesDEF  "UNSYNCED",       "WAIT_ISYNC",        "IDLE",             "COLLECT_BA_STD",  \
    "COLLECT_BA_ALT", "COLLECT_EXCEPTION", "WAIT_CONTEXTBYTE", "WAIT_INFOBYTE",   \
    "WAIT_IADDRESS",  "WAIT_ICYCLECOUNT",  "WAIT_CYCLECOUNT",  "GET_VMID",        \
    "GET_TSTAMP",     "GET_CONTEXTID"

extern const char *protoStateName[];
// ============================================================================
// The TRACE decoder state
// ============================================================================
struct TRACEDecoder

{
    struct TRACEDecoderStats stats;      /* Record of the statistics */
    enum TRACEprotoState p;              /* Current state of the receiver */
    bool rxedISYNC;                      /* Indicator that we're fully synced */

    /* Trace configuration */
    /* ------------------- */
    bool usingAltAddrEncode;             /* Set if the new (TRACE 3.4 onwards) addr formatting is used */
    enum TRACEprotocol protocol;         /* What trace protocol are we using? */
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
    struct TRACECPUState cpu;              /* Current state of the CPU */
};

// ====================================================================================================
typedef void ( *traceDecodeCB )( void *d );

void TRACEDecoderForceSync( struct TRACEDecoder *i, bool isSynced );
void TRACEDecoderZeroStats( struct TRACEDecoder *i );
bool TRACEDecoderIsSynced( struct TRACEDecoder *i );
struct TRACECPUState *TRACECPUState( struct TRACEDecoder *i );
bool TRACEStateChanged( struct TRACEDecoder *i, enum TRACEchanges c );
struct TRACEDecoderStats *TRACEDecoderGetStats( struct TRACEDecoder *i );
void TRACEDecodeUsingAltAddrEncode( struct TRACEDecoder *i, bool usingAltAddrEncodeSet );

void TRACEDecodeProtocol( struct TRACEDecoder *i, enum TRACEprotocol protocol );

void TRACEDecoderPump( struct TRACEDecoder *i, uint8_t *buf, int len, traceDecodeCB cb, genericsReportCB report, void *d );

void TRACEDecoderInit( struct TRACEDecoder *i, enum TRACEprotocol protocol, bool usingAltAddrEncodeSet );
// ====================================================================================================
#ifdef __cplusplus
}
#endif
#endif
