/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * TRACE Decoder Module
 * ====================
 *
 * Implementation of ITM/DWT decode according to the specification in Appendix D4
 * of the ARMv7-M Architecture Refrence Manual document available
 * from https://static.docs.arm.com/ddi0403/e/DDI0403E_B_armv7m_arm.pdf
 */

#include <string.h>
#include <assert.h>
#include "msgDecoder.h"
#include "traceDecoder.h"
#include "generics.h"

/* Individual trace decoders */
extern void ETM35DecoderPumpAction( struct TRACEDecoder *i, uint8_t c, traceDecodeCB cb, genericsReportCB report, void *d );
extern void MTBDecoderPumpAction( struct TRACEDecoder *i, uint32_t source, uint32_t dest, traceDecodeCB cb, genericsReportCB report, void *d );
extern void ETM4DecoderPumpAction( struct TRACEDecoder *i, uint8_t c, traceDecodeCB cb, genericsReportCB report, void *d );

static const char *TRACEprotocolString[] = { TRACEprotocolStrings, NULL };

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Externally available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
void TRACEDecoderInit( struct TRACEDecoder *i, enum TRACEprotocol protocol, bool usingAltAddrEncodeSet )

/* Reset a TRACEDecoder instance */

{
    memset( i, 0, sizeof( struct TRACEDecoder ) );
    TRACEDecoderZeroStats( i );
    TRACEDecodeUsingAltAddrEncode( i, usingAltAddrEncodeSet );
    TRACEDecodeProtocol( i, protocol );
}
// ====================================================================================================
void TRACEDecodeStateChange( struct TRACEDecoder *i, enum TRACEchanges c )
{
    i->cpu.changeRecord |= ( 1 << c );
}
// ====================================================================================================
void TRACEDecodeProtocol( struct TRACEDecoder *i, enum TRACEprotocol protocol )

{
    assert( protocol < TRACE_PROT_LIST_END );
    assert( i );
    i->protocol = protocol;
}
// ====================================================================================================
const char *TRACEDecodeProtocolName( enum TRACEprotocol protocol )

{
    assert( protocol < TRACE_PROT_LIST_END );
    return TRACEprotocolString[ protocol ];
}
// ====================================================================================================
void TRACEDecodeUsingAltAddrEncode( struct TRACEDecoder *i, bool usingAltAddrEncodeSet )

{
    assert( i );
    i->usingAltAddrEncode = usingAltAddrEncodeSet;
}
// ====================================================================================================
void TRACEDecoderZeroStats( struct TRACEDecoder *i )

{
    assert( i );
    memset( &i->stats, 0, sizeof( struct TRACEDecoderStats ) );
}
// ====================================================================================================
bool TRACEDecoderIsSynced( struct TRACEDecoder *i )

{
    assert( i );
    return i->p != TRACE_UNSYNCED;
}
// ====================================================================================================
struct TRACEDecoderStats *TRACEDecoderGetStats( struct TRACEDecoder *i )
{
    assert( i );
    return &i->stats;
}
// ====================================================================================================
struct TRACECPUState *TRACECPUState( struct TRACEDecoder *i )
{
    return &i->cpu;
}
// ====================================================================================================
bool TRACEStateChanged( struct TRACEDecoder *i, enum TRACEchanges c )
{
    bool r = ( i->cpu.changeRecord & ( 1 << c ) ) != 0;
    i->cpu.changeRecord &= ~( 1 << c );
    return r;
}
// ====================================================================================================
void TRACEDecoderForceSync( struct TRACEDecoder *i, bool isSynced )

/* Force the decoder into a specific sync state */

{
    assert( i );

    if ( i->p == TRACE_UNSYNCED )
    {
        if ( isSynced )
        {
            i->p = TRACE_IDLE;
            i->stats.syncCount++;
        }
    }
    else
    {
        if ( !isSynced )
        {
            i->stats.lostSyncCount++;
            i->asyncCount = 0;
            i->rxedISYNC = false;
            i->p = TRACE_UNSYNCED;
        }
    }
}
// ====================================================================================================
void TRACEDecoderPump( struct TRACEDecoder *i, uint8_t *buf, int len, traceDecodeCB cb, genericsReportCB report, void *d )

{
    assert( i );
    assert( buf );
    assert( cb );

    /* len can arrive as 0 for the case of an unwrapped buffer */

    switch ( i->protocol )
    {
        case TRACE_PROT_ETM35:
            while ( len-- )
            {
                /* ETM processes one octet at a time */
                ETM35DecoderPumpAction( i, *( buf++ ), cb, report, d );
            }

            break;

        case TRACE_PROT_ETM4:
            while ( len-- )
            {
                /* ETM processes one octet at a time */
                ETM4DecoderPumpAction( i, *( buf++ ), cb, report, d );
            }

            break;

        case TRACE_PROT_MTB:
            while ( len > 7 )
            {
                /* MTB processes two words at a time...a from and to address */
                /* (yes, that could be +1 on a uint32_t increment, but I prefer being explicit) */
                MTBDecoderPumpAction( i, *( uint32_t * )buf, *( uint32_t * )( buf + 4 ), cb, report, d );
                buf += 8;
                len -= 8;
            }

            break;

        default:
            assert( false );
            break;
    }
}
// ====================================================================================================
