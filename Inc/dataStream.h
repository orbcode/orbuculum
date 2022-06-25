#ifndef _DATA_STREAM_H_
#define _DATA_STREAM_H_

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

struct DataStream
{
    enum ReceiveResult (*receive)(struct DataStream* stream, void* buffer, size_t bufferSize, struct timeval* timeout, size_t* receivedSize);
    void (*close)(struct DataStream* stream);
};

struct DataStream* dataStreamCreateSocket(const char* server, int port);
struct DataStream* dataStreamCreateFile(const char* file);

#ifdef __cplusplus
}
#endif

#endif