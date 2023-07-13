/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * ETM4 TRACE Decoder Module
 * =========================
 *
 * Implementation of ETM4 decode according to the specification in
 * the Embedded Trace Macrocell Architecture Specification ETMv4.0 to ETMv4.6
 * ARM IHI0064H.a (ID120820)
 */

#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "msgDecoder.h"
#include "traceDecoder.h"
#include "generics.h"


/* Internal states of the protocol machine */
enum TRACE_ETM4protoState
{
    TRACE_UNSYNCED,
    TRACE_IDLE,
    TRACE_GET_CYCLECOUNT,
    TRACE_WAIT_INFO,
    TRACE_GET_INFO_PLCTL,
    TRACE_GET_INFO_INFO,
    TRACE_GET_INFO_KEY,
    TRACE_GET_INFO_SPEC,
    TRACE_GET_INFO_CYCT,
    TRACE_EXTENSION,
    TRACE_GET_TIMESTAMP,
    TRACE_GET_TS_CC,
    TRACE_COMMIT,
    TRACE_GET_SHORT_ADDR,
    TRACE_GET_32BIT_ADDR,
    TRACE_GET_64BIT_ADDR,
    TRACE_GET_CONTEXT,
    TRACE_GET_VCONTEXT,
    TRACE_GET_CONTEXT_ID,
    TRACE_GET_EXCEPTIONINFO1,
    TRACE_GET_EXCEPTIONINFO2
};

static const char *_protoStateName[] =
{
    "UNSYNCED",
    "IDLE",
    "GET_CYCLECOUNT",
    "WAIT_INFO",
    "GET_INFO_PLCTL",
    "GET_INFO_INFO",
    "GET_INFO_KEY",
    "GET_INFO_SPEC",
    "GET_INFO_CYCT",
    "EXTENSION",
    "GET_TIMESTAMP",
    "GET_TS_CC",
    "COMMIT",
    "GET_SHORT_ADDR",
    "GET_32BIT_ADDR",
    "GET_64BIT_ADDR",
    "GET_CONTEXT",
    "GET_VCONTEXT",
    "GET_CONTEXT_ID",
    "GET_EXCEPTIONINFO1",
    "GET_EXCEPTIONINFO1"
};

#define COND_LOAD_TRACED  1
#define COND_STORE_TRACED 2
#define COND_ALL_TRACED   7

/* Word-aligned ARM, Halfword Aligned Thumb, Table 6-19, Pg 6-292 */
enum InstSet { IS0, IS1 };

struct ETM4DecodeState
{
    struct TRACEDecoderEngine e; /* Must be first to allow object method access */
    enum TRACE_ETM4protoState p; /* Current state of the receiver */

    uint32_t asyncCount;         /* Count of 0's in preparation for ASYNC recognition */
    bool rxedISYNC;              /* Indicator that we're fully synced */

    uint8_t plctl;               /* Payload control - what sections are present in the INFO */
    bool cc_enabled;             /* Indicates cycle counting is enabled */
    uint8_t cond_enabled;        /* What conditional branching and loads/stores are traced */
    bool load_traced;            /* Load instructions are traced explicitly */
    bool store_traced;           /* Store instructions are traced explicitly */
    bool haveContext;            /* We have context to collect */

    uint32_t context;            /* Current machine context */
    uint32_t vcontext;           /* Virtual machine context */

    uint32_t nextrhkey;          /* Next rh key expected in the stream */
    uint32_t spec;               /* Max speculation depth to be expected */
    uint32_t cyct;               /* Cycnt threshold */

    uint8_t ex0;                 /* First info byte for exception */

    bool cc_follows;             /* Indication if CC follows from TS */
    uint8_t idx;                 /* General counter used for multi-byte payload indexing */
    uint32_t cntUpdate;          /* Count construction for TS packets */
    struct
    {
        uint64_t addr;
        enum InstSet inst;
    } q[3];                      /* Address queue for pushed addresses */
};

#define DEBUG(...) { if ( cpu->report ) cpu->report( V_DEBUG, __VA_ARGS__); }

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Internal routines - decoder support
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================

static void _stateChange( struct TRACECPUState *cpu, enum TRACEchanges c )
{
    cpu->changeRecord |= ( 1 << c );
}

// ====================================================================================================

static void _reportInfo( struct TRACECPUState *cpu, struct ETM4DecodeState *j )

