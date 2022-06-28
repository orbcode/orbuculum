#include "stream_win32.h"

#define SELF( stream ) ( ( struct Win32Stream* )( stream ) )

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Private routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================

// ====================================================================================================
static uint32_t _convertTimeout( const struct timeval *timeout )
{
    if ( timeout != NULL )
    {
        return ( timeout->tv_sec * ( uint64_t )1000 ) + ( timeout->tv_usec / 1000 );
    }
    else
    {
        return INFINITE;
    }
}

// ====================================================================================================
static enum ReceiveResult _win32StreamReceive( struct Stream *stream, void *buffer, size_t bufferSize,
        struct timeval *timeout, size_t *receivedSize )
{
    struct Win32Stream *self = SELF( stream );

    *receivedSize = 0;

    OVERLAPPED asyncRead;
    memset( &asyncRead, 0, sizeof( asyncRead ) );
    asyncRead.hEvent = self->readDoneEvent;
    asyncRead.Offset = self->readOffset & UINT32_MAX;
    asyncRead.OffsetHigh = ( self->readOffset >> 32 ) & UINT32_MAX;

    DWORD bytesRead;
    bool readResult = ReadFile( self->source, buffer, bufferSize, &bytesRead, &asyncRead );

    if ( readResult )
    {
        *receivedSize = bytesRead;
        asyncRead.Offset += bytesRead;
        return RECEIVE_RESULT_OK;
    }

    if ( GetLastError() != ERROR_IO_PENDING )
    {
        return RECEIVE_RESULT_ERROR;
    }

    // async operation

    DWORD waitResult = WaitForSingleObjectEx( self->readDoneEvent, _convertTimeout( timeout ), true );

    if ( waitResult == WAIT_TIMEOUT )
    {
        CancelIoEx( self->source, &asyncRead );
        WaitForSingleObjectEx( self->readDoneEvent, INFINITE, true );
        GetOverlappedResult( self->source, &asyncRead, &bytesRead, true );
        return RECEIVE_RESULT_TIMEOUT;
    }

    if ( GetOverlappedResult( self->source, &asyncRead, &bytesRead, true ) == 0 )
    {
        if ( GetLastError() != ERROR_HANDLE_EOF )
        {
            return RECEIVE_RESULT_ERROR;
        }
    }

    *receivedSize = bytesRead;

    if ( bytesRead == 0 )
    {
        if ( GetLastError() == ERROR_HANDLE_EOF )
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

// ====================================================================================================
static void _win32StreamCloseInner( struct Stream *stream )
{
    struct Win32Stream *self = SELF( stream );
    streamWin32Close( self );
}

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Publicly available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================

// ====================================================================================================
bool streamWin32Initialize( struct Win32Stream *stream, HANDLE sourceHandle )
{
    if ( sourceHandle == INVALID_HANDLE_VALUE )
    {
        return false;
    }

    stream->base.receive = _win32StreamReceive;
    stream->base.close = _win32StreamCloseInner;
    stream->source = sourceHandle;
    stream->readDoneEvent = CreateEvent( NULL, FALSE, FALSE, NULL );

    return true;
}

// ====================================================================================================
void streamWin32Close( struct Win32Stream *stream )
{
    if ( stream->source != INVALID_HANDLE_VALUE )
    {
        CancelIo( stream->source );
        CloseHandle( stream->source );
        stream->source = INVALID_HANDLE_VALUE;
    }

    CloseHandle( stream->readDoneEvent );
    stream->readDoneEvent = INVALID_HANDLE_VALUE;
}