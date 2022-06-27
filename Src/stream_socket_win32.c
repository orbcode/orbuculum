#include <winsock2.h>

#include "stream_win32.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "generics.h"


struct Win32SocketStream
{
    struct Win32Stream base;
};

#define SELF( stream ) ( ( struct Win32SocketStream* )( stream ) )

// https://stackoverflow.com/a/14388707/995351
#define SO_REUSEPORT SO_REUSEADDR

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Private routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================


// ====================================================================================================
static void _win32SocketStreamClose( struct Stream *stream )
{
    struct Win32SocketStream *self = SELF( stream );

    closesocket( (intptr_t)self->base.source );
    self->base.source = INVALID_HANDLE_VALUE;

    streamWin32Close( &self->base );
}

// ====================================================================================================
static HANDLE _win32SocketStreamCreate( const char *server, int port )
{
    WSADATA wsaData;
    WSAStartup( MAKEWORD( 2, 2 ), &wsaData );

    int sockfd = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );

    int flag = 1;
    setsockopt( sockfd, SOL_SOCKET, SO_REUSEPORT, ( const void * )&flag, sizeof( flag ) );

    if ( sockfd < 0 )
    {
        genericsReport( V_ERROR, "Error creating socket" EOL );
        return INVALID_HANDLE_VALUE;
    }

    struct hostent *serverEnt = gethostbyname( server );

    if ( !serverEnt )
    {
        close( sockfd );
        genericsReport( V_ERROR, "Cannot find host" EOL );
        return INVALID_HANDLE_VALUE;
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
        genericsReport( V_ERROR, "Could not connect" EOL );
        return INVALID_HANDLE_VALUE;
    }

    return ( HANDLE )( intptr_t )sockfd;
}

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Publicly available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================

// ====================================================================================================
struct Stream *streamCreateSocket( const char *server, int port )
{
    struct Win32SocketStream *stream = SELF( calloc( 1, sizeof( struct Win32SocketStream ) ) );

    if ( stream == NULL )
    {
        return NULL;
    }

    if( !streamWin32Initialize( (struct Win32Stream* )stream,  _win32SocketStreamCreate( server, port ) ) )
    {
        free( stream );
        return NULL;
    }

    stream->base.base.close = _win32SocketStreamClose;

    return &stream->base.base;
}
// ====================================================================================================
