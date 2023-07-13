/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * ETM3.5 TRACE Decoder Module
 * ===========================
 *
 * Implementation of ITM/DWT decode according to the specification in Appendix D4
 * of the ARMv7-M Architecture Refrence Manual document available
 * from https://static.docs.arm.com/ddi0403/e/DDI0403E_B_armv7m_arm.pdf
 */

#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include "msgDecoder.h"
#include "traceDecoder.h"
#include "generics.h"

/* Internal states of the protocol machine */
enum TRACE_ETM35protoState
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

static const char *_protoStateName[] =
{
    "UNSYNCED",       "WAIT_ISYNC",        "IDLE",             "COLLECT_BA_STD",
    "COLLECT_BA_ALT", "COLLECT_EXCEPTION", "WAIT_CONTEXTBYTE", "WAIT_INFOBYTE",
    "WAIT_IADDRESS",  "WAIT_ICYCLECOUNT",  "WAIT_CYCLECOUNT",  "GET_VMID",
    "GET_TSTAMP",     "GET_CONTEXTID"
};

struct ETM35DecodeState
{
    struct TRACEDecoderEngine e;         /* Must be first to allow object method access */
    enum TRACE_ETM35protoState p;        /* Current state of the receiver */

    bool usingAltAddrEncode;             /* Set if the new (TRACE 3.4 onwards) addr formatting is used */
    bool dataOnlyMode;                   /* If we're only tracing data, not instructions */
    uint8_t contextBytes;                /* How many context bytes we're using */

