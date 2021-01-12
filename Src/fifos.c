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
#include <inttypes.h>
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
#include "msgDecoder.h"

#define MAX_STRING_LENGTH (100)              /* Maximum length that will be output from a fifo for a single event */

struct Channel                               /* Information for an individual channel */
{
    char *chanName;                          /* Filename to be used for the fifo */
    char *presFormat;                        /* Format of data presentation to be used */

    /* Runtime state */
    int handle;                              /* Handle to the fifo */
    pthread_t thread;                        /* Thread on which it's running */

    char *fifoName;                          /* Constructed fifo name (from chanPath and name) */
};

struct fifosHandle

{
    /* The decoders and the packets from them */
    struct ITMDecoder i;
    struct ITMPacket h;
    struct TPIUDecoder t;
    struct TPIUPacket p;
    enum timeDelay timeStatus;                    /* Indicator of if this time is exact */
    uint64_t timeStamp;                           /* Latest received time */

    /* Timestamp info */
    uint64_t lastHWExceptionTS;

    /* Configuration information */
    char *chanPath;                               /* Path to where to put the fifos */
    bool useTPIU;                                 /* Is the TPIU active? */
    bool filewriter;                              /* Is the filewriter in use? */
    bool forceITMSync;                            /* Is ITM to be forced into sync? */
    bool permafile;                               /* Use permanent files rather than fifos */
    int tpiuITMChannel;                           /* TPIU channel on which ITM appears */

    struct Channel c[NUM_CHANNELS + 1];           /* Output for each channel */
};

struct _runThreadParams                   /* Structure for parameters passed to a software task thread */
{
    int portNo;
    int listenHandle;
    bool permafile;
    struct Channel *c;
};

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Private routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================

// ====================================================================================================
// Handlers for the fifos
// ====================================================================================================
static void *_runFifo( void *arg )

/* This is the control loop for the channel fifos (for each software port) */

{
    struct _runThreadParams *params = ( struct _runThreadParams * )arg;
    struct Channel *c = params->c;
    struct swMsg m;
    uint32_t w;

    char constructString[MAX_STRING_LENGTH];
    int opfile;
    size_t readDataLen, writeDataLen, written = 0;

    /* Remove the file if it exists */
    unlink( c->fifoName );

    if ( !params->permafile )
    {
        /* This is a 'conventional' fifo, so it must be created */
        if ( mkfifo( c->fifoName, 0666 ) < 0 )
        {
            pthread_exit( NULL );
        }
    }

    do
    {
        /* Keep on opening the file (in case the fifo is opened/closed multiple times */
        if ( !params->permafile )
        {
            opfile = open( c->fifoName, O_WRONLY );
        }
        else
        {
            opfile = open( c->fifoName, O_WRONLY | O_CREAT, 0666 );
        }

        do
        {
            /* ....get the packet */
            readDataLen = read( params->listenHandle, &m, sizeof( struct msg ) );

            if ( readDataLen <= 0 )
            {
                continue;
            }

            if ( c->presFormat )
            {
                // formatted output....start with specials
                if ( strstr( c->presFormat, "%f" ) )
                {
                    /* type punning on same host, after correctly building 32bit val
                     * only unsafe on systems where u32/float have diff byte order */
                    float *nastycast = ( float * )&m.value;
                    writeDataLen = snprintf( constructString, MAX_STRING_LENGTH, c->presFormat, *nastycast, *nastycast, *nastycast, *nastycast );
                }
                else if ( strstr( c->presFormat, "%c" ) )
                {
                    /* Format contains %c, so execute repeatedly for all characters in sent data */
                    writeDataLen = 0;
                    uint8_t op[4] = {m.value & 0xff, ( m.value >> 8 ) & 0xff, ( m.value >> 16 ) & 0xff, ( m.value >> 24 ) & 0xff};

                    uint32_t l = 0;

                    do
                    {
                        writeDataLen += snprintf( &constructString[writeDataLen], MAX_STRING_LENGTH - writeDataLen, c->presFormat, op[l], op[l], op[l], op[l] );
                    }
                    while ( ++l < m.len );
                }
                else
                {
                    writeDataLen = snprintf( constructString, MAX_STRING_LENGTH, c->presFormat, m.value, m.value, m.value, m.value );
                }

                written = write( opfile, constructString, ( writeDataLen < MAX_STRING_LENGTH ) ? writeDataLen : MAX_STRING_LENGTH );
            }
            else
            {
                // raw output.
                written = write( opfile, &w, sizeof ( w ) );
            }
        }
        while ( ( readDataLen > 0 ) && ( written > 0 ) );

        close( opfile );
    }
    while ( readDataLen > 0 );

    pthread_exit( NULL );
}
// ====================================================================================================
static void *_runHWFifo( void *arg )

