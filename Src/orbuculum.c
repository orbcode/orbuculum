/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Orbuculum main receiver and TPIU demux
 * ======================================
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <strings.h>
#include <string.h>
#include <pthread.h>
#if defined OSX
    #include <sys/ioctl.h>
    #include <libusb.h>
    #include <termios.h>
#else
    #if defined LINUX
        #include <libusb-1.0/libusb.h>
        #include <asm/ioctls.h>
        #if defined TCGETS2
            #include <asm/termios.h>
            /* Manual declaration to avoid conflict. */
            extern int ioctl ( int __fd, unsigned long int __request, ... ) __THROW;
        #else
            #include <sys/ioctl.h>
            #include <termios.h>
        #endif
    #else
        #error "Unknown OS"
    #endif
#endif
#include <signal.h>

#include "git_version_info.h"
#include "generics.h"
#include "tpiuDecoder.h"

#include "nwclient.h"

#define SEGGER_HOST "localhost"               /* Address to connect to SEGGER */
#define SEGGER_PORT (2332)

#define NUM_TPIU_CHANNELS 0x80

/* Table of known devices to try opening */
static const struct deviceList
{
    uint32_t vid;
    uint32_t pid;
    bool autodiscover;
    uint8_t iface;
    uint8_t ep;
    char *name;
} _deviceList[] =
{
    { 0x1209, 0x3443, true,  0, 0x81, "Orbtrace"         },
    { 0x1d50, 0x6018, false, 5, 0x85, "Blackmagic Probe" },
    { 0x2b3e, 0xc610, false, 3, 0x85, "Phywhisperer-UDT" },
    { 0, 0, 0, 0, 0 }
};

//#define DUMP_BLOCK

/* How many transfer buffers from the source to allocate */
#define NUM_RAW_BLOCKS (3)

/* Interval between blocks for timeouts..smaller means smoother, but higher CPU load */
#define BLOCK_TIMEOUT_INTERVAL_MS (50)

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
} _options =
{
    .listenPort = NWCLIENT_SERVER_PORT,
    .seggerHost = SEGGER_HOST,
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
} _r =
{
    .options = &_options
};

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Private routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
static void _intHandler( int sig )

{
    /* CTRL-C exit is not an error... */
    exit( 0 );
}
// ====================================================================================================
#if defined(LINUX) && defined (TCGETS2)
static int _setSerialConfig ( int f, speed_t speed )
{
    // Use Linux specific termios2.
    struct termios2 settings;
    int ret = ioctl( f, TCGETS2, &settings );

    if ( ret < 0 )
    {
        return ( -3 );
    }

    settings.c_iflag &= ~( ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF );
    settings.c_lflag &= ~( ICANON | ECHO | ECHOE | ISIG );
    settings.c_cflag &= ~PARENB; /* no parity */
    settings.c_cflag &= ~CSTOPB; /* 1 stop bit */
    settings.c_cflag &= ~CSIZE;
    settings.c_cflag &= ~( CBAUD | CIBAUD );
    settings.c_cflag |= CS8 | CLOCAL; /* 8 bits */
    settings.c_oflag &= ~OPOST; /* raw output */

    settings.c_cflag |= BOTHER;
    settings.c_ispeed = speed;
    settings.c_ospeed = speed;

    ret = ioctl( f, TCSETS2, &settings );

    if ( ret < 0 )
    {
        genericsReport( V_ERROR, "Unsupported baudrate" EOL );
        return ( -3 );
    }

    // Check configuration is ok.
    ret = ioctl( f, TCGETS2, &settings );

    if ( ret < 0 )
    {
        return ( -3 );
    }

    if ( ( settings.c_ispeed != speed ) || ( settings.c_ospeed != speed ) )
    {
        genericsReport( V_ERROR, "Failed to set baudrate" EOL );
        return -4;
    }

    // Flush port.
    ioctl( f, TCFLSH, TCIOFLUSH );
    return 0;
}
#else
static int _setSerialConfig ( int f, speed_t speed )
{
    struct termios settings;

    if ( tcgetattr( f, &settings ) < 0 )
    {
        perror( "tcgetattr" );
        return ( -3 );
    }

    if ( cfsetspeed( &settings, speed ) < 0 )
    {
        genericsReport( V_ERROR, "Error Setting input speed" EOL );
        return ( -3 );
    }

    settings.c_iflag &= ~( ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF );
    settings.c_lflag &= ~( ICANON | ECHO | ECHOE | ISIG );
    settings.c_cflag &= ~PARENB; /* no parity */
    settings.c_cflag &= ~CSTOPB; /* 1 stop bit */
    settings.c_cflag &= ~CSIZE;
    settings.c_cflag |= CS8 | CLOCAL; /* 8 bits */
    settings.c_oflag &= ~OPOST; /* raw output */

    if ( tcsetattr( f, TCSANOW, &settings ) < 0 )
    {
        genericsReport( V_ERROR, "Unsupported baudrate" EOL );
        return ( -3 );
    }

    tcflush( f, TCOFLUSH );
    return 0;
}
#endif
// ====================================================================================================
static void _doExit( void )

