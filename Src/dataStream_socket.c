#include "dataStream.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifdef WIN32
    #include <winsock2.h>
#else
    #include <sys/ioctl.h>
    #include <netinet/in.h>
    #include <netdb.h>
#endif

#include "generics.h"


struct SocketDataStream
{
    struct DataStream base;
    int socket;
};

#define SELF(stream) ((struct SocketDataStream*)(stream))

#ifdef WIN32
    // https://stackoverflow.com/a/14388707/995351
    #define SO_REUSEPORT SO_REUSEADDR
#endif

static enum ReceiveResult socketStreamReceive(struct DataStream* stream, void* buffer, size_t bufferSize, struct timeval* timeout, size_t* receivedSize)
{
    struct SocketDataStream* self = SELF(stream);

    *receivedSize = 0;

    fd_set readFd;
    FD_ZERO(&readFd);
    FD_SET(self->socket, &readFd);

    int r = select(self->socket + 1, &readFd, NULL, NULL, timeout);

    if(r < 0)
    {
        return RECEIVE_RESULT_ERROR;
    }

    if(r == 0)
    {
        *receivedSize = 0;
        return RECEIVE_RESULT_TIMEOUT;
    }

    *receivedSize = recv(self->socket, buffer, bufferSize, 0);
    return RECEIVE_RESULT_OK;
}

static void socketStreamClose(struct DataStream* stream)
{
    struct SocketDataStream* self = SELF(stream);
    close(self->socket);
}

static int socketStreamCreate(const char* server, int port)
{
    #ifdef WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    #endif

    int sockfd = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );

    int flag = 1;
    setsockopt( sockfd, SOL_SOCKET, SO_REUSEPORT, (const void*)&flag, sizeof( flag ) );

    if ( sockfd < 0 )
    {
        genericsReport( V_ERROR, "Error creating socket" EOL );
        return -1;
    }

    struct hostent* serverEnt = gethostbyname( server );
    if ( !serverEnt )
    {
        close(sockfd);
        genericsReport( V_ERROR, "Cannot find host" EOL );
        return -1;
    }

    struct sockaddr_in serv_addr;
    /* Now open the network connection */
    memset( &serv_addr, 0, sizeof( serv_addr ) );

    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, serverEnt->h_addr, serverEnt->h_length);
    serv_addr.sin_port = htons(port);

    if ( connect( sockfd, ( struct sockaddr * ) &serv_addr, sizeof( serv_addr ) ) < 0 )
    {
        close(sockfd);
        genericsReport( V_ERROR, "Could not connect" EOL );
        return -1;
    }

    return sockfd;
}

struct DataStream* dataStreamCreateSocket(const char* server, int port)
{
    struct SocketDataStream* stream = SELF(calloc(1, sizeof(struct SocketDataStream)));

    if(stream == NULL)
    {
        return NULL;
    }

    stream->base.receive = socketStreamReceive;
    stream->base.close = socketStreamClose;
    stream->socket = socketStreamCreate(server, port);

    if(stream->socket == -1)
    {
        free(stream);
        return NULL;
    }

    return &stream->base;
}