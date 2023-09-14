/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Orbtrace Interface Module
 * =========================
 *
 */

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include "orbtraceIf.h"
#include "generics.h"

/* List of device VID/PID pairs this library works with */
static const struct OrbtraceInterfaceType _validDevices[DEVICE_NUM_DEVICES] =
{
    { 0x1209, 0x3443, DEVICE_ORBTRACE_MINI },
    { 0x1d50, 0x6018, DEVICE_BMP},
    { 0,      0      }
};

/* BMP iInterface string */
#define BMP_IFACE "Black Magic Trace Capture"

#define SCRATCH_STRINGLEN (255)
#define MAX_DESC_FIELDLEN (50)

#define MIN_GENERIC_VOLTAGE_MV (900)
#define MAX_GENERIC_VOLTAGE_MV (5000)
#define MAX_VOLTAGE_DIFF_MV    (10)

#define RQ_CLASS       (0x41)
#define RQ_INTERFACE   (0x01)

/* Commands for Trace Endpoint */
#define RQ_SET_TWIDTH  (1)
#define RQ_SET_SPEED   (2)

/* Commands for Power Endpoint */
#define RQ_SET_ENABLE  (1)
#define RQ_SET_VOLTAGE (2)

/* Maximum descriptor length from USB specification */
#define MAX_USB_DESC_LEN (256)

/* String on front of version number to remove */
#define VERSION_FRONTMATTER "Version: "
static const struct
{
    const char *name;
    const int num;
} _powerNames[] =
{
    POWERNAMES
};


// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
static void _flushDeviceList( struct OrbtraceIf *o )

/* Flush out any old device records */

{
    for ( size_t i = 0; i < o->numDevices; i++ )
    {
        if ( o->devices[i].sn )
        {
            free( o->devices[i].sn );
        }

        if ( o->devices[i].manufacturer )
        {
            free( o->devices[i].manufacturer );
        }

        if ( o->devices[i].product )
        {
            free( o->devices[i].product );
        }

        if ( o->devices[i].version )
        {
            free( o->devices[i].version );
        }
    }

    if ( o->devices )
    {
        free( o->devices );
    }

    o->numDevices = 0;
    o->devices = NULL;
}
// ====================================================================================================
static int _strcmpint( char *s1, char *s2 )

/* Version of strcmp that accomodates NULLs */

{
    if ( ( s1 ) && ( !s2 ) )
    {
        return -1;
    }

    if ( ( !s1 ) && ( s2 ) )
    {
        return 1;
    }

    return strcmp( s1, s2 );
}
// ====================================================================================================
static int _compareFunc( const void *vd1, const void *vd2 )

/* Return a comparison for two devices, used for qsort ordering */

{
    const struct OrbtraceIfDevice *d1 = ( const struct OrbtraceIfDevice * )vd1;
    const struct OrbtraceIfDevice *d2 = ( const struct OrbtraceIfDevice * )vd2;
    int r = 0;

    if ( d1->devtype != d2->devtype )
    {
        return ( d1->devtype - d2->devtype );
    }

    if ( ( r = _strcmpint( d1->manufacturer, d2->manufacturer ) ) )
    {
        return r;
    }

    if ( ( r = _strcmpint( d1->product, d2->product ) ) )
    {
        return r;
    }

    if ( ( r = strcmp( d1->sn, d2->sn ) ) )
    {
        return r;
    }

    if ( ( r = d1->vid - d2->vid ) )
    {
        return r;
    }

    return d1->pid - d2->pid;
}
// ====================================================================================================
uint16_t _getInterface( struct OrbtraceIf *o, char intType, int *nameIndex )

{
    struct libusb_config_descriptor *config;
    int iface = NO_INTERFACE;

    if ( ( libusb_get_active_config_descriptor( o->dev, &config ) ) >= 0 )
    {
        for ( int if_num = 0; if_num < config->bNumInterfaces; if_num++ )
        {
            const struct libusb_interface_descriptor *i = &config->interface[if_num].altsetting[0];

            if ( ( i->bInterfaceClass == 0xff ) && ( i->bInterfaceSubClass == intType ) )
            {
                iface = i->bInterfaceNumber;

                if ( nameIndex != NULL )
                {
                    /* Return the name index of this interface too */
                    *nameIndex = i->iInterface;
                }

                libusb_free_config_descriptor( config );
                break;
            }
        }
    }

    return iface;
}
// ====================================================================================================
static bool _doInterfaceControlTransfer( struct OrbtraceIf *o, uint8_t interface, uint16_t request, uint16_t value, uint8_t indexUpperHalf, uint8_t dlen, void *data )

