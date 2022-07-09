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

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

/* Record for options, either defaults or from command line */
struct Options
{
    /* Probe config */
    char *sn;                 /* Any part of serial number to differentiate probe */
    char *nick;               /* Nickname for device */
    int brightness;           /* Brightness of OP LEDs */

    /* Trace settings */
    int traceWidth;           /* Width to be used for communication */
    bool swoMANCH;            /* SWO Manchester output */
    bool swoUART;             /* SWO UART output */
    bool opJSON;              /* Set output to JSON */

    /* Power settings */
    bool forceVoltage;        /* Force application of voltage */
    int TPwrmv;               /* Target power setting in mv */
    int TRefmv;               /* Target voltage setting in mv */
    bool TPwrEN;              /* If to enable/disable TPwr */
    bool TRefEN;              /* If to enable/disable TRef */
} _options;

enum Actions { ACTION_BRIGHTNESS, ACTION_ENCHANGE_VTREF, ACTION_ENCHANGE_VTPWR, ACTION_LIST_DEVICES,
               ACTION_LOCKDEVICE, ACTION_SETNICK, ACTION_VCHANGE_VTREF, ACTION_VCHANGE_VTPWR, ACTION_SN,
               ACTION_UNLOCK, ACTION_WRITE_PARAMS, ACTION_READ_PARAMS, ACTION_RESET_PARAMS, ACTION_SET_TRACE,
               ACTION_ENCHANGE_ALL
             };

struct RunTime
{
    struct OrbtraceIf *dev;     /* Link to the connected Orbtrace device */

