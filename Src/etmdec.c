/*
 * ETM Decode
 * ==========
 *
 * Copyright (C) 2017, 2019, 2021  Dave Marples  <dave@marples.net>
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
#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#include "git_version_info.h"
#include "generics.h"
#include "tpiuDecoder.h"
#include "etmDecoder.h"

struct etmdecHandle

{
    /* The decoders and the packets from them */
    struct TPIUDecoder t;
    struct TPIUPacket p;
    struct ETMDecoder i;

    /* Configuration information */
    int tpiuETMChannel;                           /* TPIU channel on which ETM appears */
    bool useTPIU;                                 /* If we should use the TPIU at all */
};

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Private routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
static void _etmdecPumpProcess( struct etmdecHandle *f, char c )

{
    enum ETMDecoderPumpEvent e;
    struct ETMCPUState *cpu = ETMCPUState( &f->i );

    switch ( ( e = ETMDecoderPump( &f->i, c ) ) )
    {
        // ------------------------------------
        case ETM_EV_NONE:
            break;

        // ------------------------------------
        case ETM_EV_UNSYNCED:
            genericsReport( V_WARN, "ETM Lost Sync (%d)" EOL, ETMDecoderGetStats( &f->i )->lostSyncCount );
            break;

        // ------------------------------------
        case ETM_EV_SYNCED:
            genericsReport( V_INFO, "ETM In Sync (%d)" EOL, ETMDecoderGetStats( &f->i )->syncCount );
            break;

        // ------------------------------------
        case ETM_EV_ERROR:
            genericsReport( V_WARN, "ETM Error" EOL );
            break;

        // ------------------------------------
        case ETM_EV_MSG_RXED:
            if ( ETMStateChanged( &f->i, EV_CH_ADDRESS ) )
            {
                printf( "JUMP TO %08x" EOL, cpu->addr );
            }

            if ( ETMStateChanged( &f->i, EV_CH_ENATOMS ) )
            {
                printf( "EXECUTE %d INSTRUCTIONS" EOL, cpu->eatoms + cpu->natoms );
            }

            break;

        // ------------------------------------
        default:
            genericsReport( V_WARN, "ETM Unknown message (%d)" EOL, e );
            break;
    }
}
// ====================================================================================================
static void _tpiuProtocolPump( struct etmdecHandle *f, uint8_t c )

{
    switch ( TPIUPump( &f->t, c ) )
    {
        // ------------------------------------
        case TPIU_EV_NEWSYNC:
            genericsReport( V_INFO, "TPIU In Sync (%d)" EOL, TPIUDecoderGetStats( &f->t )->syncCount );

        // This fall-through is deliberate
        case TPIU_EV_SYNCED:
            break;

        // ------------------------------------
        case TPIU_EV_RXING:
        case TPIU_EV_NONE:
            break;

        // ------------------------------------
        case TPIU_EV_UNSYNCED:
            genericsReport( V_INFO, "TPIU Lost Sync (%d)" EOL, TPIUDecoderGetStats( &f->t )->lostSync );
            ETMDecoderForceSync( &f->i, false );
            break;

        // ------------------------------------
        case TPIU_EV_RXEDPACKET:
            if ( !TPIUGetPacket( &f->t, &f->p ) )
            {
                genericsReport( V_WARN, "TPIUGetPacket fell over" EOL );
            }

            for ( uint32_t g = 0; g < f->p.len; g++ )
            {
                if ( f->p.packet[g].s == f->tpiuETMChannel )
                {
                    _etmdecPumpProcess( f, f->p.packet[g].d );
                    continue;
                }

                /* Its perfectly legal for TPIU channels to arrive that we aren't interested in */
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
struct TPIUCommsStats *etmdecCommsStats( struct etmdecHandle *f )

{
    return TPIUGetCommsStats( &f->t );
}
// ====================================================================================================
void etmdecProtocolPump( struct etmdecHandle *f, uint8_t c )

/* Top level protocol pump */

{
    if ( f->useTPIU )
    {
        _tpiuProtocolPump( f, c );
    }
    else
    {
        _etmdecPumpProcess( f, c );
    }
}
// ====================================================================================================
void etmdecSetupTPIUChannel( struct etmdecHandle *f, int channel )

{
    f->tpiuETMChannel = channel;
}
// ====================================================================================================
bool etmdecInit( struct etmdecHandle *f, bool useTPIU, int TPIUchannelSet )

{
    memset( f, 0, sizeof( struct etmdecHandle  ) );
    f->tpiuETMChannel = TPIUchannelSet;
    f->useTPIU = useTPIU;

    /* Reset the TPIU handler before we start */
    TPIUDecoderInit( &f->t );
    ETMDecoderInit( &f->i, false );

    return true;
}
// ====================================================================================================
