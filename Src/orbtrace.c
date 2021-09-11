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
struct
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
} options =
{
    .traceWidth = DONTSET,
    .brightness = DONTSET,
    .TPwrmv = DONTSET,
    .TRefmv = DONTSET
};

struct
{
    /* Link to the connected Orbtrace device */
    struct OrbtraceIf *dev;

    bool      ending;                                                  /* Flag indicating app is terminating */
} _r;

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
static bool _checkVoltages( struct OrbtraceIf *o )

{
    if ( ( options.TRefmv != DONTSET ) && ( 0 == OrbtraceIfValidateVoltage( o, options.TRefmv ) ) )
    {
        genericsReport( V_ERROR, "Illegal voltage specified for TRef (%d.%03dV)" EOL, options.TRefmv / 1000, options.TRefmv % 1000 );
        return false;
    }

    if ( ( options.TPwrmv != DONTSET ) && ( 0 == OrbtraceIfValidateVoltage( o, options.TPwrmv ) ) )
    {
        genericsReport( V_ERROR, "Illegal voltage specified for TPwr (%d.%03dV)" EOL, options.TPwrmv / 1000, options.TPwrmv % 1000 );
        return false;
    }

    return true;
}
// ====================================================================================================
int _processOptions( int argc, char *argv[] )

{
    int c;
    float voltage;

    while ( ( c = getopt ( argc, argv, "b:f:hlLn:o:qQ:p:r:s:Uv:wW" ) ) != -1 )
        switch ( c )
        {
            // ------------------------------------
            case 'b': /* Brightness */
                options.brightness = atoi( optarg );
                options.setCount++;
                break;

            // ------------------------------------
            case 'F': /* Input filename */
                options.forceVoltage = true;
                break;

            // ------------------------------------
            case 'h':
                _printHelp( argv[0] );
                return false;

            // ------------------------------------
            case 'j': /* Force output in JSON */
                options.opJSON = true;
                break;

            // ------------------------------------
            case 'l': /* List connected devices */
                options.listDevices = true;
                break;

            // ------------------------------------
            case 'L': /* Lock device */
                options.lock = true;
                options.setCount++;
                break;

            // ------------------------------------
            case 'n':
                options.nick = optarg;
                options.setCount++;
                break;

            // ------------------------------------
            case 'o':
                options.traceWidth = atoi( optarg );
                options.setCount++;
                break;

            // ------------------------------------
            case 'p':
                voltage = atof( optarg );
                options.TPwrmv = ( int )( ( voltage + 0.0005F ) * 1000 );
                options.setCount++;
                break;

            // ------------------------------------
            case 'r':
                voltage = atof( optarg );
                options.TRefmv = ( int )( ( voltage + 0.0005F ) * 1000 );
                options.setCount++;
                break;

            // ------------------------------------

            case 's':
                options.sn = optarg;
                break;

            // ------------------------------------
            case 'U': /* Unlock device */
                options.unlock = true;
                options.setCount++;
                break;

            // ------------------------------------
            case 'v':
                genericsSetReportLevel( atoi( optarg ) );
                break;

            // ------------------------------------
            case 'w': /* Write parameters to NVRAM */
                options.writeParams = true;
                break;

            // ------------------------------------
            case 'W': /* Reset parameters in NVRAM */
                options.resetParams = true;
                options.setCount++;
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
    if ( options.setCount )
    {
        if ( options.resetParams )
        {
            genericsReport( V_ERROR, "Cannot set a parameter while reseting all parameters" EOL );
            return false;
        }

        if ( options.listDevices )
        {
            genericsReport( V_ERROR, "Cannot list devices while setting a parameter" EOL );
            return false;
        }
    }

    if ( ( options.traceWidth != DONTSET ) &&
            ( options.traceWidth != 1 ) &&
            ( options.traceWidth != 2 ) &&
            ( options.traceWidth == 4 ) )
    {
        genericsReport( V_ERROR, "Orbtrace interface illegal port width" EOL );
        return false;
    }

    if ( !_checkVoltages( NULL ) )
    {
        return false;
    }

    if ( ( options.brightness != DONTSET ) && ( ( options.brightness < 0 ) || ( options.brightness > 255 ) ) )
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
static int _selectDevice( struct OrbtraceIf *o, int ndevices, bool listOnly )

{
    int descWidth = 0;
    int selection = 0;

    if ( ( !listOnly ) && ( ndevices == 1 ) )
    {
        return ndevices - 1;
    }

    for ( int i = 0; i < ndevices; i++ )
    {
        int l = MAX( 11, strlen( OrbtraceIfGetManufacturer( o, i ) ) + strlen( OrbtraceIfGetProduct( o, i ) ) ) + MAX( 6, strlen( OrbtraceIfGetSN( o, i ) ) );

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

    for ( int i = 0; i < ndevices; i++ )
    {
        int thisWidth = strlen( OrbtraceIfGetManufacturer( o, i ) ) + strlen( OrbtraceIfGetProduct( o, i ) ) + 1;
        printf( "%2i | %s %s", i + 1, OrbtraceIfGetManufacturer( o, i ), OrbtraceIfGetProduct( o, i ) );

        for ( int j = thisWidth; j < descWidth; j++ )
        {
            fprintf( stdout, " " );
        }

        fprintf( stdout, "| %s" EOL, OrbtraceIfGetSN( o, i ) );
    }

    if ( !listOnly )
        while ( ( selection < 1 ) || ( selection > ndevices ) )
        {
            fprintf( stdout, EOL "Selection>" );
            scanf( "%d", &selection );
        }

    return selection - 1;
}

// ====================================================================================================
static void _performActions( struct OrbtraceIf *o )

{

}
// ====================================================================================================
int main( int argc, char *argv[] )

{
    int selection = 0;

    if ( !_processOptions( argc, argv ) )
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

    struct OrbtraceIf *o = OrbtraceIfCreateContext();

    assert( o );

    int ndevices = OrbtraceIfGetDeviceList( o, options.sn );

    /* Allow option to choose between devices if there's more than one found */
    selection = _selectDevice( o, ndevices, options.listDevices );

    if ( options.setCount )
    {
        genericsReport( V_INFO, "Got device [%s %s, S/N %s]" EOL,
                        OrbtraceIfGetManufacturer( o, selection ),
                        OrbtraceIfGetProduct( o, selection ),
                        OrbtraceIfGetSN( o, selection ) );

        if ( !OrbtraceIfOpenDevice( o, selection ) )
        {
            genericsExit( -1, "Couldn't open device" EOL );
        }

        /* Check voltages again now we know what interface we're connected to */
        if ( !_checkVoltages( o ) )
        {
            genericsExit( -2, "Specified interface voltage check failed" EOL );
        }

        _performActions( o );

        OrbtraceIfCloseDevice( o );
    }
}
// ====================================================================================================
