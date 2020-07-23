/*
 * Fifo support
 * ============
 *
 * Copyright (C) 2017, 2019  Dave Marples  <dave@marples.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * * Neither the names Orbtrace, Orbuculum nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <semaphore.h>
#include <assert.h>
#if defined OSX
    #include <sys/ioctl.h>
    #include <libusb.h>
    #include <termios.h>
#else
    #if defined LINUX
        #include <libusb-1.0/libusb.h>
        #include <asm/ioctls.h>
        #if defined TCGETS2
            #include <asm/termios.h>
            /* Manual declaration to avoid conflict. */
            extern int ioctl ( int __fd, unsigned long int __request, ... ) __THROW;
        #else
            #include <sys/ioctl.h>
            #include <termios.h>
        #endif
    #else
        #error "Unknown OS"
    #endif
#endif
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#include "git_version_info.h"
#include "generics.h"
#include "tpiuDecoder.h"
#include "itmDecoder.h"
#include "fileWriter.h"
#include "fifos.h"

#define MAX_STRING_LENGTH (100)              /* Maximum length that will be output from a fifo for a single event */

struct _runThreadParams                   /* Structure for parameters passed to a software task thread */
{
    int portNo;
    int listenHandle;
    struct Channel *c;
};

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Private routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
static uint64_t _timestampuS( void )

{
    struct timeval te;
    gettimeofday( &te, NULL ); // get current time
    return ( te.tv_sec * 1000000LL + te.tv_usec ); // caculate microseconds
}
// ====================================================================================================
// Handlers for the fifos
// ====================================================================================================
static void *_runFifo( void *arg )

/* This is the control loop for the channel fifos (for each software port) */

{
    struct _runThreadParams *params = ( struct _runThreadParams * )arg;
    struct Channel *c = params->c;
    struct ITMPacket p;
    uint32_t w;

    char constructString[MAX_STRING_LENGTH];
    int fifo;
    int readDataLen, writeDataLen;

    if ( mkfifo( c->fifoName, 0666 ) < 0 )
    {
        return NULL;
    }

    while ( 1 )
    {
        /* This is the child */
        fifo = open( c->fifoName, O_WRONLY );

        while ( 1 )
        {
            /* ....get the packet */
            readDataLen = read( params->listenHandle, &p, sizeof( struct ITMPacket ) );

            if ( readDataLen != sizeof( struct ITMPacket ) )
            {
                return NULL;
            }

            /* Build 32 value the long way around to avoid type-punning issues */
            w = ( p.d[3] << 24 ) | ( p.d[2] << 16 ) | ( p.d[1] << 8 ) | ( p.d[0] );

            if ( c->presFormat )
            {
                // formatted output.
                writeDataLen = snprintf( constructString, MAX_STRING_LENGTH, c->presFormat, w );

                if ( write( fifo, constructString, ( writeDataLen < MAX_STRING_LENGTH ) ? writeDataLen : MAX_STRING_LENGTH ) <= 0 )
                {
                    break;
                }
            }
            else
            {
                // raw output.
                if ( write( fifo, &w, sizeof ( w ) ) <= 0 )
                {
                    break;
                }
            }
        }

        close( fifo );
    }

    return NULL;
}
// ====================================================================================================
static void *_runHWFifo( void *arg )

/* This is the control loop for the hardware fifo */

{
    struct _runThreadParams *params = ( struct _runThreadParams * )arg;
    struct Channel *c = params->c;
    int fifo;
    int readDataLen;
    uint8_t p[MAX_STRING_LENGTH];

    if ( mkfifo( c->fifoName, 0666 ) < 0 )
    {
        return NULL;
    }

    while ( 1 )
    {
        /* This is the child */
        fifo = open( c->fifoName, O_WRONLY );

        while ( 1 )
        {
            /* ....get the packet */
            readDataLen = read( params->listenHandle, p, MAX_STRING_LENGTH );

            if ( write( fifo, p, readDataLen ) <= 0 )
            {
                break;
            }
        }

        close( fifo );
    }

    return NULL;
}
// ====================================================================================================
// Decoders for each message
// ====================================================================================================
void _handleException( struct fifosHandle *f, struct ITMDecoder *i, struct ITMPacket *p )