{
    _r.ending = true;

    nwclientShutdown( _r.n );
    /* Give them a bit of time, then we're leaving anyway */
    usleep( 200 );

    if ( _r.opFileHandle )
    {
        close( _r.opFileHandle );
    }
}
// ====================================================================================================
void _printHelp( char *progName )

{
    genericsPrintf( "Usage: %s [options]" EOL, progName );
    genericsPrintf( "       -a: <serialSpeed> to use" EOL );
    genericsPrintf( "       -e: When reading from file, terminate at end of file" EOL );
    genericsPrintf( "       -f: <filename> Take input from specified file" EOL );
    genericsPrintf( "       -h: This help" EOL );
    genericsPrintf( "       -l: <port> Listen port for the incoming connections (defaults to %d)" EOL, NWCLIENT_SERVER_PORT );
    genericsPrintf( "       -m: <interval> Output monitor information about the link at <interval>ms" EOL );
    genericsPrintf( "       -o: <filename> to be used for dump file" EOL );
    genericsPrintf( "       -p: <serialPort> to use" EOL );
    genericsPrintf( "       -s: <Server>:<Port> to use" EOL );
    genericsPrintf( "       -t: <Channel , ...> Use TPIU channels (and strip TIPU framing from output flows)" EOL );
    genericsPrintf( "       -v: <level> Verbose mode 0(errors)..3(debug)" EOL );
}
// ====================================================================================================
int _processOptions( int argc, char *argv[], struct RunTime *r )

