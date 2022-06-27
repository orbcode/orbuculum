#ifndef _STREAM_WIN32_H_
#define _STREAM_WIN32_H_

#include <Windows.h>
#include <stdint.h>
#include <stdbool.h>
#include "stream.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Win32Stream
{
    struct Stream base;
    HANDLE source;
    HANDLE readDoneEvent;
    uint64_t readOffset;
};

bool streamWin32Initialize( struct Win32Stream* stream, HANDLE sourceHandle );
void streamWin32Close( struct Win32Stream* stream );

#ifdef __cplusplus
}
#endif


#endif