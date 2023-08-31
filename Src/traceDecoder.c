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

/* Individual trace decoders defined in their own file */
extern struct TRACEDecoderEngine *ETM35DecoderPumpCreate( void );
extern struct TRACEDecoderEngine *MTBDecoderPumpCreate( void );
extern struct TRACEDecoderEngine *ETM4DecoderPumpCreate( void );

static struct TRACEDecoderEngine *( *_engine[TRACE_PROT_NUM] )( void ) = { ETM35DecoderPumpCreate, MTBDecoderPumpCreate, ETM4DecoderPumpCreate };
static const char *TRACEprotocolString[TRACE_PROT_NUM] = { TRACEprotocolStrings };

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Externally available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
const char *TRACEDecodeGetProtocolName( enum TRACEprotocol protocol )

{
    assert( protocol < TRACE_PROT_LIST_END );
    return TRACEprotocolString[ protocol ];
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
    assert( i->engine );
    return i->engine->synced( i->engine );
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
const char *TRACEExceptionName( int exceptionNumber )

{
    return ( ( const char *[] ) {
        "???", "PE Reset", "NMI", "HardFault", "MemManage", "BusFault", "UsageFault", "SecureFault", "???", "???", "???", "SVC", "Debug Monitor", "???", "PendSV", "SysTick", "IRQ"
    } )[( exceptionNumber < 16 ) ? exceptionNumber : 16];
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
    assert( i->engine );

    if ( isSynced ) {
        i->stats.syncCount++;
    } else {
        if ( TRACEDecoderIsSynced( i ) ) {
            i->stats.lostSyncCount++;
        }
    }

    i->engine->forceSync( i->engine, isSynced );
}
// ====================================================================================================
void TRACEDecoderPump( struct TRACEDecoder *i, uint8_t *buf, int len, traceDecodeCB cb, void *d )

{
    assert( i );
    assert( buf );
    assert( cb );

    /* len can arrive as 0 for the case of an unwrapped buffer */

    if ( i->engine->action ) {
        while ( len-- ) {
            if ( i->engine->action(  i->engine, &i->cpu, *( buf++ ) ) ) {
                /* Something worthy of being reported happened */
                cb( d );
            }
        }

    } else if ( i->engine->actionPair ) {
        while ( len > 7 ) {
            /* MTB processes two words at a time...a from and to address */
            /* (yes, that could be +1 on a uint32_t increment, but I prefer being explicit) */
            if ( i->engine->actionPair( i->engine, &i->cpu, *( uint32_t * )buf, *( uint32_t * )( buf + 4 ) ) ) {
                /* Something worthy of being reported happened */
                cb( d );
            }

            buf += 8;
            len -= 8;
        }
    }
}
// ====================================================================================================
void TRACEDecoderInit( struct TRACEDecoder *i, enum TRACEprotocol protocol, bool usingAltAddrEncodeSet, genericsReportCB report )

/* Reset a TRACEDecoder instance */

{
    assert( protocol < TRACE_PROT_NUM );

    memset( i, 0, sizeof( struct TRACEDecoder ) );

    TRACEDecoderZeroStats( i );
    i->cpu.addr = ADDRESS_UNKNOWN;
    i->cpu.cycleCount = COUNT_UNKNOWN;
    i->cpu.report = report;
    i->protocol = protocol;

    i->engine = _engine[ protocol ]();

    if ( i->engine->altAddrEncode ) {
        i->engine->altAddrEncode( i->engine, usingAltAddrEncodeSet );
    }
}
// ====================================================================================================