{
    uint64_t ts = _timestampuS(); /* Stamp as early as possible */
    char outputString[MAX_STRING_LENGTH];
    int opLen;
    uint32_t exceptionNumber = ( ( p->d[1] & 0x01 ) << 8 ) | p->d[0];
    uint32_t eventType = p->d[1] >> 4;
    uint64_t eventdifftS = ts - f->lastHWExceptionTS;


    const char *exNames[] = {"Thread", "Reset", "NMI", "HardFault", "MemManage", "BusFault", "UsageFault", "UNKNOWN_7",
                             "UNKNOWN_8", "UNKNOWN_9", "UNKNOWN_10", "SVCall", "Debug Monitor", "UNKNOWN_13", "PendSV", "SysTick"
                            };
    const char *exEvent[] = {"Unknown", "Enter", "Exit", "Resume"};

    f->lastHWExceptionTS = ts;

    if ( exceptionNumber < 16 )
    {
        /* This is a system based exception */
        opLen = snprintf( outputString, MAX_STRING_LENGTH, "%d,%ld,%s,%s" EOL, HWEVENT_EXCEPTION, eventdifftS, exEvent[eventType & 0x03], exNames[exceptionNumber & 0x0F] );
    }
    else
    {
        /* This is a CPU defined exception */
        opLen = snprintf( outputString, MAX_STRING_LENGTH, "%d,%ld,%s,External,%d" EOL, HWEVENT_EXCEPTION, eventdifftS, exEvent[eventType & 0x03], exceptionNumber - 16 );
    }

    write( f->c[HW_CHANNEL].handle, outputString, opLen );
}
// ====================================================================================================
void _handleDWTEvent( struct fifosHandle *f, struct ITMDecoder *i, struct ITMPacket *p )

{
    uint64_t ts = _timestampuS(); /* Stamp as early as possible */
    char outputString[MAX_STRING_LENGTH];
    int opLen;
    uint32_t event = p->d[1] & 0x2F;
    const char *evName[] = {"CPI", "Exc", "Sleep", "LSU", "Fold", "Cyc"};
    uint64_t eventdifftS = ts - f->lastHWExceptionTS;

    f->lastHWExceptionTS = ts;

    for ( uint32_t i = 0; i < 6; i++ )
    {
        if ( event & ( 1 << i ) )
        {
            opLen = snprintf( outputString, MAX_STRING_LENGTH, "%d,%ld,%s" EOL, HWEVENT_DWT, eventdifftS, evName[event] );
            write( f->c[HW_CHANNEL].handle, outputString, opLen );
        }
    }
}
// ====================================================================================================
void _handlePCSample( struct fifosHandle *f, struct ITMDecoder *i, struct ITMPacket *p )

/* We got a sample of the PC */

{
    uint64_t ts = _timestampuS(); /* Stamp as early as possible */
    char outputString[MAX_STRING_LENGTH];
    int opLen;
    uint64_t eventdifftS = ts - f->lastHWExceptionTS;

    f->lastHWExceptionTS = ts;

    if ( p->len == 1 )
    {
        /* This is a sleep packet */
        opLen = snprintf( outputString, ( MAX_STRING_LENGTH - 1 ), "%d,%ld,**SLEEP**" EOL, HWEVENT_PCSample, eventdifftS );
    }
    else
    {
        uint32_t pc = ( p->d[3] << 24 ) | ( p->d[2] << 16 ) | ( p->d[1] << 8 ) | ( p->d[0] );
        opLen = snprintf( outputString, ( MAX_STRING_LENGTH - 1 ), "%d,%ld,0x%08x" EOL, HWEVENT_PCSample, eventdifftS, pc );
    }

    write( f->c[HW_CHANNEL].handle, outputString, opLen );
}
// ====================================================================================================
void _handleDataRWWP( struct fifosHandle *f, struct ITMDecoder *i, struct ITMPacket *p )

/* We got an alert due to a watch pointer */

