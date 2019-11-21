/*
 * Network Server support
 * ======================
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

#ifndef _NW_CLIENT_
#define _NW_CLIENT_

#include "generics.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <semaphore.h>
  
// ====================================================================================================

#define NWCLIENT_SERVER_PORT (3443)           /* Server port definition */
#define TRANSFER_SIZE (4096)
  
struct nwclientHandle

{
    struct nwClient *firstClient;             /* Head of linked list of network clients */
    sem_t clientList;                         /* Locking semaphore for list of network clients */

    int sockfd;                               /* The socket for the inferior */
    pthread_t ipThread;                       /* The listening thread for n/w clients */
};

// ====================================================================================================

void nwclientSend( struct nwclientHandle *h, uint32_t len, uint8_t *buffer );

bool nwclientStop( struct nwclientHandle *h );
bool nwclientStart( struct nwclientHandle *h, int port );
struct nwclientHandle *nwclientInit( void );

// ====================================================================================================
#ifdef __cplusplus
}
#endif
#endif
