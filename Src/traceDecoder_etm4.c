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
#include "msgDecoder.h"
#include "traceDecoder.h"
#include "generics.h"


/* Internal states of the protocol machine */
enum TRACE_ETM4protoState
{
    TRACE_WAIT_INFO = TRACE_GET_CONTEXTID + 1,
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
    TRACE_GET_CONTEXT_ID
};


static const char *protoStateName[] =
{
    "UNSYNCED",       "WAIT_ISYNC",        "IDLE",             "COLLECT_BA_STD",
    "COLLECT_BA_ALT", "COLLECT_EXCEPTION", "WAIT_CONTEXTBYTE", "WAIT_INFOBYTE",
    "WAIT_IADDRESS",  "WAIT_ICYCLECOUNT",  "WAIT_CYCLECOUNT",  "GET_VMID",
    "GET_TSTAMP",     "GET_CONTEXTID",

    "TRACE_WAIT_INFO",
    "TRACE_GET_INFO_PLCTL",
    "TRACE_GET_INFO_INFO",
    "TRACE_GET_INFO_KEY",
    "TRACE_GET_INFO_SPEC",
    "TRACE_GET_INFO_CYCT",
    "TRACE_EXTENSION",
    "TRACE_GET_TIMESTAMP",
    "TRACE_GET_TS_CC",
    "TRACE_COMMIT",
    "TRACE_GET_SHORT_ADDR",
    "TRACE_GET_32BIT_ADDR",
    "TRACE_GET_64BIT_ADDR",
    "TRACE_GET_CONTEXT",
    "TRACE_GET_VCONTEXT",
    "TRACE_GET_CONTEXT_ID"
};

#define COND_LOAD_TRACED  1
#define COND_STORE_TRACED 2
#define COND_ALL_TRACED   7

/* Word-aligned ARM, Halfword Aligned Thumb, Table 6-19, Pg 6-292 */
enum InstSet { IS0, IS1 };

struct
{
    uint8_t plctl;  /* Payload control - what sections are present in the INFO */
    bool cc_enabled; /* Indicates cycle counting is enabled */
    uint8_t cond_enabled; /* What conditional branching and loads/stores are traced */
    bool load_traced; /* Load instructions are traced explicitly */
    bool store_traced; /* Store instructions are traced explicitly */
    bool haveContext;  /* We have context to collect */

    uint32_t context; /* Current machine context */
    uint32_t vcontext;   /* Virtual machine context */

    uint32_t nextrhkey; /* Next rh key expected in the stream */
    uint32_t spec;      /* Max speculation depth to be expected */
    uint32_t cyct;      /* Cycnt threshold */

    bool cc_follows; /* Indication if CC follows from TS */
    uint8_t idx; /* General counter used for multi-byte payload indexing */
    uint32_t cntUpdate; /* Count construction for TS packets */
    struct
    {
        uint64_t addr;
        enum InstSet inst;
    } q[3];  /* Address queue for pushed addresses */
} j;

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Internal routines - decoder support
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================

static void _reportInfo( genericsReportCB report )

{
    return;
    report( V_INFO, EOL "Cycle counting is %senabled" EOL, j.cc_enabled ? "" : "not " );
    report( V_INFO, "Conditional loads are %straced" EOL, ( j.cond_enabled & COND_LOAD_TRACED ) ? "" : "not " );
    report( V_INFO, "Conditional stores are %straced" EOL, ( j.cond_enabled & COND_STORE_TRACED ) ? "" : "not " );
    report( V_INFO, "All conditionals are %straced" EOL, ( j.cond_enabled == COND_ALL_TRACED ) ? "" : "not " );
    report( V_INFO, "Next RH key is %d" EOL, j.nextrhkey );
    report( V_INFO, "Max speculative execution depth is %d instructions" EOL, j.spec );
    report( V_INFO, "CYCNT threshold value is %d" EOL, j.cyct );
}

static void _flushQ( void )

{
    j.q[2].addr = j.q[1].addr = j.q[0].addr = 0;
    j.q[2].inst = j.q[1].inst = j.q[0].inst = IS0;
}

static void _stackQ( void )
{
    j.q[2].addr = j.q[1].addr;
    j.q[1].addr = j.q[0].addr;
    j.q[2].inst = j.q[1].inst;
    j.q[1].inst = j.q[0].inst;
}

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Externally available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================