{
    DEBUG( EOL "Cycle counting is %senabled" EOL, j->cc_enabled ? "" : "not " );
    DEBUG( "Conditional loads are %straced" EOL, ( j->cond_enabled & COND_LOAD_TRACED ) ? "" : "not " );
    DEBUG( "Conditional stores are %straced" EOL, ( j->cond_enabled & COND_STORE_TRACED ) ? "" : "not " );
    DEBUG( "All conditionals are %straced" EOL, ( j->cond_enabled == COND_ALL_TRACED ) ? "" : "not " );
    DEBUG( "Next RH key is %d" EOL, j->nextrhkey );
    DEBUG( "Max speculative execution depth is %d instructions" EOL, j->spec );
    DEBUG( "CYCNT threshold value is %d" EOL, j->cyct );
}

// ====================================================================================================

static void _flushQ( struct ETM4DecodeState *j )

{
    j->q[2].addr = j->q[1].addr = j->q[0].addr = 0;
    j->q[2].inst = j->q[1].inst = j->q[0].inst = IS0;
}

// ====================================================================================================

static void _stackQ( struct ETM4DecodeState *j )
{
    j->q[2].addr = j->q[1].addr;
    j->q[1].addr = j->q[0].addr;
    j->q[2].inst = j->q[1].inst;
    j->q[1].inst = j->q[0].inst;
}

// ====================================================================================================

static bool _pumpAction( struct TRACEDecoderEngine *e, struct TRACECPUState *cpu, uint8_t c )

/* Pump next byte into the protocol decoder */

