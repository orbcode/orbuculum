#include "stream.h"
#include <stdlib.h>
#include <stdio.h>
#include <Windows.h>

#include "generics.h"


struct Win32FileStream
{
    struct Stream base;
    HANDLE file;
    HANDLE readDoneEvent;
    OVERLAPPED asyncRead;
    uint64_t readOffset;
};

#define SELF(stream) ((struct Win32FileStream*)(stream))

static uint32_t convertTimeout(const struct timeval* timeout)
{
    if(timeout != NULL)
    {
        return (timeout->tv_sec * (uint64_t)1000) + (timeout->tv_usec / 1000);
    }
    else
    {
        return INFINITE;
    }
}

static enum ReceiveResult win32FileStreamReceive(struct Stream* stream, void* buffer, size_t bufferSize, struct timeval* timeout, size_t* receivedSize)
{
    struct Win32FileStream* self = SELF(stream);

    *receivedSize = 0;

    OVERLAPPED asyncRead;
    memset(&asyncRead, 0, sizeof(asyncRead));
    asyncRead.hEvent = self->readDoneEvent;
    asyncRead.Offset = self->readOffset & UINT32_MAX;
    asyncRead.OffsetHigh = (self->readOffset >> 32) & UINT32_MAX;

    DWORD bytesRead;
    bool readResult = ReadFile(self->file, buffer, bufferSize, &bytesRead, &asyncRead);

    if(readResult)
    {
        *receivedSize = bytesRead;
        asyncRead.Offset += bytesRead;
        return RECEIVE_RESULT_OK;
    }

    if(GetLastError() != ERROR_IO_PENDING)
    {
        return RECEIVE_RESULT_ERROR;
    }

    // async operation

    DWORD waitResult = WaitForSingleObjectEx(self->readDoneEvent, convertTimeout(timeout), true);
    if(waitResult == WAIT_TIMEOUT)
    {
        CancelIoEx(self->file, &asyncRead);
        WaitForSingleObjectEx(self->readDoneEvent, INFINITE, true);
    }

    if((GetOverlappedResult(self->file, &asyncRead, &bytesRead, true) == 0))
    {
        if(GetLastError() != ERROR_HANDLE_EOF)
        {
            return RECEIVE_RESULT_ERROR;
        }
    }

    *receivedSize = bytesRead;

    if(bytesRead == 0)
    {
        if(GetLastError() == ERROR_HANDLE_EOF)
        {
            return RECEIVE_RESULT_EOF;
        }
        else
        {
            return RECEIVE_RESULT_TIMEOUT;
        }
    }

    self->readOffset += bytesRead;
    return RECEIVE_RESULT_OK;
}

static void win32FileStreamClose(struct Stream* stream)
{
    struct Win32FileStream* self = SELF(stream);
    CancelIo(self->file);
    CloseHandle(self->readDoneEvent);
    CloseHandle(self->file);
}

static HANDLE win32FileStreamCreate(const char* file)
{
    HANDLE h = CreateFile(
        file,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        NULL
    );

    return h;
}

struct Stream* streamCreateFile(const char* file)
{
    struct Win32FileStream* stream = SELF(calloc(1, sizeof(struct Win32FileStream)));

    if(stream == NULL)
    {
        return NULL;
    }

    stream->base.receive = win32FileStreamReceive;
    stream->base.close = win32FileStreamClose;
    stream->file = win32FileStreamCreate(file);

    if(stream->file == INVALID_HANDLE_VALUE)
    {
        free(stream);
        return NULL;
    }

    stream->readDoneEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    return &stream->base;
}