    /* Purely internal matters.... */
    /* --------------------------- */
    uint64_t tsConstruct;                /* Timestamp under construction */
    uint32_t asyncCount;                 /* Count of 0's in preparation for ASYNC recognition */
    uint32_t addrConstruct;              /* Address under construction */
    uint32_t byteCount;                  /* How many bytes of this packet do we have? */
    uint32_t cycleConstruct;             /* Cycle count under construction */
    uint32_t contextConstruct;           /* Context under construction */
    bool rxedISYNC;                      /* Indicator that we're fully synced */
    bool cycleAccurate;                  /* Using cycle accurate mode */
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
// ====================================================================================================
// ====================================================================================================
// Externally available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
static bool _pumpAction( struct TRACEDecoderEngine *e, struct TRACECPUState *cpu, uint8_t c )

/* Pump next byte into the protocol decoder */

{
    bool C;                               /* Is address packet continued? */
    bool X = false;                       /* Is there exception information following address */
    int8_t ofs;                           /* Offset for bits in address calculation */
    uint8_t mask;                         /* Mask for bits in address calculation */

    struct ETM35DecodeState *j = ( struct ETM35DecodeState * )e;
    enum TRACE_ETM35protoState newState = j->p;
    enum TRACEDecoderPumpEvent retVal = TRACE_EV_NONE;

    /* Perform A-Sync accumulation check */
    if ( ( j->asyncCount >= 5 ) && ( c == 0x80 ) )
    {
        DEBUG( "A-Sync Accumulation complete" EOL );
        newState = TRACE_IDLE;
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

                // *************************************************
                // ************** BRANCH PACKET ********************
                // *************************************************
                if ( c & 0b1 )
                {
                    /* The lowest order 6 bits of address info... */

                    switch ( cpu->addrMode )
                    {
                        case TRACE_ADDRMODE_ARM:
                            j->addrConstruct = ( j->addrConstruct & ~( 0b11111100 ) ) | ( ( c & 0b01111110 ) << 1 );
                            break;

                        case TRACE_ADDRMODE_THUMB:
                            j->addrConstruct = ( j->addrConstruct & ~( 0b01111111 ) ) | ( c & 0b01111110 );
                            break;

                        case TRACE_ADDRMODE_JAZELLE:
                            j->addrConstruct = ( j->addrConstruct & ~( 0b00111111 ) ) | ( ( c & 0b01111110 ) >> 1 );
                            break;
                    }

                    j->byteCount = 1;
                    C = ( c & 0x80 ) != 0;
                    X = false;
                    _stateChange( cpu, EV_CH_ADDRESS );

                    newState = ( j->usingAltAddrEncode ) ? TRACE_COLLECT_BA_ALT_FORMAT : TRACE_COLLECT_BA_STD_FORMAT;
                    goto terminateAddrByte;
                }

                // *************************************************
                // ************** A-SYNC PACKET ********************
                // *************************************************
                if ( c == 0b00000000 )
                {
                    break;
                }

                // *************************************************
                // ************ CYCLECOUNT PACKET ******************
                // *************************************************
                if ( c == 0b00000100 )
                {
                    DEBUG( "CYCCNT " EOL );
                    j->byteCount = 0;
                    j->cycleConstruct = 0;
                    newState = TRACE_GET_CYCLECOUNT;
                    break;
                }

                // *************************************************
                // ************** ISYNC PACKETS ********************
                // *************************************************
                if ( c == 0b00001000 ) /* Normal ISYNC */
                {
                    DEBUG( "Normal ISYNC " EOL );
                    /* Collect either the context or the Info Byte next */
                    j->byteCount = 0;
                    j->contextConstruct = 0;
                    newState = j->contextBytes ? TRACE_GET_CONTEXTBYTE : TRACE_GET_INFOBYTE;

                    /* We won't start reporting data until a valid ISYNC has been received */
                    if ( !j->rxedISYNC )
                    {
                        DEBUG( "Initial ISYNC" );
                        cpu->changeRecord = 0;
                        j->rxedISYNC = true;
                    }

                    break;
                }

                if ( c == 0b01110000 ) /* ISYNC with Cycle Count */
                {
                    DEBUG( "ISYNC+CYCCNT " EOL );
                    /* Collect the cycle count next */
                    j->byteCount = 0;
                    j->cycleConstruct = 0;
                    newState = TRACE_GET_ICYCLECOUNT;
                    break;
                }

                // *************************************************
                // ************** TRIGGER PACKET *******************
                // *************************************************
                if ( c == 0b00001100 )
                {
                    DEBUG( "TRIGGER " EOL );
                    _stateChange( cpu, EV_CH_TRIGGER );
                    retVal = TRACE_EV_MSG_RXED;
                    break;
                }

                // *************************************************
                // **************** VMID PACKET ********************
                // *************************************************
                if ( c == 0b00111100 )
                {
                    DEBUG( "VMID " EOL );
                    newState = TRACE_GET_VMID;
                    break;
                }

                // *************************************************
                // *********** TIMESTAMP PACKET ********************
                // *************************************************
                if ( ( c & 0b11111011 ) == 0b01000010 )
                {
                    DEBUG( "TS " EOL );
                    newState = TRACE_GET_TSTAMP;

                    if ( ( c & ( 1 << 2 ) ) != 0 )
                    {
                        _stateChange( cpu, EV_CH_CLOCKSPEED );
                    }

                    j->byteCount = 0;
                    break;
                }

                // *************************************************
                // ************** IGNORE PACKET ********************
                // *************************************************
                if ( c == 0b01100110 )
                {
                    DEBUG( "Ignore Packet" EOL );
                    break;
                }

                // *************************************************
                // ************ CONTEXTID PACKET *******************
                // *************************************************
                if ( c == 0b01101110 )
                {
                    DEBUG( "CONTEXTID " EOL );
                    newState = TRACE_GET_CONTEXTID;
                    cpu->contextID = 0;
                    j->byteCount = 0;
                    break;
                }

                // *************************************************
                // ******** EXCEPTION EXIT PACKET ******************
                // *************************************************
                if ( c == 0b01110110 )
                {
                    DEBUG( "EXCEPT-EXIT " EOL );
                    _stateChange( cpu, EV_CH_EX_EXIT );
                    retVal = TRACE_EV_MSG_RXED;
                    break;
                }

                // *************************************************
                // ******** EXCEPTION ENTRY PACKET *****************
                // *************************************************
                if ( c == 0b01111110 )
                {
                    /* Note this is only used on CPUs with data tracing */
                    DEBUG( "EXCEPT-ENTRY " EOL );
                    _stateChange( cpu, EV_CH_EX_ENTRY );
                    retVal = TRACE_EV_MSG_RXED;
                    break;
                }

                // *************************************************
                // ************** P-HEADER PACKET ******************
                // *************************************************
                if ( ( c & 0b10000001 ) == 0b10000000 )
                {
                    if ( !j->cycleAccurate )
                    {
                        if ( ( c & 0b10000011 ) == 0b10000000 )
                        {
                            /* Format-1 P-header */
                            cpu->eatoms = ( c & 0x3C ) >> 2;
                            cpu->natoms = ( c & ( 1 << 6 ) ) ? 1 : 0;
                            cpu->instCount += cpu->eatoms + cpu->natoms;

                            /* Put a 1 in each element of disposition if was executed */
                            cpu->disposition = ( 1 << cpu->eatoms ) - 1;
                            _stateChange( cpu, EV_CH_ENATOMS );
                            retVal = TRACE_EV_MSG_RXED;
                            DEBUG( "PHdr FMT1 (%02x E=%d, N=%d)" EOL, c, cpu->eatoms, cpu->natoms );
                            break;
                        }

                        if ( ( c & 0b11110011 ) == 0b10000010 )
                        {
                            /* Format-2 P-header */
                            cpu->eatoms = ( ( c & ( 1 << 2 ) ) == 0 ) + ( ( c & ( 1 << 3 ) ) == 0 );
                            cpu->natoms = 2 - cpu->eatoms;

                            cpu->disposition = ( ( c & ( 1 << 3 ) ) == 0 ) |
                                               ( ( ( c & ( 1 << 2 ) ) == 0 ) << 1 );

                            _stateChange( cpu, EV_CH_ENATOMS );
                            cpu->instCount += cpu->eatoms + cpu->natoms;
                            retVal = TRACE_EV_MSG_RXED;

                            DEBUG( "PHdr FMT2 (E=%d, N=%d)" EOL, cpu->eatoms, cpu->natoms );
                            break;
                        }

                        DEBUG( "Unprocessed P-Header (%02X)" EOL, c );
                    }
                    else
                    {
                        if ( c == 0b10000000 )
                        {
                            /* Format 0 cycle-accurate P-header */
                            cpu->watoms = 1;
                            cpu->instCount += cpu->watoms;
                            cpu->eatoms = cpu->natoms = 0;
                            _stateChange( cpu, EV_CH_ENATOMS );
                            _stateChange( cpu, EV_CH_WATOMS );
                            retVal = TRACE_EV_MSG_RXED;

                            DEBUG( "CA PHdr FMT0 (W=%d)" EOL, cpu->watoms );
                            break;
                        }

                        if ( ( c & 0b10100011 ) == 0b10000000 )
                        {
                            /* Format 1 cycle-accurate P-header */
                            cpu->eatoms = ( c & 0x1c ) >> 2;
                            cpu->natoms = ( c & 0x40 ) != 0;
                            cpu->watoms = cpu->eatoms + cpu->natoms;
                            cpu->instCount += cpu->watoms;
                            cpu->disposition = ( 1 << cpu->eatoms ) - 1;
                            _stateChange( cpu, EV_CH_ENATOMS );
                            _stateChange( cpu, EV_CH_WATOMS );
                            retVal = TRACE_EV_MSG_RXED;

                            DEBUG( "CA PHdr FMT1 (E=%d, N=%d)" EOL, cpu->eatoms, cpu->natoms );
                            break;
                        }

                        if ( ( c & 0b11110011 ) == 0b10000010 )
                        {
                            /* Format 2 cycle-accurate P-header */
                            cpu->eatoms = ( ( c & ( 1 << 2 ) ) != 0 ) + ( ( c & ( 1 << 3 ) ) != 0 );
                            cpu->natoms = 2 - cpu->eatoms;
                            cpu->watoms = 1;
                            cpu->instCount += cpu->watoms;
                            cpu->disposition = ( ( c & ( 1 << 3 ) ) != 0 ) | ( ( c & ( 1 << 2 ) ) != 0 );
                            _stateChange( cpu, EV_CH_ENATOMS );
                            _stateChange( cpu, EV_CH_WATOMS );
                            retVal = TRACE_EV_MSG_RXED;

                            DEBUG( "CA PHdr FMT2 (E=%d, N=%d, W=1)" EOL, cpu->eatoms, cpu->natoms );
                            break;
                        }

                        if ( ( c & 0b10100000 ) == 0b10100000 )
                        {
                            /* Format 3 cycle-accurate P-header */
                            cpu->eatoms = ( c & 0x40 ) != 0;
                            cpu->natoms = 0;
                            cpu->watoms = ( c & 0x1c ) >> 2;
                            cpu->instCount += cpu->watoms;
                            /* Either 1 or 0 eatoms */
                            cpu->disposition = cpu->eatoms;
                            _stateChange( cpu, EV_CH_ENATOMS );
                            _stateChange( cpu, EV_CH_WATOMS );
                            retVal = TRACE_EV_MSG_RXED;

                            DEBUG( "CA PHdr FMT3 (E=%d, N=%d W=%d)" EOL, cpu->eatoms, cpu->natoms, cpu->watoms );
                            break;
                        }

                        if ( ( c & 0b11111011 ) == 0b10010010 )
                        {
                            /* Format 4 cycle-accurate P-header */
                            cpu->eatoms = ( c & 0x4 ) != 0;
                            cpu->natoms = ( c & 0x4 ) == 0;
                            cpu->watoms = 0;

                            /* Either 1 or 0 eatoms */
                            cpu->disposition = cpu->eatoms;
                            _stateChange( cpu, EV_CH_ENATOMS );
                            _stateChange( cpu, EV_CH_WATOMS );
                            retVal = TRACE_EV_MSG_RXED;

                            DEBUG( "CA PHdr FMT4 (E=%d, N=%d W=%d)" EOL, cpu->eatoms, cpu->natoms, cpu->watoms );
                            break;
                        }

                        DEBUG( "Unprocessed Cycle-accurate P-Header (%02X)" EOL, c );
                    }

                    break;
                }

                break;


            // -----------------------------------------------------
            // ADDRESS COLLECTION RELATED ACTIVITIES
            // -----------------------------------------------------

            case TRACE_COLLECT_BA_ALT_FORMAT: /* Collecting a branch address, alt format */
                C = c & 0x80;
                /* This is a proper mess. Mask and collect bits according to address mode in use and */
                /* if it's the last byte of the sequence */
                mask = C ? 0x7f : 0x3f;
                ofs = ( cpu->addrMode == TRACE_ADDRMODE_ARM ) ? 1 : ( cpu->addrMode == TRACE_ADDRMODE_THUMB ) ? 0 : -1;


                j->addrConstruct = ( j->addrConstruct &   ~( mask << ( 7 * j->byteCount + ofs ) ) )
                                   | ( ( c & mask ) << ( 7 * j->byteCount + ofs ) );
                /* There is exception information only if no continuation and bit 6 set */
                X = ( ( !C ) && ( c & 0x40 ) );
                j->byteCount++;
                goto terminateAddrByte;

            // -----------------------------------------------------

            case TRACE_COLLECT_BA_STD_FORMAT: /* Collecting a branch address, standard format */
                /* This will potentially collect too many bits, but that is OK */
                ofs = ( cpu->addrMode == TRACE_ADDRMODE_ARM ) ? 1 : ( cpu->addrMode == TRACE_ADDRMODE_THUMB ) ? 0 : -1;
                j->addrConstruct = ( j->addrConstruct &  ~( 0x7F << ( ( 7 * j->byteCount ) + ofs ) ) ) | ( c & ( 0x7F <<  ( ( 7 * j->byteCount ) + ofs ) ) );
                j->byteCount++;
                C = ( j->byteCount < 5 ) ? c & 0x80 : c & 0x40;
                X = ( j->byteCount == 5 ) && C;
                goto terminateAddrByte;

                // -----------------------------------------------------

                /* For all cases, see if the address is complete, and process if so */
                /* this is a continuation of TRACE_COLLECT_BA_???_FORMAT.             */
            terminateAddrByte:

                /* Check to see if this packet is complete, and encode to return if so */
                if ( ( !C ) || ( j->byteCount == 5 ) )
                {
                    cpu->addr = j->addrConstruct;

                    if ( ( j->byteCount == 5 ) && ( cpu->addrMode == TRACE_ADDRMODE_ARM ) && C )
                    {
                        /* There is (legacy) exception information in here */
                        cpu->exception = ( c >> 4 ) & 0x07;
                        _stateChange( cpu, EV_CH_EX_ENTRY );
                        _stateChange( cpu, ( ( c & 0x40 ) != 0 ) ? EV_CH_CANCELLED : 0 );
                        newState = TRACE_IDLE;
                        retVal = TRACE_EV_MSG_RXED;

                        DEBUG( "Branch to %08x with exception %d" EOL, cpu->addr, cpu->exception );
                        break;
                    }

                    if ( ( !C ) & ( !X ) )
                    {
                        /* This packet is complete, so can return it */
                        newState = TRACE_IDLE;
                        retVal = TRACE_EV_MSG_RXED;
                        DEBUG( "Branch to %08x" EOL, cpu->addr );
                    }
                    else
                    {
                        /* This packet also contains exception information, so collect it */
                        j->byteCount = 0; /* Used as a flag of which byte of exception we're collecting */
                        cpu->resume = 0;
                        _stateChange( cpu, EV_CH_EX_ENTRY );
                        newState = TRACE_COLLECT_EXCEPTION;
                    }
                }

                break;

            // -----------------------------------------------------

            case TRACE_COLLECT_EXCEPTION: /* Collecting exception information */
                if ( j->byteCount == 0 )
                {
                    if ( ( ( c & ( 1 << 0 ) ) != 0 ) != cpu->nonSecure )
                    {
                        cpu->nonSecure = ( ( c & ( 1 << 0 ) ) != 0 );
                        _stateChange( cpu, EV_CH_SECURE );
                    }

                    cpu->exception = ( c >> 1 ) & 0x0f;
                    _stateChange( cpu, ( ( c & ( 1 << 5 ) ) != 0 ) ? EV_CH_CANCELLED : 0 );

                    if ( cpu->altISA != ( ( c & ( 1 << 6 ) ) != 0 ) )
                    {
                        cpu->altISA = ( ( c & ( 1 << 6 ) ) != 0 );
                        _stateChange( cpu, EV_CH_ALTISA );
                    }

                    if ( c & 0x80 )
                    {
                        j->byteCount++;
                    }
                    else
                    {
                        DEBUG( "Exception jump (%d) to 0x%08x" EOL, cpu->exception, cpu->addr );
                        newState = TRACE_IDLE;
                        retVal = TRACE_EV_MSG_RXED;
                    }
                }
                else
                {
                    if ( c & 0x80 )
                    {
                        /* This is exception byte 1 */
                        cpu->exception |= ( c & 0x1f ) << 4;

                        if ( cpu->hyp != ( ( c & ( 1 << 5 ) ) != 0 ) )
                        {
                            cpu->hyp = ( ( c & ( 1 << 5 ) ) != 0 );
                            _stateChange( cpu, EV_CH_HYP );
                        }

                        if ( !( c & 0x40 ) )
                        {
                            /* There will not be another one along, return idle */
                            DEBUG( "Exception jump (%d) to 0x%08x" EOL, cpu->exception, cpu->addr );
                            newState = TRACE_IDLE;
                            retVal = TRACE_EV_MSG_RXED;
                        }
                    }
                    else
                    {
                        /* This is exception byte 2 */
                        cpu->resume = ( c & 0xf );

                        if ( cpu->resume )
                        {
                            _stateChange( cpu, EV_CH_RESUME );
                        }

                        /* Exception byte 2 is always the last one, return */
                        DEBUG( "Exception jump %s(%d) to 0x%08x" EOL, cpu->resume ? "with resume " : "", cpu->exception, cpu->addr );
                        newState = TRACE_IDLE;
                        retVal = TRACE_EV_MSG_RXED;
                    }
                }

                break;


            // -----------------------------------------------------
            // VMID RELATED ACTIVITIES
            // -----------------------------------------------------
            case TRACE_GET_VMID: /* Collecting virtual machine ID */
                if ( cpu->vmid != c )
                {
                    cpu->vmid = c;
                    _stateChange( cpu, EV_CH_VMID );
                }

                DEBUG( "VMID Set to (%d)" EOL, cpu->vmid );
                newState = TRACE_IDLE;
                retVal = TRACE_EV_MSG_RXED;
                break;

            // -----------------------------------------------------
            // TIMESTAMP RELATED ACTIVITIES
            // -----------------------------------------------------

            case TRACE_GET_TSTAMP: /* Collecting current timestamp */
                if ( j->byteCount < 8 )
                {
                    j->tsConstruct = ( j->tsConstruct & ( ~( 0x7F << j->byteCount ) ) ) | ( ( c & 0x7f ) << j->byteCount );
                }
                else
                {
                    j->tsConstruct = ( j->tsConstruct & ( ~( 0xff << j->byteCount ) ) ) | ( ( c & 0xff ) << j->byteCount );
                }

                j->byteCount++;

                if ( ( !( c & 0x80 ) ) || ( j->byteCount == 9 ) )
                {
                    newState = TRACE_IDLE;
                    cpu->ts = j->tsConstruct;
                    _stateChange( cpu, EV_CH_TSTAMP );

                    DEBUG( "CPU Timestamp %d" EOL, cpu->ts );
                    retVal = TRACE_EV_MSG_RXED;
                }

                break;

            // -----------------------------------------------------
            // CYCLECOUNT RELATED ACTIVITIES
            // -----------------------------------------------------

            case TRACE_GET_CYCLECOUNT: /* Collecting cycle count as standalone packet */
                j->cycleConstruct = ( j->cycleConstruct & ~( 0x7f << ( ( j->byteCount ) * 7 ) ) ) | ( ( c & 0x7f ) << ( ( j->byteCount ) * 7 ) );
                j->byteCount++;

                if ( ( !( c & ( 1 << 7 ) ) ) || ( j->byteCount == 5 ) )
                {
                    newState = TRACE_IDLE;
                    cpu->cycleCount = j->cycleConstruct;
                    _stateChange( cpu, EV_CH_CYCLECOUNT );

                    DEBUG( "Cyclecount %d" EOL, cpu->cycleCount );
                    retVal = TRACE_EV_MSG_RXED;
                }

                break;


            // -----------------------------------------------------
            // CONTEXTID RELATED ACTIVITIES
            // -----------------------------------------------------

            case TRACE_GET_CONTEXTID: /* Collecting contextID */
                j->contextConstruct = j->contextConstruct + ( c << ( 8 * j->byteCount ) );
                j->byteCount++;

                if ( j->byteCount == j->contextBytes )
                {
                    if ( cpu->contextID != j->contextConstruct )
                    {
                        cpu->contextID = j->contextConstruct;
                        _stateChange( cpu, EV_CH_CONTEXTID );
                    }

                    DEBUG( "CPU ContextID %d" EOL, cpu->contextID );
                    retVal = TRACE_EV_MSG_RXED;
                    newState = TRACE_IDLE;
                }

                break;


            // -----------------------------------------------------
            // I-SYNC RELATED ACTIVITIES
            // -----------------------------------------------------

            case TRACE_WAIT_ISYNC:
                if ( c == 0b00001000 )
                {
                    if ( !j->rxedISYNC )
                    {
                        retVal = TRACE_EV_SYNCED;
                        j->rxedISYNC = true;
                    }

                    j->byteCount = j->contextBytes;
                    j->contextConstruct = 0;
                    newState = j->contextBytes ? TRACE_GET_CONTEXTBYTE : TRACE_GET_INFOBYTE;
                }

                break;

            // -----------------------------------------------------

            case TRACE_GET_CONTEXTBYTE: /* Collecting I-Sync contextID bytes */
                j->contextConstruct = j->contextConstruct + ( c << ( 8 * j->byteCount ) );
                j->byteCount++;

                if ( j->byteCount == j->contextBytes )
                {
                    if ( cpu->contextID != j->contextConstruct )
                    {
                        cpu->contextID = j->contextConstruct;
                        _stateChange( cpu, EV_CH_CONTEXTID );
                    }

                    newState = TRACE_GET_INFOBYTE;
                }

                break;

            // -----------------------------------------------------

            case TRACE_GET_INFOBYTE: /* Collecting I-Sync Information byte */
                if ( ( ( c & 0x10000000 ) != 0 ) != cpu->isLSiP )
                {
                    cpu->isLSiP = ( c & 0x10000000 ) != 0;
                    _stateChange( cpu, EV_CH_ISLSIP );
                }

                if ( cpu->reason != ( ( c & 0x01100000 ) >> 5 ) )
                {
                    cpu->reason    = ( c & 0x01100000 ) >> 5;
                    _stateChange( cpu, EV_CH_REASON );
                }

                if ( cpu->jazelle   != ( ( c & 0x00010000 ) != 0 ) )
                {
                    cpu->jazelle   = ( c & 0x00010000 ) != 0;
                    _stateChange( cpu, EV_CH_JAZELLE );
                }

                if ( cpu->nonSecure != ( ( c & 0x00001000 ) != 0 ) )
                {
                    cpu->nonSecure = ( c & 0x00001000 ) != 0;
                    _stateChange( cpu, EV_CH_SECURE );
                }

                if ( cpu->altISA != ( ( c & 0x00000100 ) != 0 ) )
                {
                    cpu->altISA    = ( c & 0x00000100 ) != 0;
                    _stateChange( cpu, EV_CH_ALTISA );
                }

                if ( cpu->hyp != ( ( c & 0x00000010 ) != 0 ) )
                {
                    cpu->hyp       = ( c & 0x00000010 ) != 0;
                    _stateChange( cpu, EV_CH_HYP );
                }

                j->byteCount = 0;

                if ( j->dataOnlyMode )
                {
                    DEBUG( "ISYNC in dataOnlyMode" EOL );
                    retVal = TRACE_EV_MSG_RXED;
                    newState = TRACE_IDLE;
                }
                else
                {
                    newState = TRACE_GET_IADDRESS;
                }

                break;

            // -----------------------------------------------------

            case TRACE_GET_IADDRESS: /* Collecting I-Sync Address bytes */
                j->addrConstruct = ( j->addrConstruct & ( ~( 0xff << ( 8 * j->byteCount ) ) ) )  | ( c << ( 8 * j->byteCount ) ) ;
                j->byteCount++;

                if ( j->byteCount == 4 )
                {
                    _stateChange( cpu, EV_CH_ADDRESS );

                    if ( cpu->jazelle )
                    {
                        /* This is Jazelle mode..can ignore the AltISA bit */
                        /* and bit 0 is bit 0 of the address */
                        cpu->addrMode = TRACE_ADDRMODE_JAZELLE;
                        cpu->addr = j->addrConstruct;
                    }
                    else
                    {
                        if ( ( j->addrConstruct & ( 1 << 0 ) ) ^ ( !cpu->thumb ) )
                        {
                            cpu->thumb     = ( c & 0x00000001 ) != 0;
                            _stateChange( cpu, EV_CH_THUMB );
                        }

                        if ( j->addrConstruct & ( 1 << 0 ) )
                        {
                            cpu->addrMode = TRACE_ADDRMODE_THUMB;
                            j->addrConstruct &= ~( 1 << 0 );
                            cpu->addr = j->addrConstruct;
                        }
                        else
                        {
                            cpu->addrMode = TRACE_ADDRMODE_ARM;
                            cpu->addr = j->addrConstruct & 0xFFFFFFFC;
                        }
                    }

                    if ( cpu->isLSiP )
                    {
                        /* If this is an LSiP packet we need to go get the address */
                        newState = ( j->usingAltAddrEncode ) ? TRACE_COLLECT_BA_ALT_FORMAT : TRACE_COLLECT_BA_STD_FORMAT;
                    }
                    else
                    {
                        DEBUG( "ISYNC with IADDRESS 0x%08x" EOL, cpu->addr );
                        newState = TRACE_IDLE;
                        retVal = TRACE_EV_MSG_RXED;
                    }
                }

                break;

            // -----------------------------------------------------

            case TRACE_GET_ICYCLECOUNT: /* Collecting cycle count on front of ISYNC packet */
                j->cycleConstruct = ( j->cycleConstruct & ~( 0x7f << ( ( j->byteCount ) * 7 ) ) ) | ( ( c & 0x7f ) << ( ( j->byteCount ) * 7 ) );
                j->byteCount++;

                if ( ( !( c & ( 1 << 7 ) ) ) || ( j->byteCount == 5 ) )
                {
                    /* Now go to get the rest of the ISYNC packet */
                    /* Collect either the context or the Info Byte next */
                    cpu->cycleCount = j->cycleConstruct;
                    j->byteCount = j->contextBytes;
                    j->contextConstruct = 0;
                    _stateChange( cpu, EV_CH_CYCLECOUNT );
                    newState = j->contextBytes ? TRACE_GET_CONTEXTBYTE : TRACE_GET_INFOBYTE;
                    break;
                }

                break;

                // -----------------------------------------------------

        }
    }

