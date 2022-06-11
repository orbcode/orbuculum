#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <Windows.h>
#include "tpiuDecoder.h"
#include "nw.h"
#include "orbuculumOptions.h"


static bool setSerialSpeed(HANDLE handle, int speed)
{
    DCB dcb;
    SecureZeroMemory(&dcb, sizeof(DCB));
    dcb.DCBlength = sizeof(DCB);
    BOOL ok = GetCommState(handle, &dcb);

    if(!ok)
    {
        return false;
    }

    dcb.BaudRate = speed;
    dcb.ByteSize = 8;
    dcb.Parity   = NOPARITY;
    dcb.StopBits = ONESTOPBIT;

    ok = SetCommState(handle, &dcb);

    if(!ok)
    {
        return false;
    }

    // Set timeouts
    COMMTIMEOUTS timeouts;
    ok = GetCommTimeouts(handle, &timeouts);
    if(!ok)
    {
        return false;
    }
    timeouts.ReadIntervalTimeout         = 0;
    timeouts.ReadTotalTimeoutConstant    = 0;
    timeouts.ReadTotalTimeoutMultiplier  = 0;
    ok = SetCommTimeouts(handle, &timeouts);
    if(!ok)
    {
        return false;
    }

    return true;
}

int serialFeeder( struct RunTime *r )
{
    char portPath[MAX_PATH] = { 0 };
    snprintf(portPath, sizeof(portPath), "\\\\.\\%s", r->options->port);
    while ( !r->ending )
    {
        HANDLE portHandle = CreateFile(portPath,
                      GENERIC_READ,
                      0,      //  must be opened with exclusive-access
                      NULL,   //  default security attributes
                      OPEN_EXISTING, //  must use OPEN_EXISTING
                      0,    //  not overlapped I/O
                      NULL ); //  hTemplate must be NULL for comm devices

        if(portHandle == INVALID_HANDLE_VALUE) {
            genericsExit( 1, "Can't open serial port" EOL );
        }

        genericsReport( V_INFO, "Port opened" EOL );

        if(!setSerialSpeed(portHandle, r->options->speed))
        {
            genericsExit( 2, "setSerialConfig failed" EOL );
        }

        SetCommMask(portHandle, EV_RXCHAR);

        genericsReport( V_INFO, "Port configured" EOL );

        while( !r->ending )
        {
            DWORD eventMask = 0;
            WaitCommEvent(portHandle, &eventMask, NULL);
            DWORD unused;
            COMSTAT stats;
            ClearCommError(portHandle, &unused, &stats);

            if(stats.cbInQue == 0)
            {
                continue;
            }


            struct dataBlock *rxBlock = &r->rawBlock[r->wp];
            DWORD transferSize = stats.cbInQue;
            if(transferSize > TRANSFER_SIZE)
            {
                transferSize = TRANSFER_SIZE;
            }

            DWORD readBytes = 0;
            ReadFile(portHandle, rxBlock->buffer, transferSize, &readBytes, NULL);

            rxBlock->fillLevel = readBytes;

            if ( rxBlock->fillLevel <= 0 )
            {
                break;
            }

            r->wp = ( r->wp + 1 ) % NUM_RAW_BLOCKS;
            sem_post( &r->dataForClients );
        }

        if ( ! r->ending )
        {
            genericsReport( V_INFO, "Read failed" EOL );
        }

        CloseHandle(portHandle);
    }

    return 0;
}