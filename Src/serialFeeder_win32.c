#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <Windows.h>
#include "tpiuDecoder.h"
#include "nw.h"

#define NUM_RAW_BLOCKS (10)

// Copied from orbuculum.c
struct Options
{
    /* Config information */
    bool segger;                                         /* Using a segger debugger */

    /* Source information */
    char *seggerHost;                                    /* Segger host connection */
    int32_t seggerPort;                                  /* ...and port */
    char *port;                                          /* Serial host connection */
    int speed;                                           /* Speed of serial link */
    bool useTPIU;                                        /* Are we using TPIU, and stripping TPIU frames? */
    uint32_t dataSpeed;                                  /* Effective data speed (can be less than link speed!) */
    char *file;                                          /* File host connection */
    bool fileTerminate;                                  /* Terminate when file read isn't successful */

    uint32_t intervalReportTime;                         /* If we want interval reports about performance */

    char *channelList;                                   /* List of TPIU channels to be serviced */

    /* Network link */
    int listenPort;                                      /* Listening port for network */
};

struct dataBlock
{
    ssize_t fillLevel;
    uint8_t buffer[TRANSFER_SIZE];
    struct libusb_transfer *usbtfr;
};

struct handlers
{
    uint8_t channel;
    uint64_t intervalBytes;                                                  /* Number of depacketised bytes output on this channel */
    struct dataBlock *strippedBlock;                                         /* Processed buffer for output to clients */
    struct nwclientsHandle *n;                                               /* Link to the network client subsystem */
};

struct RunTime
{
    struct TPIUDecoder t;                                                    /* TPIU decoder instance, in case we need it */

    uint64_t  intervalBytes;                                                 /* Number of bytes transferred in current interval */

    pthread_t intervalThread;                                                /* Thread reporting on intervals */
    pthread_t processThread;                                                 /* Thread distributing to clients */
    sem_t     dataForClients;                                                /* Semaphore counting data for clients */
    bool      ending;                                                        /* Flag indicating app is terminating */
    int f;                                                                   /* File handle to data source */

    struct Options *options;                                                 /* Command line options (reference to above) */

    uint8_t wp;                                                              /* Read and write pointers into transfer buffers */
    uint8_t rp;
    struct dataBlock rawBlock[NUM_RAW_BLOCKS];                               /* Transfer buffers from the receiver */

    uint8_t numHandlers;                                                     /* Number of TPIU channel handlers in use */
    struct handlers *handler;
    struct nwclientsHandle *n;                                               /* Link to the network client subsystem (used for non-TPIU case) */
};

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