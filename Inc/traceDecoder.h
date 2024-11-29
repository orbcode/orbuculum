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
    TRACE_PROT_ETM4,
    TRACE_PROT_LIST_END,
    TRACE_PROT_NUM        = TRACE_PROT_LIST_END,
    TRACE_PROT_NONE       = TRACE_PROT_LIST_END
};

#define TRACEprotocolStrings "ETM3.5", "MTB", "ETM4"


/* Events from the process of pumping bytes through the TRACE decoder */
enum TRACEDecoderPumpEvent
{
    TRACE_EV_NONE,
    TRACE_EV_UNSYNCED,
    TRACE_EV_SYNCED,
    TRACE_EV_ERROR,
    TRACE_EV_MSG_RXED
};

enum TRACEchanges
{
    EV_CH_EX_ENTRY,
    EV_CH_EX_EXIT,
    EV_CH_CLOCKSPEED,
    EV_CH_ENATOMS,
    EV_CH_WATOMS,
    EV_CH_ADDRESS,
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

    EV_CH_DISCARD,
    EV_CH_OVERFLOW,
    EV_CH_FNRETURN,
    EV_CH_EXRETURN,
    EV_CH_DATASYNC,
    EV_CH_UDATASYNC,
    EV_CH_EVENT0,
    EV_CH_EVENT1,
    EV_CH_EVENT2,
    EV_CH_EVENT3,
    EV_CH_NUM_CHANGES
};

/* Flag for unknown/illegal cycle count */
#define COUNT_UNKNOWN 0xffffffffffffffffL
#define ADDRESS_UNKNOWN ((symbolMemaddr)-1)

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
    symbolMemaddr addr;                  /* Latest fully computed address */
    symbolMemaddr toAddr;                /* Address to run to in linear mode (MTB) */
    symbolMemaddr nextAddr;              /* Next address to run from in linear mode (MTB) */
    enum Mode addrMode;                  /* What kind of addressing are we using at the moment? */
    uint32_t contextID;                  /* Currently executing context */
    uint8_t vmid;                        /* Current virtual machine ID */
    uint64_t cycleCount;                 /* Cycle Count for exact mode */
    uint16_t exception;                  /* Exception type being executed */
    uint16_t resume;                     /* Interrupt resume code */
    bool serious;                        /* True if there is a serious fault pending */
    uint64_t instCount;                  /* Number of instructions executed */
    uint8_t exceptionLevel;              /* Current exception level */
    bool am64bit;                        /* Running in 64 bit mode */
    bool amSecure;                       /* Running in secure mode */

    // I-Sync related
    enum Reason reason;                  /* Why this i-sync was generated */
    bool isLSiP;                         /* Flag indicating this is a LSiP startup ISYNC */
    uint8_t numInstructions;             /* Number of dispatched instructions in this call */
    uint8_t watoms;                      /* Number of watoms in this step (exact timing mode only) */
    uint8_t eatoms;                      /* Number of E (executed) atoms in this step */
    uint8_t natoms;                      /* Number of N (non-executed) atoms in this step */
    uint32_t disposition;                /* What happened to condition codes for each instruction? */

    // D-Sync related
    uint8_t dsync_mark;                  /* Co-ordination mark in data flow */
    uint8_t udsync_mark;                 /* Co-ordination mark for unnumbered sync in data flow */

    // State flags
    bool jazelle;                        /* Executing jazelle mode instructions */
    bool nonSecure;                      /* CPU is operating in non-secure mode */
    bool altISA;                         /* CPU is using alt ISA */
    bool hyp;                            /* CPU is in hypervisor mode */
    bool thumb;                          /* CPU is in thumb mode */
    bool clockSpeedChanged;              /* CPU Clockspeed changed since last timestamp */

    // Convinience, for debug reporting
    genericsReportCB report;

    // Debugging
    uint64_t overflows;
};

// ============================================================================
// The TRACE decoder state
// ============================================================================

typedef void ( *traceDecodeCB )( void *d );

struct TRACEDecoder;

struct TRACEDecoderEngine
{
    bool ( *action )        ( struct TRACEDecoderEngine *e, struct TRACECPUState *cpu, uint8_t c  );
    bool ( *actionPair )    ( struct TRACEDecoderEngine *e, struct TRACECPUState *cpu, uint32_t source, uint32_t dest );
    void ( *destroy )       ( struct TRACEDecoderEngine *e );
    bool ( *synced )        ( struct TRACEDecoderEngine *e );
    void ( *forceSync )     ( struct TRACEDecoderEngine *e, bool isSynced );
    const char ( *name )    ( void );

    /* Config specific to ETM3.5 */
    void ( *altAddrEncode ) ( struct TRACEDecoderEngine *e, bool using );
};

struct TRACEDecoder

{
    struct TRACEDecoderStats stats;    /* Record of the statistics */
    struct TRACECPUState cpu;          /* Current state of the CPU */

    enum TRACEprotocol protocol;       /* What trace protocol are we using? */

    struct TRACEDecoderEngine *engine; /* The actual engine for the decode, including internal state */

};

// ====================================================================================================

void TRACEDecoderForceSync( struct TRACEDecoder *i, bool isSynced );
bool TRACEDecoderIsSynced( struct TRACEDecoder *i );

void TRACEDecoderZeroStats( struct TRACEDecoder *i );
struct TRACEDecoderStats *TRACEDecoderGetStats( struct TRACEDecoder *i );

struct TRACECPUState *TRACECPUState( struct TRACEDecoder *i );
bool TRACEStateChanged( struct TRACEDecoder *i, enum TRACEchanges c );

const char *TRACEExceptionName( int exceptionNumber );

const char *TRACEDecodeGetProtocolName( enum TRACEprotocol protocol );

void TRACEDecoderPump( struct TRACEDecoder *i, uint8_t *buf, int len, traceDecodeCB cb, void *d );

void TRACEDecoderInit( struct TRACEDecoder *i, enum TRACEprotocol protocol, bool usingAltAddrEncodeSet, genericsReportCB report );
// ====================================================================================================
#ifdef __cplusplus
}
#endif
#endif