    bool     ending;            /* Flag indicating app is terminating */
    int      ndevices;          /* Number of devices found connected that match search spec */
    uint64_t actions;           /* Actions to be performed */
    struct Options *options;    /* Runtime command line options */
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
static void _set_action( struct RunTime *r, int x )

/* Set up an action flag */

{
    r->actions |= ( 1 << x );
}
// ====================================================================================================
static void _clr_action( struct RunTime *r, int x )

/* Clear an action flag */

{
    r->actions &= ~( 1 << x );
}
// ====================================================================================================
static bool _tst_action( struct RunTime *r, int x )

/* Query an action flag */

{
    return ( 0 != ( r->actions & ( 1 << x ) ) );
}
// ====================================================================================================
static bool _tcl_action( struct RunTime *r, int x )

/* Query and then clear an action flag */

{
    if ( !_tst_action( r, x ) )
    {
        return false;
    }
    else
    {
        _clr_action( r, x );
        return true;
    }
}
// ====================================================================================================
static int _num_actions( struct RunTime *r )

/* Return how many actions flags are set */

{
    return ( __builtin_popcount( r->actions ) );
}

// ====================================================================================================
static void _intHandler( int sig )

/* Signal handler for CTRL-C */

{
    /* CTRL-C exit is not an error... */
    exit( 0 );
}
// ====================================================================================================
static void _printHelp( char *progName )

{
    genericsPrintf( "Usage: %s [options]" EOL, progName );
    //    genericsPrintf( "      *-b: <Brightness> Set default brightness of output leds" EOL );
    genericsPrintf( "       -e: <Ch>,<On> Enable or Disable power. Ch is vtref, vtpwr or all" EOL );
    //    genericsPrintf( "      *-F: Force voltage setting" EOL );
    genericsPrintf( "       -h: This help" EOL );
    //    genericsPrintf( "      *-j: Format output in JSON" EOL );
    genericsPrintf( "       -l: Show all OrbTrace devices attached to system" EOL );
    //    genericsPrintf( "      *-L: Lock device (prevent further changes)" EOL );
    //    genericsPrintf( "      *-n: <Nick> Specify nickname for adaptor (8 chars max)" EOL );
    genericsPrintf( "       -t: <x> Trace format; 1,2 or 4 bit parallel, m for Manchester SWO, u=UART SWO" EOL );
    //    genericsPrintf( "      *-q: Query all data from connected device" EOL );
    //    genericsPrintf( "      *-Q: Query specified data from connected device (pPrR VPwr/IPwr/VRef/IRef)" EOL );
    genericsPrintf( "       -p: <Ch>,<Voltage> Set voltage in V, Ch is vtref or vtpwr" EOL );
    genericsPrintf( "       -s: <Serial> any part of serial number to differentiate specific OrbTrace device" EOL );
    //    genericsPrintf( "      *-U: Unlock device (allow changes, default state)" EOL );
    genericsPrintf( "       -v: <level> Verbose mode 0(errors)..3(debug)" EOL );
    //    genericsPrintf( "      *-w: Write parameters specified on command line to NVRAM" EOL );
    //    genericsPrintf( "      *-W: Reset all NVRAM parameters to default values" EOL );
}
// ====================================================================================================
static bool _checkVoltages( struct RunTime *r )

/* Check that voltages, if requested, are sensible */

{
    if ( _tst_action( r, ACTION_VCHANGE_VTREF ) && ( 0 == OrbtraceIfValidateVoltage( r->dev, r->options->TRefmv ) ) )
    {
        genericsReport( V_ERROR, "Illegal voltage specified for TRef (%d.%03dV)" EOL, r->options->TRefmv / 1000, r->options->TRefmv % 1000 );
        return false;
    }

    if ( _tst_action( r, ACTION_VCHANGE_VTPWR ) && ( 0 == OrbtraceIfValidateVoltage( r->dev, r->options->TPwrmv ) ) )
    {
        genericsReport( V_ERROR, "Illegal voltage specified for TPwr (%d.%03dV)" EOL, r->options->TPwrmv / 1000, r->options->TPwrmv % 1000 );
        return false;
    }

    return true;
}
// ====================================================================================================
static int _processOptions( struct RunTime *r, int argc, char *argv[]  )

/* Process command line options into options and actions records */

{
    int c;
    float voltage;
    int channel;
    bool action;
    char *a;

    while ( ( c = getopt ( argc, argv, "e:hlp:s:t:v:" ) ) != -1 )
        switch ( c )
        {
            // ------------------------------------
            case 'b': /* Brightness */
                r->options->brightness = atoi( optarg );
                _set_action( r, ACTION_BRIGHTNESS );
                break;

            // ------------------------------------
            case 'e':
                channel = OrbtraceIfNameToChannel( optarg );
                a = optarg;

                while ( ( *a ) && ( *a != ',' ) )
                {
                    a++;
                }

                if ( ( *a == ',' ) || ( channel != CH_NONE ) )
                {
                    a++;
                    action = ( ( *a == '1' ) || ( !strcasecmp( "on", a ) ) );

                    if ( action || ( *a == '0' ) || ( !strcasecmp( "off", a ) ) )
                    {
                        if ( channel == CH_VTREF )
                        {
                            r->options->TRefEN = action;
                            _set_action( r, ACTION_ENCHANGE_VTREF );
                            break;
                        }

                        if ( channel == CH_VTPWR )
                        {
                            r->options->TPwrEN = action;
                            _set_action( r, ACTION_ENCHANGE_VTPWR );
                            break;
                        }

                        if ( channel == CH_ALL )
                        {
                            r->options->TPwrEN = r->options->TRefEN = action;
                            _set_action( r, ACTION_ENCHANGE_ALL );
                            break;
                        }
                    }
                }

                genericsReport( V_ERROR, "Badly formatted enable" EOL );
                return false;

            // ------------------------------------
            case 'F': /* Force voltage */
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
                _set_action( r, ACTION_LIST_DEVICES );
                break;

            // ------------------------------------
            case 'L': /* Lock device */
                _set_action( r, ACTION_LOCKDEVICE );
                break;

            // ------------------------------------
            case 'n': /* Set nickname */
                r->options->nick = optarg;
                _set_action( r, ACTION_SETNICK );
                break;

            // ------------------------------------
            case 't': /* Set tracewidth */
                r->options->traceWidth = 0;

                if ( ( *optarg == 'u' ) && ( !*( optarg + 1 ) ) )
                {
                    r->options->swoUART = true;
                }
                else if ( ( *optarg == 'm' ) && ( !*( optarg + 1 ) ) )
                {
                    r->options->swoMANCH = true;
                }
                else
                {
                    r->options->traceWidth = atoi( optarg );
                }

                _set_action( r, ACTION_SET_TRACE );
                break;

            // ------------------------------------
            case 'p': /* Set power */
                channel = OrbtraceIfNameToChannel( optarg );
                a = optarg;

                while ( ( *a ) && ( *a != ',' ) )
                {
                    a++;
                }

                if ( ( *a == ',' ) && ( channel != CH_NONE ) )
                {
                    a++;
                    voltage = atof( a );

                    if ( channel == CH_VTREF )
                    {
                        r->options->TRefmv = ( int )( ( voltage + 0.0005F ) * 1000 );
                        _set_action( r, ACTION_VCHANGE_VTREF );
                        break;
                    }

                    if ( channel == CH_VTPWR )
                    {
                        r->options->TPwrmv = ( int )( ( voltage + 0.0005F ) * 1000 );
                        _set_action( r, ACTION_VCHANGE_VTPWR );
                        break;
                    }
                }

                genericsReport( V_ERROR, "Badly formatted power statement" EOL );
                return false;

            // ------------------------------------

            case 's': /* Set serial number */
                r->options->sn = optarg;
                _set_action( r, ACTION_SN );
                break;

            // ------------------------------------
            case 'U': /* Unlock device */
                _set_action( r, ACTION_UNLOCK );
                break;

            // ------------------------------------
            case 'v':
                genericsSetReportLevel( atoi( optarg ) );
                break;

            // ------------------------------------
            case 'w': /* Write parameters to NVRAM */
                _set_action( r, ACTION_WRITE_PARAMS );
                break;

            // ------------------------------------
            case 'W': /* Reset parameters in NVRAM */
                _set_action( r, ACTION_RESET_PARAMS );
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
    if ( _tst_action( r, ACTION_RESET_PARAMS ) && ( _num_actions( r ) > 1 ) )
    {
        genericsReport( V_ERROR, "Resetting parameters is an exclusive operation" EOL );
        return false;
    }

    if ( ( _tst_action( r, ACTION_SET_TRACE ) ) &&
            ( ( ( r->options->traceWidth ) && ( ( r->options->swoUART ) || ( r->options->swoMANCH ) ) ) ||
              ( ( r->options->swoUART ) && ( r->options->swoMANCH ) ) ) )
    {
        genericsReport( V_ERROR, "Only one trace configuration can be set at the same time" EOL );
        return false;
    }

    if ( _tst_action( r, ACTION_LIST_DEVICES ) && ( _num_actions( r ) > 1 ) )
    {
        genericsReport( V_ERROR, "Listing devices is an exclusive operation" EOL );
        return false;
    }

    if (    ( r->options->traceWidth != 0 ) &&
            ( r->options->traceWidth != 1 ) &&
            ( r->options->traceWidth != 2 ) &&
            ( r->options->traceWidth != 4 ) )
    {
        genericsReport( V_ERROR, "Orbtrace interface illegal port width" EOL );
        return false;
    }

    if ( _tst_action( r, ACTION_BRIGHTNESS ) && ( ( r->options->brightness < 0 ) || ( r->options->brightness > 255 ) ) )
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

/* Whatever we were asked to do, do it */

{
    /* Beware, there is a logic to the order of actions...think before you change them or insert new ones */
    // -----------------------------------------------------------------------------------
    if ( _tst_action( r, ACTION_UNLOCK ) )
    {
    }

    // -----------------------------------------------------------------------------------
    if ( _tst_action( r, ACTION_RESET_PARAMS ) )
    {
    }

    // -----------------------------------------------------------------------------------
    if ( _tcl_action( r, ACTION_VCHANGE_VTREF ) )
    {
        genericsReport( V_INFO, "Setting VTRef %d.%03dV : ", r->options->TRefmv / 1000, r->options->TRefmv % 1000 );

        if ( OrbtraceIfVoltage( r->dev, CH_VTREF, r->options->TRefmv ) )
        {
            genericsReport( V_INFO, "OK" EOL );
        }
        else
        {
            genericsReport( V_ERROR, "Failed" EOL );
        }
    }

    // -----------------------------------------------------------------------------------
    if ( _tcl_action( r, ACTION_VCHANGE_VTPWR ) )
    {
        genericsReport( V_INFO, "Setting VTPwr %d.%03dV : ", r->options->TPwrmv / 1000, r->options->TPwrmv % 1000 );

        if ( OrbtraceIfVoltage( r->dev, CH_VTPWR, r->options->TPwrmv ) )
        {
            genericsReport( V_INFO, "OK" EOL );
        }
        else
        {
            genericsReport( V_ERROR, "Failed" EOL );
        }
    }

    // -----------------------------------------------------------------------------------
    if ( _tcl_action( r, ACTION_ENCHANGE_VTREF ) )
    {
        genericsReport( V_INFO, "VTRef %s : ", r->options->TRefEN ? "On" : "Off" );

        if ( OrbtraceIfSetVoltageEn( r->dev, CH_VTREF, r->options->TRefEN ) )
        {
            genericsReport( V_INFO, "OK" EOL );
        }
        else
        {
            genericsReport( V_INFO, "Failed" EOL );
        }
    }

    // -----------------------------------------------------------------------------------
    if ( _tcl_action( r, ACTION_ENCHANGE_ALL ) )
    {
        genericsReport( V_INFO, "All Channels %s : ", r->options->TRefEN ? "On" : "Off" );

        if ( OrbtraceIfSetVoltageEn( r->dev, CH_ALL, r->options->TRefEN ) )
        {
            genericsReport( V_INFO, "OK" EOL );
        }
        else
        {
            genericsReport( V_INFO, "Failed" EOL );
        }
    }

    // -----------------------------------------------------------------------------------
    if ( _tcl_action( r, ACTION_ENCHANGE_VTPWR ) )
    {
        genericsReport( V_INFO, "VTPwr %s : ", r->options->TPwrEN ? "On" : "Off" );

        if ( OrbtraceIfSetVoltageEn( r->dev, CH_VTPWR, r->options->TPwrEN ) )
        {
            genericsReport( V_INFO, "OK" EOL );
        }
        else
        {
            genericsReport( V_INFO, "Failed" EOL );
        }
    }

    // -----------------------------------------------------------------------------------
    if ( _tst_action( r, ACTION_BRIGHTNESS ) )
    {
    }

    // -----------------------------------------------------------------------------------
    if ( _tst_action( r, ACTION_SN ) )
    {
    }

    // -----------------------------------------------------------------------------------
    if ( _tst_action( r, ACTION_READ_PARAMS ) )
    {
    }

    // -----------------------------------------------------------------------------------
    if ( _tst_action( r, ACTION_SETNICK ) )
    {
    }

    // -----------------------------------------------------------------------------------
    if ( _tcl_action( r, ACTION_SET_TRACE ) )
    {
        if ( r->options->traceWidth )
        {
            genericsReport( V_INFO, "Setting port width to %d" EOL, r->options->traceWidth );

            if ( OrbtraceIfSetTraceWidth( r->dev, r->options->traceWidth ) )
            {
                genericsReport( V_INFO, "OK" EOL );
            }
            else
            {
                genericsReport( V_INFO, "Failed" EOL );
            }
        }
        else if ( ( r->options->swoMANCH ) ||  ( r->options->swoUART ) )
        {
            genericsReport( V_INFO, "Setting SWO with %s encoding" EOL, r->options->swoMANCH ? "Manchester" : "UART" );

            if ( OrbtraceIfSetTraceSWO( r->dev, r->options->swoMANCH ) )
            {
                genericsReport( V_INFO, "OK" EOL );
            }
            else
            {
                genericsReport( V_INFO, "Failed" EOL );
            }
        }
    }

    // -----------------------------------------------------------------------------------
    if ( _tst_action( r, ACTION_WRITE_PARAMS ) )
    {
    }

    // -----------------------------------------------------------------------------------
    if ( _tst_action( r, ACTION_LOCKDEVICE ) )
    {
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
        int seldevice = _selectDevice( &_r, _tcl_action ( &_r, ACTION_LIST_DEVICES ) );

        if ( _num_actions( &_r ) )
        {
            genericsReport( V_INFO, "Got device [%s %s, S/N %s]" EOL,
                            OrbtraceIfGetManufacturer( _r.dev, selection ),
                            OrbtraceIfGetProduct( _r.dev, selection ),
                            OrbtraceIfGetSN( _r.dev, selection ) );

            if ( !OrbtraceIfOpenDevice( _r.dev, seldevice ) )
            {
                genericsExit( -1, "Couldn't open device" EOL );
            }

            /* Check voltages now we know what interface we're connected to */
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
