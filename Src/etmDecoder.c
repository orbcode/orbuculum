/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * ETM Decoder Module
 * ==================
 *
 * Implementation of ITM/DWT decode according to the specification in Appendix D4
 * of the ARMv7-M Architecture Refrence Manual document available
 * from https://static.docs.arm.com/ddi0403/e/DDI0403E_B_armv7m_arm.pdf
 */

#include <string.h>
#include "etmDecoder.h"
#include "msgDecoder.h"

// Define this to get transitions printed out
#ifdef DEBUG
    #include <stdio.h>
    #include "generics.h"
#else
    #define genericsReport(x...)
#endif

/* Events from the process of pumping bytes through the ETM decoder */
enum ETMDecoderPumpEvent
{
    ETM_EV_NONE,
    ETM_EV_UNSYNCED,
    ETM_EV_SYNCED,
    ETM_EV_ERROR,
    ETM_EV_MSG_RXED
};

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Internal routines - the decoder itself
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
#ifdef DEBUG
static char *_protoNames[] = {ETM_PROTO_NAME_LIST};
#endif

static void _ETMDecoderPumpAction( struct ETMDecoder *i, uint8_t c, etmDecodeCB cb, void *d )

/* Pump next byte into the protocol decoder */

{
    bool C;                               /* Is address packet continued? */
    bool X = false;                       /* Is there exception information following address */
    int8_t ofs;                           /* Offset for bits in address calculation */
    uint8_t mask;                         /* Mask for bits in address calculation */

    enum ETMprotoState newState = i->p;
    struct ETMCPUState *cpu = &i->cpu;
    enum ETMDecoderPumpEvent retVal = ETM_EV_NONE;


    /* Perform A-Sync accumulation check */
    if ( ( i->asyncCount >= 5 ) && ( c == 0x80 ) )
    {
        newState = ETM_IDLE;
    }
    else
    {
        i->asyncCount = c ? 0 : i->asyncCount + 1;

        switch ( i->p )
        {
            // -----------------------------------------------------
            case ETM_UNSYNCED:
                break;

            // -----------------------------------------------------
            case ETM_IDLE:
                /* Start off by making sure entire received packet changes are 0'ed */
                cpu->changeRecord = 0;

                // *************************************************
                // ************** BRANCH PACKET ********************
                // *************************************************
                if ( c & 0b1 )
                {
                    genericsReport( V_DEBUG, "BRANCH " );

                    /* The lowest order 6 bits of address info... */
                    switch ( cpu->addrMode )
                    {
                        case ETM_ADDRMODE_ARM:
                            i->addrConstruct = ( i->addrConstruct & ~( 0b11111100 ) ) | ( ( c & 0b01111110 ) << 1 );
                            break;

                        case ETM_ADDRMODE_THUMB:
                            i->addrConstruct = ( i->addrConstruct & ~( 0b01111111 ) ) | ( c & 0b01111110 );
                            break;

                        case ETM_ADDRMODE_JAZELLE:
                            i->addrConstruct = ( i->addrConstruct & ~( 0b00111111 ) ) | ( ( c & 0b01111110 ) >> 1 );
                            break;
                    }

                    i->byteCount = 1;
                    C = c & 0x80;
                    X = false;
                    cpu->changeRecord |= ( 1 << EV_CH_ADDRESS );

                    newState = ( i->usingAltAddrEncode ) ? ETM_COLLECT_BA_ALT_FORMAT : ETM_COLLECT_BA_STD_FORMAT;
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
                    genericsReport( V_DEBUG, "CYCCNT " EOL );
                    i->byteCount = 0;
                    i->cycleConstruct = 0;
                    newState = ETM_GET_CYCLECOUNT;
                    break;
                }

                // *************************************************
                // ************** ISYNC PACKETS ********************
                // *************************************************
                if ( c == 0b00001000 ) /* Normal ISYNC */
                {
                    genericsReport( V_DEBUG, "Normal ISYNC " EOL );
                    /* Collect either the context or the Info Byte next */
                    i->byteCount = 0;
                    i->contextConstruct = 0;
                    newState = i->contextBytes ? ETM_GET_CONTEXTBYTE : ETM_GET_INFOBYTE;

                    /* We won't start reporting data until a valid ISYNC has been received */
                    if ( !i->rxedISYNC )
                    {
                        retVal = ETM_EV_SYNCED;
                        i->rxedISYNC = true;
                    }

                    break;
                }

                if ( c == 0b01110000 ) /* ISYNC with Cycle Count */
                {
                    genericsReport( V_DEBUG, "ISYNC+CYCCNT " EOL );
                    /* Collect the cycle count next */
                    i->byteCount = 0;
                    i->cycleConstruct = 0;
                    newState = ETM_GET_ICYCLECOUNT;
                    break;
                }

                // *************************************************
                // ************** TRIGGER PACKET *******************
                // *************************************************
                if ( c == 0b00001100 )
                {
                    genericsReport( V_DEBUG, "TRIGGER " EOL );
                    cpu->changeRecord |= ( 1 << EV_CH_TRIGGER );
                    retVal = ETM_EV_MSG_RXED;
                    break;
                }

                // *************************************************
                // **************** VMID PACKET ********************
                // *************************************************
                if ( c == 0b00111100 )
                {
                    genericsReport( V_DEBUG, "VMID " EOL );
                    newState = ETM_GET_VMID;
                    break;
                }

                // *************************************************
                // *********** TIMESTAMP PACKET ********************
                // *************************************************
                if ( ( c & 0b11111011 ) == 0b01000010 )
                {
                    genericsReport( V_DEBUG, "TS " EOL );
                    newState = ETM_GET_TSTAMP;

                    if ( ( c & ( 1 << 2 ) ) != 0 )
                    {
                        cpu->changeRecord |= ( 1 << EV_CH_CLOCKSPEED );
                    }

                    i->byteCount = 0;
                    break;
                }

                // *************************************************
                // ************** IGNORE PACKET ********************
                // *************************************************
                if ( c == 0b01100110 )
                {
                    break;
                }

                // *************************************************
                // ************ CONTEXTID PACKET *******************
                // *************************************************
                if ( c == 0b01101110 )
                {
                    genericsReport( V_DEBUG, "CONTEXTID " EOL );
                    newState = ETM_GET_CONTEXTID;
                    cpu->contextID = 0;
                    i->byteCount = 0;
                    break;
                }

                // *************************************************
                // ******** EXCEPTION EXIT PACKET ******************
                // *************************************************
                if ( c == 0b01110110 )
                {
                    genericsReport( V_DEBUG, "EXCEPT-EXIT " EOL );
                    cpu->changeRecord |= ( 1 << EV_CH_EX_EXIT );
                    retVal = ETM_EV_MSG_RXED;
                    break;
                }

                // *************************************************
                // ******** EXCEPTION ENTRY PACKET *****************
                // *************************************************
                if ( c == 0b01111110 )
                {
                    /* Note this is only used on CPUs with data tracing */
                    genericsReport( V_DEBUG, "EXCEPT-ENTRY " EOL );
                    cpu->changeRecord |= ( 1 << EV_CH_EX_ENTRY );
                    retVal = ETM_EV_MSG_RXED;
                    break;
                }

                // *************************************************
                // ************** P-HEADER PACKET ******************
                // *************************************************
                if ( ( c & 0b10000001 ) == 0b10000000 )
                {
                    genericsReport( V_DEBUG, "P " EOL );

                    if ( !i->cycleAccurate )
                    {
                        if ( ( c & 0b10000011 ) == 0b10000000 )
                        {
                            /* Format-1 P-header */
                            cpu->eatoms = ( c & 0x3C ) >> 2;
                            cpu->natoms = ( c & ( 1 << 6 ) ) ? 1 : 0;
                            cpu->instCount += cpu->eatoms + cpu->natoms;

                            /* Put a 1 in each element of disposition if was executed */
                            cpu->disposition = ( 1 << cpu->eatoms ) - 1;
                            cpu->changeRecord |= ( 1 << EV_CH_ENATOMS );
                            retVal = ETM_EV_MSG_RXED;
                            break;
                        }

                        if ( ( c & 0b11110011 ) == 0b10000010 )
                        {
                            /* Format-2 P-header */
                            cpu->eatoms = ( ( c & ( 1 << 2 ) ) == 0 ) + ( ( c & ( 1 << 3 ) ) == 0 );
                            cpu->natoms = 2 - cpu->eatoms;

                            cpu->disposition = ( ( c & ( 1 << 3 ) ) == 0 ) |
                                               ( ( ( c & ( 1 << 2 ) ) == 0 ) << 1 );

                            cpu->changeRecord |= ( 1 << EV_CH_ENATOMS );
                            cpu->instCount += cpu->eatoms + cpu->natoms;
                            retVal = ETM_EV_MSG_RXED;
                            break;
                        }

                        genericsReport( V_ERROR, "Unprocessed P-Header (%02X)" EOL, c );
                    }
                    else
                    {
                        if ( c == 0b10000000 )
                        {
                            /* Format 0 cycle-accurate P-header */
                            cpu->watoms = 1;
                            cpu->instCount += cpu->watoms;
                            cpu->eatoms = cpu->natoms = 0;
                            cpu->changeRecord |= ( 1 << EV_CH_ENATOMS );
                            cpu->changeRecord |= ( 1 << EV_CH_WATOMS );
                            retVal = ETM_EV_MSG_RXED;
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
                            cpu->changeRecord |= ( 1 << EV_CH_ENATOMS );
                            cpu->changeRecord |= ( 1 << EV_CH_WATOMS );
                            retVal = ETM_EV_MSG_RXED;
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
                            cpu->changeRecord |= ( 1 << EV_CH_ENATOMS );
                            cpu->changeRecord |= ( 1 << EV_CH_WATOMS );
                            retVal = ETM_EV_MSG_RXED;
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
                            cpu->changeRecord |= ( 1 << EV_CH_ENATOMS );
                            cpu->changeRecord |= ( 1 << EV_CH_WATOMS );
                            retVal = ETM_EV_MSG_RXED;
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
                            cpu->changeRecord |= ( 1 << EV_CH_ENATOMS );
                            cpu->changeRecord |= ( 1 << EV_CH_WATOMS );
                            retVal = ETM_EV_MSG_RXED;
                            break;
                        }

                        genericsReport( V_ERROR, "Unprocessed Cycle-accurate P-Header (%02X)" EOL, c );
                    }

                    break;
                }

                break;


            // -----------------------------------------------------
            // ADDRESS COLLECTION RELATED ACTIVITIES
            // -----------------------------------------------------

            case ETM_COLLECT_BA_ALT_FORMAT: /* Collecting a branch address, alt format */
                C = c & 0x80;
                /* This is a proper mess. Mask and collect bits according to address mode in use and */
                /* if it's the last byte of the sequence */

                mask = C ? 0x7f : 0x3f;
                ofs = ( cpu->addrMode == ETM_ADDRMODE_ARM ) ? 1 : ( cpu->addrMode == ETM_ADDRMODE_THUMB ) ? 0 : -1;

                i->addrConstruct = ( i->addrConstruct &   ~( mask << ( 7 * i->byteCount + ofs ) ) )
                                   | ( ( c & mask ) << ( 7 * i->byteCount + ofs ) );
                /* There is exception information only if no continuation and bit 6 set */
                X = ( ( !C ) && ( c & 0x40 ) );
                i->byteCount++;
                goto terminateAddrByte;

            // -----------------------------------------------------

            case ETM_COLLECT_BA_STD_FORMAT: /* Collecting a branch address, standard format */
                /* This will potentially collect too many bits, but that is OK */
                ofs = ( cpu->addrMode == ETM_ADDRMODE_ARM ) ? 1 : ( cpu->addrMode == ETM_ADDRMODE_THUMB ) ? 0 : -1;
                i->addrConstruct = ( i->addrConstruct &  ~( 0x7F << ( ( 7 * i->byteCount ) + ofs ) ) ) | ( c & ( 0x7F <<  ( ( 7 * i->byteCount ) + ofs ) ) );
                i->byteCount++;
                C = ( i->byteCount < 5 ) ? c & 0x80 : c & 0x40;
                X = ( i->byteCount == 5 ) && C;
                goto terminateAddrByte;

            terminateAddrByte:

                /* Check to see if this packet is complete, and encode to return if so */
                if ( ( !C ) || ( i->byteCount == 5 ) )
                {
                    cpu->addr = i->addrConstruct;

                    if ( ( i->byteCount == 5 ) && ( cpu->addrMode == ETM_ADDRMODE_ARM ) && C )
                    {
                        /* There is (legacy) exception information in here */
                        cpu->exception = ( c >> 4 ) & 0x07;
                        cpu->changeRecord |= ( 1 << EV_CH_EXCEPTION );
                        cpu->changeRecord |= ( ( c & 0x40 ) != 0 ) ? ( 1 << EV_CH_CANCELLED ) : 0;
                        newState = ETM_IDLE;
                        retVal = ETM_EV_MSG_RXED;
                        break;
                    }

                    if ( ( !C ) & ( !X ) )
                    {
                        /* This packet is complete, so can return it */
                        newState = ETM_IDLE;
                        retVal = ETM_EV_MSG_RXED;
                    }
                    else
                    {
                        /* This packet also contains exception information, so collect it */
                        i->byteCount = 0; /* Used as a flag of which byte of exception we're collecting */
                        cpu->resume = 0;
                        cpu->changeRecord |= ( 1 << EV_CH_EX_ENTRY );
                        newState = ETM_COLLECT_EXCEPTION;
                    }
                }

                break;

            // -----------------------------------------------------

            case ETM_COLLECT_EXCEPTION: /* Collecting exception information */
                if ( i->byteCount == 0 )
                {
                    if ( ( ( c & ( 1 << 0 ) ) != 0 ) != cpu->nonSecure )
                    {
                        cpu->nonSecure = ( ( c & ( 1 << 0 ) ) != 0 );
                        cpu->changeRecord |= ( 1 << EV_CH_SECURE );
                    }

                    cpu->exception = ( c >> 1 ) & 0x0f;
                    cpu->changeRecord |= ( ( c & ( 1 << 5 ) ) != 0 ) ? ( 1 << EV_CH_CANCELLED ) : 0;

                    if ( cpu->altISA != ( ( c & ( 1 << 6 ) ) != 0 ) )
                    {
                        cpu->altISA = ( ( c & ( 1 << 6 ) ) != 0 );
                        cpu->changeRecord |= ( 1 << EV_CH_ALTISA );
                    }

                    if ( c & 0x80 )
                    {
                        i->byteCount++;
                    }
                    else
                    {
                        newState = ETM_IDLE;
                        retVal = ETM_EV_MSG_RXED;
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
                            cpu->changeRecord |= ( 1 << EV_CH_HYP );
                        }

                        if ( !( c & 0x40 ) )
                        {
                            /* There will not be another one along, return idle */
                            newState = ETM_IDLE;
                            retVal = ETM_EV_MSG_RXED;
                        }
                    }
                    else
                    {
                        /* This is exception byte 2 */
                        cpu->resume = ( c & 0xf );

                        if ( cpu->resume )
                        {
                            cpu->changeRecord |= ( 1 << EV_CH_RESUME );
                        }

                        /* Exception byte 2 is always the last one, return */

                        newState = ETM_IDLE;
                        retVal = ETM_EV_MSG_RXED;
                    }
                }

                break;


            // -----------------------------------------------------
            // VMID RELATED ACTIVITIES
            // -----------------------------------------------------
            case ETM_GET_VMID: /* Collecting virtual machine ID */
                if ( cpu->vmid != c )
                {
                    cpu->vmid = c;
                    cpu->changeRecord |= ( 1 << EV_CH_VMID );
                }

                newState = ETM_IDLE;
                retVal = ETM_EV_MSG_RXED;
                break;

            // -----------------------------------------------------
            // TIMESTAMP RELATED ACTIVITIES
            // -----------------------------------------------------

            case ETM_GET_TSTAMP: /* Collecting current timestamp */
                if ( i->byteCount < 8 )
                {
                    i->tsConstruct = ( i->tsConstruct & ( ~( 0x7F << i->byteCount ) ) ) | ( ( c & 0x7f ) << i->byteCount );
                }
                else
                {
                    i->tsConstruct = ( i->tsConstruct & ( ~( 0xff << i->byteCount ) ) ) | ( ( c & 0xff ) << i->byteCount );
                }

                i->byteCount++;

                if ( ( !( c & 0x80 ) ) || ( i->byteCount == 9 ) )
                {
                    newState = ETM_IDLE;
                    cpu->ts = i->tsConstruct;
                    cpu->changeRecord |= ( 1 << EV_CH_TSTAMP );
                    retVal = ETM_EV_MSG_RXED;
                }

                break;

            // -----------------------------------------------------
            // CYCLECOUNT RELATED ACTIVITIES
            // -----------------------------------------------------

            case ETM_GET_CYCLECOUNT: /* Collecting cycle count as standalone packet */
                i->cycleConstruct = ( i->cycleConstruct & ~( 0x7f << ( ( i->byteCount ) * 7 ) ) ) | ( ( c & 0x7f ) << ( ( i->byteCount ) * 7 ) );
                i->byteCount++;

                if ( ( !( c & ( 1 << 7 ) ) ) || ( i->byteCount == 5 ) )
                {
                    newState = ETM_IDLE;
                    cpu->cycleCount = i->cycleConstruct;
                    cpu->changeRecord |= EV_CH_CYCLECOUNT;
                    retVal = ETM_EV_MSG_RXED;
                }

                break;


            // -----------------------------------------------------
            // CONTEXTID RELATED ACTIVITIES
            // -----------------------------------------------------

            case ETM_GET_CONTEXTID: /* Collecting contextID */
                i->contextConstruct = i->contextConstruct + ( c << ( 8 * i->byteCount ) );
                i->byteCount++;

                if ( i->byteCount == i->contextBytes )
                {
                    if ( cpu->contextID != i->contextConstruct )
                    {
                        cpu->contextID = i->contextConstruct;
                        cpu->changeRecord |= ( 1 << EV_CH_CONTEXTID );
                    }

                    retVal = ETM_EV_MSG_RXED;
                    newState = ETM_IDLE;
                }

                break;


            // -----------------------------------------------------
            // I-SYNC RELATED ACTIVITIES
            // -----------------------------------------------------

            case ETM_WAIT_ISYNC:
                if ( c == 0b00001000 )
                {
                    if ( !i->rxedISYNC )
                    {
                        retVal = ETM_EV_SYNCED;
                        i->rxedISYNC = true;
                    }

                    i->byteCount = i->contextBytes;
                    i->contextConstruct = 0;
                    newState = i->contextBytes ? ETM_GET_CONTEXTBYTE : ETM_GET_INFOBYTE;
                }

                break;

            // -----------------------------------------------------

            case ETM_GET_CONTEXTBYTE: /* Collecting I-Sync contextID bytes */
                i->contextConstruct = i->contextConstruct + ( c << ( 8 * i->byteCount ) );
                i->byteCount++;

                if ( i->byteCount == i->contextBytes )
                {
                    if ( cpu->contextID != i->contextConstruct )
                    {
                        cpu->contextID = i->contextConstruct;
                        cpu->changeRecord |= ( 1 << EV_CH_CONTEXTID );
                    }

                    newState = ETM_GET_INFOBYTE;
                }

                break;

            // -----------------------------------------------------

            case ETM_GET_INFOBYTE: /* Collecting I-Sync Information byte */
                if ( ( ( c & 0x10000000 ) != 0 ) != cpu->isLSiP )
                {
                    cpu->isLSiP = ( c & 0x10000000 ) != 0;
                    cpu->changeRecord |= ( 1 << EV_CH_ISLSIP );
                }

                if ( cpu->reason != ( ( c & 0x01100000 ) >> 5 ) )
                {
                    cpu->reason    = ( c & 0x01100000 ) >> 5;
                    cpu->changeRecord |= ( 1 << EV_CH_REASON );
                }

                if ( cpu->jazelle   != ( ( c & 0x00010000 ) != 0 ) )
                {
                    cpu->jazelle   = ( c & 0x00010000 ) != 0;
                    cpu->changeRecord |= ( 1 << EV_CH_JAZELLE );
                }

                if ( cpu->nonSecure != ( ( c & 0x00001000 ) != 0 ) )
                {
                    cpu->nonSecure = ( c & 0x00001000 ) != 0;
                    cpu->changeRecord |= ( 1 << EV_CH_SECURE );
                }

                if ( cpu->altISA != ( ( c & 0x00000100 ) != 0 ) )
                {
                    cpu->altISA    = ( c & 0x00000100 ) != 0;
                    cpu->changeRecord |= ( 1 << EV_CH_ALTISA );
                }

                if ( cpu->hyp != ( ( c & 0x00000010 ) != 0 ) )
                {
                    cpu->hyp       = ( c & 0x00000010 ) != 0;
                    cpu->changeRecord |= ( 1 << EV_CH_HYP );
                }

                i->byteCount = 0;

                if ( i->dataOnlyMode )
                {
                    retVal = ETM_EV_MSG_RXED;
                    newState = ETM_IDLE;
                }
                else
                {
                    newState = ETM_GET_IADDRESS;
                }

                break;

            // -----------------------------------------------------

            case ETM_GET_IADDRESS: /* Collecting I-Sync Address bytes */
                i->addrConstruct = ( i->addrConstruct & ( ~( 0xff << ( 8 * i->byteCount ) ) ) )  | ( c << ( 8 * i->byteCount ) ) ;
                i->byteCount++;

                if ( i->byteCount == 4 )
                {
                    cpu->changeRecord |= ( 1 << EV_CH_ADDRESS );

                    if ( cpu->jazelle )
                    {
                        /* This is Jazelle mode..can ignore the AltISA bit */
                        /* and bit 0 is bit 0 of the address */
                        cpu->addrMode = ETM_ADDRMODE_JAZELLE;
                        cpu->addr = i->addrConstruct;
                    }
                    else
                    {
                        if ( ( i->addrConstruct & ( 1 << 0 ) ) ^ ( !cpu->thumb ) )
                        {
                            cpu->thumb     = ( c & 0x00000001 ) != 0;
                            cpu->changeRecord |= ( 1 << EV_CH_THUMB );
                        }

                        if ( i->addrConstruct & ( 1 << 0 ) )
                        {
                            cpu->addrMode = ETM_ADDRMODE_THUMB;
                            cpu->addr = i->addrConstruct & 0xFFFFFFFE;
                        }
                        else
                        {
                            cpu->addrMode = ETM_ADDRMODE_ARM;
                            cpu->addr = i->addrConstruct & 0xFFFFFFFC;
                        }
                    }

                    if ( cpu->isLSiP )
                    {
                        /* If this is an LSiP packet we need to go get the address */
                        newState = ( i->usingAltAddrEncode ) ? ETM_COLLECT_BA_ALT_FORMAT : ETM_COLLECT_BA_STD_FORMAT;
                    }
                    else
                    {
                        newState = ETM_IDLE;
                        retVal = ETM_EV_MSG_RXED;
                    }

                }

                break;

            // -----------------------------------------------------

            case ETM_GET_ICYCLECOUNT: /* Collecting cycle count on front of ISYNC packet */
                i->cycleConstruct = ( i->cycleConstruct & ~( 0x7f << ( ( i->byteCount ) * 7 ) ) ) | ( ( c & 0x7f ) << ( ( i->byteCount ) * 7 ) );
                i->byteCount++;

                if ( ( !( c & ( 1 << 7 ) ) ) || ( i->byteCount == 5 ) )
                {
                    /* Now go to get the rest of the ISYNC packet */
                    /* Collect either the context or the Info Byte next */
                    cpu->cycleCount = i->cycleConstruct;
                    i->byteCount = i->contextBytes;
                    i->contextConstruct = 0;
                    cpu->changeRecord |= ( 1 << EV_CH_CYCLECOUNT );
                    newState = i->contextBytes ? ETM_GET_CONTEXTBYTE : ETM_GET_INFOBYTE;
                    break;
                }

                break;

                // -----------------------------------------------------

        }
    }

    if ( i->p != ETM_UNSYNCED )
    {
        genericsReport( V_DEBUG, "%02x:%s --> %s %s(%d)", c, ( i->p == ETM_IDLE ) ? _protoNames[i->p] : "", _protoNames[newState],
                        ( ( newState == ETM_IDLE ) ? ( ( retVal == ETM_EV_NONE ) ? "!!!" : "OK" ) : " : " ), retVal );
    }

    i->p = newState;

    if ( ( retVal != ETM_EV_NONE ) && ( i->rxedISYNC ) )
    {
        cb( d );
    }
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Externally available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
void ETMDecoderInit( struct ETMDecoder *i, bool usingAltAddrEncodeSet )