{

    enum TRACEDecoderPumpEvent retVal = TRACE_EV_NONE;


    struct ETM4DecodeState *j = ( struct ETM4DecodeState * )e;
    enum TRACE_ETM4protoState newState = j->p;

    assert( j );

    /* Perform A-Sync accumulation check ( Section 6.4.2 ) */
    if ( ( j->asyncCount == 11 ) && ( c == 0x80 ) )
    {
        DEBUG( "A-Sync Accumulation complete" EOL );
        j->rxedISYNC = true;
        newState = TRACE_WAIT_INFO;
    }
    else
    {
        j->asyncCount = c ? 0 : j->asyncCount + 1;

        switch ( j->p )
        {
            // -----------------------------------------------------
            case TRACE_UNSYNCED:
                break;

            // -----------------------------------------------------
            case TRACE_IDLE:
                switch ( c )
                {
                    case 0b00000001:
                        newState = TRACE_GET_INFO_PLCTL;
                        break;

                    case 0b01110000: /* Ignore packet, Figure 6-30, Pg 6-289 */
                        break;

                    case 0b11110110 ... 0b11110111: /* Atom Format 1, Figure 6-39, Pg 6-304 */
                        cpu->eatoms = ( 0 != ( c & 1 ) );
                        cpu->natoms = !cpu->eatoms;
                        cpu->instCount += 1;
                        cpu->disposition = c & 1;
                        DEBUG( "Atom Format 1 [%b]", cpu->disposition );


                        if ( cpu->addr != ADDRESS_UNKNOWN )
                        {
                            retVal = TRACE_EV_MSG_RXED;
                            _stateChange( cpu, EV_CH_ENATOMS );
                        }

                        break;

                    case 0b11011000 ... 0b11011011: /* Atom Format 2, Figure 6-40, Pg 6-304 */
                        cpu->eatoms = ( 0 != ( c & 2 ) ) + ( 0 != ( c & 1 ) );
                        cpu->natoms = 2 - cpu->eatoms;
                        cpu->instCount += 2;

                        /* Put a 1 in each element of disposition if was executed */
                        cpu->disposition = c & 3;
                        DEBUG( "Atom Format 2 [%02b]", cpu->disposition );

                        if ( cpu->addr != ADDRESS_UNKNOWN )
                        {
                            retVal = TRACE_EV_MSG_RXED;
                            _stateChange( cpu, EV_CH_ENATOMS );
                        }

                        break;

                    case 0b11111000 ... 0b11111111: /* Atom Format 3, Figure 6-41, Pg 6-305 */
                        cpu->eatoms = ( 0 != ( c & 1 ) ) + ( 0 != ( c & 2 ) ) + ( 0 != ( c & 4 ) );
                        cpu->natoms = 3 - cpu->eatoms;
                        cpu->instCount += 3;

                        /* Put a 1 in each element of disposition if was executed */
                        cpu->disposition = c & 7;
                        DEBUG( "Atom Format 3 [%03b]", cpu->disposition );

                        if ( cpu->addr != ADDRESS_UNKNOWN )
                        {
                            retVal = TRACE_EV_MSG_RXED;
                            _stateChange( cpu, EV_CH_ENATOMS );
                        }

                        break;

                    case 0b11011100 ... 0b11011111: /* Atom Format 4, Figure 6-42, Pg 6-305 */
                        switch ( c & 3 )
                        {
                            case 0b00:
                                cpu->natoms = 1;
                                cpu->disposition = 0b1110;
                                break;

                            case 0b01:
                                cpu->natoms = 4;
                                cpu->disposition = 0b0000;
                                break;

                            case 0b10:
                                cpu->natoms = 2;
                                cpu->disposition = 0b1010;
                                break;

                            case 0b11:
                                cpu->natoms = 2;
                                cpu->disposition = 0b0101;
                                break;
                        }

                        cpu->eatoms = 4 - cpu->natoms;
                        cpu->instCount += 4;
                        DEBUG( "Atom Format 4 [%04b]", cpu->disposition );

                        if ( cpu->addr != ADDRESS_UNKNOWN )
                        {
                            retVal = TRACE_EV_MSG_RXED;
                            _stateChange( cpu, EV_CH_ENATOMS );
                        }

                        break;


                    case 0b11010101:
                    case 0b11010110:
                    case 0b11010111:
                    case 0b11110101: /* Atom format 5, Figure 6-43, Pg 6-306 ... use bits 5, 1 and 0 */
                        switch ( ( ( 0 != ( c & ( 1 << 5 ) ) ) << 2 ) | ( ( 0 != ( c & ( 1 << 1 ) ) ) << 1 ) | ( ( 0 != ( c & ( 1 << 0 ) ) ) << 0 ) )
                        {
                            case 0b101:
                                cpu->natoms = 1;
                                cpu->disposition = 0b11110;
                                break;

                            case 0b001:
                                cpu->natoms = 5;
                                cpu->disposition = 0b00000;
                                break;

                            case 0b010:
                                cpu->natoms = 3;
                                cpu->disposition = 0b01010;
                                break;

                            case 0b011:
                                cpu->natoms = 2;
                                cpu->disposition = 0b10101;
                                break;

                            default:
                                DEBUG( "Illegal value for Atom type 5 (0x%02x)" EOL, c );
                                break;
                        }

                        cpu->eatoms = 5 - cpu->natoms;
                        cpu->instCount += 5;
                        DEBUG( "Atom Format 5 [%05b]", cpu->disposition );

                        if ( cpu->addr != ADDRESS_UNKNOWN )
                        {
                            retVal = TRACE_EV_MSG_RXED;
                            _stateChange( cpu, EV_CH_ENATOMS );
                        }

                        break;

                    case 0b11000000 ... 0b11010100:
                    case 0b11100000 ... 0b11110100: /* Atom format 6, Figure 6-44, Pg 6.307 */
                        cpu->eatoms = ( c & 0x1f ) + 3;
                        cpu->instCount = cpu->eatoms;
                        cpu->disposition = ( 1 << ( cpu->eatoms + 1 ) ) - 1;

                        if ( c & ( 1 << 5 ) )
                        {
                            cpu->disposition &= 0xfffffffe;
                            cpu->eatoms--;
                            cpu->natoms = 1;
                        }
                        else
                        {
                            cpu->natoms = 0;
                        }

                        char construct[30];
                        sprintf( construct, "Atom Format 6 [%%ld %%%ldb]", cpu->instCount );
                        DEBUG( construct, cpu->instCount, cpu->disposition );

                        if ( cpu->addr != ADDRESS_UNKNOWN )
                        {
                            retVal = TRACE_EV_MSG_RXED;
                            _stateChange( cpu, EV_CH_ENATOMS );
                        }

                        break;


                    case 0b10100000 ... 0b10101111: /* Q instruction trace packet, Figure 6-45, Pg 6-308 */

                        break;

                    case 0b01110001 ... 0b01111111: /* Event tracing, Figure 6-31, Pg 6-289 */
                        if ( c & 0b0001 )
                        {
                            _stateChange( cpu, EV_CH_EVENT0 );
                        }

                        if ( c & 0b0010 )
                        {
                            _stateChange( cpu, EV_CH_EVENT1 );
                        }

                        if ( c & 0b0100 )
                        {
                            _stateChange( cpu, EV_CH_EVENT2 );
                        }

                        if ( c & 0b1000 )
                        {
                            _stateChange( cpu, EV_CH_EVENT3 );
                        }

                        break;

                    case 0b00000100: /* Trace On, Figure 6.3, Pg 6-261 */
                        retVal = TRACE_EV_MSG_RXED;
                        _stateChange( cpu, EV_CH_TRACESTART );
                        break;

                    case 0b10010000 ... 0b10010011: /* Exact Match Address */
                        int match = c & 0x03;
                        assert( c != 3 ); /* This value is reserved */
                        _stackQ( j );
                        cpu->addr = j->q[match].addr;
                        retVal = TRACE_EV_MSG_RXED;
                        _stateChange( cpu, EV_CH_ADDRESS );
                        break;

                    case 0b10010101: /* Short address, IS0 short, Figure 6-32, Pg 6-294 */
                        j->idx = 2;
                        _stackQ( j );
                        newState = TRACE_GET_SHORT_ADDR;
                        break;

                    case 0b10010110: /* Short address, IS1 short, Figure 6-32, Pg 6-294 */
                        j->idx = 1;
                        _stackQ( j );
                        newState = TRACE_GET_SHORT_ADDR;
                        break;

                    case 0b10011010: /* Long address, 32 bit, IS0, Figure 6.33 Pg 6-295 */
                        j->idx = 2;
                        j->haveContext = false;
                        _stackQ( j );
                        j->q[0].inst = IS0;
                        j->q[0].addr &= 0xFFFFFFFC;
                        newState = TRACE_GET_32BIT_ADDR;
                        break;

                    case 0b10011011: /* Long address, 32 bit, IS1, Figure 6.33 Pg 6-295 */
                        j->idx = 1;
                        j->haveContext = false;
                        _stackQ( j );
                        j->q[0].inst = IS1;
                        j->q[0].addr &= 0xFFFFFFFE;
                        newState = TRACE_GET_32BIT_ADDR;
                        break;

                    case 0b10011101: /* Long address, 64 bit, IS0, Figure 6.34 Pg 6-295 */
                        j->idx = 2;
                        j->haveContext = false;
                        _stackQ( j );
                        j->q[0].inst = IS0;
                        j->q[0].addr &= 0xFFFFFFFC;
                        newState = TRACE_GET_64BIT_ADDR;
                        break;


                    case 0b10011110: /* Long address, 64 bit, IS0, Figure 6.34 Pg 6-295 */
                        j->idx = 1;
                        _stackQ( j );
                        j->haveContext = false;
                        j->q[0].inst = IS1;
                        j->q[0].addr &= 0xFFFFFFFC;
                        newState = TRACE_GET_64BIT_ADDR;
                        break;

                    case 0b10000000: /* Context element with no payload, Figure 6-36, Pg 6-297 */
                        /* Context is same as prevously, nothing to report */
                        break;

                    case 0b10000001: /* Context element with payload, Figure 6-36, Pg 6-297*/
                        newState = TRACE_GET_CONTEXT;
                        break;

                    case 0b10000010: /* Long address, 32 bit, IS0, Figure 6-37 case 1, Pg 6-299 */
                        j->haveContext = true;
                        j->idx = 2;
                        _stackQ( j );
                        j->q[0].inst = IS0;
                        j->q[0].addr &= 0xFFFFFFFC;
                        newState = TRACE_GET_32BIT_ADDR;
                        break;


                    case 0b10000011: /* Long address, 32 bit, IS1, Figure 6-37 case 2, Pg 6-299 */
                        j->idx = 1;
                        j->haveContext = true;
                        _stackQ( j );
                        j->q[0].inst = IS1;
                        j->q[0].addr &= 0xFFFFFFFE;
                        newState = TRACE_GET_32BIT_ADDR;
                        break;

                    case 0b10000101: /* Long address, 64 bit, IS0, Figure 6-38 case 1, Pg 6-300 */
                        j->idx = 2;
                        j->haveContext = true;
                        _stackQ( j );
                        j->q[0].inst = IS0;
                        j->q[0].addr &= 0xFFFFFFFC;
                        newState = TRACE_GET_64BIT_ADDR;
                        break;


                    case 0b10000110: /* Long address, 64 bit, IS1, Figure 6-38 case 2, Pg 6-300 */
                        j->idx = 1;
                        _stackQ( j );
                        j->haveContext = true;
                        j->q[0].inst = IS1;
                        j->q[0].addr &= 0xFFFFFFFC;
                        newState = TRACE_GET_64BIT_ADDR;
                        break;

                    case 0b00000000:
                        newState = TRACE_EXTENSION;
                        break;

                    case 0b00001000: /* Resynchronisation, Figure 6-6, Pg 6-263 */
                        j->rxedISYNC = false;
                        newState = TRACE_UNSYNCED;
                        break;

                    case 0b00000110: /* Exception instruction trace packet Figure 6-10, Pg 6-267 */
                        newState = TRACE_GET_EXCEPTIONINFO1;
                        break;

                    case 0b00000010 ... 0b00000011: /* Timestamp, Figure 6-7, Pg 6-264 */
                        newState = TRACE_GET_TIMESTAMP;
                        j->cc_follows = ( 0 != ( c & 1 ) );

                        if ( !j->cc_follows )
                        {
                            cpu->cycleCount = COUNT_UNKNOWN;
                        }

                        j->idx = 0;
                        break;

                    case 0b10001000: /* Timestamp marker element, Figure 6-8, Pg 6-265 */
                        break;

                    case 0b00000101: /* Function return element, Figure 6-9, Pg 6-265 */
                        retVal = TRACE_EV_MSG_RXED;
                        _stateChange( cpu, EV_CH_FNRETURN );
                        break;

                    case 0b00000111: /* Exception Return element, Figure 6-11, Pg 6-271 */
                        retVal = TRACE_EV_MSG_RXED;
                        _stateChange( cpu, EV_CH_EXRETURN );
                        break;

                    case 0b00100000 ... 0b00100111: /* Data sync mark, Figure 6-15, Pg 6-275 */
                        cpu->dsync_mark = c & 0x07;
                        retVal = TRACE_EV_MSG_RXED;
                        _stateChange( cpu, EV_CH_DATASYNC );
                        break;

                    case 0b00101000 ... 0b00101100: /* Unnumbered data sync mark, Figure 6-16, Pg 6-275 */
                        cpu->udsync_mark = c & 0x07;
                        retVal = TRACE_EV_MSG_RXED;
                        _stateChange( cpu, EV_CH_UDATASYNC );
                        break;

                    case 0b00110000 ... 0b00110011: /* Mispredict instruction, Figure 6-21, Pg 6-279 */
                        break;

                    case 0b00101101: /* Commit instruction trace packet, Figure 6-17, Pg 6-277 */
                        newState = TRACE_COMMIT;
                        break;

                    default:
                        DEBUG( "Unknown element %02x in TRACE_IDLE" EOL, c );
                        break;
                }

                break;

            // -----------------------------------------------------
            case TRACE_GET_CONTEXT: /* Get context information byte, Figure 6-36, Pg 6-297 */
                cpu->exceptionLevel = c & 3;
                cpu->am64bit = ( 0 != ( c & ( 1 << 4 ) ) );
                cpu->amSecure = ( 0 == ( c & ( 1 << 5 ) ) );
                j->haveContext = ( 0 != ( c & ( 1 << 7 ) ) );

                if ( c & ( 1 << 6 ) )
                {
                    j->vcontext = 0;
                    j->idx = 0;
                    newState = TRACE_GET_VCONTEXT;
                }
                else if ( j->haveContext )
                {
                    j->context = 0;
                    j->idx = 0;
                    newState = TRACE_GET_CONTEXT_ID;
                }
                else
                {
                    retVal = TRACE_EV_MSG_RXED;
                    _stateChange( cpu, EV_CH_CONTEXTID );
                    newState = TRACE_IDLE;
                }

                break;

            // -----------------------------------------------------

            case TRACE_GET_EXCEPTIONINFO1:
                j->ex0 = c;
                newState = TRACE_GET_EXCEPTIONINFO2;
                break;

            // -----------------------------------------------------

            case TRACE_GET_EXCEPTIONINFO2:
                newState = TRACE_GET_EXCEPTIONINFO2;
                cpu->exception = ( ( j->ex0 >> 1 ) & 0x1f ) | ( ( c & 0x1f ) << 5 );
                cpu->serious = 0 != ( c & ( 1 << 5 ) );
                _stateChange( cpu, EV_CH_EX_ENTRY );

                /* We aren't really returning idle, but we need to collect a standard formatted address packet    */
                /* Then, when the address is delivered to the CPU Processor, it will have the EV_CH_EXCEPTION set */
                /* too, which the code must recogise as setting a preferred return address.                       */
                newState = TRACE_IDLE;
                break;

            // -----------------------------------------------------

            case TRACE_GET_VCONTEXT:
                j->vcontext |= c << j->idx;
                j->idx += 8;

                if ( ( j->idx == 32 ) && ( !j->haveContext ) )
                {
                    cpu->vmid = j->vcontext;
                    retVal = TRACE_EV_MSG_RXED;
                    _stateChange( cpu, EV_CH_VMID );
                    newState = TRACE_IDLE;
                }
                else
                {
                    j->context = 0;
                    j->idx = 0;
                    newState = TRACE_GET_CONTEXT_ID;
                }

                break;

            // -----------------------------------------------------

            case TRACE_GET_CONTEXT_ID:
                j->context |= c << j->idx;
                j->idx += 8;

                if ( j->idx == 32 )
                {
                    cpu->contextID = j->vcontext;
                    retVal = TRACE_EV_MSG_RXED;
                    _stateChange( cpu, EV_CH_CONTEXTID );
                    newState = TRACE_IDLE;
                }

                break;

            // -----------------------------------------------------
            case TRACE_GET_SHORT_ADDR: /* Short format address for IS0 or IS1, offset set by idx. Figure 6-32, Pg 6-294 */
                if ( j->idx <= 2 )
                {
                    /* First byte of received data */
                    j->q[0].addr = ( j->q[0].addr & ~( 0x7f << j->idx ) ) | ( ( c & 0x7f ) << ( j->idx ) );
                    j->idx += 7;
                }
                else
                {
                    j->q[0].addr = ( j->q[0].addr & ~( 0xff << j->idx ) ) |  ( ( c & 0xff ) << ( j->idx ) );
                    j->idx += 8;
                }

                if ( ( !( c & 0x80 ) ) || ( j->idx > 9 ) )
                {
                    cpu->addr = j->q[0].addr;
                    retVal = TRACE_EV_MSG_RXED;
                    _stateChange( cpu, EV_CH_ADDRESS );
                    newState = TRACE_IDLE;
                }

                break;

            // -----------------------------------------------------
            case TRACE_GET_32BIT_ADDR: /* Long format 32 bit address for IS0 or IS1, offset set by idx. Figure 6-33, Pg 6-295 and Figure 6-37, Pg 6-299 */
                if ( j->idx < 3 )
                {
                    /* First byte of received data */
                    j->q[0].addr = ( j->q[0].addr & ( ~( 0x7F << j->idx ) ) ) | ( ( c & 0x7f ) << ( j->idx ) );
                    j->idx += 7;
                }
                else
                {
                    if ( j->idx == 8 )
                    {
                        /* Second byte of IS1 case - mask MSB */
                        j->q[0].addr = ( j->q[0].addr & ( ~( 0x7F << j->idx ) ) ) | ( ( c & 0x7f ) << ( j->idx ) );
                        j->idx = 16;
                    }
                    else
                    {
                        j->q[0].addr = ( j->q[0].addr & ( ~( 0xFF << j->idx ) ) ) | ( ( c & 0xFf ) << ( j->idx ) );
                        j->idx += 8;
                    }
                }

                if ( j->idx == 32 )
                {
                    cpu->addr = j->q[0].addr;
                    _stateChange( cpu, EV_CH_ADDRESS );

                    if ( j->haveContext )
                    {
                        newState = TRACE_GET_CONTEXT;
                    }
                    else
                    {
                        retVal = TRACE_EV_MSG_RXED;
                        newState = TRACE_IDLE;
                    }
                }

                break;

            // -----------------------------------------------------

            case TRACE_GET_64BIT_ADDR: /* Long format 64 bit address for IS0 or IS1, offset set by idx. Figure 6-34, Pg 6-295 */
                if ( j->idx < 3 )
                {
                    /* First byte of received data */
                    j->q[0].addr = ( j->q[0].addr & ( 0x7F << j->idx ) ) | ( ( c & 0x7f ) << ( j->idx ) );
                    j->idx += 7;
                }
                else
                {
                    if ( ( j->q[0].inst == IS1 ) && ( j->idx == 8 ) )
                    {
                        /* Second byte of IS1 case - mask MSB */
                        j->q[0].addr = ( j->q[0].addr & ( 0x7F << j->idx ) ) | ( ( c & 0x7f ) << ( j->idx ) );
                        j->idx = 16;
                    }
                    else
                    {
                        j->q[0].addr = ( j->q[0].addr & ( 0xFF << j->idx ) ) | ( ( c & 0xFf ) << ( j->idx ) );
                        j->idx += 8;
                    }
                }

                if ( j->idx == 64 )
                {
                    cpu->addr = j->q[0].addr;
                    _stateChange( cpu, EV_CH_ADDRESS );

                    if ( j->haveContext )
                    {
                        newState = TRACE_GET_CONTEXT;
                    }
                    else
                    {
                        retVal = TRACE_EV_MSG_RXED;
                        newState = TRACE_IDLE;
                    }
                }

                break;

            // -----------------------------------------------------
            case TRACE_GET_TIMESTAMP: /* Timestamp, Figure 6-7, Pg 6-264 */
                if ( j->idx < 56 )
                {
                    cpu->ts = ( cpu->ts & ( 0x7F << j->idx ) ) | ( ( c & 0x7f ) << j->idx );
                }
                else
                {
                    cpu->ts = ( cpu->ts & ( 0xFF << j->idx ) ) | ( ( c & 0xff ) << j->idx );
                }

                j->idx += 7;

                if ( ( !( c & 0x80 ) ) || ( j->idx == 63 ) )
                {
                    if ( cpu->cycleCount != COUNT_UNKNOWN )
                    {
                        _stateChange( cpu, EV_CH_TSTAMP );
                    }

                    if ( j->cc_enabled )
                    {
                        if ( j->cc_follows )
                        {
                            j->idx = 0;
                            j->cntUpdate = 0;
                            newState = TRACE_GET_TS_CC;
                        }
                        else
                        {
                            retVal = TRACE_EV_MSG_RXED;
                            newState = TRACE_IDLE;
                        }
                    }
                    else
                    {
                        retVal = TRACE_EV_MSG_RXED;
                        newState = TRACE_IDLE;
                    }
                }

                break;

            // -----------------------------------------------------
            case TRACE_GET_TS_CC: /* Part of timestamp, Figure 6-7, Pg 6-264 */
                if ( j->idx < 2 )
                {
                    j->cntUpdate |= ( c & 0x7f ) << ( j->idx * 7 );
                }
                else
                {
                    j->cntUpdate |= ( c & 0x7f ) << ( j->idx * 7 );
                }

                j->idx++;

                if ( ( j->idx == 3 ) || ( c & 0x80 ) )
                {

                    cpu->cycleCount += j->cntUpdate;
                    retVal = TRACE_EV_MSG_RXED;
                    _stateChange( cpu, EV_CH_CYCLECOUNT );
                    newState = TRACE_IDLE;
                }

                break;

            // -----------------------------------------------------
            case TRACE_EXTENSION:
                switch ( c )
                {
                    case 0b00000011: /* Discard packet, Figure 6.4, Pg 6-262 */
                        _stateChange( cpu, EV_CH_DISCARD );
                        _stateChange( cpu, EV_CH_TRACESTOP );
                        break;

                    case 0b00000101: /* Overflow packet, Figure 6.5, Pg. 6-263 */
                        _stateChange( cpu, EV_CH_OVERFLOW );
                        break;

                    case 0b00000111: /* Branch future flush */
                        break;

                    default:
                        DEBUG( "Reserved extension packet" EOL );
                        break;
                }

                retVal = TRACE_EV_MSG_RXED;
                newState = TRACE_IDLE;
                break;

            // -----------------------------------------------------
            case TRACE_WAIT_INFO:
                if ( c == 0b00000001 )
                {
                    newState = TRACE_GET_INFO_PLCTL;
                }

                break;

            case TRACE_GET_INFO_PLCTL:
                j->plctl = c;
                j->nextrhkey = 0;
                _flushQ( j ); /* Reset the address stack too ( Section 6.4.12, Pg 6-291 ) */

                if ( j->plctl & ( 1 << 0 ) )
                {
                    newState = TRACE_GET_INFO_INFO;
                }
                else if ( j->plctl & ( 1 << 1 ) )
                {
                    newState = TRACE_GET_INFO_KEY;
                }
                else if ( j->plctl & ( 1 << 2 ) )
                {
                    newState = TRACE_GET_INFO_SPEC;
                }
                else if ( j->plctl & ( 1 << 3 ) )
                {
                    newState = TRACE_GET_INFO_CYCT;
                }
                else
                {
                    _reportInfo( cpu, j );
                    retVal = TRACE_EV_MSG_RXED;
                    newState = TRACE_IDLE;
                }

                break;

            case TRACE_GET_INFO_INFO:
                j->cc_enabled   = ( 0 != ( c & ( 1 << 0 ) ) );
                j->cond_enabled = ( c << 1 ) & 7;
                j->load_traced  = ( 0 != ( c & ( 1 << 4 ) ) );
                j->store_traced = ( 0 != ( c & ( 1 << 5 ) ) );

                if ( j->plctl & ( 1 << 1 ) )
                {
                    newState = TRACE_GET_INFO_KEY;
                }
                else if ( j->plctl & ( 1 << 2 ) )
                {
                    newState = TRACE_GET_INFO_SPEC;
                }
                else if ( j->plctl & ( 1 << 3 ) )
                {
                    newState = TRACE_GET_INFO_CYCT;
                }
                else
                {
                    _reportInfo( cpu, j );
                    retVal = TRACE_EV_MSG_RXED;
                    newState = TRACE_IDLE;
                }

                break;

            case TRACE_GET_INFO_KEY:
                j->nextrhkey = c;  // FIXME: Greater than 256 keys???

                if ( j->plctl & ( 1 << 2 ) )
                {
                    newState = TRACE_GET_INFO_SPEC;
                }
                else if ( j->plctl & ( 1 << 3 ) )
                {
                    newState = TRACE_GET_INFO_CYCT;
                }
                else
                {
                    _reportInfo( cpu, j );
                    retVal = TRACE_EV_MSG_RXED;
                    newState = TRACE_IDLE;
                }

                break;

            case TRACE_GET_INFO_SPEC:
                j->spec = c;

                if ( j->plctl & ( 1 << 3 ) )
                {
                    _reportInfo( cpu, j );
                    newState = TRACE_GET_INFO_CYCT;
                }
                else
                {
                    _reportInfo( cpu, j );
                    retVal = TRACE_EV_MSG_RXED;
                    newState = TRACE_IDLE;
                }

                break;

            // -----------------------------------------------------

            default:
                DEBUG( "Case %d not handled in switch" EOL, j->p );
                break;
        }
    }

    if ( j->p != TRACE_UNSYNCED )
    {
        DEBUG( "%02x:%s --> %s (%s:%d)", c, ( j->p == TRACE_IDLE ) ? _protoStateName[j->p] : "", _protoStateName[newState],
               ( ( newState == TRACE_IDLE ) ? ( ( retVal == TRACE_EV_NONE ) ? "DROPPED" : "OK" ) : "-" ), retVal );

        if ( newState == TRACE_IDLE )
        {
            DEBUG( "\r\n" );
        }
    }

    j->p = newState;

    /* Tell the caller we have information to report is something interesting has happened */
    return ( ( retVal != TRACE_EV_NONE ) && ( j->rxedISYNC ) );
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
    return ( ( struct ETM4DecodeState * )e )->p != TRACE_UNSYNCED;
}

// ====================================================================================================
static void _forceSync(  struct TRACEDecoderEngine *e, bool isSynced )

{
    ( ( struct ETM4DecodeState * )e )->p = ( isSynced ) ? TRACE_IDLE : TRACE_UNSYNCED;
}

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Publicly accessible methods
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================

struct TRACEDecoderEngine *ETM4DecoderPumpCreate( void )

{

    struct TRACEDecoderEngine *e = ( struct TRACEDecoderEngine * )calloc( 1, sizeof( struct ETM4DecodeState ) );
    e->action    = _pumpAction;
    e->destroy   = _pumpDestroy;
    e->synced    = _synced;
    e->forceSync = _forceSync;
    return e;
}

// ====================================================================================================