{
    int c;
#define DELIMITER ','

    while ( ( c = getopt ( argc, argv, "a:ef:hl:m:no:p:s:t:v:" ) ) != -1 )
        switch ( c )
        {
            // ------------------------------------
            case 'a':
                r->options->speed = atoi( optarg );
                r->options->dataSpeed = r->options->speed;
                break;

            // ------------------------------------

            case 'e':
                r->options->fileTerminate = true;
                break;

            // ------------------------------------
            case 'f':
                r->options->file = optarg;
                break;

            // ------------------------------------
            case 'h':
                _printHelp( argv[0] );
                return false;

            // ------------------------------------

            case 'l':
                r->options->listenPort = atoi( optarg );
                break;

            // ------------------------------------

            case 'm':
                r->options->intervalReportTime = atoi( optarg );
                break;

            // ------------------------------------

            case 'o':
                r->options->outfile = optarg;
                break;

            // ------------------------------------

            case 'p':
                r->options->port = optarg;
                break;

            // ------------------------------------

            case 's':
                r->options->seggerHost = optarg;

                // See if we have an optional port number too
                char *a = optarg;

                while ( ( *a ) && ( *a != ':' ) )
                {
                    a++;
                }

                if ( *a == ':' )
                {
                    *a = 0;
                    r->options->seggerPort = atoi( ++a );
                }

                if ( !r->options->seggerPort )
                {
                    r->options->seggerPort = SEGGER_PORT;
                }

                break;

            // ------------------------------------
            case 't':
                r->options->useTPIU = true;
                r->options->channelList = optarg;
                break;

            // ------------------------------------
            case 'v':
                genericsSetReportLevel( atoi( optarg ) );
                break;

            // ------------------------------------

            case '?':
                if ( optopt == 'b' )
                {
                    genericsReport( V_ERROR, "Option '%c' requires an argument." EOL, optopt );
                }
                else if ( !isprint ( optopt ) )
                {
                    genericsReport( V_ERROR, "Unknown option character `\\x%x'." EOL, optopt );
                }

                return false;

            // ------------------------------------
            default:
                genericsReport( V_ERROR, "Unrecognised option '%c'" EOL, c );
                return false;
                // ------------------------------------
        }

    /* ... and dump the config if we're being verbose */
    genericsReport( V_INFO, "Orbuculum V" VERSION " (Git %08X %s, Built " BUILD_DATE ")" EOL, GIT_HASH, ( GIT_DIRTY ? "Dirty" : "Clean" ) );

    if ( r->options->intervalReportTime )
    {
        genericsReport( V_INFO, "Report Intv    : %d mS" EOL, r->options->intervalReportTime );
    }

    if ( r->options->port )
    {
        genericsReport( V_INFO, "Serial Port    : %s" EOL, r->options->port );
    }

    if ( r->options->speed )
    {
        genericsReport( V_INFO, "Serial Speed   : %d baud" EOL, r->options->speed );
    }

    if ( r->options->dataSpeed )
    {
        genericsReport( V_INFO, "Max Data Rt    : %d bps" EOL, r->options->dataSpeed );
    }

    if ( r->options->outfile )
    {
        genericsReport( V_INFO, "Raw Output file: %s" EOL, r->options->outfile );
    }

    if ( r->options->seggerPort )
    {
        genericsReport( V_INFO, "SEGGER H&P    : %s:%d" EOL, r->options->seggerHost, r->options->seggerPort );
    }

    if ( r->options->useTPIU )
    {
        genericsReport( V_INFO, "Use/Strip TPIU : True (Channel List %s)" EOL, r->options->channelList );
    }
    else
    {
        genericsReport( V_INFO, "Use/Strip TPIU : False" EOL );
    }

    if ( r->options->file )
    {
        genericsReport( V_INFO, "Input File  : %s", r->options->file );

        if ( r->options->fileTerminate )
        {
            genericsReport( V_INFO, " (Terminate on exhaustion)" EOL );
        }
        else
        {
            genericsReport( V_INFO, " (Ongoing read)" EOL );
        }
    }

    if ( ( r->options->file ) && ( ( r->options->port ) || ( r->options->seggerPort ) ) )
    {
        genericsReport( V_ERROR, "Cannot specify file and port or Segger at same time" EOL );
        return false;
    }

    if ( ( r->options->port ) && ( r->options->seggerPort ) )
    {
        genericsReport( V_ERROR, "Cannot specify port and Segger at same time" EOL );
        return false;
    }

    return true;
}
// ====================================================================================================
void *_checkInterval( void *params )

/* Perform any interval reporting that may be needed */

{
    struct RunTime *r = ( struct RunTime * )params;
    uint64_t snapInterval;
    struct handlers *h;

    while ( !r->ending )
    {
        usleep( r->options->intervalReportTime * 1000 );

        /* Grab the interval and scale to 1 second */
        snapInterval = r->intervalBytes * 1000 / r->options->intervalReportTime;

        snapInterval *= 8;
        genericsPrintf( C_PREV_LN C_CLR_LN C_DATA );

        if ( snapInterval / 1000000 )
        {
            genericsPrintf( "%4d.%d " C_RESET "MBits/sec ", snapInterval / 1000000, ( snapInterval * 1 / 100000 ) % 10 );
        }
        else if ( snapInterval / 1000 )
        {
            genericsPrintf( "%4d.%d " C_RESET "KBits/sec ", snapInterval / 1000, ( snapInterval / 100 ) % 10 );
        }
        else
        {
            genericsPrintf( "  %4d " C_RESET " Bits/sec ", snapInterval );
        }

        h = r->handler;
        uint64_t totalDat = 0;

        if ( ( r->intervalBytes ) && ( r->options->useTPIU ) )
        {
            for ( int chIndex = 0; chIndex < r->numHandlers; chIndex++ )
            {
                genericsPrintf( " %d:%3d%% ",  h->channel, ( h->intervalBytes * 100 ) / r->intervalBytes );
                totalDat += h->intervalBytes;
                /* TODO: This needs a mutex */
                h->intervalBytes = 0;

                h++;
            }

            genericsPrintf( " Waste:%3d%% ",  100 - ( ( totalDat * 100 ) / r->intervalBytes ) );
        }

        r->intervalBytes = 0;

        if ( r->options->dataSpeed > 100 )
        {
            /* Conversion to percentage done as a division to avoid overflow */
            uint32_t fullPercent = ( snapInterval * 100 ) / r->options->dataSpeed;
            genericsPrintf( "(" C_DATA " %3d%% " C_RESET "full)", ( fullPercent > 100 ) ? 100 : fullPercent );
        }

        genericsPrintf( C_RESET EOL );
    }

    return NULL;
}
// ====================================================================================================
static void _purgeBlock( struct RunTime *r )