{
    if ( !o->handle )
    {
        return false;
    }

    if ( interface == ( uint8_t )NO_INTERFACE )
    {
        return false;
    }

#ifdef WIN32

    if ( libusb_claim_interface( o->handle,  interface ) != 0 )
    {
        return false;
    }

#endif

    bool isOk = libusb_control_transfer(
                            o->handle,
                            RQ_CLASS | RQ_INTERFACE,
                            request,
                            value,
                            ( indexUpperHalf << 8 ) | interface,
                            data,
                            dlen,
                            0
                ) >= 0;

#ifdef WIN32

    if ( libusb_release_interface( o->handle,  interface ) != 0 )
    {
        return false;
    }

#endif

    return isOk;
}

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
int OrbtraceIfValidateVoltage( struct OrbtraceIf *o, int vmv )

/* Return matching voltage or zero if none can be found */

{
    return ( ( vmv >= MIN_GENERIC_VOLTAGE_MV ) && ( vmv <= ( MAX_GENERIC_VOLTAGE_MV + MAX_VOLTAGE_DIFF_MV ) ) ) ? vmv : 0;
}
// ====================================================================================================
struct OrbtraceIf *OrbtraceIfCreateContext( void )

{
    struct OrbtraceIf *o = ( struct OrbtraceIf * )calloc( 1, sizeof( struct OrbtraceIf ) );

    if ( libusb_init( &o->context ) < 0 )
    {
        free( o );
        return NULL;
    }
    else
    {
        //        libusb_set_debug(o->context, LIBUSB_LOG_LEVEL_DEBUG);
        return o;
    }
}

// ====================================================================================================
void OrbtraceIfDestroyContext( struct OrbtraceIf *o )

{
    if ( o )
    {
        libusb_exit( o->context );
    }
}
// ====================================================================================================

int OrbtraceIfGetDeviceList( struct OrbtraceIf *o, char *sn, uint32_t devmask )

/* Get list of devices that match (partial) serial number & devmask */

{
    char tfrString[MAX_USB_DESC_LEN];
    struct OrbtraceIfDevice *d;
    int versionIndex;
    size_t y;

    assert( o );

    /* Close any active device */
    OrbtraceIfCloseDevice( o );

    /* Flush out any old scans we might be holding */
    _flushDeviceList( o );

    if ( o->list )
    {
        libusb_free_device_list( o->list, true );
    }

    int count = libusb_get_device_list( o->context, &o->list );

    for ( size_t i = 0; i < count; i++ )
    {
        o->dev = o->list[i];
        struct libusb_device_descriptor desc = { 0 };
        libusb_get_device_descriptor( o->dev, &desc );

        /* Loop through known devices to see if this one is one we recognise */
        for ( y = 0; ( ( _validDevices[y].vid ) &&
                       ( ( _validDevices[y].vid != desc.idVendor ) || ( _validDevices[y].pid != desc.idProduct ) ) ); y++ );

        /* If it's one we're interested in then process further */
        if ( _validDevices[y].vid )
        {
            /* We'll store this match for access later */
            o->devices = realloc( o->devices, ( o->numDevices + 1 ) * sizeof( struct OrbtraceIfDevice ) );
            d = &o->devices[o->numDevices];
            memset( d, 0, sizeof( struct OrbtraceIfDevice ) );
            d->devtype = _validDevices[y].devtype;

            if ( !libusb_open( o->list[i], &o->handle ) )
            {
                if ( desc.iSerialNumber )
                {
                    libusb_get_string_descriptor_ascii( o->handle, desc.iSerialNumber, ( unsigned char * )tfrString, MAX_USB_DESC_LEN );
                }

                /* This is a match if no S/N match was requested or if there is a S/N and they part-match, and it's a matching devtype */
                if ( ( devmask & ( 1 << d->devtype ) ) && ( ( !sn ) || ( ( desc.iSerialNumber ) && ( strstr( tfrString, sn ) ) ) ) )
                {
                    /* We will keep this one! */
                    o->numDevices++;
                    d->sn = strdup( desc.iSerialNumber ? tfrString : "" );

                    if ( desc.iManufacturer )
                    {
                        libusb_get_string_descriptor_ascii( o->handle, desc.iManufacturer, ( unsigned char * )tfrString, MAX_USB_DESC_LEN );
                    }

                    d->manufacturer = strdup( desc.iManufacturer ? tfrString : "" );

                    if ( desc.iProduct )
                    {
                        libusb_get_string_descriptor_ascii( o->handle, desc.iProduct, ( unsigned char * )tfrString, MAX_USB_DESC_LEN );
                    }

                    d->product = strdup( desc.iProduct ? tfrString : "" );
                    d->devIndex = i;

                    switch ( d->devtype )
                    {
                        case DEVICE_ORBTRACE_MINI:
                            d->powerIf = _getInterface( o, 'P', NULL );
                            d->traceIf = _getInterface( o, 'T', NULL );

                            /* Collect the probe version from the version interface */
                            d->versionIf = _getInterface( o, 'V', &versionIndex );

                            if ( versionIndex )
                            {
                                libusb_get_string_descriptor_ascii( o->handle, versionIndex, ( unsigned char * )tfrString, MAX_USB_DESC_LEN );
                                /* If string contains 'Version: ' at the start then remove it */
                                d->version = strdup( ( strstr( tfrString, VERSION_FRONTMATTER ) ) ? &tfrString[strlen( VERSION_FRONTMATTER )] : "" );
                            }
                            else
                            {
                                d->version = strdup( "" );
                            }

                            break;

                        case DEVICE_BMP:
                            /* On BMP version and serial are merged, so let's unmerge them by making the last word of the product the version */
                            d->version = &d->product[strlen( d->product ) - 1];

                            while ( ( !isspace( *d->version ) ) && ( d->version != d->product ) )
                            {
                                d->version--;
                            }

                            *d->version++ = 0;
                            break;

                        default:
                            break;
                    }
                }

                libusb_close( o->handle );
                o->handle = NULL;
            }
        }
    }

    /* Now sort matching devices into defined order, so they're always the same way up */
    qsort( o->devices, o->numDevices, sizeof( struct OrbtraceIfDevice ), _compareFunc );

    return o->numDevices;
}
// ====================================================================================================
void OrbtraceIfListDevices( struct OrbtraceIf *o )