/* This is the control loop for the hardware fifo */

{
    struct _runThreadParams *params = ( struct _runThreadParams * )arg;
    struct Channel *c = params->c;
    int opfile;
    size_t readDataLen, writeDataLen = 0;
    uint8_t p[MAX_STRING_LENGTH];

    /* Remove the file if it exists */
    unlink( c->fifoName );

    if ( !params->permafile )
    {
        /* This is a 'conventional' fifo, so it must be created */
        if ( mkfifo( c->fifoName, 0666 ) < 0 )
        {
            pthread_exit( NULL );
        }
    }

    do
    {
        if ( !params->permafile )
        {
            opfile = open( c->fifoName, O_WRONLY );
        }
        else
        {
            opfile = open( c->fifoName, O_WRONLY | O_CREAT, 0666 );
        }

        do
        {
            /* ....get the packet, don't worry if it can't be written */
            readDataLen = read( params->listenHandle, p, MAX_STRING_LENGTH );

            if ( readDataLen )
            {
                writeDataLen = write( opfile, p, readDataLen );
            }
        }
        while ( ( readDataLen > 0 ) && ( writeDataLen > 0 ) );

        close( opfile );
    }
    while ( readDataLen > 0 );

    pthread_exit( NULL );
}

// ====================================================================================================
// Decoders for each message
// ====================================================================================================
void _handleException( struct excMsg *m, struct fifosHandle *f )

{
    char outputString[MAX_STRING_LENGTH];
    int opLen;
    uint64_t eventdifftS = m->ts - f->lastHWExceptionTS;

    const char *exNames[] = {"Thread", "Reset", "NMI", "HardFault", "MemManage", "BusFault", "UsageFault", "UNKNOWN_7",
                             "UNKNOWN_8", "UNKNOWN_9", "UNKNOWN_10", "SVCall", "Debug Monitor", "UNKNOWN_13", "PendSV", "SysTick"
                            };
    const char *exEvent[] = {"Unknown", "Enter", "Exit", "Resume"};

    f->lastHWExceptionTS = m->ts;

    if ( m->exceptionNumber < 16 )
    {
        /* This is a system based exception */
        opLen = snprintf( outputString, MAX_STRING_LENGTH, "%d,%" PRIu64 ",%s,%s" EOL, HWEVENT_EXCEPTION, eventdifftS, exEvent[m->eventType & 0x03], exNames[m->exceptionNumber & 0x0F] );
    }
    else
    {
        /* This is a CPU defined exception */
        opLen = snprintf( outputString, MAX_STRING_LENGTH, "%d,%" PRIu64 ",%s,External,%d" EOL, HWEVENT_EXCEPTION, eventdifftS, exEvent[m->eventType & 0x03], m->exceptionNumber - 16 );
    }

    write( f->c[HW_CHANNEL].handle, outputString, opLen );
}
// ====================================================================================================
void _handleDWTEvent( struct dwtMsg *m, struct fifosHandle *f )

{
    char outputString[MAX_STRING_LENGTH];
    int opLen;

#define NUM_EVENTS 6
    const char *evName[NUM_EVENTS] = {"CPI", "Exc", "Sleep", "LSU", "Fold", "Cyc"};
    uint64_t eventdifftS = m->ts - f->lastHWExceptionTS;

    f->lastHWExceptionTS = m->ts;
    opLen = snprintf( outputString, MAX_STRING_LENGTH, "%d,%" PRIu64, HWEVENT_DWT, eventdifftS );

    for ( uint32_t i = 0; i < NUM_EVENTS; i++ )
    {
        if ( m->event & ( 1 << i ) )
        {
            // Copy this event into the output string
            outputString[opLen++] = ',';
            const char *u = evName[i];

            do
            {
                outputString[opLen++] = *u++;
            }
            while ( *u );
        }
    }

    write( f->c[HW_CHANNEL].handle, outputString, opLen );
    write( f->c[HW_CHANNEL].handle, EOL, strlen( EOL ) );
}
// ====================================================================================================
void _handlePCSample( struct pcSampleMsg *m, struct fifosHandle *f )

