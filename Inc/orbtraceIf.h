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
#else
    #if defined LINUX
        #include <libusb-1.0/libusb.h>
    #else
        #error "Unknown OS"
    #endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define NO_INTERFACE (-1)
#define NO_DEVICE    (-1)

struct OrbtraceInterfaceType
{
    int vid;
    int pid;
    int *voltageListmv;
};

struct OrbtraceIfDevice
{
    char *sn;
    char *manufacturer;
    char *product;
    int vid;
    int pid;
    int traceIf;
    int powerIf;
    size_t devIndex;
    const struct OrbtraceInterfaceType *type;
};

struct OrbtraceIf
{
    int activeDevice;                            /* Number in the list of devices of the active device */
    libusb_device_handle *handle;
    libusb_device *dev;                          /* usb handle for currently active device (or NULL for non active) */
    libusb_device **list;                        /* List of available usbdevices */
    libusb_context *context;                     /* Any active context */

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

/* Device selection management */
int OrbtraceIfGetDeviceList( struct OrbtraceIf *o, char *sn );
bool OrbtraceIfOpenDevice( struct OrbtraceIf *o, unsigned int entry );
void OrbtraceIfCloseDevice( struct OrbtraceIf *o );

/* Device manipulation */
bool OrbtraceIfSetTraceWidth( struct OrbtraceIf *o, int width );

/* Device context control */
struct OrbtraceIf *OrbtraceIfCreateContext( void );
void OrbtraceIfDestroyContext( struct OrbtraceIf *o );

// ====================================================================================================
#ifdef __cplusplus
}
#endif
#endif