{
    char printConstruct[SCRATCH_STRINGLEN];

    /* Get longest line */
    genericsPrintf( C_RESET " Id |                    Description                    |      Serial      |           Version" EOL );
    genericsPrintf( " ---+---------------------------------------------------+------------------+----------------------------" EOL );

    for ( int i = 0; i < o->numDevices; i++ )
    {
        snprintf( printConstruct, MAX_DESC_FIELDLEN, "%s %s", OrbtraceIfGetManufacturer( o, i ), OrbtraceIfGetProduct( o, i ) ) ;
        genericsPrintf( C_SEL " %2d " C_RESET "|"C_ELEMENT" %-49s "C_RESET"|"C_ELEMENT" %16s "C_RESET"|"C_ELEMENT" %s" C_RESET EOL,
                        i + 1, printConstruct, OrbtraceIfGetSN( o, i ), OrbtraceIfGetVersion( o, i ) );
    }
}

// ====================================================================================================
int OrbtraceIfSelectDevice( struct OrbtraceIf *o )

{
    int selection = o->numDevices;

    if ( o->numDevices > 1 )
    {
        OrbtraceIfListDevices( o );

        selection = 0;

        while ( ( selection < 1 ) || ( selection > o->numDevices ) )
        {
            genericsPrintf( EOL C_SEL "Selection>" C_RESET );
            scanf( "%d", &selection );
        }
    }

    return selection - 1;
}
// ====================================================================================================
bool OrbtraceIfOpenDevice( struct OrbtraceIf *o, int entry )

{
    if ( ( entry < 0 ) || ( entry >= o->numDevices ) )
    {
        return false;
    }

    o->dev = o->list[ o->devices[entry].devIndex];

    if ( libusb_open( o->dev, &o->handle ) )
    {
        o->dev = NULL;
        o->handle = NULL;
        return false;
    }

    o->activeDevice = entry;
    return true;
}
// ====================================================================================================
bool OrbtraceGetIfandEP( struct OrbtraceIf *o )

