/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef _ORBUCULUM_OPTIONS
#define _ORBUCULUM_OPTIONS

#include <stdint.h>
#include <pthread.h>
#include "tpiuDecoder.h"
#include "nwclient.h"

#if defined LINUX
    #include <libusb-1.0/libusb.h>
#elif defined WIN32
    #include <libusb-1.0/libusb.h>
#else
    #error "Unknown OS"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* How many transfer buffers from the source to allocate */
#define NUM_RAW_BLOCKS (3)

/* Record for options, either defaults or from command line */
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
    char *outfile;                                       /* Output file for raw data dumping */

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

    int opFileHandle;                                                         /* Handle if we're writing orb output locally */
    struct Options *options;                                                 /* Command line options (reference to above) */

    uint8_t wp;                                                              /* Read and write pointers into transfer buffers */
    uint8_t rp;
    struct dataBlock rawBlock[NUM_RAW_BLOCKS];                               /* Transfer buffers from the receiver */

    uint8_t numHandlers;                                                     /* Number of TPIU channel handlers in use */
    struct handlers *handler;
    struct nwclientsHandle *n;                                               /* Link to the network client subsystem (used for non-TPIU case) */
};

#ifdef __cplusplus
}
#endif

#endif