/* We got a sample of the PC */

{
    char outputString[MAX_STRING_LENGTH];
    int opLen;
    uint64_t eventdifftS = m->ts - f->lastHWExceptionTS;

    f->lastHWExceptionTS = m->ts;

    if ( m->sleep )
    {
        /* This is a sleep packet */
        opLen = snprintf( outputString, ( MAX_STRING_LENGTH - 1 ), "%d,%" PRIu64 ",**SLEEP**" EOL, HWEVENT_PCSample, eventdifftS );
    }
    else
    {
        opLen = snprintf( outputString, ( MAX_STRING_LENGTH - 1 ), "%d,%" PRIu64 ",0x%08x" EOL, HWEVENT_PCSample, eventdifftS, m->pc );
    }

    /* We don't need to worry if this write does not succeed, it just means there is no other side of the fifo */
    write( f->c[HW_CHANNEL].handle, outputString, opLen );
}
// ====================================================================================================
void _handleDataRWWP( struct watchMsg *m, struct fifosHandle *f )

/* We got an alert due to a watch pointer */

{
    char outputString[MAX_STRING_LENGTH];
    int opLen;
    uint64_t eventdifftS = m->ts - f->lastHWExceptionTS;

    f->lastHWExceptionTS = m->ts;

    opLen = snprintf( outputString, MAX_STRING_LENGTH, "%d,%" PRIu64 ",%d,%s,0x%x" EOL, HWEVENT_RWWT, eventdifftS, m->comp, m->isWrite ? "Write" : "Read", m->data );
    write( f->c[HW_CHANNEL].handle, outputString, opLen );
}
// ====================================================================================================
void _handleDataAccessWP( struct wptMsg *m, struct fifosHandle *f )

/* We got an alert due to a watchpoint */

{
    char outputString[MAX_STRING_LENGTH];
    int opLen;

    uint64_t eventdifftS = m->ts - f->lastHWExceptionTS;

    f->lastHWExceptionTS = m->ts;
    opLen = snprintf( outputString, MAX_STRING_LENGTH, "%d,%" PRIu64 ",%d,0x%08x" EOL, HWEVENT_AWP, eventdifftS, m->comp, m->data );
    write( f->c[HW_CHANNEL].handle, outputString, opLen );
}
// ====================================================================================================
void _handleDataOffsetWP( struct oswMsg *m, struct fifosHandle *f )

/* We got an alert due to an offset write event */

{
    char outputString[MAX_STRING_LENGTH];
    int opLen;
    uint64_t eventdifftS = m->ts - f->lastHWExceptionTS;

    f->lastHWExceptionTS = m->ts;
    opLen = snprintf( outputString, MAX_STRING_LENGTH, "%d,%" PRIu64 ",%d,0x%04x" EOL, HWEVENT_OFS, eventdifftS, m->comp, m->offset );
    write( f->c[HW_CHANNEL].handle, outputString, opLen );
}
// ====================================================================================================
void _handleSW( struct swMsg *m, struct fifosHandle *f )

{
    /* Filter off filewriter packets and let the filewriter module deal with those */
    if ( ( m->srcAddr == FW_CHANNEL ) && ( f->filewriter ) )
    {
        filewriterProcess( m );
    }
    else
    {
        if ( ( m->srcAddr < NUM_CHANNELS ) && ( f->c[m->srcAddr].handle ) )
        {
            write( f->c[m->srcAddr].handle, m, sizeof( struct msg ) );
        }
    }
}
// ====================================================================================================
void _handleNISYNC( struct nisyncMsg *m, struct fifosHandle *f )

{
    char outputString[MAX_STRING_LENGTH];
    int opLen;

    opLen = snprintf( outputString, MAX_STRING_LENGTH, "%d,%02x,0x%08x" EOL, HWEVENT_NISYNC, m->type, m->addr );
    write( f->c[HW_CHANNEL].handle, outputString, opLen );
}

