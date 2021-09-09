/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Msg Sequencer Module
 * ====================
 *
 * Sequencer for re-ordering decoded messages from the ITM according to timestamp
 * information in the flow.
 *
 * from https://static.docs.arm.com/ddi0403/e/DDI0403E_B_armv7m_arm.pdf
 */

#ifndef _MSGSEQ_H_
#define _MSGSEQ_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef DEBUG
    #include "generics.h"
#else
    #define genericsReport(x...)
#endif

#include "itmDecoder.h"
#include "msgDecoder.h"

#ifdef __cplusplus
extern "C" {
#endif

struct MsgSeq

{
    struct ITMDecoder *i;

    uint32_t wp;
    uint32_t rp;
    uint32_t pbl;
    struct msg *pbuffer;
    bool releaseTimeMsg;
};

// ====================================================================================================

void MsgSeqInit( struct MsgSeq *d, struct ITMDecoder *i, uint32_t maxEntries );
struct msg *MsgSeqGetPacket( struct MsgSeq *d );

bool MsgSeqPump( struct MsgSeq *d, uint8_t c );

// ====================================================================================================
#ifdef __cplusplus
}
#endif
#endif
