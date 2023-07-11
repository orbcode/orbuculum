/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * TRACE Decoder Module
 * ====================
 *
 * Implementation of MTB decode.
 */

#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include "msgDecoder.h"
#include "traceDecoder.h"
#include "generics.h"

/* Internal states of the protocol machine */
enum TRACE_MTBprotoState
{
    TRACE_UNSYNCED,
    TRACE_IDLE
};

struct MTBDecodeState
{
    struct TRACEDecoderEngine e; /* Must be first to allow object method access */
    enum TRACE_MTBprotoState p;  /* Current state of the receiver */
};

#define REPORT(...) { if ( cpu->report ) cpu->report( V_DEBUG, __VA_ARGS__); }

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Externally available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================

static void _stateChange( struct TRACECPUState *cpu, enum TRACEchanges c )
{
    cpu->changeRecord |= ( 1 << c );
}

// ====================================================================================================
static bool _pumpActionPair( struct TRACEDecoderEngine *e, struct TRACECPUState *cpu, uint32_t source, uint32_t dest )

/* Pump next words through the protocol decoder */

{
    struct MTBDecodeState *j = ( struct MTBDecodeState * )e;

    enum TRACE_MTBprotoState newState = j->p;
    enum TRACEDecoderPumpEvent retVal = TRACE_EV_NONE;

    REPORT( "[From 0x%08x to 0x%08x]" EOL, source, dest );

    switch ( j->p )
    {
        // -----------------------------------------------------

        case TRACE_UNSYNCED:
            /* For the first instruction we only have the destination */
            /* but we code the exception indication into here so we know we arrived via an exception */
            cpu->nextAddr = ( dest & 0xFFFFFFFE ) | ( source & 1 );

            /* If the low bit of dest was set then this is a start of trace event */
            if ( dest & 1 )
            {
                _stateChange( cpu, EV_CH_TRACESTART );
            }

            newState = TRACE_IDLE;
            break;

        // -----------------------------------------------------

        case TRACE_IDLE:
            if ( cpu->nextAddr & 1 )
            {
                /* If low bit of nextAddr is set then we got here via an exception */
                _stateChange( cpu, EV_CH_EX_ENTRY );
            }

            /* If low bit of dest is set then this is a start of trace */
            if ( dest & 1 )
            {
                _stateChange( cpu, EV_CH_TRACESTART );
            }

            cpu->addr = cpu->nextAddr & 0xFFFFFFFE;
            cpu->nextAddr = ( dest & 0xFFFFFFFE ) | ( source & 1 );
            cpu->toAddr = source & 0xFFFFFFFE;
            cpu->exception = 0; /* We don't known exception cause on a M0 */
            _stateChange( cpu, EV_CH_ADDRESS );
            _stateChange( cpu, EV_CH_LINEAR );
            retVal = TRACE_EV_MSG_RXED;
            break;

        // -----------------------------------------------------

        default:
            assert( false );
            break;

            // -----------------------------------------------------

    }


    j->p = newState;
    return ( retVal != TRACE_EV_NONE );
}
// ====================================================================================================

static void _pumpDestroy( struct TRACEDecoderEngine *e )

{
    assert( e );
    free( e );
}

// ====================================================================================================

static bool _synced( struct TRACEDecoderEngine *e )

{
    assert( e );
    return ( ( struct MTBDecodeState * )e )->p != TRACE_UNSYNCED;
}

// ====================================================================================================
static void _forceSync(  struct TRACEDecoderEngine *e, bool isSynced )

{
    ( ( struct MTBDecodeState * )e )->p = ( isSynced ) ? TRACE_IDLE : TRACE_UNSYNCED;
}

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Publicly accessible methods
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================

struct TRACEDecoderEngine *MTBDecoderPumpCreate( void )

{

    struct TRACEDecoderEngine *e = ( struct TRACEDecoderEngine * )calloc( 1, sizeof( struct MTBDecodeState ) );
    e->actionPair    = _pumpActionPair;
    e->destroy       = _pumpDestroy;
    e->synced        = _synced;
    e->forceSync     = _forceSync;
    return e;
}

// ====================================================================================================