{
    /* Now send any packets to clients who want it */

    if ( r->options->useTPIU )
    {
        struct handlers *h = r->handler;
        int i = r->numHandlers;

        while ( i-- )
        {
            if ( h->strippedBlock->fillLevel )
            {
                nwclientSend( h->n, h->strippedBlock->fillLevel, h->strippedBlock->buffer );
                h->intervalBytes += h->strippedBlock->fillLevel;
                h->strippedBlock->fillLevel = 0;
            }

            h++;
        }
    }
}
// ====================================================================================================
static void _stripTPIU( struct RunTime *r, uint8_t *c, int bytes )

{
    struct TPIUPacket p;

    struct handlers *h = NULL;
    int cachedChannel;
    int chIndex = 0;

    cachedChannel = -1;

    while ( bytes-- )
    {
        switch ( TPIUPump( &r->t, *c++ ) )
        {
            case TPIU_EV_RXEDPACKET:
                if ( !TPIUGetPacket( &r->t, &p ) )
                {
                    genericsReport( V_WARN, "TPIUGetPacket fell over" EOL );
                }

                /* Iterate through the packet, putting it into the correct output buffers */
                for ( uint32_t g = 0; g < p.len; g++ )
                {
                    if ( cachedChannel != p.packet[g].s )
                    {
                        /* Whatever happens, cache this result */
                        cachedChannel = p.packet[g].s;

                        /* Search for channel */
                        h = r->handler;

                        for ( chIndex = 0; chIndex < r->numHandlers; chIndex++ )
                        {
                            if ( h->channel == p.packet[g].s )
                            {
                                break;
                            }

                            h++;
                        }
                    }

                    if ( chIndex != r->numHandlers )
                    {
                        /* We must have found a match for this at some point, so add it to the queue */
                        h->strippedBlock->buffer[h->strippedBlock->fillLevel++] = p.packet[g].d;
                    }
                }

                break;

            case TPIU_EV_ERROR:
                genericsReport( V_WARN, "****ERROR****" EOL );
                break;

            case TPIU_EV_NEWSYNC:
            case TPIU_EV_SYNCED:
            case TPIU_EV_RXING:
            case TPIU_EV_NONE:
            case TPIU_EV_UNSYNCED:
            default:
                break;
        }
    }
}
// ====================================================================================================
static void *_processBlocks( void *params )
/* Generic block processor for received data */

{
    struct RunTime *r = ( struct RunTime * )params;

    while ( !r->ending )
    {
        sem_wait( &r->dataForClients );

        if ( r->rp != r->wp )
        {
            genericsReport( V_DEBUG, "RXED Packet of %d bytes" EOL, r->rawBlock[r->rp].fillLevel );

            if ( r->rawBlock[r->rp].fillLevel )
            {
                /* Account for this reception */
                r->intervalBytes += r->rawBlock[r->rp].fillLevel;

#ifdef DUMP_BLOCK
                uint8_t *c = r->rawBlock[r->rp].buffer;
                uint32_t y = r->rawBlock[r->rp].fillLevel;

                fprintf( stderr, EOL );

                while ( y-- )
                {
                    fprintf( stderr, "%02X ", *c++ );

                    if ( !( y % 16 ) )
                    {
                        fprintf( stderr, EOL );
                    }
                }

#endif

                if ( _r.opFileHandle )
                {
                    if ( write( _r.opFileHandle, r->rawBlock[r->rp].buffer, r->rawBlock[r->rp].fillLevel ) < 0 )
                    {
                        genericsExit( -3, "Writing to file failed" EOL );
                    }
                }

                if ( r-> options->useTPIU )
                {
                    /* Strip the TPIU framing from this input */
                    _stripTPIU( r, r->rawBlock[r->rp].buffer, r->rawBlock[r->rp].fillLevel );
                    _purgeBlock( r );
                }
                else
                {
                    /* Do it the old fashioned way and send out the unfettered block */
                    nwclientSend( _r.n, r->rawBlock[r->rp].fillLevel, r->rawBlock[r->rp].buffer );
                }
            }

            r->rp = ( r->rp + 1 ) % NUM_RAW_BLOCKS;
        }
    }

    return NULL;
}