{
    struct libusb_config_descriptor *config;
    bool interface_found = false;
    uint8_t altsetting = 0;
    uint8_t num_altsetting = 0;
    int32_t err;

    if ( ( !o->dev ) || ( !o->handle ) )
    {
        return false;
    }

    /* For the BMP case we can quickly return the correct values */
    switch ( o->devices[o->activeDevice].devtype )
    {
        default: // -------------------------------------------------------------------------------------
            return false;

        case DEVICE_BMP: // -----------------------------------------------------------------------------
            genericsReport( V_DEBUG, "Searching for BMP trace interface" EOL );

            if ( ( err = libusb_get_active_config_descriptor( o->dev, &config ) ) < 0 )
            {
                genericsReport( V_WARN, "Failed to get config descriptor (%d)" EOL, err );
                return false;
            }

            /* Loop through the interfaces looking for ours */
            for ( int if_num = 0; if_num < config->bNumInterfaces && !interface_found; if_num++ )
            {
                for ( int alt_num = 0; alt_num < config->interface[if_num].num_altsetting && !interface_found; alt_num++ )
                {
                    char tfrString[MAX_USB_DESC_LEN];
                    const struct libusb_interface_descriptor *i = &config->interface[if_num].altsetting[alt_num];

                    int ret = libusb_get_string_descriptor_ascii( o->handle, i->iInterface, ( unsigned char * )tfrString, MAX_USB_DESC_LEN );
                    if ( ret < 0 )
                    {
                        /* No string means not correct interface */
                        continue;
                    }

                    if ( strcmp( tfrString, BMP_IFACE ) != 0 )
                    {
                        /* Not the interface we're looking for */
                        continue;
                    }

                    o->iface = i->bInterfaceNumber;
                    o->ep = i->endpoint[0].bEndpointAddress;

                    altsetting = i->bAlternateSetting;
                    num_altsetting = config->interface[if_num].num_altsetting;

                    genericsReport( V_DEBUG, "Found interface %#x with altsetting %#x and ep %#x" EOL, o->iface, altsetting, o->ep );
                    interface_found = true;
                }
            }

            libusb_free_config_descriptor( config );

            if ( !interface_found )
            {
                genericsReport( V_DEBUG, "No supported interfaces found" EOL );
                return false;
            }

            break;

        case DEVICE_ORBTRACE_MINI: // -------------------------------------------------------------------
            genericsReport( V_DEBUG, "Searching for trace interface" EOL );
            struct libusb_config_descriptor *config;

            if ( ( err = libusb_get_active_config_descriptor( o->dev, &config ) ) < 0 )
            {
                genericsReport( V_WARN, "Failed to get config descriptor (%d)" EOL, err );
                return false;
            }

            /* Loop through the interfaces looking for ours */
            for ( int if_num = 0; if_num < config->bNumInterfaces && !interface_found; if_num++ )
            {
                for ( int alt_num = 0; alt_num < config->interface[if_num].num_altsetting && !interface_found; alt_num++ )
                {
                    const struct libusb_interface_descriptor *i = &config->interface[if_num].altsetting[alt_num];

                    if (
                                i->bInterfaceClass != 0xff ||
                                i->bInterfaceSubClass != 0x54 ||
                                ( i->bInterfaceProtocol != 0x00 && i->bInterfaceProtocol != 0x01 ) ||
                                i->bNumEndpoints != 0x01 )
                    {
                        /* Not the interface we're looking for */
                        continue;
                    }

                    o->iface = i->bInterfaceNumber;
                    o->ep = i->endpoint[0].bEndpointAddress;

                    altsetting = i->bAlternateSetting;
                    num_altsetting = config->interface[if_num].num_altsetting;

                    genericsReport( V_DEBUG, "Found interface %#x with altsetting %#x and ep %#x" EOL, o->iface, altsetting, o->ep );
                    interface_found = true;
                }
            }

            libusb_free_config_descriptor( config );

            if ( !interface_found )
            {
                genericsReport( V_DEBUG, "No supported interfaces found" EOL );
                return false;
            }

            o->isOrbtrace = true;
            break;
    }

    if ( ( err = libusb_claim_interface ( o->handle, o->iface ) ) < 0 )
    {
        genericsReport( V_DEBUG, "Failed to claim interface (%d)" EOL, err );
        return false;
    }

    if ( num_altsetting > 1 && ( err = libusb_set_interface_alt_setting ( o->handle, o->iface, altsetting ) ) < 0 )
    {
        genericsReport( V_WARN, "Failed to set altsetting (%d)" EOL, err );
        return false;
    }

    return true;
}

// ====================================================================================================

bool OrbtraceIsOrbtrace( struct OrbtraceIf *o )

{
    return o->isOrbtrace;
}

// ====================================================================================================

bool OrbtraceIfSetupTransfers( struct OrbtraceIf *o, bool hiresTime, struct dataBlock *d, int numBlocks, libusb_transfer_cb_fn callback )

{
    assert( !o->numBlocks );
    assert( !o->d );

    o->numBlocks = numBlocks;
    o->d = d;

    for ( uint32_t t = 0; t < o->numBlocks ; t++ )
    {
        /* Allocate memory */
        if ( !o->d[t].usbtfr )
        {
            o->d[t].usbtfr = libusb_alloc_transfer( 0 );
        }

        libusb_fill_bulk_transfer ( o->d[t].usbtfr, o->handle, o->ep,
                                    o->d[t].buffer,
                                    USB_TRANSFER_SIZE,
                                    callback,
                                    &o->d[t].usbtfr,
                                    hiresTime ? 1 : 100 /* Use 1ms timeout if hires mode, otherwise 100ms */
                                  );

        if ( libusb_submit_transfer( o->d[t].usbtfr ) )
        {
            genericsReport( V_INFO, "Error submitting USB requests" EOL );
            return false;
        }
    }

    return true;
}

