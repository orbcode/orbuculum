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
    size_t devIndex;
    const struct OrbtraceInterfaceType *type;
};

struct OrbtraceIf
{
    libusb_device_handle *handle;
    libusb_device *dev;
    libusb_device **list;
    libusb_context *context;

    const struct OrbtraceInterfaceType *type;    /* Type of interface currently connected */
    int numDevices;                              /* Number of matching devices found */
    struct OrbtraceIfDevice *device;             /* List of matching devices found */
};
// ====================================================================================================

int OrbtraceIfValidateVoltage( struct OrbtraceIf *o, int vmv );

/* Device access */
inline char *OrbtraceIfGetManufacturer( struct OrbtraceIf *o, int e )
{
    return ( ( e < o->numDevices ) && ( o->device[e].manufacturer ) ) ? o->device[e].manufacturer : "";
}
inline char *OrbtraceIfGetProduct( struct OrbtraceIf *o, int e )
{
    return ( ( e < o->numDevices ) && ( o->device[e].product ) ) ? o->device[e].product : "";
}
inline char *OrbtraceIfGetSN( struct OrbtraceIf *o, int e )
{
    return ( ( e < o->numDevices ) && ( o->device[e].sn ) ) ? o->device[e].sn : "";
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