{
    uint64_t ts = _timestampuS(); /* Stamp as early as possible */
    uint8_t comp = ( p->srcAddr >> 1 ) & 0x3;
    bool isWrite = ( ( p->srcAddr & 0x01 ) != 0 );
    uint32_t data;
    char outputString[MAX_STRING_LENGTH];
    int opLen;
    uint64_t eventdifftS = ts - f->lastHWExceptionTS;

    f->lastHWExceptionTS = ts;

    switch ( p->len )
    {
        case 1:
            data = p->d[0];
            break;

        case 2:
            data = ( p->d[0] ) | ( ( p->d[1] ) << 8 );
            break;

        default:
            data = ( p->d[0] ) | ( ( p->d[1] ) << 8 ) | ( ( p->d[2] ) << 16 ) | ( ( p->d[3] ) << 24 );
            break;
    }

    opLen = snprintf( outputString, MAX_STRING_LENGTH, "%d,%ld,%d,%s,0x%x" EOL, HWEVENT_RWWT, eventdifftS, comp, isWrite ? "Write" : "Read", data );
    write( f->c[HW_CHANNEL].handle, outputString, opLen );
}
// ====================================================================================================
void _handleDataAccessWP( struct fifosHandle *f, struct ITMDecoder *i, struct ITMPacket *p )

/* We got an alert due to a watchpoint */

{
    uint64_t ts = _timestampuS(); /* Stamp as early as possible */
    uint8_t comp = ( p->srcAddr >> 1 ) & 0x3;
    uint32_t data = ( p->d[0] ) | ( ( p->d[1] ) << 8 ) | ( ( p->d[2] ) << 16 ) | ( ( p->d[3] ) << 24 );
    char outputString[MAX_STRING_LENGTH];
    int opLen;

    uint64_t eventdifftS = ts - f->lastHWExceptionTS;

    f->lastHWExceptionTS = ts;
    opLen = snprintf( outputString, MAX_STRING_LENGTH, "%d,%ld,%d,0x%08x" EOL, HWEVENT_AWP, eventdifftS, comp, data );
    write( f->c[HW_CHANNEL].handle, outputString, opLen );
}
// ====================================================================================================
void _handleDataOffsetWP( struct fifosHandle *f, struct ITMDecoder *i, struct ITMPacket *p )

/* We got an alert due to an offset write event */

{
    uint64_t ts = _timestampuS(); /* Stamp as early as possible */
    uint8_t comp = ( p->srcAddr >> 1 ) & 0x3;
    uint32_t offset = ( p->d[0] ) | ( ( p->d[1] ) << 8 );
    char outputString[MAX_STRING_LENGTH];
    int opLen;
    uint64_t eventdifftS = ts - f->lastHWExceptionTS;

    f->lastHWExceptionTS = ts;
    opLen = snprintf( outputString, MAX_STRING_LENGTH, "%d,%ld,%d,0x%04x" EOL, HWEVENT_OFS, eventdifftS, comp, offset );
    write( f->c[HW_CHANNEL].handle, outputString, opLen );
}

// ====================================================================================================
// Handlers for each message type
// ====================================================================================================

void _handleSW( struct fifosHandle *f, struct ITMDecoder *i )

{
    struct ITMPacket p;

    if ( ITMGetPacket( i, &p ) )
    {
        /* Filter off filewriter packets and let the filewriter module deal with those */
        if ( ( p.srcAddr == FW_CHANNEL ) && ( f->filewriter ) )
        {
            filewriterProcess( &p );
        }
        else
        {
            if ( ( p.srcAddr < NUM_CHANNELS ) && ( f->c[p.srcAddr].handle ) )
            {
                write( f->c[p.srcAddr].handle, &p, sizeof( struct ITMPacket ) );
            }
        }
    }
}
// ====================================================================================================
void _handleHW( struct fifosHandle *f, struct ITMDecoder *i )

/* ... a hardware event has been received, dispatch it */

