#ifndef _STREAM_H_
#define _STREAM_H_

#include <stddef.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ReceiveResult
{
    RECEIVE_RESULT_OK,
    RECEIVE_RESULT_TIMEOUT,
    RECEIVE_RESULT_EOF,
    RECEIVE_RESULT_ERROR,
};

struct Stream
{
    enum ReceiveResult ( *receive )( struct Stream *stream, void *buffer, size_t bufferSize,
                                     struct timeval *timeout, size_t *receivedSize );
    void ( *close )( struct Stream *stream );
};

struct Stream *streamCreateSocket( const char *server, int port );
struct Stream *streamCreateFile( const char *file );

#ifdef __cplusplus
}
#endif

#endif