// ====================================================================================================

int OrbtraceIfHandleEvents( struct OrbtraceIf *o )

{
    return libusb_handle_events_completed( o->context, NULL );
}

// ====================================================================================================

void OrbtraceIfCloseTransfers( struct OrbtraceIf *o )

{
    for ( uint32_t t = 0; t < o->numBlocks; t++ )
    {
        if ( o->d[t].usbtfr )
        {
            libusb_cancel_transfer( o->d[t].usbtfr );
            libusb_free_transfer( o->d[t].usbtfr );
            o->d[t].usbtfr = 0;
        }

        o->d[t].usbtfr = NULL;
    }

    o->d = NULL;
    o->numBlocks = 0;
}

// ====================================================================================================

bool OrbtraceIfSetTraceWidth( struct OrbtraceIf *o, int width )

{
    uint16_t d = ( width != 4 ) ? width : 3;

    if ( ( d < 1 ) || ( d > 3 ) )
    {
        return false;
    }

    return _doInterfaceControlTransfer(
                       o,
                       OrbtraceIfGetTraceIF( o, OrbtraceIfGetActiveDevnum( o ) ),
                       RQ_SET_TWIDTH,
                       d,
                       0,
                       0,
                       NULL
           );
}
// ====================================================================================================
bool OrbtraceIfSetTraceSWO( struct OrbtraceIf *o, bool isMANCH )

{
    return _doInterfaceControlTransfer(
                       o,
                       OrbtraceIfGetTraceIF( o, OrbtraceIfGetActiveDevnum( o ) ),
                       RQ_SET_TWIDTH,
                       isMANCH ? 0x10 : 0x12,
                       0,
                       0,
                       NULL
           );
}
// ====================================================================================================
bool OrbtraceIfSetSWOBaudrate( struct OrbtraceIf *o, uint32_t speed )

{
    uint8_t speed_le[4] = { ( speed & 0xff ), ( speed >> 8 ) & 0xff, ( speed >> 16 ) & 0xff, ( speed >> 24 ) & 0xff };

    return _doInterfaceControlTransfer(
                       o,
                       OrbtraceIfGetTraceIF( o, OrbtraceIfGetActiveDevnum( o ) ),
                       RQ_SET_SPEED,
                       0,
                       0,
                       4,
                       &speed_le
           );
}
// ====================================================================================================
enum Channel OrbtraceIfNameToChannel( char *x )

/* Turn case insensitive text name to channel number. Can be terminated with NULL or a ',' */

{
    int j;

    for ( int i = 0; _powerNames[i].name; i++ )
    {
        char *y = x;

        for ( j = 0; _powerNames[i].name[j] && _powerNames[i].name[j] == tolower( *y++ ); j++ );

        if ( ( ( !*y ) || ( *y == ',' ) ) && ( !_powerNames[i].name[j] ) )
        {
            return _powerNames[i].num;
        }
    }

    return CH_NONE;
}
// ====================================================================================================
bool OrbtraceIfVoltage( struct OrbtraceIf *o, enum Channel ch, int voltage )

{
    if ( ( ch >= CH_MAX ) || !OrbtraceIfValidateVoltage( o, voltage ) )
    {
        return false;
    }

    return _doInterfaceControlTransfer(
                       o,
                       OrbtraceIfGetPowerIF( o, OrbtraceIfGetActiveDevnum( o ) ),
                       RQ_SET_VOLTAGE,
                       voltage,
                       ch,
                       0,
                       NULL
           );
}

// ====================================================================================================
bool OrbtraceIfSetVoltageEn( struct OrbtraceIf *o, enum Channel ch, bool isOn )

{
    if ( ( ch >= CH_MAX ) && ( ch != CH_ALL ) )
    {
        return false;
    }

    return _doInterfaceControlTransfer(
                       o,
                       OrbtraceIfGetPowerIF( o, OrbtraceIfGetActiveDevnum( o ) ),
                       RQ_SET_ENABLE,
                       isOn,
                       ch,
                       0,
                       NULL
           );
}
// ====================================================================================================
void OrbtraceIfCloseDevice( struct OrbtraceIf *o )

{
    if ( o->handle )
    {
        libusb_close( o->handle );
    }

    o->activeDevice = NO_DEVICE;
    o->handle = NULL;
    o->dev = NULL;
}
// ====================================================================================================