{
    struct ITMPacket p;
    ITMGetPacket( i, &p );

    switch ( p.srcAddr )
    {
        // --------------
        case 0: /* DWT Event */
            break;

        // --------------
        case 1: /* Exception */
            _handleException( f, i, &p );
            break;

        // --------------
        case 2: /* PC Counter Sample */
            _handlePCSample( f, i, &p );
            break;

        // --------------
        default:
            if ( ( p.srcAddr & 0x19 ) == 0x11 )
            {
                _handleDataRWWP( f, i, &p );
            }
            else if ( ( p.srcAddr & 0x19 ) == 0x08 )
            {
                _handleDataAccessWP( f, i, &p );
            }
            else if ( ( p.srcAddr & 0x19 ) == 0x09 )
            {
                _handleDataOffsetWP( f, i, &p );
            }

            break;
            // --------------
    }
}
// ====================================================================================================
void _handleTS( struct fifosHandle *f, struct ITMDecoder *i )

/* ... a timestamp */

{
    char outputString[MAX_STRING_LENGTH];
    int opLen;
    struct ITMPacket p;
    uint32_t stamp = 0;

    if ( ITMGetPacket( i, &p ) )
    {
        if ( !( p.d[0] & 0x80 ) )
        {
            /* This is packet format 2 ... just a simple increment */
            stamp = p.d[0] >> 4;
        }
        else
        {
            /* This is packet format 1 ... full decode needed */
            i->timeStatus = ( p.d[0] & 0x30 ) >> 4;
            stamp = ( p.d[1] ) & 0x7f;

            if ( p.len > 2 )
            {
                stamp |= ( p.d[2] ) << 7;

                if ( p.len > 3 )
                {
                    stamp |= ( p.d[3] & 0x7F ) << 14;

                    if ( p.len > 4 )
                    {
                        stamp |= ( p.d[4] & 0x7f ) << 21;
                    }
                }
            }
        }

        i->timeStamp += stamp;
    }

    opLen = snprintf( outputString, MAX_STRING_LENGTH, "%d,%d,%" PRIu64 EOL, HWEVENT_TS, i->timeStatus, i->timeStamp );
    write( f->c[HW_CHANNEL].handle, outputString, opLen );
}
// ====================================================================================================
void _itmPumpProcess( struct fifosHandle *f, char c )

/* Handle individual characters into the itm decoder */

{
    switch ( ITMPump( &f->i, c ) )
    {
        // ------------------------------------
        case ITM_EV_NONE:
            break;

        // ------------------------------------
        case ITM_EV_UNSYNCED:
            genericsReport( V_WARN, "ITM Lost Sync (%d)" EOL, ITMDecoderGetStats( &f->i )->lostSyncCount );
            break;

        // ------------------------------------
        case ITM_EV_SYNCED:
            genericsReport( V_INFO, "ITM In Sync (%d)" EOL, ITMDecoderGetStats( &f->i )->syncCount );
            break;

        // ------------------------------------
        case ITM_EV_OVERFLOW:
            genericsReport( V_WARN, "ITM Overflow (%d)" EOL, ITMDecoderGetStats( &f->i )->overflow );
            break;

        // ------------------------------------
        case ITM_EV_ERROR:
            genericsReport( V_WARN, "ITM Error" EOL );
            break;

        // ------------------------------------
        case ITM_EV_TS_PACKET_RXED:
            _handleTS( f, &f->i );
            break;

        // ------------------------------------
        case ITM_EV_SW_PACKET_RXED:
            _handleSW( f, &f->i );
            break;

        // ------------------------------------
        case ITM_EV_HW_PACKET_RXED:
            _handleHW( f, &f->i );
            break;

        // ------------------------------------
        case ITM_EV_RESERVED_PACKET_RXED:
            genericsReport( V_INFO, "Reserved Packet Received" EOL );
            break;

        // ------------------------------------
        case ITM_EV_XTN_PACKET_RXED:
            genericsReport( V_INFO, "Unknown Extension Packet Received" EOL );
            break;

            // ------------------------------------
    }
}
// ====================================================================================================
static void _tpiuProtocolPump( struct fifosHandle *f, uint8_t c )

