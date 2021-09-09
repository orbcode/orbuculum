/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Message Sequencer Module
 * ========================
 *
 * Sequencer for re-ordering messages from the ITM according to prioritize
 * timestamp information in the flow.
 *
 * Spec at https://static.docs.arm.com/ddi0403/e/DDI0403E_B_armv7m_arm.pdf
 */

#ifndef _ITMSEQ_H_
#define _ITMSEQ_H_

#include <stdbool.h>
#include <stdint.h>
#include "generics.h"

#include "itmDecoder.h"
#include "msgDecoder.h"

#ifdef __cplusplus
extern "C" {
#endif

struct MSGSeq

{
    struct ITMDecoder *i;

    uint32_t wp;             /* Write pointer */
    uint32_t rp;             /* Read pointer */
    uint32_t pbl;            /* Buffer length */
    bool releaseTimeMsg;     /* Indicator to release msg at head of queue */

    struct msg *pbuffer;     /* The buffer */
};

// ====================================================================================================

void MSGSeqInit( struct MSGSeq *d, struct ITMDecoder *i, uint32_t maxEntries );
struct msg *MSGSeqGetPacket( struct MSGSeq *d );

bool MSGSeqPump( struct MSGSeq *d, uint8_t c );

// ====================================================================================================
#ifdef __cplusplus
}
#endif
#endif
