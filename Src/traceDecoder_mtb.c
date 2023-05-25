/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * TRACE Decoder Module
 * ====================
 *
 * Implementation of MTB decode.
 */

#include <string.h>
#include <assert.h>
#include "msgDecoder.h"
#include "traceDecoder.h"
#include "generics.h"

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Externally available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
void MTBDecoderPumpAction( struct TRACEDecoder *i, uint32_t source, uint32_t dest, traceDecodeCB cb, genericsReportCB report, void *d )

/* Pump next words through the protocol decoder */

{
    enum TRACEprotoState newState = i->p;
    struct TRACECPUState *cpu = &i->cpu;
    enum TRACEDecoderPumpEvent retVal = TRACE_EV_NONE;

    if ( report )
    {
        report( V_ERROR, "[From 0x%08x to 0x%08x]" EOL, source, dest );
    }

    switch ( i->p )
    {
        // -----------------------------------------------------

        case TRACE_UNSYNCED:
            /* For the first instruction we only have the destination */
            /* but we code the exception indication into here so we know we arrived via an exception */
            cpu->nextAddr = ( dest & 0xFFFFFFFE ) | ( source & 1 );

            /* If the low bit of dest was set then this is a start of trace event */
            if ( dest & 1 )
            {
                TRACEDecodeStateChange( i, EV_CH_TRACESTART );
            }

            newState = TRACE_IDLE;
            break;

        // -----------------------------------------------------

        case TRACE_IDLE:
            if ( cpu->nextAddr & 1 )
            {
                /* If low bit of nextAddr is set then we got here via an exception */
                TRACEDecodeStateChange( i, EV_CH_EX_ENTRY );
            }

            /* If low bit of dest is set then this is a start of trace */
            if ( dest & 1 )
            {
                TRACEDecodeStateChange( i, EV_CH_TRACESTART );
            }

            cpu->addr = cpu->nextAddr & 0xFFFFFFFE;
            cpu->nextAddr = ( dest & 0xFFFFFFFE ) | ( source & 1 );
            cpu->toAddr = source & 0xFFFFFFFE;
            cpu->exception = 0; /* We don't known exception cause on a M0 */
            TRACEDecodeStateChange( i, EV_CH_ADDRESS );
            TRACEDecodeStateChange( i, EV_CH_LINEAR );
            retVal = TRACE_EV_MSG_RXED;
            break;

        // -----------------------------------------------------

        default:
            assert( false );
            break;

            // -----------------------------------------------------

    }

    if ( retVal != TRACE_EV_NONE )
    {
        cb( d );
    }

    i->p = newState;
}
// ====================================================================================================