/* Reset a ETMDecoder instance */

{
    memset( i, 0, sizeof( struct ETMDecoder ) );
    ETMDecoderZeroStats( i );
    ETMDecodeUsingAltAddrEncode( i, usingAltAddrEncodeSet );
}
// ====================================================================================================
void ETMDecodeUsingAltAddrEncode( struct ETMDecoder *i, bool usingAltAddrEncodeSet )

{
    i->usingAltAddrEncode = usingAltAddrEncodeSet;
}
// ====================================================================================================

void ETMDecoderZeroStats( struct ETMDecoder *i )

{
    memset( &i->stats, 0, sizeof( struct ETMDecoderStats ) );
}
// ====================================================================================================
bool ETMDecoderIsSynced( struct ETMDecoder *i )

{
    return i->p != ETM_UNSYNCED;
}
// ====================================================================================================
struct ETMDecoderStats *ETMDecoderGetStats( struct ETMDecoder *i )
{
    return &i->stats;
}
// ====================================================================================================
void ETMDecoderForceSync( struct ETMDecoder *i, bool isSynced )

/* Force the decoder into a specific sync state */

{
    if ( i->p == ETM_UNSYNCED )
    {
        if ( isSynced )
        {
            i->p = ETM_IDLE;
            i->stats.syncCount++;
        }
    }
    else
    {
        if ( !isSynced )
        {
            i->stats.lostSyncCount++;
            i->p = ETM_UNSYNCED;
        }
    }
}
// ====================================================================================================
void ETMDecoderPump( struct ETMDecoder *i, uint8_t *buf, int len, etmDecodeCB cb, void *d )

{
    while ( len-- )
    {
        _ETMDecoderPumpAction( i, *buf++, cb, d );
    }
}
// ====================================================================================================
