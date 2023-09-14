/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Orbtrace Interface Module
 * =========================
 *
 */

#ifndef _ORBTRACE_IF_
#define _ORBTRACE_IF_

#include <stdint.h>
#include <stdbool.h>

#if defined OSX
    #include <sys/ioctl.h>
    #include <libusb.h>
    #include <termios.h>
#elif defined LINUX
    #include <libusb-1.0/libusb.h>
#elif defined FREEBSD
    #include <libusb.h>
#elif defined WIN32
    #include <libusb.h>
#else
    #error "Unknown OS"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define USB_TRANSFER_SIZE (65536)

#define NO_INTERFACE (-1)
#define NO_DEVICE    (-1)

enum ORBTraceDevice { DEVICE_NULL, DEVICE_ORBTRACE_MINI, DEVICE_BMP, DEVICE_NUM_DEVICES };

#define DEVTYPE_ALL 0xffffffff
#define DEVTYPE(x) (1<<x)

enum Channel {CH_VTREF, CH_VTPWR, CH_MAX, CH_NONE, CH_ALL = 0xff};
#define  POWERNAMES      \
    { "vtpwr", CH_VTPWR }, \
    { "vtref", CH_VTREF }, \
    { "all"  , CH_ALL   }, \
    { NULL   , CH_NONE  }  \

struct OrbtraceInterfaceType
{
    int vid;
    int pid;
    enum ORBTraceDevice devtype;
};

struct OrbtraceIfDevice
{
    char *sn;
    char *manufacturer;
    char *product;
    char *version;
    enum ORBTraceDevice devtype;
    int vid;
    int pid;
    int traceIf;
    int powerIf;
    int versionIf;
    size_t devIndex;
    const struct OrbtraceInterfaceType *type;
};

struct dataBlock
{
    ssize_t fillLevel;                                   /* How full this block is */
    uint8_t buffer[USB_TRANSFER_SIZE];                   /* Block buffer */
    struct libusb_transfer *usbtfr;                      /* USB Transfer handle */
};

struct OrbtraceIf
{
    int activeDevice;                            /* Number in the list of devices of the active device */
    libusb_device_handle *handle;
    libusb_device *dev;                          /* usb handle for currently active device (or NULL for non active) */
    libusb_device **list;                        /* List of available usbdevices */
    libusb_context *context;                     /* Any active context */

    /* Data transfer specific structures */
    struct dataBlock *d;                         /* Transfer datablocks */
    int numBlocks;                               /* ...and how many */
    uint8_t ep;                                  /* Endpoint used for data transfer */
    uint8_t iface;                               /* ...and the interface */
    bool isOrbtrace;                             /* Is this an orbtrace device? */

    int numDevices;                              /* Number of matching devices found */
    struct OrbtraceIfDevice *devices;            /* List of matching devices found */
};
// ====================================================================================================

int OrbtraceIfValidateVoltage( struct OrbtraceIf *o, int vmv );

/* Device access */
static inline char *OrbtraceIfGetManufacturer( struct OrbtraceIf *o, unsigned int e )
{
    return ( ( e < o->numDevices ) && ( o->devices[e].manufacturer ) ) ? o->devices[e].manufacturer : "";
}
static inline char *OrbtraceIfGetProduct( struct OrbtraceIf *o, unsigned int e )
{
    return ( ( e < o->numDevices ) && ( o->devices[e].product ) ) ? o->devices[e].product : "";
}
static inline enum ORBTraceDevice OrbtraceIfGetDevtype( struct OrbtraceIf *o, unsigned int e )
{
    return ( ( e < o->numDevices ) && ( o->devices[e].devtype ) ) ? o->devices[e].devtype : 0;
}
static inline char *OrbtraceIfGetVersion( struct OrbtraceIf *o, unsigned int e )
{
    return ( ( e < o->numDevices ) && ( o->devices[e].version ) ) ? o->devices[e].version : "";
}
static inline char *OrbtraceIfGetSN( struct OrbtraceIf *o, unsigned int e )
{
    return ( ( e < o->numDevices ) && ( o->devices[e].sn ) ) ? o->devices[e].sn : "";
}
static inline int OrbtraceIfGetTraceIF( struct OrbtraceIf *o, unsigned int e )
{
    return ( ( e < o->numDevices ) ? ( o->devices[e].traceIf ) : NO_INTERFACE );
}
static inline int OrbtraceIfGetPowerIF( struct OrbtraceIf *o, unsigned int e )
{
    return ( ( e < o->numDevices ) ? ( o->devices[e].powerIf ) : NO_INTERFACE );
}
static inline int OrbtraceIfGetActiveDevnum( struct OrbtraceIf *o )
{
    return o->activeDevice;
}
static inline libusb_device *OrbtraceIfGetDev( struct OrbtraceIf *o )
{
    return o->dev;
}
static inline libusb_device_handle *OrbtraceIfGetHandle( struct OrbtraceIf *o )
{
    return o->handle;
}

/* Device selection management */
int OrbtraceIfGetDeviceList( struct OrbtraceIf *o, char *sn, uint32_t devmask );
void OrbtraceIfListDevices( struct OrbtraceIf *o );
int OrbtraceIfSelectDevice( struct OrbtraceIf *o );
bool OrbtraceIfOpenDevice( struct OrbtraceIf *o, int entry );
bool OrbtraceGetIfandEP( struct OrbtraceIf *o );
void OrbtraceIfCloseDevice( struct OrbtraceIf *o );
enum Channel OrbtraceIfNameToChannel( char *x );
bool OrbtraceIsOrbtrace( struct OrbtraceIf *o );

/* Device manipulation */
bool OrbtraceIfSetTraceWidth( struct OrbtraceIf *o, int width );
bool OrbtraceIfSetTraceSWO( struct OrbtraceIf *o, bool isMANCH );
bool OrbtraceIfSetSWOBaudrate( struct OrbtraceIf *o, uint32_t speed );

bool OrbtraceIfVoltage( struct OrbtraceIf *o, enum Channel ch, int voltage );
bool OrbtraceIfSetVoltageEn( struct OrbtraceIf *o, enum Channel ch, bool isOn );

/* Data transfer specifics */
bool OrbtraceIfSetupTransfers( struct OrbtraceIf *o, bool hiresTime, struct dataBlock *d, int numBlocks, libusb_transfer_cb_fn callback );
int OrbtraceIfHandleEvents( struct OrbtraceIf *o );
void OrbtraceIfCloseTransfers( struct OrbtraceIf *o );

/* Device context control */
struct OrbtraceIf *OrbtraceIfCreateContext( void );
void OrbtraceIfDestroyContext( struct OrbtraceIf *o );

// ====================================================================================================
#ifdef __cplusplus
}
#endif
#endif
