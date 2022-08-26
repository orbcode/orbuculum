/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Network Server support
 * ======================
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <threads.h>
#ifdef WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netdb.h>
    #include <arpa/inet.h>
    #include <linux/tcp.h>
    #include <string.h>
#endif
#include <assert.h>
#include <strings.h>
#include <stdio.h>
#include "generics.h"
#include "nwclient.h"


#ifdef WIN32
    // https://stackoverflow.com/a/14388707/995351
    #define SO_REUSEPORT SO_REUSEADDR
    #define MSG_NOSIGNAL 0
#endif

/* Shared ring buffer for data */
#define SHARED_BUFFER_SIZE (8*TRANSFER_SIZE)

/* Master structure for the set of nwclients */
struct nwclientsHandle

{
    volatile struct nwClient *firstClient;    /* Head of linked list of network clients */
    mtx_t                     clientList;     /* Lock for list of network clients */

    int                       wp;             /* Next write to shared buffer */
    uint8_t sharedBuffer[SHARED_BUFFER_SIZE]; /* Data waiting to be sent to the clients */

    int                       sockfd;         /* The socket for the inferior */
    thrd_t                    ipThread;       /* The listening thread for n/w clients */
    bool                      finish;         /* Its time to leave */
};

/* Descriptor for individual connected network clients */
struct nwClient

{
    int                       handle;         /* Handle to client */
    thrd_t                    thread;         /* Execution thread */
    struct nwclientsHandle   *parent;         /* Who owns this list */
    volatile struct nwClient *nextClient;
    volatile struct nwClient *prevClient;
    bool                      finish;        /* Flag indicating it's time to cease operation */
    mtx_t                     dataAvailable; /* Semaphore to say there's stuff to process */

    /* Parameters used to run the client */
    int                       portNo;        /* Port of connection */
    int                       rp;            /* Current read pointer in data stream */
};

// ====================================================================================================
// Network server implementation for raw SWO feed
// ====================================================================================================
static void _clientRemove( struct nwClient *c )

{
    const struct timespec ts = {.tv_sec = 1, .tv_nsec = 0};

    close( c->portNo );

    /* First of all, make sure we can get access to the client list */

    if ( thrd_success != mtx_timedlock( &c->parent->clientList, &ts ) )
    {
        genericsExit( -1, "Failed to acquire mutex" EOL );
    }

    if ( c->prevClient )
    {
        c->prevClient->nextClient = c->nextClient;
    }
    else
    {
        c->parent->firstClient = c->nextClient;
    }

    if ( c->nextClient )
    {
        c->nextClient->prevClient = c->prevClient;
    }

    /* OK, we made our modifications */
    if ( thrd_success != mtx_unlock( &c->parent->clientList ) )
    {
        genericsExit( -1, "Failed to unlock mutex" EOL );
    }


    /* Remove the memory that was allocated for this client */
    free( c );
}
// ====================================================================================================
static int _client( void *args )

/* Handle an individual network client account */

{
    struct nwClient *c = ( struct nwClient * )args;
    ssize_t readDataLen;
    uint8_t *p;
    ssize_t sent = 0;

    while ( !c->finish )
    {
        /* Spin until we're told there's something to send along */
        if ( thrd_success != mtx_lock( &c->dataAvailable ) )
        {
            genericsExit( -1, "Failed to lock dataAvailable mutex" EOL );
        }

        while ( c->rp != c->parent->wp )
        {
            /* Data to send is either to the end of the ring buffer or to the wp, whichever is first */
            readDataLen = ( c->rp < c->parent->wp ) ? c->parent->wp - c->rp : SHARED_BUFFER_SIZE - c->rp;
            p = &( c->parent->sharedBuffer[c->rp] );
            c->rp = ( c->rp + readDataLen ) % SHARED_BUFFER_SIZE;

            while ( ( readDataLen > 0 ) && ( sent >= 0 ) )
            {
                sent = send( c->portNo, ( const void * )p, readDataLen, MSG_NOSIGNAL );
                p += sent;
                readDataLen -= sent;
            }

            if ( c->finish || readDataLen )
            {
                /* This port went away, so remove it */
                if ( !c->finish )
                {
                    genericsReport( V_INFO, "Connection dropped" EOL );
                }

                c->finish = true;
                break;
            }
        }
    }

    _clientRemove( c );
    return 0;
}
// ====================================================================================================
static void _dataAvailable( volatile struct nwClient *c )

{
    if ( thrd_success != mtx_unlock( ( mtx_t * )&c->dataAvailable ) )
    {
        genericsExit( -1, "Couldn't unlock dataAvailable mutex" EOL );
    }
}

// ====================================================================================================
static int _listenTask( void *arg )