{
    switch ( TPIUPump( &f->t, c ) )
    {
        // ------------------------------------
        case TPIU_EV_NEWSYNC:
            genericsReport( V_INFO, "TPIU In Sync (%d)" EOL, TPIUDecoderGetStats( &f->t )->syncCount );

        // This fall-through is deliberate
        case TPIU_EV_SYNCED:

            ITMDecoderForceSync( &f->i, true );
            break;

        // ------------------------------------
        case TPIU_EV_RXING:
        case TPIU_EV_NONE:
            break;

        // ------------------------------------
        case TPIU_EV_UNSYNCED:
            genericsReport( V_INFO, "TPIU Lost Sync (%d)" EOL, TPIUDecoderGetStats( &f->t )->lostSync );
            ITMDecoderForceSync( &f->i, false );
            break;

        // ------------------------------------
        case TPIU_EV_RXEDPACKET:
            if ( !TPIUGetPacket( &f->t, &f->p ) )
            {
                genericsReport( V_WARN, "TPIUGetPacket fell over" EOL );
            }

            for ( uint32_t g = 0; g < f->p.len; g++ )
            {
                if ( f->p.packet[g].s == f->tpiuITMChannel )
                {
                    _itmPumpProcess( f, f->p.packet[g].d );
                    continue;
                }

                if ( ( f->p.packet[g].s != 0 ) && ( f->p.packet[g].s != 0x7f ) )
                {
                    genericsReport( V_INFO, "Unknown TPIU channel %02x" EOL, f->p.packet[g].s );
                }
            }

            break;

        // ------------------------------------
        case TPIU_EV_ERROR:
            genericsReport( V_ERROR, "****ERROR****" EOL );
            break;
            // ------------------------------------
    }
}

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Public interface
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================

// ====================================================================================================
// Getters and Setters
// ====================================================================================================
void fifoSetChanPath( struct fifosHandle *f, char *s )

{
    if ( f->chanPath )
    {
        free( f->chanPath );
    }

    f->chanPath = s ? strdup( s ) : NULL;
}

// ====================================================================================================
void fifoSetChannel( struct fifosHandle *f, int chan, char *n, char *s )

{
    assert( chan <= NUM_CHANNELS );

    if ( f->c[chan].presFormat )
    {
        free( f->c[chan].presFormat );
    }

    f->c[chan].chanName = strdup( n );
    f->c[chan].presFormat = s ? strdup( s ) : NULL;
}
// ====================================================================================================
void fifoSetUseTPIU( struct fifosHandle *f, bool s )

{
    f->useTPIU = s;
}
// ====================================================================================================
void fifoSetForceITMSync( struct fifosHandle *f, bool s )

{
    f->forceITMSync = s;
}
// ====================================================================================================
void fifoSettpiuITMChannel( struct fifosHandle *f, int channel )

{
    f->tpiuITMChannel = channel;
}
// ====================================================================================================
char *fifoGetChannelName( struct fifosHandle *f, int chan )

{
    assert( chan <= NUM_CHANNELS );
    return f->c[chan].chanName;
}
// ====================================================================================================
char *fifoGetChannelFormat( struct fifosHandle *f, int chan )

{
    assert( chan <= NUM_CHANNELS );
    return f->c[chan].chanName;
}
// ====================================================================================================
char *fifoGetChanPath( struct fifosHandle *f )

{
    return f->chanPath;
}
// ====================================================================================================
bool fifoGetUseTPIU( struct fifosHandle *f )

{
    return f->useTPIU;
}
// ====================================================================================================
bool fifoGetForceITMSync( struct fifosHandle *f )

{
    return f->forceITMSync;
}
// ====================================================================================================
int fifoGettpiuITMChannel( struct fifosHandle *f )

{
    return f->tpiuITMChannel;
}

// ====================================================================================================
// Main interface components
// ====================================================================================================
void fifoProtocolPump( struct fifosHandle *f, uint8_t c )

/* Top level protocol pump */