// ====================================================================================================
static void _usb_callback( struct libusb_transfer *t )

/* For the USB case the ringbuffer isn't used .. packets are sent directly from this callback */

{
    /* Whatever the status that comes back, there may be data... */
    if ( t->actual_length > 0 )
    {
        _r.intervalBytes += t->actual_length;

        if ( _r.opFileHandle )
        {
            if ( write( _r.opFileHandle, t->buffer, t->actual_length ) < 0 )
            {
                genericsExit( -4, "Writing to file failed (%s)" EOL, strerror( errno ) );
            }
        }

#ifdef DUMP_BLOCK
        uint8_t *c = t->buffer;
        uint32_t y = t->actual_length;

        fprintf( stderr, EOL );

        while ( y-- )
        {
            fprintf( stderr, "%02X ", *c++ );

            if ( !( y % 16 ) )
            {
                fprintf( stderr, EOL );
            }
        }

#endif

        if ( _r.options->useTPIU )
        {
            /* Strip the TPIU framing from this input */
            _stripTPIU( &_r, t->buffer, t->actual_length );
            _purgeBlock( &_r );
        }
        else
        {
            /* Do it the old fashioned way and send out the unfettered block */
            nwclientSend( _r.n, t->actual_length, t->buffer );
        }
    }

    libusb_submit_transfer( t );
}
// ====================================================================================================
int usbFeeder( struct RunTime *r )

{
    libusb_device_handle *handle = NULL;
    libusb_device *dev;
    const struct deviceList *p;
    uint8_t iface;
    uint8_t ep;
    uint8_t altsetting = 0;
    uint8_t num_altsetting = 0;
    int32_t err;

    while ( !r->ending )
    {
        if ( libusb_init( NULL ) < 0 )
        {
            genericsReport( V_ERROR, "Failed to initalise USB interface" EOL );
            return ( -1 );
        }

        /* Snooze waiting for the device to appear .... this is useful for when they come and go */
        while ( 1 )
        {
            p = _deviceList;

            while ( p->vid != 0 )
            {
                genericsReport( V_DEBUG, "Looking for %s (%04x:%04x)" EOL, p->name, p->vid, p->pid );

                if ( ( handle = libusb_open_device_with_vid_pid( NULL, p->vid, p->pid ) ) )
                {
                    break;
                }

                p++;
            }

            if ( handle )
            {
                break;
            }

            /* Take a pause before looking again */
            usleep( 500000 );
        }

        genericsReport( V_INFO, "Found %s" EOL, p->name );

        if ( !( dev = libusb_get_device( handle ) ) )
        {
            /* We didn't get the device, so try again in a while */
            continue;
        }

        iface = p->iface;
        ep = p->ep;

        if ( p->autodiscover )
        {
            genericsReport( V_DEBUG, "Searching for trace interface" EOL );

            struct libusb_config_descriptor *config;

            if ( ( err = libusb_get_active_config_descriptor( dev, &config ) ) < 0 )
            {
                genericsReport( V_WARN, "Failed to get config descriptor (%d)" EOL, err );
                continue;
            }

            bool interface_found = false;

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
                        continue;
                    }

                    iface = i->bInterfaceNumber;
                    altsetting = i->bAlternateSetting;
                    num_altsetting = config->interface[if_num].num_altsetting;
                    ep = i->endpoint[0].bEndpointAddress;

                    genericsReport( V_DEBUG, "Found interface %#x with altsetting %#x and ep %#x" EOL, iface, altsetting, ep );

                    interface_found = true;
                }
            }

            if ( !interface_found )
            {
                genericsReport( V_DEBUG, "No supported interfaces found, falling back to hardcoded values" EOL );
            }

            libusb_free_config_descriptor( config );
        }

        if ( ( err = libusb_claim_interface ( handle, iface ) ) < 0 )
        {
            genericsReport( V_WARN, "Failed to claim interface (%d)" EOL, err );
            continue;
        }

        if ( num_altsetting > 1 && ( err = libusb_set_interface_alt_setting ( handle, iface, altsetting ) ) < 0 )
        {
            genericsReport( V_WARN, "Failed to set altsetting (%d)" EOL, err );
        }

        genericsReport( V_DEBUG, "USB Interface claimed, ready for data" EOL );

        for ( uint32_t t = 0; t < NUM_RAW_BLOCKS; t++ )
        {
            r->rawBlock[t].usbtfr = libusb_alloc_transfer( 0 );

            libusb_fill_bulk_transfer ( r->rawBlock[t].usbtfr, handle, ep,
                                        r->rawBlock[t].buffer,
                                        TRANSFER_SIZE,
                                        _usb_callback,
                                        &r->rawBlock[t].usbtfr,
                                        BLOCK_TIMEOUT_INTERVAL_MS
                                      );

            int ret = libusb_submit_transfer( r->rawBlock[t].usbtfr );

            if ( ret )
            {
                genericsReport( V_ERROR, "Error submitting USB requests %d" EOL, ret );
                _doExit();
            }
        }

        while ( !r->ending )
        {
            int ret = libusb_handle_events_completed( NULL, ( int * )&r->ending );

            if ( ret )
            {
                genericsReport( V_ERROR, "Error waiting for USB requests to complete %d" EOL, ret );
                _doExit();
            }
        }

        libusb_close( handle );
        genericsReport( V_INFO, "USB Interface closed" EOL );
    }

    return 0;
}
// ====================================================================================================
int seggerFeeder( struct RunTime *r )

