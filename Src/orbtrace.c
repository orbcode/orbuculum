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
#include <getopt.h>

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
    bool useTPIU;             /* Decode TPIU on SWO */

    bool opJSON;              /* Set output to JSON */
    bool mono;                /* Supress colour in output */
    uint32_t serial_speed;    /* Speed of serial communication via SWO */

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
               ACTION_SERIAL_SPEED,
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
static void _printHelp( const char *const progName )

{
    genericsPrintf( "Usage: %s [options]" EOL, progName );
    genericsPrintf( "       -a, --serial-speed:  <serialSpeed> to use (when SWO UART is selected)" EOL );
    genericsPrintf( "       -e, --power:         <Ch>,<On> Enable or Disable power. Ch is vtref, vtpwr or all" EOL );
    genericsPrintf( "       -h, --help::         This help" EOL );
    genericsPrintf( "       -l, --list:          Show all OrbTrace devices attached to system" EOL );
    genericsPrintf( "       -M, --no-colour:    Supress colour in output" EOL );
    genericsPrintf( "       -T, --trace-format:  <x> Trace format; 1,2 or 4 bit parallel, m for Manchester SWO, u=UART SWO," EOL );
    genericsPrintf( "                                              M for Manchester SWO with TPIU decode, U=UART SWO with TPIU decode" EOL );
    genericsPrintf( "       -n, --serial-number: <Serial> any part of serial number to differentiate specific OrbTrace device" EOL );
    genericsPrintf( "       -p, --voltage:       <Ch>,<Voltage> Set voltage in V, Ch is vtref or vtpwr" EOL );
    genericsPrintf( "       -v, --verbose:       <level> Verbose mode 0(errors)..3(debug)" EOL );
    genericsPrintf( "       -V, --version:       Print version and exit" EOL );

    //    genericsPrintf( "      *-b: <Brightness> Set default brightness of output leds" EOL );
    //    genericsPrintf( "      *-F: Force voltage setting" EOL );
    //    genericsPrintf( "      *-j: Format output in JSON" EOL );
    //    genericsPrintf( "      *-L: Lock device (prevent further changes)" EOL );
    //    genericsPrintf( "      *-N: <Nick> Specify nickname for adaptor (8 chars max)" EOL );
    //    genericsPrintf( "      *-q: Query all data from connected device" EOL );
    //    genericsPrintf( "      *-Q: Query specified data from connected device (pPrR VPwr/IPwr/VRef/IRef)" EOL );
    //    genericsPrintf( "      *-U: Unlock device (allow changes, default state)" EOL );
    //    genericsPrintf( "      *-w: Write parameters specified on command line to NVRAM" EOL );
    //    genericsPrintf( "      *-W: Reset all NVRAM parameters to default values" EOL );
}
// ====================================================================================================
void _printVersion( void )

