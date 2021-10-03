/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Orbtrace Interface
 * ==================
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <signal.h>

#include "git_version_info.h"
#include "generics.h"

#include "orbtraceIf.h"

#define DONTSET (-1)
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

/* Record for options, either defaults or from command line */
struct Options
{
    char *sn;                 /* Any part of serial number to differentiate probe */
    char *qQuery;             /* V/I Parameters to query from probe */
    char *nick;               /* Nickname for device */
    int brightness;           /* Brightness of OP LEDs */
    int traceWidth;           /* Width to be used for communication */
    bool forceVoltage;        /* Force application of voltage */
    bool opJSON;              /* Set output to JSON */
    bool listDevices;         /* List devices connected to system */
    int TPwrmv;               /* Target power setting in mv */
    int TRefmv;               /* Target voltage setting in mv */
    bool writeParams;         /* Write specified parameters to NVRAM */
    bool resetParams;         /* Reset all parameters in NVRAM */
    bool unlock;              /* Unlock device */
    bool lock;                /* Lock device */
    int setCount;             /* Number of device changes to be processed */
} _options =
{
    .traceWidth = DONTSET,
    .brightness = DONTSET,
    .TPwrmv = DONTSET,
    .TRefmv = DONTSET
};

struct RunTime
{
    /* Link to the connected Orbtrace device */
    struct OrbtraceIf *dev;

    bool      ending;                                                  /* Flag indicating app is terminating */
    int ndevices;
    int seldevice;

