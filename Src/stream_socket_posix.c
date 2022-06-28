#include "stream.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netdb.h>

#include "generics.h"


struct PosixSocketStream
{
    struct Stream base;
    int socket;
};

#define SELF(stream) ((struct PosixSocketStream*)(stream))

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Private routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
static enum ReceiveResult _posixSocketStreamReceive( struct Stream *stream, void *buffer, size_t bufferSize,
        struct timeval *timeout, size_t *receivedSize )
{
    struct PosixSocketStream *self = SELF( stream );

    *receivedSize = 0;

    fd_set readFd;
    FD_ZERO( &readFd );
    FD_SET( self->socket, &readFd );

    int r = select( self->socket + 1, &readFd, NULL, NULL, timeout );

    if ( r < 0 )
    {
        return RECEIVE_RESULT_ERROR;
    }

    if ( r == 0 )
    {
        *receivedSize = 0;
        return RECEIVE_RESULT_TIMEOUT;
    }

    ssize_t result = recv( self->socket, buffer, bufferSize, 0 );

    if ( result <= 0 )
    {
        // report connection broken as error
        return RECEIVE_RESULT_ERROR;
    }

    *receivedSize = result;
    return RECEIVE_RESULT_OK;
}

// ====================================================================================================
static void _posixSocketStreamClose( struct Stream *stream )
{
    struct PosixSocketStream *self = SELF( stream );
    close( self->socket );
}

// ====================================================================================================
static int _posixSocketStreamCreate( const char *server, int port )
{
    int sockfd = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );

    int flag = 1;
    setsockopt( sockfd, SOL_SOCKET, SO_REUSEPORT, ( const void * )&flag, sizeof( flag ) );

    if ( sockfd < 0 )
    {
        genericsReport( V_ERROR, "Error creating socket" EOL );
        return -1;
    }

    struct hostent *serverEnt = gethostbyname( server );

    if ( !serverEnt )
    {
        close( sockfd );
        genericsReport( V_ERROR, "Cannot find host" EOL );
        return -1;
    }

    struct sockaddr_in serv_addr;

    /* Now open the network connection */
    memset( &serv_addr, 0, sizeof( serv_addr ) );

    serv_addr.sin_family = AF_INET;

    memcpy( &serv_addr.sin_addr.s_addr, serverEnt->h_addr, serverEnt->h_length );

    serv_addr.sin_port = htons( port );

    if ( connect( sockfd, ( struct sockaddr * ) &serv_addr, sizeof( serv_addr ) ) < 0 )
    {
        close( sockfd );
        return -1;
    }

    return sockfd;
}

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Publicly available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================

struct Stream *streamCreateSocket( const char *server, int port )
{
    struct PosixSocketStream *stream = SELF( calloc( 1, sizeof( struct PosixSocketStream ) ) );

    if ( stream == NULL )
    {
        return NULL;
    }

    stream->base.receive = _posixSocketStreamReceive;
    stream->base.close = _posixSocketStreamClose;
    stream->socket = _posixSocketStreamCreate( server, port );

    if ( stream->socket == -1 )
    {
        free( stream );
        return NULL;
    }

    return &stream->base;
}
// ====================================================================================================
