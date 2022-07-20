#include "stream.h"
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include "generics.h"


struct PosixFileStream
{
    struct Stream base;
    int file;
};

#define SELF(stream) ((struct PosixFileStream*)(stream))

// ====================================================================================================
static enum ReceiveResult _posixFileStreamReceive( struct Stream *stream, void *buffer, size_t bufferSize,
        struct timeval *timeout, size_t *receivedSize )
{
    struct PosixFileStream *self = SELF( stream );
    fd_set readFd;
    FD_ZERO( &readFd );
    FD_SET( self->file, &readFd );

    int r = select( self->file + 1, &readFd, NULL, NULL, timeout );

    if ( r < 0 )
    {
        return RECEIVE_RESULT_ERROR;
    }

    if ( r == 0 )
    {
        *receivedSize = 0;
        return RECEIVE_RESULT_TIMEOUT;
    }

    *receivedSize = read( self->file, buffer, bufferSize );

    if ( *receivedSize == 0 )
    {
        return RECEIVE_RESULT_EOF;
    }

    return RECEIVE_RESULT_OK;
}

// ====================================================================================================
static void _posixFileStreamClose( struct Stream *stream )
{
    struct PosixFileStream *self = SELF( stream );
    close( self->file );
}

// ====================================================================================================
static int _posixFileStreamCreate( const char *file )
{
    int f = open( file, O_RDONLY );

    if ( f < 0 )
    {
        genericsExit( -4, "Can't open file %s" EOL, file );
    }

    return f;
}

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Publicly available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================

// Malloc leak is deliberately ignored. That is the central purpose of this code!
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-malloc-leak"

struct Stream *streamCreateFile( const char *file )
{
    struct PosixFileStream *stream = SELF( calloc( 1, sizeof( struct PosixFileStream ) ) );

    if ( stream == NULL )
    {
        return NULL;
    }

    stream->base.receive = _posixFileStreamReceive;
    stream->base.close = _posixFileStreamClose;
    stream->file = _posixFileStreamCreate( file );

    if ( stream->file == -1 )
    {
        free( stream );
        return NULL;
    }

    return &stream->base;
}
#pragma GCC diagnostic pop
// ====================================================================================================
