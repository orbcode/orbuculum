/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Network Server support
 * ======================
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#ifdef WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netdb.h>
    #include <arpa/inet.h>
    #include <string.h>
    #include <poll.h>
#endif
#ifdef FREEBSD
    #include <sys/types.h>
    #include <sys/socket.h>
#endif
#ifdef LINUX
    #include <linux/tcp.h>
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
    #define MSG_DONTWAIT 0
#endif

#if defined OSX || defined FREEBSD
    #ifndef MSG_NOSIGNAL
        #define MSG_NOSIGNAL 0
    #endif
#endif

/* Master structure for the set of nwclients */
struct nwclientsHandle

{
    volatile struct nwClient *firstClient;    /* Head of linked list of network clients */
    pthread_mutex_t           clientList;     /* Lock for list of network clients */

    int                       sockfd;         /* The socket for the inferior */
    pthread_t                 ipThread;       /* The listening thread for n/w clients */
};

/* Descriptor for individual connected network clients */
struct nwClient

{
    int                       handle;            /* Handle to client */
    struct nwclientsHandle   *parent;            /* Who owns this list */
    volatile struct nwClient *nextClient;
    volatile struct nwClient *prevClient;

    /* Parameters used to run the client */
    int                       portNo;            /* Port of connection */
};

// ====================================================================================================
static int _lock_with_timeout( pthread_mutex_t *mutex, const struct timespec *ts )
{
    int ret;
    int left, step;

    left = ts->tv_sec * 1000;       /* how much waiting is left, in msec */
    step = 10;                      /* msec to sleep at each trywait() failure */

    do
    {
        if ( ( ret = pthread_mutex_trylock( mutex ) ) != 0 )
        {
            struct timespec dly;

            dly.tv_sec = 0;
            dly.tv_nsec = step * 1000000;
            nanosleep( &dly, NULL );

            left -= step;
        }
    }
    while ( ret != 0 && left > 0 );

    return ret;
}

// ====================================================================================================
// Network server implementation for raw SWO feed
// ====================================================================================================
static void _clientRemoveNoLock( volatile struct nwClient *c )

{
    close( c->portNo );

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

    /* Remove the memory that was allocated for this client */
    free( ( void * )c );
}
// ====================================================================================================
static void *_listenTask( void *arg )

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

    while ( true )
    {
        newsockfd = accept( h->sockfd, ( struct sockaddr * ) &cli_addr, &clilen );

        if ( newsockfd < 0 )
        {
            close( newsockfd );
            break;
        }

        inet_ntop( AF_INET, &cli_addr.sin_addr, s, 99 );
        genericsReport( V_INFO, "New connection from %s" EOL, s );

        /* We got a new connection - spawn a record to handle it */
        client = ( struct nwClient * )calloc( 1, sizeof( struct nwClient ) );
        MEMCHECK( client, NULL );

        client->parent = h;
        client->portNo = newsockfd;

        /* Make port non-blocking */
#ifdef WIN32
        unsigned long mode = 0;
        ioctlsocket( newsockfd, FIONBIO, &mode );
#else
        int flags = fcntl( newsockfd, F_GETFL, 0 );

        if ( flags == -1 )
        {
            return false;
        }

        flags |= O_NONBLOCK;
        fcntl( newsockfd, F_SETFL, flags );
#endif

        /* Hook into linked list */
        if ( _lock_with_timeout( &h->clientList, &ts ) < 0 )
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

        pthread_mutex_unlock( &h->clientList );
    }

    if ( h->sockfd )
    {
        close( h->sockfd );
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
void nwclientSend( struct nwclientsHandle *h, uint32_t len, const uint8_t *ipbuffer )

{
    const struct timespec ts = {.tv_sec = 1, .tv_nsec = 0};

    if ( h && h->firstClient )
    {
        if ( _lock_with_timeout( &h->clientList, &ts ) < 0 )
        {
            genericsExit( -1, "Failed to acquire mutex" EOL );
        }

        /* Now kick all the clients that new data arrived for them to distribute */
        volatile struct nwClient *n = h->firstClient;

        while ( n )
        {
            ssize_t t = len;
            ssize_t sent = 0;
            void *p = ( void * )ipbuffer;

            while ( t && ( sent >= 0 ) )
            {
                sent = send( n->portNo, p, t, MSG_NOSIGNAL );
                p += sent;
                t -= sent;
            }

            if ( t )
            {
                volatile struct nwClient *newn = n->nextClient;
                _clientRemoveNoLock( n );
                n = newn;
            }
            else
            {
                n = n->nextClient;
            }
        }

        pthread_mutex_unlock( &h->clientList );
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
    pthread_mutex_init( &h->clientList, NULL );

    /* We have the listening socket - spawn a thread to handle it */
    if ( pthread_create( &( h->ipThread ), NULL, &_listenTask, h ) )
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

    /* Flag that we're ending - this ensures the listening thread dies */
    int tsockfd = h->sockfd;
    h->sockfd = 0;
    close( tsockfd );

    if ( _lock_with_timeout( &h->clientList, &ts ) < 0 )
    {
        genericsExit( -1, "Failed to acquire mutex" EOL );
    }

    /* Shut all the client connections */
    volatile struct nwClient *c = h->firstClient;

    /* Terminate all the clients */
    while ( c )
    {
        volatile struct nwClient *newc = c->nextClient;
        _clientRemoveNoLock( c );
        c = newc;
    }

    pthread_mutex_unlock( &h->clientList );
    free( h );
}
// ====================================================================================================