    if ( j->p != TRACE_UNSYNCED )
    {
        DEBUG( "%02x:%s --> %s %s(%d)", c, ( j->p == TRACE_IDLE ) ? _protoStateName[j->p] : "", _protoStateName[newState],
               ( ( newState == TRACE_IDLE ) ? ( ( retVal == TRACE_EV_NONE ) ? "!!!" : "OK" ) : " : " ), retVal );
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
    return ( ( struct ETM35DecodeState * )e )->p != TRACE_UNSYNCED;
}

// ====================================================================================================

static void _usingAltAddrEncode( struct TRACEDecoderEngine *e, bool using )

{
    ( ( struct ETM35DecodeState * )e )->usingAltAddrEncode = using;
}

// ====================================================================================================

static void _forceSync(  struct TRACEDecoderEngine *e, bool isSynced )

{
    if ( !isSynced )
    {
        ( ( struct ETM35DecodeState * )e )->asyncCount = 0;
        ( ( struct ETM35DecodeState * )e )->rxedISYNC = false;
    }

    ( ( struct ETM35DecodeState * )e )->p = ( isSynced ) ? TRACE_IDLE : TRACE_UNSYNCED;
}

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Publicly accessible methods
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================

struct TRACEDecoderEngine *ETM35DecoderPumpCreate( void )

{

    struct TRACEDecoderEngine *e = ( struct TRACEDecoderEngine * )calloc( 1, sizeof( struct ETM35DecodeState ) );
    e->action        = _pumpAction;
    e->destroy       = _pumpDestroy;
    e->synced        = _synced;
    e->forceSync     = _forceSync;
    e->altAddrEncode = _usingAltAddrEncode;
    return e;
}

// ====================================================================================================