void ETM4DecoderPumpAction( struct TRACEDecoder *i, uint8_t c, traceDecodeCB cb, genericsReportCB report, void *d )

/* Pump next byte into the protocol decoder */

{

    enum TRACEprotoState newState = i->p;
    enum TRACEDecoderPumpEvent retVal = TRACE_EV_NONE;


    /* Perform A-Sync accumulation check ( Section 6.4.2 ) */
    if ( ( i->asyncCount == 11 ) && ( c == 0x80 ) )
    {
        if ( report )
        {
            report( V_DEBUG, "A-Sync Accumulation complete" EOL );
        }

        i->rxedISYNC = true;
        newState = TRACE_WAIT_INFO;
    }
    else
    {
        i->asyncCount = c ? 0 : i->asyncCount + 1;

        switch ( ( int )i->p )
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
                        i->cpu.eatoms = ( 0 != ( c & 1 ) );
                        i->cpu.natoms = !i->cpu.eatoms;
                        i->cpu.instCount += 1;
                        i->cpu.disposition = c & 1;
		      report( V_DEBUG,"Atom Format 1 [%b]",i->cpu.disposition);

			
			if (i->cpu.addr != ADDRESS_UNKNOWN)
			  {
			    retVal = TRACE_EV_MSG_RXED;
			    TRACEDecodeStateChange( i, EV_CH_ENATOMS );
			  }
                        break;

                    case 0b11011000 ... 0b11011011: /* Atom Format 2, Figure 6-40, Pg 6-304 */
                        i->cpu.eatoms = ( 0 != ( c & 2 ) ) + ( 0 != ( c & 1 ) );
                        i->cpu.natoms = 2 - i->cpu.eatoms;
                        i->cpu.instCount += 2;

                        /* Put a 1 in each element of disposition if was executed */
                        i->cpu.disposition = c & 3;
			report( V_DEBUG,"Atom Format 2 [%02b]",i->cpu.disposition);
			if (i->cpu.addr != ADDRESS_UNKNOWN)
			  {
			    retVal = TRACE_EV_MSG_RXED;
			    TRACEDecodeStateChange( i, EV_CH_ENATOMS );
			  }
                        break;

                    case 0b11111000 ... 0b11111111: /* Atom Format 3, Figure 6-41, Pg 6-305 */
                        i->cpu.eatoms = ( 0 != ( c & 1 ) ) + ( 0 != ( c & 2 ) ) + ( 0 != ( c & 4 ) );
                        i->cpu.natoms = 3 - i->cpu.eatoms;
                        i->cpu.instCount += 3;

                        /* Put a 1 in each element of disposition if was executed */
                        i->cpu.disposition = c & 7;
			report( V_DEBUG,"Atom Format 3 [%03b]",i->cpu.disposition);
			if (i->cpu.addr != ADDRESS_UNKNOWN)
			  {
			    retVal = TRACE_EV_MSG_RXED;
			    TRACEDecodeStateChange( i, EV_CH_ENATOMS );
			  }
                        break;

                    case 0b11011100 ... 0b11011111: /* Atom Format 4, Figure 6-42, Pg 6-305 */
                        switch ( c & 3 )
                        {
                            case 0b00:
                                i->cpu.natoms = 1;
                                i->cpu.disposition = 0b1110;
                                break;

                            case 0b01:
                                i->cpu.natoms = 4;
                                i->cpu.disposition = 0b0000;
                                break;

                            case 0b10:
                                i->cpu.natoms = 2;
                                i->cpu.disposition = 0b1010;
                                break;

                            case 0b11:
                                i->cpu.natoms = 2;
                                i->cpu.disposition = 0b0101;
                                break;
                        }

                        i->cpu.eatoms = 4 - i->cpu.natoms;
                        i->cpu.instCount += 4;
			report( V_DEBUG,"Atom Format 4 [%04b]",i->cpu.disposition);
			if (i->cpu.addr != ADDRESS_UNKNOWN)
			  {
			    retVal = TRACE_EV_MSG_RXED;
			    TRACEDecodeStateChange( i, EV_CH_ENATOMS );
			  }
                        break;


                    case 0b11010101:
                    case 0b11010110:
                    case 0b11010111:
                    case 0b11110101: /* Atom format 5, Figure 6-43, Pg 6-306 ... use bits 5, 1 and 0 */
                        switch ( ( ( 0 != ( c & ( 1 << 5 ) ) ) << 2 ) | ( ( 0 != ( c & ( 1 << 1 ) ) ) << 1 ) | ( ( 0 != ( c & ( 1 << 0 ) ) ) << 0 ) )
                        {
                            case 0b101:
                                i->cpu.natoms = 1;
                                i->cpu.disposition = 0b11110;
                                break;

                            case 0b001:
                                i->cpu.natoms = 5;
                                i->cpu.disposition = 0b00000;
                                break;

                            case 0b010:
                                i->cpu.natoms = 3;
                                i->cpu.disposition = 0b01010;
                                break;

                            case 0b011:
                                i->cpu.natoms = 2;
                                i->cpu.disposition = 0b10101;
                                break;

                            default:
                                report( V_ERROR, "Illegal value for Atom type 5 (0x%02x)" EOL, c );
                                break;
                        }

                        i->cpu.eatoms = 5 - i->cpu.natoms;
                        i->cpu.instCount += 5;
			report( V_DEBUG,"Atom Format 5 [%05b]",i->cpu.disposition);
			if (i->cpu.addr != ADDRESS_UNKNOWN)
			  {
			    retVal = TRACE_EV_MSG_RXED;
			    TRACEDecodeStateChange( i, EV_CH_ENATOMS );
			  }
                        break;

                    case 0b11000000 ... 0b11010100:
                    case 0b11100000 ... 0b11110100: /* Atom format 6, Figure 6-44, Pg 6.307 */
                        i->cpu.eatoms = ( c & 0x1f ) + 3;
                        i->cpu.instCount = i->cpu.eatoms;
                        i->cpu.disposition = ( 1 << ( i->cpu.eatoms + 1 ) ) - 1;

                        if ( c & ( 1 << 5 ) )
                        {
                            i->cpu.disposition &= 0xfffffffe;
                            i->cpu.eatoms--;
                            i->cpu.natoms = 1;
                        }
                        else
                        {
                            i->cpu.natoms = 0;
                        }

			char construct[30];
			sprintf(construct,"Atom Format 6 [%%ld %%%ldb]",i->cpu.instCount);
			report( V_DEBUG,construct,i->cpu.instCount,i->cpu.disposition);
			
			if (i->cpu.addr != ADDRESS_UNKNOWN)
			  {
			    retVal = TRACE_EV_MSG_RXED;
			    TRACEDecodeStateChange( i, EV_CH_ENATOMS );
			  }
                        break;


                    case 0b10100000 ... 0b10101111: /* Q instruction trace packet, Figure 6-45, Pg 6-308 */

                        break;

                    case 0b01110001 ... 0b01111111: /* Event tracing, Figure 6-31, Pg 6-289 */
                        if ( c & 0b0001 )
                        {
                            TRACEDecodeStateChange( i, EV_CH_EVENT0 );
                        }

                        if ( c & 0b0010 )
                        {
                            TRACEDecodeStateChange( i, EV_CH_EVENT1 );
                        }

                        if ( c & 0b0100 )
                        {
                            TRACEDecodeStateChange( i, EV_CH_EVENT2 );
                        }

                        if ( c & 0b1000 )
                        {
                            TRACEDecodeStateChange( i, EV_CH_EVENT3 );
                        }

                        break;

                    case 0b00000100: /* Trace On, Figure 6.3, Pg 6-261 */
                        retVal = TRACE_EV_MSG_RXED;
                        TRACEDecodeStateChange( i, EV_CH_TRACESTART );
                        break;

                    case 0b10010000 ... 0b10010011: /* Exact Match Address */
                        int match = c & 0x03;
                        assert( c != 3 ); /* This value is reserved */
                        _stackQ();
                        i->cpu.addr = j.q[match].addr;
                        retVal = TRACE_EV_MSG_RXED;
                        TRACEDecodeStateChange( i, EV_CH_ADDRESS );
                        break;

                    case 0b10010101: /* Short address, IS0 short, Figure 6-32, Pg 6-294 */
                        j.idx = 2;
                        _stackQ();
                        j.q[0].addr &= 0xFC;
                        newState = TRACE_GET_SHORT_ADDR;
                        break;

                    case 0b10010110: /* Short address, IS1 short, Figure 6-32, Pg 6-294 */
                        j.idx = 1;
                        _stackQ();
                        j.q[0].addr &= 0xFE;
                        newState = TRACE_GET_SHORT_ADDR;
                        break;

                    case 0b10011010: /* Long address, 32 bit, IS0, Figure 6.33 Pg 6-295 */
                        j.idx = 2;
                        j.haveContext = false;
                        _stackQ();
                        j.q[0].inst = IS0;
                        j.q[0].addr &= 0xFFFFFFFC;
                        newState = TRACE_GET_32BIT_ADDR;
                        break;

                    case 0b10011011: /* Long address, 32 bit, IS1, Figure 6.33 Pg 6-295 */
                        j.idx = 1;
                        j.haveContext = false;
                        _stackQ();
                        j.q[0].inst = IS1;
                        j.q[0].addr &= 0xFFFFFFFE;
                        newState = TRACE_GET_32BIT_ADDR;
                        break;

                    case 0b10011101: /* Long address, 64 bit, IS0, Figure 6.34 Pg 6-295 */
                        j.idx = 2;
                        j.haveContext = false;
                        _stackQ();
                        j.q[0].inst = IS0;
                        j.q[0].addr &= 0xFFFFFFFC;
                        newState = TRACE_GET_64BIT_ADDR;
                        break;


                    case 0b10011110: /* Long address, 64 bit, IS0, Figure 6.34 Pg 6-295 */
                        j.idx = 1;
                        _stackQ();
                        j.haveContext = false;
                        j.q[0].inst = IS1;
                        j.q[0].addr &= 0xFFFFFFFC;
                        newState = TRACE_GET_64BIT_ADDR;
                        break;

                    case 0b10000000: /* Context element with no payload, Figure 6-36, Pg 6-297 */
                        /* Context is same as prevously, nothing to report */
                        break;

                    case 0b10000001: /* Context element with payload, Figure 6-36, Pg 6-297*/
                        newState = TRACE_GET_CONTEXT;
                        break;

                    case 0b10000010: /* Long address, 32 bit, IS0, Figure 6-37 case 1, Pg 6-299 */
                        j.haveContext = true;
                        j.idx = 2;
                        _stackQ();
                        j.q[0].inst = IS0;
                        j.q[0].addr &= 0xFFFFFFFC;
                        newState = TRACE_GET_32BIT_ADDR;
                        break;


                    case 0b10000011: /* Long address, 32 bit, IS1, Figure 6-37 case 2, Pg 6-299 */
                        j.idx = 1;
                        j.haveContext = true;
                        _stackQ();
                        j.q[0].inst = IS1;
                        j.q[0].addr &= 0xFFFFFFFE;
                        newState = TRACE_GET_32BIT_ADDR;
                        break;

                    case 0b10000101: /* Long address, 64 bit, IS0, Figure 6-38 case 1, Pg 6-300 */
                        j.idx = 2;
                        j.haveContext = true;
                        _stackQ();
                        j.q[0].inst = IS0;
                        j.q[0].addr &= 0xFFFFFFFC;
                        newState = TRACE_GET_64BIT_ADDR;
                        break;


                    case 0b10000110: /* Long address, 64 bit, IS1, Figure 6-38 case 2, Pg 6-300 */
                        j.idx = 1;
                        _stackQ();
                        j.haveContext = true;
                        j.q[0].inst = IS1;
                        j.q[0].addr &= 0xFFFFFFFC;
                        newState = TRACE_GET_64BIT_ADDR;
                        break;

                    case 0b00000000:
                        newState = TRACE_EXTENSION;
                        break;

                    case 0b00001000: /* Resynchronisation, Figure 6-6, Pg 6-263 */
                        i->rxedISYNC = false;
                        newState = TRACE_UNSYNCED;
                        break;

                    case 0b00000010 ... 0b00000011: /* Timestamp, Figure 6-7, Pg 6-264 */
                        newState = TRACE_GET_TIMESTAMP;
                        j.cc_follows = (0 != ( c & 1 ));
			if (!j.cc_follows)
			  {
			    i->cpu.cycleCount = COUNT_UNKNOWN;
			  }
                        j.idx = 0;
                        break;

                    case 0b10001000: /* Timestamp marker element, Figure 6-8, Pg 6-265 */
                        break;

                    case 0b00000101: /* Function return element, Figure 6-9, Pg 6-265 */
                        retVal = TRACE_EV_MSG_RXED;
                        TRACEDecodeStateChange( i, EV_CH_FNRETURN );
                        break;

                    case 0b00000111: /* Exception Return element, Figure 6-11, Pg 6-271 */
                        retVal = TRACE_EV_MSG_RXED;
                        TRACEDecodeStateChange( i, EV_CH_EXRETURN );
                        break;

                    case 0b00100000 ... 0b00100111: /* Data sync mark, Figure 6-15, Pg 6-275 */
                        i->cpu.dsync_mark = c & 0x07;
                        retVal = TRACE_EV_MSG_RXED;
                        TRACEDecodeStateChange( i, EV_CH_DATASYNC );
                        break;

                    case 0b00101000 ... 0b00101100: /* Unnumbered data sync mark, Figure 6-16, Pg 6-275 */
                        i->cpu.udsync_mark = c & 0x07;
                        retVal = TRACE_EV_MSG_RXED;
                        TRACEDecodeStateChange( i, EV_CH_UDATASYNC );
                        break;

                    case 0b00110000 ... 0b00110011: /* Mispredict instruction, Figure 6-21, Pg 6-279 */
                        break;

                    case 0b00101101: /* Commit instruction trace packet, Figure 6-17, Pg 6-277 */
                        newState = TRACE_COMMIT;
                        break;

                    default:
                        report( V_ERROR, "Unknown element %02x in TRACE_IDLE" EOL, c );
                        break;
                }

                break;

            // -----------------------------------------------------
            case TRACE_GET_CONTEXT: /* Get context information byte, Figure 6-36, Pg 6-297 */
                i->cpu.exceptionLevel = c & 3;
                i->cpu.am64bit = ( 0 != ( c & ( 1 << 4 ) ) );
                i->cpu.amSecure = ( 0 == ( c & ( 1 << 5 ) ) );
                j.haveContext = ( 0 != ( c & ( 1 << 7 ) ) );

                if ( c & ( 1 << 6 ) )
                {
                    j.vcontext = 0;
                    j.idx = 0;
                    newState = TRACE_GET_VCONTEXT;
                }
                else if ( j.haveContext )
                {
                    j.context = 0;
                    j.idx = 0;
                    newState = TRACE_GET_CONTEXT_ID;
                }
                else
                {
                    retVal = TRACE_EV_MSG_RXED;
                    TRACEDecodeStateChange( i, EV_CH_CONTEXTID );
                    newState = TRACE_IDLE;
                }

                break;

            // -----------------------------------------------------

            case TRACE_GET_VCONTEXT:
                j.vcontext |= c << j.idx;
                j.idx += 8;

                if ( ( j.idx == 32 ) && ( !j.haveContext ) )
                {
                    i->cpu.vmid = j.vcontext;
                    retVal = TRACE_EV_MSG_RXED;
                    TRACEDecodeStateChange( i, EV_CH_VMID );
                    newState = TRACE_IDLE;
                }
                else
                {
                    j.context = 0;
                    j.idx = 0;
                    newState = TRACE_GET_CONTEXT_ID;
                }

                break;

            // -----------------------------------------------------

            case TRACE_GET_CONTEXT_ID:
                j.context |= c << j.idx;
                j.idx += 8;

                if ( j.idx == 32 )
                {
                    i->cpu.contextID = j.vcontext;
                    retVal = TRACE_EV_MSG_RXED;
                    TRACEDecodeStateChange( i, EV_CH_CONTEXTID );
                    newState = TRACE_IDLE;
                }

                break;

            // -----------------------------------------------------
            case TRACE_GET_SHORT_ADDR: /* Short format address for IS0 or IS1, offset set by idx. Figure 6-32, Pg 6-294 */
                if ( j.q[0].inst == IS0 )
                {
                    /* First byte of received data */
                    j.q[0].addr = ( j.q[0].addr & ( 0x7F << j.idx ) ) | ( ( c & 0x7f ) << ( j.idx ) );
                    j.idx += 7;
                }
                else
                {
                    j.q[0].addr = ( j.q[0].addr & ( 0xFF << j.idx ) ) | ( ( c & 0xFf ) << ( j.idx ) );
                }

                if ( ( c & 0x80 ) || ( j.idx > 7 ) )
                {
                    i->cpu.addr = j.q[0].addr;
                    retVal = TRACE_EV_MSG_RXED;
                    TRACEDecodeStateChange( i, EV_CH_ADDRESS );
                    newState = TRACE_IDLE;
                }

                break;

            // -----------------------------------------------------
            case TRACE_GET_32BIT_ADDR: /* Long format 32 bit address for IS0 or IS1, offset set by idx. Figure 6-33, Pg 6-295 and Figure 6-37, Pg 6-299 */
                if ( j.idx < 3 )
                {
                    /* First byte of received data */
                    j.q[0].addr = ( j.q[0].addr & ( ~( 0x7F << j.idx ) ) ) | ( ( c & 0x7f ) << ( j.idx ) );
                    j.idx += 7;
                }
                else
                {
                    if ( j.idx == 8 )
                    {
                        /* Second byte of IS1 case - mask MSB */
                        j.q[0].addr = ( j.q[0].addr & ( ~( 0x7F << j.idx ) ) ) | ( ( c & 0x7f ) << ( j.idx ) );
                        j.idx = 16;
                    }
                    else
                    {
                        j.q[0].addr = ( j.q[0].addr & ( ~( 0xFF << j.idx ) ) ) | ( ( c & 0xFf ) << ( j.idx ) );
                        j.idx += 8;
                    }
                }

                if ( j.idx == 32 )
                {
                    i->cpu.addr = j.q[0].addr;
                    TRACEDecodeStateChange( i, EV_CH_ADDRESS );

                    if ( j.haveContext )
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
                if ( j.idx < 3 )
                {
                    /* First byte of received data */
                    j.q[0].addr = ( j.q[0].addr & ( 0x7F << j.idx ) ) | ( ( c & 0x7f ) << ( j.idx ) );
                    j.idx += 7;
                }
                else
                {
                    if ( ( j.q[0].inst == IS1 ) && ( j.idx == 8 ) )
                    {
                        /* Second byte of IS1 case - mask MSB */
                        j.q[0].addr = ( j.q[0].addr & ( 0x7F << j.idx ) ) | ( ( c & 0x7f ) << ( j.idx ) );
                        j.idx = 16;
                    }
                    else
                    {
                        j.q[0].addr = ( j.q[0].addr & ( 0xFF << j.idx ) ) | ( ( c & 0xFf ) << ( j.idx ) );
                        j.idx += 8;
                    }
                }

                if ( j.idx == 64 )
                {
                    i->cpu.addr = j.q[0].addr;
                    TRACEDecodeStateChange( i, EV_CH_ADDRESS );

                    if ( j.haveContext )
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
                if ( j.idx < 56 )
                {
		  i->cpu.ts = ( i->cpu.ts & ( 0x7F << j.idx ) ) | ( ( c & 0x7f ) << j.idx );
                }
                else
                {
		  i->cpu.ts = ( i->cpu.ts & ( 0xFF << j.idx ) ) | ( ( c & 0xff ) << j.idx );
                }

                j.idx+=7;

                if ( ( !( c & 0x80 ) ) || ( j.idx == 63 ) )
                {
                    TRACEDecodeStateChange( i, EV_CH_TSTAMP );

                    if ( j.cc_enabled )
                    {
                        if ( j.cc_follows )
                        {
                            j.idx = 0;
                            j.cntUpdate = 0;
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
                if ( j.idx < 2 )
                {
                    j.cntUpdate |= ( c & 0x7f ) << ( j.idx * 7 );
                }
                else
                {
                    j.cntUpdate |= ( c & 0x7f ) << ( j.idx * 7 );
                }

                j.idx++;

                if ( ( j.idx == 3 ) || ( c & 0x80 ) )
                {

                    i->cpu.cycleCount += j.cntUpdate;
                    retVal = TRACE_EV_MSG_RXED;
                    TRACEDecodeStateChange( i, EV_CH_CYCLECOUNT );
                    newState = TRACE_IDLE;
                }

                break;

            // -----------------------------------------------------
            case TRACE_EXTENSION:
                switch ( c )
                {
                    case 0b00000011: /* Discard packet, Figure 6.4, Pg 6-262 */
                        TRACEDecodeStateChange( i, EV_CH_DISCARD );
                        TRACEDecodeStateChange( i, EV_CH_TRACESTOP );
                        break;

                    case 0b00000101: /* Overflow packet, Figure 6.5, Pg. 6-263 */
                        TRACEDecodeStateChange( i, EV_CH_OVERFLOW );
                        break;

                    case 0b00000111: /* Branch future flush */
                        break;

                    default:
                        report( V_ERROR, "Reserved extension packet" EOL );
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
                j.plctl = c;
                j.nextrhkey = 0;
                _flushQ(); /* Reset the address stack too ( Section 6.4.12, Pg 6-291 ) */

                if ( j.plctl & ( 1 << 0 ) )
                {
                    newState = TRACE_GET_INFO_INFO;
                }
                else if ( j.plctl & ( 1 << 1 ) )
                {
                    newState = TRACE_GET_INFO_KEY;
                }
                else if ( j.plctl & ( 1 << 2 ) )
                {
                    newState = TRACE_GET_INFO_SPEC;
                }
                else if ( j.plctl & ( 1 << 3 ) )
                {
                    newState = TRACE_GET_INFO_CYCT;
                }
                else
                {
                    _reportInfo( report );
                    retVal = TRACE_EV_MSG_RXED;
                    newState = TRACE_IDLE;
                }

                break;

            case TRACE_GET_INFO_INFO:
                j.cc_enabled   = ( 0 != ( c & ( 1 << 0 ) ) );
                j.cond_enabled = ( c << 1 ) & 7;
                j.load_traced  = ( 0 != ( c & ( 1 << 4 ) ) );
                j.store_traced = ( 0 != ( c & ( 1 << 5 ) ) );

                if ( j.plctl & ( 1 << 1 ) )
                {
                    newState = TRACE_GET_INFO_KEY;
                }
                else if ( j.plctl & ( 1 << 2 ) )
                {
                    newState = TRACE_GET_INFO_SPEC;
                }
                else if ( j.plctl & ( 1 << 3 ) )
                {
                    newState = TRACE_GET_INFO_CYCT;
                }
                else
                {
                    _reportInfo( report );
                    retVal = TRACE_EV_MSG_RXED;
                    newState = TRACE_IDLE;
                }

                break;

            case TRACE_GET_INFO_KEY:
                j.nextrhkey = c;  // FIXME: Greater than 256 keys???

                if ( j.plctl & ( 1 << 2 ) )
                {
                    newState = TRACE_GET_INFO_SPEC;
                }
                else if ( j.plctl & ( 1 << 3 ) )
                {
                    newState = TRACE_GET_INFO_CYCT;
                }
                else
                {
                    _reportInfo( report );
                    retVal = TRACE_EV_MSG_RXED;
                    newState = TRACE_IDLE;
                }

                break;

            case TRACE_GET_INFO_SPEC:
                j.spec = c;

                if ( j.plctl & ( 1 << 3 ) )
                {
                    _reportInfo( report );
                    newState = TRACE_GET_INFO_CYCT;
                }
                else
                {
                    _reportInfo( report );
                    retVal = TRACE_EV_MSG_RXED;
                    newState = TRACE_IDLE;
                }

                break;

            // -----------------------------------------------------

            default:
                report( V_WARN, "Case %d not handled in switch" EOL, i->p );
                break;
        }
    }

    if ( i->p != TRACE_UNSYNCED )
    {
        if ( report ) report( V_DEBUG, "%02x:%s --> %s (%s:%d)", c, ( i->p == TRACE_IDLE ) ? protoStateName[i->p] : "", protoStateName[newState],
                                  ( ( newState == TRACE_IDLE ) ? ( ( retVal == TRACE_EV_NONE ) ? "DROPPED" : "OK" ) : "-" ), retVal );

        if ( newState == TRACE_IDLE )
        {
            report( V_DEBUG, "\r\n" );
        }
    }

    i->p = newState;

    if ( ( retVal != TRACE_EV_NONE ) && ( i->rxedISYNC ) )
    {
        cb( d );
    }
}
// ====================================================================================================