{
    struct nwclientsHandle *h = ( struct nwclientsHandle * )arg;
    const struct timespec ts = {.tv_sec = 1, .tv_nsec = 0};
    int newsockfd;
#ifdef WIN32
    int clilen;
#else
    socklen_t clilen;
#endif
    struct sockaddr_in cli_addr;
    struct nwClient *client;
    char s[100];

    clilen = sizeof( cli_addr );
    listen( h->sockfd, 5 );

    while ( !h->finish )
    {
        newsockfd = accept( h->sockfd, ( struct sockaddr * ) &cli_addr, &clilen );

        if ( h->finish )
        {
            close( newsockfd );
            break;
        }

        inet_ntop( AF_INET, &cli_addr.sin_addr, s, 99 );
        genericsReport( V_INFO, "New connection from %s" EOL, s );

        /* We got a new connection - spawn a thread to handle it */
        client = ( struct nwClient * )calloc( 1, sizeof( struct nwClient ) );
        MEMCHECK( client, 0 );

        client->parent = h;
        client->portNo = newsockfd;
        client->rp     = h->wp;

        if ( thrd_success != mtx_init( &client->dataAvailable, mtx_plain ) )
        {
            genericsExit( -1, "Failed to establish dataAvailable mutex" EOL );
        }

        if ( thrd_success != thrd_create( &( client->thread ), &_client, client ) )
        {
            genericsExit( -1, "Failed to create thread" EOL );
        }

        /* Hook into linked list */
        if ( thrd_success != mtx_timedlock( &h->clientList, &ts ) )
        {
            genericsExit( -1, "Failed to acquire mutex" EOL );
        }

        client->nextClient = h->firstClient;
        client->prevClient = NULL;

        if ( client->nextClient )
        {
            client->nextClient->prevClient = client;
        }

        h->firstClient = client;

        if ( thrd_success != mtx_unlock( &h->clientList ) )
        {
            genericsExit( -1, "Failed to unlock mutex" EOL );
        }
    }

    close( h->sockfd );
    return 0;
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Externally available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
void nwclientSend( struct nwclientsHandle *h, uint32_t len, uint8_t *ipbuffer )

{
    const struct timespec ts = {.tv_sec = 1, .tv_nsec = 0};
    int newWp = ( h->wp + len );
    int toEnd = ( newWp > SHARED_BUFFER_SIZE ) ? SHARED_BUFFER_SIZE - h->wp : len;
    int fromStart = len - toEnd;

    /* Copy the received data into the shared buffer */
    memcpy( &h->sharedBuffer[h->wp], ipbuffer, toEnd );

    if ( fromStart )
    {
        memcpy( h->sharedBuffer, &ipbuffer[toEnd], fromStart );
    }

    h->wp = newWp % SHARED_BUFFER_SIZE;

    if ( !h->finish )
    {
        if ( thrd_success != mtx_timedlock( &h->clientList, &ts ) )
        {
            genericsExit( -1, "Failed to acquire mutex" EOL );
        }

        /* Now kick all the clients that new data arrived for them to distribute */
        volatile struct nwClient *n = h->firstClient;

        while ( n )
        {
            _dataAvailable( n );
            n = n->nextClient;
        }

        if ( thrd_success != mtx_unlock( &h->clientList ) )
        {
            genericsExit( -1, "Failed to unlock mutex" EOL );
        }
    }
}
// ====================================================================================================
struct nwclientsHandle *nwclientStart( int port )

/* Creating the listening server thread */

{
    struct sockaddr_in serv_addr;
    int flag = 1;
    struct nwclientsHandle *h = ( struct nwclientsHandle * )calloc( 1, sizeof( struct nwclientsHandle ) );
    MEMCHECK( h, NULL );

    h->sockfd = socket( AF_INET, SOCK_STREAM, 0 );
    setsockopt( h->sockfd, SOL_SOCKET, SO_REUSEPORT, ( const void * )&flag, sizeof( flag ) );

    if ( h->sockfd < 0 )
    {
        genericsReport( V_ERROR, "Error opening socket" EOL );
        goto free_and_return;
    }

    memset( ( char * ) &serv_addr, 0, sizeof( serv_addr ) );
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons( port );

    if ( setsockopt( h->sockfd, SOL_SOCKET, SO_REUSEADDR, ( const void * ) &flag, sizeof( flag ) ) < 0 )
    {
        genericsReport( V_ERROR, "setsockopt(SO_REUSEADDR) failed" );
        goto free_and_return;
    }

    if ( bind( h->sockfd, ( struct sockaddr * ) &serv_addr, sizeof( serv_addr ) ) < 0 )
    {
        genericsReport( V_ERROR, "Error on binding" EOL );
        goto free_and_return;
    }

    /* Create a mutex to lock the client list */
    if ( thrd_success != mtx_init( &h->clientList, mtx_timed | mtx_recursive ) )
    {
        genericsExit( -1, "Failed to initialise client list mutex" EOL );
    }

    /* We have the listening socket - spawn a thread to handle it */
    if ( thrd_success != thrd_create( &( h->ipThread ), &_listenTask, h ) )
    {
        genericsReport( V_ERROR, "Failed to create listening thread" EOL );
        goto free_and_return;
    }

    return h;

free_and_return:
    free( h );
    return NULL;
}
// ====================================================================================================
void nwclientShutdown( struct nwclientsHandle *h )

{
    const struct timespec ts = {.tv_sec = 1, .tv_nsec = 0};

    if ( !h )
    {
        return;
    }

    /* Flag that we're ending */
    h->finish = true;

    if ( thrd_success != mtx_timedlock( &h->clientList, &ts ) )
    {
        genericsExit( -1, "Failed to acquire mutex" EOL );
    }

    volatile struct nwClient *c = h->firstClient;

    /* Tell all the clients to terminate */
    while ( c )
    {
        c->finish = true;

        /* Closing the connection will kill the client */
        close( c->handle );

        /* This is safe because we are locked by the mutex */
        c = c->nextClient;
    }

    if ( thrd_success != mtx_unlock( &h->clientList ) )
    {
        genericsExit( -1, "Failed to acquire mutex" EOL );
    }
}
// ====================================================================================================
bool nwclientShutdownComplete( struct nwclientsHandle *h )

{
    if ( ! h->firstClient )
    {
        free( h );
        return true;
    }

    return false;
}
// ====================================================================================================
