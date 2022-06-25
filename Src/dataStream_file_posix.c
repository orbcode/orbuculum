#include "dataStream.h"
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include "generics.h"


struct PosixFileDataStream
{
    struct DataStream base;
    int file;
};

#define SELF(stream) ((struct PosixFileDataStream*)(stream))

static enum ReceiveResult posixFileStreamReceive(struct DataStream* stream, void* buffer, size_t bufferSize, struct timeval* timeout, size_t* receivedSize)
{
    struct PosixFileDataStream* self = SELF(stream);
    fd_set readFd;
    FD_ZERO(&readFd);
    FD_SET(self->file, &readFd);

    int r = select(self->file + 1, &readFd, NULL, NULL, timeout);

    if(r < 0)
    {
        return RECEIVE_RESULT_ERROR;
    }

    if(r == 0)
    {
        *receivedSize = 0;
        return RECEIVE_RESULT_TIMEOUT;
    }

    *receivedSize = read(self->file, buffer, bufferSize);
    if(*receivedSize == 0)
    {
        return RECEIVE_RESULT_EOF;
    }

    return RECEIVE_RESULT_OK;
}

static void posixFileStreamClose(struct DataStream* stream)
{
    struct PosixFileDataStream* self = SELF(stream);
    close(self->file);
}

static int posixFileStreamCreate(const char* file)
{
    int f = open(file, O_RDONLY);

    if (f < 0)
    {
        genericsExit( -4, "Can't open file %s" EOL, file );
    }

    return f;
}

struct DataStream* dataStreamCreateFile(const char* file)
{
    struct PosixFileDataStream* stream = SELF(calloc(1, sizeof(struct PosixFileDataStream)));

    if(stream == NULL)
    {
        return NULL;
    }

    stream->base.receive = posixFileStreamReceive;
    stream->base.close = posixFileStreamClose;
    stream->file = posixFileStreamCreate(file);

    if(stream->file == -1)
    {
        free(stream);
        return NULL;
    }

    return &stream->base;
}