{
    struct sockaddr_in serv_addr;
    struct hostent *server;

    int flag = 1;

    bzero( ( char * ) &serv_addr, sizeof( serv_addr ) );
    server = gethostbyname( r->options->seggerHost );

    if ( !server )
    {
        genericsReport( V_ERROR, "Cannot find host" EOL );
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    bcopy( ( char * )server->h_addr,
           ( char * )&serv_addr.sin_addr.s_addr,
           server->h_length );
    serv_addr.sin_port = htons( r->options->seggerPort );

    while ( !r->ending )
    {
        r->f = socket( AF_INET, SOCK_STREAM, 0 );
        setsockopt( r->f, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof( flag ) );

        if ( r->f < 0 )
        {
            genericsReport( V_ERROR, "Error creating socket" EOL );
            return -1;
        }

        while ( ( !r->ending ) && ( connect( r->f, ( struct sockaddr * ) &serv_addr, sizeof( serv_addr ) ) < 0 ) )
        {
            usleep( 500000 );
        }

        if ( r->ending )
        {
            break;
        }

        genericsReport( V_INFO, "Established Segger Link" EOL );

        while ( !r->ending )

        {
            struct dataBlock *rxBlock = &r->rawBlock[r->wp];

            if ( ( rxBlock->fillLevel = read( r->f, rxBlock->buffer, TRANSFER_SIZE ) ) <= 0 )
            {
                break;
            }

            r->wp = ( r->wp + 1 ) % NUM_RAW_BLOCKS;
            sem_post( &r->dataForClients );
        }

        close( r->f );

        if ( ! r->ending )
        {
            genericsReport( V_INFO, "Lost Segger Link" EOL );
        }
    }

    return -2;
}
// ====================================================================================================
int serialFeeder( struct RunTime *r )
{
    int ret;

    while ( !r->ending )
    {
#ifdef OSX
        int flags;

        while ( !r->ending && ( r->f = open( r->options->port, O_RDONLY | O_NONBLOCK ) ) < 0 )
#else
        while ( !r->ending && ( r->f = open( r->options->port, O_RDONLY ) ) < 0 )
#endif
        {
            genericsReport( V_WARN, "Can't open serial port" EOL );
            usleep( 500000 );
        }

        genericsReport( V_INFO, "Port opened" EOL );

#ifdef OSX
        /* Remove the O_NONBLOCK flag now the port is open (OSX Only) */

        if ( ( flags = fcntl( r->f, F_GETFL, NULL ) ) < 0 )
        {
            genericsExit( -3, "F_GETFL failed" EOL );
        }

        flags &= ~O_NONBLOCK;

        if ( ( flags = fcntl( r->f, F_SETFL, flags ) ) < 0 )
        {
            genericsExit( -3, "F_SETFL failed" EOL );
        }

#endif

        if ( ( ret = _setSerialConfig ( r->f, r->options->speed ) ) < 0 )
        {
            genericsExit( ret, "setSerialConfig failed" EOL );
        }

        while ( !r->ending )
        {
            struct dataBlock *rxBlock = &r->rawBlock[r->wp];

            if ( ( rxBlock->fillLevel = read( r->f, rxBlock->buffer, TRANSFER_SIZE ) ) <= 0 )
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

        close( r->f );
    }

    return 0;
}
// ====================================================================================================
int fileFeeder( struct RunTime *r )

{
    if ( ( r->f = open( r->options->file, O_RDONLY ) ) < 0 )
    {
        genericsExit( -4, "Can't open file %s" EOL, r->options->file );
    }

    while ( !r->ending )
    {
        struct dataBlock *rxBlock = &r->rawBlock[r->wp];
        rxBlock->fillLevel = read( r->f, rxBlock->buffer, TRANSFER_SIZE );

        if ( !rxBlock->fillLevel )
        {
            if ( r->options->fileTerminate )
            {
                break;
            }
            else
            {
                // Just spin for a while to avoid clogging the CPU
                usleep( 100000 );
                continue;
            }
        }

        r->wp = ( r->wp + 1 ) % NUM_RAW_BLOCKS;
        sem_post( &r->dataForClients );
    }

    if ( !r->options->fileTerminate )
    {
        genericsReport( V_INFO, "File read error" EOL );
    }

    close( r->f );
    return true;
}
// ====================================================================================================
int main( int argc, char *argv[] )

{
    /* Setup TPIU in case we call it into service later */
    TPIUDecoderInit( &_r.t );
    sem_init( &_r.dataForClients, 0, 0 );

    if ( !_processOptions( argc, argv, &_r ) )
    {
        /* processOptions generates its own error messages */
        genericsExit( -1, "" EOL );
    }

    /* Make sure the network clients get removed at the end */
    atexit( _doExit );

    /* This ensures the atexit gets called */
    if ( SIG_ERR == signal( SIGINT, _intHandler ) )
    {
        genericsExit( -1, "Failed to establish Int handler" EOL );
    }

    /* Don't kill a sub-process when any reader or writer evaporates */
    if ( SIG_ERR == signal( SIGPIPE, SIG_IGN ) )
    {
        genericsExit( -1, "Failed to ignore SIGPIPEs" EOL );
    }

    if ( _r.options->useTPIU )
    {
        char *c = _r.options->channelList;
        int x = 0;

        while ( *c )
        {
            while ( *c == ',' )
            {
                c++;
            }

            while ( isdigit( *c ) )
            {
                x = x * 10 + ( *c++ -'0' );
            }

            if ( ( *c ) && ( *c != ',' ) )
            {
                genericsExit( -1, "Illegal character in channel list (%c)" EOL, *c );
            }

            if ( x )
            {
                /* This is a good number, so open */
                if ( ( x < 0 ) || ( x >= NUM_TPIU_CHANNELS ) )
                {
                    genericsExit( -1, "Channel number out of range" EOL );
                }

                _r.handler = ( struct handlers * )realloc( _r.handler, sizeof( struct handlers ) * ( _r.numHandlers + 1 ) );

                _r.handler[_r.numHandlers].channel = x;
                _r.handler[_r.numHandlers].strippedBlock = ( struct dataBlock * )calloc( 1, sizeof( struct dataBlock ) );
                _r.handler[_r.numHandlers].n = nwclientStart(  _r.options->listenPort + _r.numHandlers );
                genericsReport( V_WARN, "Started Network interface for channel %d on port %d" EOL, x, _r.options->listenPort + _r.numHandlers );
                _r.numHandlers++;
                x = 0;
            }
        }
    }
    else
    {
        if ( !( _r.n = nwclientStart( _r.options->listenPort ) ) )
        {
            genericsExit( -1, "Failed to make network server" EOL );
        }
    }

    if ( _r.options->intervalReportTime )
    {
        pthread_create( &_r.intervalThread, NULL, &_checkInterval, &_r );
    }

    /* Now start the distribution task */
    pthread_create( &_r.processThread, NULL, &_processBlocks, &_r );

    if ( _r.options->outfile )
    {
        _r.opFileHandle = open( _r.options->outfile, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH );

        if ( _r.opFileHandle < 0 )
        {
            genericsReport( V_ERROR, "Could not open output file for writing" EOL );
            return -2;
        }
    }

    if ( _r.options->seggerPort )
    {
        exit( seggerFeeder( &_r ) );
    }

    if ( _r.options->port )
    {
        exit( serialFeeder( &_r ) );
    }

    if ( _r.options->file )
    {
        exit( fileFeeder( &_r ) );
    }

    exit( usbFeeder( &_r ) );
}
// ====================================================================================================
