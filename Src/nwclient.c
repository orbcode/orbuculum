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

#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <strings.h>
#include "generics.h"
#include "nwclient.h"

/* List of any connected network clients */
struct nwClient

{
    int handle;                               /* Handle to client */
    pthread_t thread;                         /* Execution thread */
    struct nwclientHandle *parent;            /* Who owns this list */
    struct nwClient *nextClient;
    struct nwClient *prevClient;
};

/* Informtation about each individual network client */
struct nwClientParams

{
    struct nwClient *client;                  /* Information about the client */
    int portNo;                               /* Port of connection */
    int listenHandle;                         /* Handle for listener */
};

// ====================================================================================================
// Network server implementation for raw SWO feed
// ====================================================================================================
static void *_client( void *args )

/* Handle an individual network client account */

{
    struct nwClientParams *params = ( struct nwClientParams * )args;
    int readDataLen;
    uint8_t maxTransitPacket[TRANSFER_SIZE];

    while ( 1 )
    {
        readDataLen = read( params->listenHandle, maxTransitPacket, TRANSFER_SIZE );

        if ( ( readDataLen <= 0 ) || ( write( params->portNo, maxTransitPacket, readDataLen ) < 0 ) )
        {
            /* This port went away, so remove it */
            genericsReport( V_INFO, "Connection dropped" EOL );

            close( params->portNo );
            close( params->listenHandle );

            /* First of all, make sure we can get access to the client list */
            sem_wait( &params->client->parent->clientList );

            if ( params->client->prevClient )
            {
                params->client->prevClient->nextClient = params->client->nextClient;
            }
            else
            {
                params->client->parent->firstClient = params->client->nextClient;
            }

            if ( params->client->nextClient )
            {
                params->client->nextClient->prevClient = params->client->prevClient;
            }

            /* OK, we made our modifications */
            sem_post( &params->client->parent->clientList );

            return NULL;
        }
    }
}
// ====================================================================================================
static void *_listenTask( void *arg )

{
    struct nwclientHandle *h = ( struct nwclientHandle * )arg;
    int newsockfd;
    socklen_t clilen;
    struct sockaddr_in cli_addr;
    int f[2];                               /* File descriptor set for pipe */
    struct nwClientParams *params;
    char s[100];

    while ( 1 )
    {
        listen( h->sockfd, 5 );
        clilen = sizeof( cli_addr );
        newsockfd = accept( h->sockfd, ( struct sockaddr * ) &cli_addr, &clilen );

        inet_ntop( AF_INET, &cli_addr.sin_addr, s, 99 );
        genericsReport( V_INFO, "New connection from %s" EOL, s );

        /* We got a new connection - spawn a thread to handle it */
        if ( !pipe( f ) )
        {
            params = ( struct nwClientParams * )malloc( sizeof( struct nwClientParams ) );

            params->client = ( struct nwClient * )malloc( sizeof( struct nwClient ) );
            params->client->handle = f[1];
            params->client->parent = h;
            params->listenHandle = f[0];
            params->portNo = newsockfd;


            if ( !pthread_create( &( params->client->thread ), NULL, &_client, params ) )
            {
                /* Auto-cleanup for this thread */
                pthread_detach( params->client->thread );

                /* Hook into linked list */
                sem_wait( &h->clientList );

                params->client->nextClient = h->firstClient;
                params->client->prevClient = NULL;

                if ( params->client->nextClient )
                {
                    params->client->nextClient->prevClient = params->client;
                }

                h->firstClient = params->client;

                sem_post( &h->clientList );
            }
        }
    }

    return NULL;
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Externally available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
void nwclientSend( struct nwclientHandle *h, uint32_t len, uint8_t *buffer )

{
    assert( h );
    struct nwClient *n = h->firstClient;

    sem_wait( &h->clientList );

    while ( n )
    {
        write( n->handle, buffer, len );
        n = n->nextClient;
    }

    sem_post( &h->clientList );
}
// ====================================================================================================
bool nwclientStop( struct nwclientHandle *h )

{

    return true;
}
// ====================================================================================================
bool nwclientStart( struct nwclientHandle *h, int port )

/* Creating the listening server thread */

{
    struct sockaddr_in serv_addr;
    int flag = 1;

    assert( h );

    h->sockfd = socket( AF_INET, SOCK_STREAM, 0 );
    setsockopt( h->sockfd, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof( flag ) );

    if ( h->sockfd < 0 )
    {
        genericsReport( V_ERROR, "Error opening socket" EOL );
        return false;
    }

    bzero( ( char * ) &serv_addr, sizeof( serv_addr ) );
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons( port );

    if ( setsockopt( h->sockfd, SOL_SOCKET, SO_REUSEADDR, &( int )
{
    1
}, sizeof( int ) ) < 0 )
    {
        genericsReport( V_ERROR, "setsockopt(SO_REUSEADDR) failed" );
        return false;
    }

    if ( bind( h->sockfd, ( struct sockaddr * ) &serv_addr, sizeof( serv_addr ) ) < 0 )
    {
        genericsReport( V_ERROR, "Error on binding" EOL );
        return false;
    }

    /* Create a semaphore to lock the client list */
    sem_init( &h->clientList, 0, 1 );

    /* We have the listening socket - spawn a thread to handle it */
    if ( pthread_create( &( h->ipThread ), NULL, &_listenTask, h ) )
    {
        genericsReport( V_ERROR, "Failed to create listening thread" EOL );
        return false;
    }

    return true;
}
// ====================================================================================================
struct nwclientHandle *nwclientInit( )

{
    return ( struct nwclientHandle * )calloc( 1, sizeof( struct nwclientHandle ) );
}
// ====================================================================================================