{
    genericsPrintf( "Orbtrace version " GIT_DESCRIBE );
}
// ====================================================================================================
static struct option _longOptions[] =
{
    {"serial-speed", required_argument, NULL, 'a'},
    {"power", required_argument, NULL, 'e'},
    {"help", no_argument, NULL, 'h'},
    {"list", no_argument, NULL, 'l'},
    {"monitor", required_argument, NULL, 'm'},
    {"no-colour", no_argument, NULL, 'M'},
    {"no-color", no_argument, NULL, 'M'},
    {"trace-format", required_argument, NULL, 'T'},
    {"serial-number", required_argument, NULL, 'n'},
    {"voltage", required_argument, NULL, 'p'},
    {"verbose", required_argument, NULL, 'v'},
    {"version", no_argument, NULL, 'V'},
    {NULL, no_argument, NULL, 0}
};
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
    int optionIndex = 0;
    float voltage;
    int channel;
    bool action;
    char *a;

    while ( ( c = getopt_long ( argc, argv, "a:e:hlp:Mn:T:v:V", _longOptions, &optionIndex ) ) != -1 )
        switch ( c )
        {
            // ------------------------------------
            case 'a': /* Serial Speed */
                r->options->serial_speed = atoi( optarg );
                _set_action( r, ACTION_SERIAL_SPEED );
                break;

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
            case 'M':
                r->options->mono = true;
                break;

            // ------------------------------------
            case 'N': /* Set nickname */
                r->options->nick = optarg;
                _set_action( r, ACTION_SETNICK );
                break;

            // ------------------------------------
            case 'T': /* Set tracewidth */
                r->options->traceWidth = 0;

                if ( strlen( optarg ) != 1 )
                {
                    *optarg = 0;
                }

                switch ( *optarg )
                {
                    case 'u':
                        r->options->swoUART = true;
                        break;

                    case 'm':
                        r->options->swoMANCH = true;
			break;

                    case 'U':
                        r->options->swoUART = true;
                        r->options->useTPIU = true;
                        break;

                    case 'M':
                        r->options->swoMANCH = true;
                        r->options->useTPIU = true;
                        break;

                    case '1':
                    case '2':
                    case '3':
                    case '4':
                        r->options->traceWidth = atoi( optarg );
                        break;

                    default:
                        genericsReport( V_ERROR, "Badly formatted tracewidth" EOL );
                        return false;
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

            case 'n': /* Set serial number */
                r->options->sn = optarg;
                _set_action( r, ACTION_SN );
                break;

            // ------------------------------------
            case 'U': /* Unlock device */
                _set_action( r, ACTION_UNLOCK );
                break;

            // ------------------------------------
            case 'v':
                if ( !isdigit( *optarg ) )
                {
                    genericsReport( V_ERROR, "-v requires a numeric argument." EOL );
                    return false;
                }

                genericsSetReportLevel( atoi( optarg ) );
                break;

            // ------------------------------------
            case 'V':
                /* Print the version of this utility, and schedule to get the version of the probes */
                _printVersion();
                genericsPrintf( EOL "Attached Probe(s);" EOL );
                _set_action( r, ACTION_LIST_DEVICES );
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

    if ( ( ( r->options->serial_speed ) && ( !r->options->swoUART ) ) &&
            ( ( !r->options->serial_speed ) && ( r->options->swoUART ) ) )
    {
        genericsReport( V_ERROR, "For SWO/UART both baudrate and mode need to be set" EOL );
        return false;
    }

    if ( ( _tst_action( r, ACTION_SET_TRACE ) ) &&
            ( ( ( r->options->traceWidth ) && ( ( r->options->swoUART ) || ( r->options->swoMANCH ) ) ) ||
              ( ( r->options->swoUART ) && ( r->options->swoMANCH ) ) ) )
    {
        genericsReport( V_ERROR, "Only one trace configuration can be set at the same time" EOL );
        return false;
    }

    if ( _tst_action( r, ACTION_LIST_DEVICES ) && ( ( _num_actions( r ) > 1 ) ) )
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
    genericsReport( V_INFO, "orbtrace version " GIT_DESCRIBE EOL );
    return true;
}
// ====================================================================================================
static void _doExit( void )

{
    _r.ending = true;
}

// ====================================================================================================

// ====================================================================================================
static int _performActions( struct RunTime *r )

/* Whatever we were asked to do, do it */

{
    int retVal = 0;

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
            genericsReport( V_ERROR, "Setting VTRef failed" EOL );
            retVal |= -1;
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
            genericsReport( V_ERROR, "Setting VTPwr failed" EOL );
            retVal |= -1;
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
            genericsReport( V_ERROR, "Changing VTRef state failed" EOL );
            retVal |= -1;
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
            genericsReport( V_ERROR, "Changing all power channel states failed" EOL );
            retVal |= -1;
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
            genericsReport( V_ERROR, "Changing VTPwr state failed" EOL );
            retVal |= -1;
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
    if ( _tcl_action( r, ACTION_SERIAL_SPEED ) )
    {
        genericsReport( V_INFO, "Setting baudrate to %d bps" EOL, r->options->serial_speed );

        if ( OrbtraceIfSetSWOBaudrate( r->dev, r->options->serial_speed ) )
        {
            genericsReport( V_INFO, "OK" EOL );
        }
        else
        {
            genericsReport( V_ERROR, "Setting serial speed failed" EOL );
            retVal |= -1;
        }
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
                genericsReport( V_ERROR, "Setting port width failed" EOL );
                retVal |= -1;
            }
        }
        else if ( ( r->options->swoMANCH ) ||  ( r->options->swoUART ) )
        {
            genericsReport( V_INFO, "Setting SWO with %s encoding%s" EOL, r->options->swoMANCH ? "Manchester" : "UART", r->options->useTPIU ? " and TPIU decode" : "" );

            if ( OrbtraceIfSetTraceSWO( r->dev, r->options->swoMANCH, r->options->useTPIU ) )
            {
                genericsReport( V_INFO, "OK" EOL );
            }
            else
            {
                genericsReport( V_ERROR, "Setting SWO encoding failed" EOL );
                retVal |= -1;
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

    return retVal;
}
// ====================================================================================================
int main( int argc, char *argv[] )

{
    int selection = 0;
    int retVal = 0;

    if ( !_processOptions( &_r, argc, argv ) )
    {
        /* processOptions generates its own error messages */
        genericsExit( -1, "" EOL );
    }

    genericsScreenHandling( !_r.options->mono );

    /* Make sure everything gets removed at the end */
    atexit( _doExit );

    /* This ensures the atexit gets called */
    if ( SIG_ERR == signal( SIGINT, _intHandler ) )
    {
        genericsExit( -1, "Failed to establish Int handler" EOL );
    }

    _r.dev = OrbtraceIfCreateContext();

    assert( _r.dev );

    _r.ndevices = OrbtraceIfGetDeviceList( _r.dev, _r.options->sn, DEVTYPE( DEVICE_ORBTRACE_MINI ) );

    if ( !_r.ndevices )
    {
        genericsReport( V_ERROR, "No devices found" EOL );
    }
    else
    {
        /* Allow option to choose between devices if there's more than one found */
        if ( _tcl_action ( &_r, ACTION_LIST_DEVICES ) )
        {
            OrbtraceIfListDevices( _r.dev );
        }
        else
        {
            selection = OrbtraceIfSelectDevice( _r.dev );

            if ( _num_actions( &_r ) )
            {
                genericsReport( V_INFO, "Got device [%s %s, S/N %s]" EOL,
                                OrbtraceIfGetManufacturer( _r.dev, selection ),
                                OrbtraceIfGetProduct( _r.dev, selection ),
                                OrbtraceIfGetSN( _r.dev, selection ) );

                if ( !OrbtraceIfOpenDevice( _r.dev, selection ) )
                {
                    genericsExit( -1, "Couldn't open device" EOL );
                }

                /* Check voltages now we know what interface we're connected to */
                if ( !_checkVoltages( &_r ) )
                {
                    genericsExit( -2, "Specified interface voltage check failed" EOL );
                }

                retVal = _performActions( &_r );
            }

            OrbtraceIfCloseDevice( _r.dev );
        }
    }

    return retVal;
}
// ====================================================================================================