// ====================================================================================================
void _handleTS( struct TSMsg *m, struct fifosHandle *f )

/* ... a timestamp */

{
    assert( m->msgtype == MSG_TS );
    char outputString[MAX_STRING_LENGTH];
    int opLen;

    f->timeStamp += m->timeInc;
    f->timeStatus = m->timeStatus;

    opLen = snprintf( outputString, MAX_STRING_LENGTH, "%d,%d,%" PRIu32 EOL, HWEVENT_TS, m->timeStatus, m->timeInc );
    write( f->c[HW_CHANNEL].handle, outputString, opLen );
}
// ====================================================================================================
void _itmPumpProcess( struct fifosHandle *f, char c )

/* Handle individual characters into the itm decoder */

{
    struct msg decoded;

    typedef void ( *handlers )( void *decoded, struct fifosHandle * f );

    /* Handlers for each complete message received */
    static const handlers h[MSG_NUM_MSGS] =
    {
        /* MSG_UNKNOWN */         NULL,
        /* MSG_RESERVED */        NULL,
        /* MSG_ERROR */           NULL,
        /* MSG_NONE */            NULL,
        /* MSG_SOFTWARE */        ( handlers )_handleSW,
        /* MSG_NISYNC */          ( handlers )_handleNISYNC,
        /* MSG_OSW */             ( handlers )_handleDataOffsetWP,
        /* MSG_DATA_ACCESS_WP */  ( handlers )_handleDataAccessWP,
        /* MSG_DATA_RWWP */       ( handlers )_handleDataRWWP,
        /* MSG_PC_SAMPLE */       ( handlers )_handlePCSample,
        /* MSG_DWT_EVENT */       ( handlers )_handleDWTEvent,
        /* MSG_EXCEPTION */       ( handlers )_handleException,
        /* MSG_TS */              ( handlers )_handleTS
    };

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
        case ITM_EV_PACKET_RXED:
            ITMGetDecodedPacket( &f->i, &decoded );

            /* See if we decoded a dispatchable match. genericMsg is just used to access */
            /* the first two members of the decoded structs in a portable way.           */
            if ( h[decoded.genericMsg.msgtype] )
            {
                ( h[decoded.genericMsg.msgtype] )( &decoded, f );
            }

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
                    genericsReport( V_INFO, "Unhandled TPIU channel %02x" EOL, f->p.packet[g].s );
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
struct TPIUCommsStats *fifoGetCommsStats( struct fifosHandle *f )

{
  return TPIUGetCommsStats( &f->t );
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
    f->lastHWExceptionTS = genericsTimestampuS();

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

                if ( !f->permafile )
                {
                    /* If this is not a permanent file then some data is allowed to get lost */
                    fcntl( fd[1], F_SETFL, O_NONBLOCK );
                }

                f->c[t].handle = fd[1];

                params = ( struct _runThreadParams * )malloc( sizeof( struct _runThreadParams ) );
                params->listenHandle = fd[0];
                params->portNo = t;
                params->permafile = f->permafile;
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

            if ( !f->permafile )
            {
                /* If this is not a permanent file then some data is allowed to get lost */
                fcntl( fd[1], F_SETFL, O_NONBLOCK );
            }

            f->c[t].handle = fd[1];

            params = ( struct _runThreadParams * )malloc( sizeof( struct _runThreadParams ) );
            params->listenHandle = fd[0];
            params->portNo = t;
            params->permafile = f->permafile;
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

/* Destroy the per-port sub-processes. These will terminate when the fifos close */

{
    struct timespec ts;

    if ( !f )
    {
        return;
    }

    for ( int t = 0; t < NUM_CHANNELS + 1; t++ )
    {
        if ( f->c[t].handle > 0 )
        {
            close( f->c[t].handle );

            clock_gettime( CLOCK_REALTIME, &ts );
            /* Wait a max of one second for the thread to exit */
            ts.tv_sec += 1;

#ifdef OSX
            pthread_join( f->c[t].thread, NULL );
#else
            pthread_timedjoin_np( f->c[t].thread, NULL, &ts );
#endif

            if ( ! f->permafile )
            {
                unlink( f->c[t].fifoName );
            }
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
void fifoUsePermafiles( struct fifosHandle *f, bool usePermafilesSet )

{
    f->permafile = usePermafilesSet;
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