{
    if ( f->useTPIU )
    {
        _tpiuProtocolPump( f, c );
    }
    else
    {
        /* There's no TPIU in use, so this goes straight to the ITM layer */
        _itmPumpProcess( f, c );
    }
}
// ====================================================================================================
void fifoForceSync( struct fifosHandle *f, bool synced )

/* Reset TPIU state and put ITM into defined state */

{
    TPIUDecoderForceSync( &f->t, 0 );
    ITMDecoderForceSync( &f->i, synced );
}
// ====================================================================================================
bool fifoCreate( struct fifosHandle *f )

/* Create each sub-process that will handle a port */

{
    struct _runThreadParams *params;
    int fd[2];

    /* Make sure there's an initial timestamp to work with */
    f->lastHWExceptionTS = _timestampuS();

    /* Reset the TPIU handler before we start */
    TPIUDecoderInit( &f->t );
    ITMDecoderInit( &f->i, f->forceITMSync );

    /* Cycle through channels and create a fifo for each one that is enabled */
    for ( int t = 0; t < ( NUM_CHANNELS + 1 ); t++ )
    {
        if ( t < NUM_CHANNELS )
        {
            if ( f->c[t].chanName )
            {
                /* This is a live software channel fifo */
                if ( pipe( fd ) < 0 )
                {
                    return false;
                }

                fcntl( fd[1], F_SETFL, O_NONBLOCK );
                f->c[t].handle = fd[1];

                params = ( struct _runThreadParams * )malloc( sizeof( struct _runThreadParams ) );
                params->listenHandle = fd[0];
                params->portNo = t;
                params->c = &f->c[t];

                f->c[t].fifoName = ( char * )malloc( strlen( f->c[t].chanName ) + strlen( f->chanPath ) + 2 );
                strcpy( f->c[t].fifoName, f->chanPath );
                strcat( f->c[t].fifoName, f->c[t].chanName );

                if ( pthread_create( &( f->c[t].thread ), NULL, &_runFifo, params ) )
                {
                    return false;
                }
            }
        }
        else
        {
            /* This is the hardware fifo channel */
            if ( pipe( fd ) < 0 )
            {
                return false;
            }

            fcntl( fd[1], F_SETFL, O_NONBLOCK );
            f->c[t].handle = fd[1];

            params = ( struct _runThreadParams * )malloc( sizeof( struct _runThreadParams ) );
            params->listenHandle = fd[0];
            params->portNo = t;
            params->c = &f->c[t];

            f->c[t].fifoName = ( char * )malloc( strlen( HWFIFO_NAME ) + strlen( f->chanPath ) + 2 );
            strcpy( f->c[t].fifoName, f->chanPath );
            strcat( f->c[t].fifoName, HWFIFO_NAME );

            if ( pthread_create( &( f->c[t].thread ), NULL, &_runHWFifo, params ) )
            {
                return false;
            }
        }
    }

    return true;
}
// ====================================================================================================
void fifoShutdown( struct fifosHandle *f )

/* Destroy the per-port sub-processes */

{
    for ( int t = 0; t < NUM_CHANNELS + 1; t++ )
    {
        if ( f->c[t].handle > 0 )
        {
            close( f->c[t].handle );
            unlink( f->c[t].fifoName );
        }

        /* Remove the name string too */
        if ( f->c[t].presFormat )
        {
            free( f->c[t].presFormat );
        }
    }

    free( f );
}
// ====================================================================================================

void fifoFilewriter( struct fifosHandle *f, bool useFilewriter, char *workingPath )

{
    f->filewriter = useFilewriter;

    if ( f->filewriter )
    {
        filewriterInit( workingPath );
    }
}

// ====================================================================================================
struct fifosHandle *fifoInit( bool forceITMSyncSet, bool useTPIUSet, int TPIUchannelSet )

{
    struct fifosHandle *f = ( struct fifosHandle * )calloc( 1, sizeof( struct fifosHandle  ) );
    f->chanPath = strdup( "" );
    f->useTPIU = useTPIUSet;
    f->forceITMSync = forceITMSyncSet;
    f->tpiuITMChannel = TPIUchannelSet;

    return f;
}
// ====================================================================================================