    struct Options *options;
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
void _printHelp( char *progName )

{
    genericsPrintf( "Usage: %s [options]" EOL, progName );
    genericsPrintf( "       -b: <Brightness> Set default brightness of output leds" EOL );
    genericsPrintf( "       -f: <filename> Take input from specified file" EOL );
    genericsPrintf( "       -h: This help" EOL );
    genericsPrintf( "       -j: Format output in JSON" EOL );
    genericsPrintf( "       -l: Show all OrbTrace devices attached to system" EOL );
    genericsPrintf( "       -L: Lock device (prevent further changes)" EOL );
    genericsPrintf( "       -n: <Nick> Specify nickname for adaptor (8 chars max)" EOL );
    genericsPrintf( "       -o: <num> Specify 1, 2 or 4 bits trace width" EOL );
    genericsPrintf( "       -q: Query all data from connected device" EOL );
    genericsPrintf( "       -Q: Query specified data from connected device (pPrR VPwr/IPwr/VRef/IRef)" EOL );
    genericsPrintf( "       -p: <Voltage> Set TPwr voltage (0=Off)" EOL );
    genericsPrintf( "       -r: <Voltage> Set TRef voltage (0=Passive)" EOL );
    genericsPrintf( "       -s: <Serial> any part of serial number to differentiate specific OrbTrace device" EOL );
    genericsPrintf( "       -U: Unlock device (allow changes, default state)" EOL );
    genericsPrintf( "       -v: <level> Verbose mode 0(errors)..3(debug)" EOL );
    genericsPrintf( "       -w: Write parameters specified on command line to NVRAM" EOL );
    genericsPrintf( "       -W: Reset all NVRAM parameters to default values" EOL );
}
// ====================================================================================================
static bool _checkVoltages( struct RunTime *r )

{
    if ( ( r->options->TRefmv != DONTSET ) && ( 0 == OrbtraceIfValidateVoltage( r->dev, r->options->TRefmv ) ) )
    {
        genericsReport( V_ERROR, "Illegal voltage specified for TRef (%d.%03dV)" EOL, r->options->TRefmv / 1000, r->options->TRefmv % 1000 );
        return false;
    }

    if ( ( r->options->TPwrmv != DONTSET ) && ( 0 == OrbtraceIfValidateVoltage( r->dev, r->options->TPwrmv ) ) )
    {
        genericsReport( V_ERROR, "Illegal voltage specified for TPwr (%d.%03dV)" EOL, r->options->TPwrmv / 1000, r->options->TPwrmv % 1000 );
        return false;
    }

    return true;
}
// ====================================================================================================
int _processOptions( struct RunTime *r, int argc, char *argv[]  )

{
    int c;
    float voltage;

    while ( ( c = getopt ( argc, argv, "b:f:hlLn:o:qQ:p:r:s:Uv:wW" ) ) != -1 )
        switch ( c )
        {
            // ------------------------------------
            case 'b': /* Brightness */
                r->options->brightness = atoi( optarg );
                r->options->setCount++;
                break;

            // ------------------------------------
            case 'F': /* Input filename */
                r->options->forceVoltage = true;
                break;

            // ------------------------------------
            case 'h':
                _printHelp( argv[0] );
                return false;

            // ------------------------------------
            case 'j': /* Force output in JSON */
                r->options->opJSON = true;
                break;

            // ------------------------------------
            case 'l': /* List connected devices */
                r->options->listDevices = true;
                break;

            // ------------------------------------
            case 'L': /* Lock device */
                r->options->lock = true;
                r->options->setCount++;
                break;

            // ------------------------------------
            case 'n':
                r->options->nick = optarg;
                r->options->setCount++;
                break;

            // ------------------------------------
            case 'o':
                r->options->traceWidth = atoi( optarg );
                r->options->setCount++;
                break;

            // ------------------------------------
            case 'p':
                voltage = atof( optarg );
                r->options->TPwrmv = ( int )( ( voltage + 0.0005F ) * 1000 );
                r->options->setCount++;
                break;

            // ------------------------------------
            case 'r':
                voltage = atof( optarg );
                r->options->TRefmv = ( int )( ( voltage + 0.0005F ) * 1000 );
                r->options->setCount++;
                break;

            // ------------------------------------

            case 's':
                r->options->sn = optarg;
                break;

            // ------------------------------------
            case 'U': /* Unlock device */
                r->options->unlock = true;
                r->options->setCount++;
                break;

            // ------------------------------------
            case 'v':
                genericsSetReportLevel( atoi( optarg ) );
                break;

            // ------------------------------------
            case 'w': /* Write parameters to NVRAM */
                r->options->writeParams = true;
                break;

            // ------------------------------------
            case 'W': /* Reset parameters in NVRAM */
                r->options->resetParams = true;
                r->options->setCount++;
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

    /* Test parameters for sanity */
    if ( r->options->setCount )
    {
        if ( r->options->resetParams )
        {
            genericsReport( V_ERROR, "Cannot set a parameter while reseting all parameters" EOL );
            return false;
        }

        if ( r->options->listDevices )
        {
            genericsReport( V_ERROR, "Cannot list devices while setting a parameter" EOL );
            return false;
        }
    }

    if ( ( r->options->traceWidth != DONTSET ) &&
            ( r->options->traceWidth != 1 ) &&
            ( r->options->traceWidth != 2 ) &&
            ( r->options->traceWidth != 4 ) )
    {
        genericsReport( V_ERROR, "Orbtrace interface illegal port width" EOL );
        return false;
    }

    if ( !_checkVoltages( r ) )
    {
        return false;
    }

    if ( ( r->options->brightness != DONTSET ) && ( ( r->options->brightness < 0 ) || ( r->options->brightness > 255 ) ) )
    {
        genericsReport( V_ERROR, "Brightness setting out of range" EOL );
        return false;
    }

    /* ... and dump the config if we're being verbose */
    genericsReport( V_INFO, "%s V" VERSION " (Git %08X %s, Built " BUILD_DATE ")" EOL, argv[0], GIT_HASH, ( GIT_DIRTY ? "Dirty" : "Clean" ) );

    return true;
}
// ====================================================================================================
static void _doExit( void )

{
    _r.ending = true;
}
// ====================================================================================================
static int _selectDevice( struct RunTime *r, bool listOnly )

{
    int descWidth = 0;
    int selection = 0;

    if ( ( !listOnly ) && ( r->ndevices == 1 ) )
    {
        return r->ndevices - 1;
    }

    for ( int i = 0; i < r->ndevices; i++ )
    {
        int l = MAX( 11, strlen( OrbtraceIfGetManufacturer( r->dev, i ) ) + strlen( OrbtraceIfGetProduct( r->dev, i ) ) ) + MAX( 6, strlen( OrbtraceIfGetSN( r->dev, i ) ) );

        if ( l > descWidth )
        {
            descWidth = l;
        }
    }

    descWidth += 1;

    fprintf( stdout, "Id | " );

    for ( int i = 0; i < ( ( descWidth + 1 ) / 2 - 6 ); i++ )
    {
        fprintf( stdout, " " );
    }

    fprintf( stdout, "Description" );

    for ( int i = 0; i < ( descWidth / 2 - 6 ); i++ )
    {
        fprintf( stdout, " " );
    }

    fprintf( stdout, " | Serial" EOL );

    for ( int i = 0; i < ( descWidth + 5 + 10 ); i++ )
    {
        fprintf( stdout, "-" );
    }

    fprintf( stdout, EOL );

    for ( int i = 0; i < r->ndevices; i++ )
    {
        int thisWidth = strlen( OrbtraceIfGetManufacturer( r->dev, i ) ) + strlen( OrbtraceIfGetProduct( r->dev, i ) ) + 1;
        printf( "%2i | %s %s", i + 1, OrbtraceIfGetManufacturer( r->dev, i ), OrbtraceIfGetProduct( r->dev, i ) );

        for ( int j = thisWidth; j < descWidth; j++ )
        {
            fprintf( stdout, " " );
        }

        fprintf( stdout, "| %s" EOL, OrbtraceIfGetSN( r->dev, i ) );
    }

    if ( !listOnly )
        while ( ( selection < 1 ) || ( selection > r->ndevices ) )
        {
            fprintf( stdout, EOL "Selection>" );
            scanf( "%d", &selection );
        }

    return selection - 1;
}

// ====================================================================================================
static void _performActions( struct RunTime *r )

{
    if ( r->options->traceWidth != DONTSET )
    {
        genericsReport( V_INFO, "Setting port width to %d" EOL, r->options->traceWidth );
    }

    if ( OrbtraceIfSetTraceWidth( r->dev, r->options->traceWidth ) )
    {
        genericsReport( V_INFO, "OK" EOL );
    }
    else
    {
        genericsReport( V_INFO, "Failed" EOL );
    }
}
// ====================================================================================================
int main( int argc, char *argv[] )

{
    int selection = 0;

    if ( !_processOptions( &_r, argc, argv ) )
    {
        /* processOptions generates its own error messages */
        genericsExit( -1, "" EOL );
    }

    /* Make sure everything gets removed at the end */
    atexit( _doExit );

    /* This ensures the atexit gets called */
    if ( SIG_ERR == signal( SIGINT, _intHandler ) )
    {
        genericsExit( -1, "Failed to establish Int handler" EOL );
    }

    _r.dev = OrbtraceIfCreateContext();

    assert( _r.dev );

    _r.ndevices = OrbtraceIfGetDeviceList( _r.dev, _r.options->sn );

    if ( !_r.ndevices )
    {
        genericsReport( V_ERROR, "No devices found" EOL );
    }
    else
    {
        /* Allow option to choose between devices if there's more than one found */
        _r.seldevice = _selectDevice( &_r, _r.options->listDevices );

        if ( _r.options->setCount )
        {
            genericsReport( V_INFO, "Got device [%s %s, S/N %s]" EOL,
                            OrbtraceIfGetManufacturer( _r.dev, selection ),
                            OrbtraceIfGetProduct( _r.dev, selection ),
                            OrbtraceIfGetSN( _r.dev, selection ) );

            if ( !OrbtraceIfOpenDevice( _r.dev, _r.seldevice ) )
            {
                genericsExit( -1, "Couldn't open device" EOL );
            }

            /* Check voltages again now we know what interface we're connected to */
            if ( !_checkVoltages( &_r ) )
            {
                genericsExit( -2, "Specified interface voltage check failed" EOL );
            }

            _performActions( &_r );
        }

        OrbtraceIfCloseDevice( _r.dev );
    }
}
// ====================================================================================================
