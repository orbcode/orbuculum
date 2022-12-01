/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * OrbLCD remote display
 * =====================
 *
 * Note that this file can be used as a generic skeleton for an ITM processing application. The app-specific code
 * is clearly labelled in sections ***** APPLICATION SPECIFIC *****...the other stuff should be pretty much
 * boiler-place for any ITM processing app. There will obviously be some changes needed (e.g. options setting)
 * but they should be pretty minimal and simple to spot.
 *
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <getopt.h>
#include <ctype.h>
#include <unistd.h>
#include <SDL.h>


#include "git_version_info.h"
#include "generics.h"
#include "tpiuDecoder.h"
#include "itmDecoder.h"
#include "msgDecoder.h"
#include "stream.h"
#include "nw.h"
#include "orblcd_protocol.h"

/************** APPLICATION SPECIFIC ********************************************************************/
/* Target application specifics */
struct TApp
{
    /* Application specific Options */
    int chan;                                             /* The channel we are listening on */
    int sbcolour;                                         /* Colour to be used for single bit renders */

    /* Operational stuff */
    int depth;                                            /* Bitdepth of the source */
    int x;                                                /* Current X pos */
    int y;                                                /* Current Y pos */
    float scale;                                          /* Scale for output window */
    bool fresh;                                           /* Is this a clear screen I only need to paint new data onto? */
    int modeDescriptor;
    int h;
    int w;

    /* SDL stuff */
    SDL_Window   *mainWindow;
    SDL_Renderer *renderer;
    SDL_Surface  *screen;                                 /* The output screen being rendered onto */
    SDL_Surface  *content;                                /* The content being rendered */

} _app =
{
    .chan     = LCD_DATA_CHANNEL,
    .sbcolour = 0x00ff00,
    .scale    = 1.5f

};
/************** APPLICATION SPECIFIC ENDS ***************************************************************/

/* Record for options, either defaults or from command line */
struct Options
{
    /* Source information */
    int  port;                                           /* Source port, or zero if no port set */
    char *server;                                        /* Source server */
    char *file;                                          /* File host connection */
    bool fileTerminate;                                  /* Terminate when file read isn't successful */

    /* Demux information */
    int tpiuChannel;                                     /* TPIU channel to be used (for case TPIU present, 0 otherwise) */
    bool forceITMSync;                                   /* Do we need ITM syncs? */

} _options = {.forceITMSync = true, .port = NWCLIENT_SERVER_PORT, .server = "localhost"};

struct RunTime
{
    /* The decoders and the packets from them */
    struct ITMDecoder  i;
    struct ITMPacket   h;
    struct TPIUDecoder t;
    struct TPIUPacket  p;

    bool      ending;                                    /* Flag indicating app is terminating */
    bool      errored;                                   /* Flag indicating problem in reception process */

    int f;                                               /* File handle to data source */
    struct Options *options;                             /* Command line options (reference to above) */
    struct TApp    *app;                                 /* Data storage for target application */

} _r =
{
    .options  = &_options,
    .app      = &_app,
};

/************** APPLICATION SPECIFIC ******************************************************************/
// Target application specifics
// ====================================================================================================
static void _paintPixels( struct swMsg *m, struct RunTime *r )

{
    /* This is LCD data */
    int d = m->value;

    if ( r->app->depth == ORBLCD_DEPTH_1 )
    {
        /* 32 bit word representing 32 mono output bits */
        for ( int w = 0; w < 4; w++ )
        {
            for ( int b = 7; b >= 0; b-- )
            {
                if ( r->app->fresh )
                {
                    /* This is a fresh screen, so only need to write the 1's */
                    if ( d & ( 1 << b ) )
                    {
                        SDL_RenderDrawPoint( r->app->renderer, r->app->x, r->app->y );
                    }
                }
                else
                {
                    if ( d & ( 1 << b ) )
                    {
                        SDL_SetRenderDrawColor( r->app->renderer,
                                                r->app->sbcolour & 0xff,
                                                ( r->app->sbcolour >> 8 ) & 0xff,
                                                ( r->app->sbcolour >> 16 ) & 0xff,
                                                0xff - ( ( r->app->sbcolour >> 24 ) & 0xff ) );
                    }
                    else
                    {
                        SDL_SetRenderDrawColor( r->app->renderer, 0, 0, 0, 0xff );
                    }

                    SDL_RenderDrawPoint( r->app->renderer, r->app->x, r->app->y );
                }

                if ( ++r->app->x >= r->app->w )
                {
                    r->app->y++;
                    r->app->x = 0;

                    if ( r->app->y == r->app->h )
                    {
                        r->app->y = 0;
                    }

                    /* Anything left over in a word at the end of a line is thrown away */
                    w = 4;
                    break;
                }
            }

            d >>= 8;
        }
    }
    else
    {
        for ( int b = ORBLCD_BITS_PER_WORD( r->app->depth ); b >= 0; b-- )
        {
            switch ( r->app->depth )
            {
                case ORBLCD_DEPTH_16:
                    SDL_SetRenderDrawColor( r->app->renderer, d & 0x1f, ( d >> 5 ) & 0x3f, ( d >> 11 ) & 0x1f, 0xff );
                    break;

                case ORBLCD_DEPTH_32:
                case ORBLCD_DEPTH_24:
                    SDL_SetRenderDrawColor( r->app->renderer, d & 0xff, ( d >> 8 ) & 0xff, ( d >> 16 ) & 0xff, ( r->app->depth == ORBLCD_DEPTH_24 ) ? 0xff : ( b >> 24 ) & 0xff );
                    break;

                default:
                    break;
            }

            SDL_RenderDrawPoint( r->app->renderer, r->app->x, r->app->y );

            if ( ++r->app->x >= r->app->w )
            {
                r->app->y++;
                r->app->x = 0;

                if ( r->app->y == r->app->h )
                {
                    r->app->y = 0;
                }

                break;
            }

            d >>= 16;
        }
    }
}

/*************************************/

static void _handleCommand( struct swMsg *m, struct RunTime *r )

{
    /* This is control data */
    switch ( ORBLCD_DECODE_C( m->value ) )
    {
        case ORBLCD_CMD_INIT_LCD: // -------------------------------------------------------
            if ( ( !r->app->mainWindow ) || ( m->value != r->app->modeDescriptor ) )
            {
                /* Create a new, or replacement, SDL window */
                genericsReport( V_ERROR, "%s window %dx%d, depth %d" EOL,
                                ( r->app->modeDescriptor ) ? "Replacement" : "New",
                                ORBLCD_DECODE_X( m->value ), ORBLCD_DECODE_Y( m->value ), ORBLCD_GET_DEPTH( m->value ) );
                r->app->modeDescriptor = m->value;
                r->app->depth          = ORBLCD_DECODE_D( m->value );
                r->app->h              = ORBLCD_DECODE_Y( m->value );
                r->app->w              = ORBLCD_DECODE_X( m->value );

                /* If this is due to a resize activity then destroy the old stuff */
                if ( r->app->renderer )
                {
                    SDL_DestroyRenderer( r->app->renderer );
                }

                if ( r->app->mainWindow )
                {
                    SDL_DestroyWindow( r->app->mainWindow );
                }

                r->app->mainWindow    = SDL_CreateWindow( "ORBLcd Output Window",
                                        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                        r->app->w * r->app->scale, r->app->h * r->app->scale, SDL_WINDOW_SHOWN );
                r->app->renderer      = SDL_CreateRenderer( r->app->mainWindow, -1, SDL_RENDERER_ACCELERATED );
                SDL_RenderSetLogicalSize( r->app->renderer, r->app->w, r->app->h );
                SDL_SetRenderDrawColor( r->app->renderer, 0, 0, 0, 0 );
                SDL_RenderClear( r->app->renderer );
            }
            else
            {
                /* Repaint the SDL window */
                SDL_RenderPresent( r->app->renderer );
                r->app->fresh = false;
            }

            r->app->x = r->app->y = 0;
            break;

        case ORBLCD_CMD_CLEAR: // -------------------------------------------------------------
            SDL_SetRenderDrawColor( r->app->renderer, 0, 0, 0, 0 );
            SDL_RenderClear( r->app->renderer );
            SDL_SetRenderDrawColor( r->app->renderer,
                                    r->app->sbcolour & 0xff,
                                    ( r->app->sbcolour >> 8 ) & 0xff,
                                    ( r->app->sbcolour >> 16 ) & 0xff,
                                    0xff - ( ( r->app->sbcolour >> 24 ) & 0xff ) );
            r->app->fresh = true;
            break;

        case ORBLCD_CMD_GOTOXY: // ------------------------------------------------------------
            if ( ORBLCD_DECODE_X( m->value ) < r->app->w )
            {
                r->app->x = ORBLCD_DECODE_X( m->value );
            }

            if ( ORBLCD_DECODE_Y( m->value ) < r->app->h )
            {
                r->app->y = ORBLCD_DECODE_Y( m->value );
            }

            break;

        default:  // --------------------------------------------------------------------------------------------
            genericsReport( V_INFO, "Unknown LCD protocol message %d,length %d" EOL, ORBLCD_DECODE_C( m->value ), m->len );
            break;
    }
}

/*************************************/

static void _handleSW( struct swMsg *m, struct RunTime *r )

{
    if ( ( m->srcAddr == r->app->chan ) && ( r->app->mainWindow ) )
    {
        _paintPixels( m, r );
    }
    else if ( m->srcAddr == r->app->chan + 1 )
    {
        _handleCommand( m, r );
    }
}

/************** APPLICATION SPECIFIC ENDS ***************************************************************/


// ====================================================================================================
// Generic Stream processing to extract data from incoming stream
// ====================================================================================================

void _itmPumpProcess( char c, struct RunTime *r )

{
    struct msg decoded;

    switch ( ITMPump( &r->i, c ) )
    {
        case ITM_EV_NONE:
            break;

        case ITM_EV_UNSYNCED:
            genericsReport( V_INFO, "ITM Unsynced" EOL );
            break;

        case ITM_EV_SYNCED:
            genericsReport( V_INFO, "ITM Synced" EOL );
            break;

        case ITM_EV_OVERFLOW:
            genericsReport( V_INFO, "ITM Overflow" EOL );
            break;

        case ITM_EV_ERROR:
            genericsReport( V_WARN, "ITM Error" EOL );
            break;

        case ITM_EV_PACKET_RXED:
            ITMGetDecodedPacket( &_r.i, &decoded );

            /* See if we decoded a dispatchable match. genericMsg is just used to access */
            /* the first two members of the decoded structs in a portable way.           */
            if ( decoded.genericMsg.msgtype == MSG_SOFTWARE )
            {
                _handleSW( ( struct swMsg * )&decoded, r );
            }

            break;

        default:
            break;
    }
}
// ====================================================================================================

static void _TPIUpacketRxed( enum TPIUPumpEvent e, struct TPIUPacket *p, void *param )

/* Callback for when a TPIU frame has been assembled */

{
    struct RunTime *r = ( struct RunTime * )param;

    switch ( e )
    {
        case TPIU_EV_RXEDPACKET:
            for ( uint32_t g = 0; g < p->len; g++ )
            {
                if ( 1 == p->packet[g].s )
                {
                    _itmPumpProcess( p->packet[g].d, r );
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
// ====================================================================================================

static struct Stream *_tryOpenStream( struct RunTime *r )
{
    if ( r->options->file != NULL )
    {
        return streamCreateFile( r->options->file );
    }
    else
    {
        return streamCreateSocket( r->options->server, r->options->port );
    }
}
// ====================================================================================================

static bool _feedStream( struct Stream *stream, struct RunTime *r )
{
    unsigned char cbw[TRANSFER_SIZE];
    struct timeval t =
    {
        .tv_sec = 0,
        .tv_usec = 100000
    };
    SDL_Event e;

    while ( true )
    {
        size_t receivedSize;
        enum ReceiveResult result = stream->receive( stream, cbw, TRANSFER_SIZE, &t, &receivedSize );

        /* Check for SDL close */
        if ( SDL_PollEvent( &e ) != 0 )
        {
            if ( e.type == SDL_QUIT )
            {
                return false;
            }
        }


        if ( result != RECEIVE_RESULT_OK )
        {
            if ( result == RECEIVE_RESULT_EOF && r->options->fileTerminate )
            {
                return true;
            }
            else if ( result == RECEIVE_RESULT_ERROR )
            {
                break;
            }
            else
            {
                usleep( 100000 );
            }
        }

        unsigned char *c = cbw;

        if ( _r.options->tpiuChannel )
        {
            TPIUPump2( &r->t, cbw, receivedSize, _TPIUpacketRxed, r );
        }
        else
        {
            while ( receivedSize-- )
            {
                _itmPumpProcess( *c++, r );
            }
        }
    }

    return true;
}

// ====================================================================================================
// Application Setup stuff
// ====================================================================================================
void _printHelp( const char *const progName )

{
    genericsPrintf( "Usage: %s [options]" EOL, progName );
    genericsPrintf( "    -c, --channel:      <Number> of first channel in pair containing display data" EOL );
    genericsPrintf( "    -f, --input-file:   <filename> Take input from specified file" EOL );
    genericsPrintf( "    -h, --help:         This help" EOL );
    genericsPrintf( "    -n, --itm-sync:     Enforce sync requirement for ITM (i.e. ITM needsd to issue syncs)" EOL );
    genericsPrintf( "    -s, --server:       <Server>:<Port> to use" EOL );
    genericsPrintf( "    -S, --sbcolour:     <Colour> to be used for single bit renders, ignored for other bit depths" EOL );
    genericsPrintf( "    -t, --tpiu:         <channel>: Use TPIU decoder on specified channel (normally 1)" EOL );
    genericsPrintf( "    -v, --verbose:      <level> Verbose mode 0(errors)..3(debug)" EOL );
    genericsPrintf( "    -V, --version:      Print version and exit" EOL );
    genericsPrintf( "    -z, --size:         <Scale(float)> Set relative size of output window (normally 1)" EOL );
}

// ====================================================================================================
void _printVersion( void )

{
    genericsPrintf( "orblcd version " GIT_DESCRIBE EOL );
}
// ====================================================================================================
static struct option _longOptions[] =
{
    {"channel", required_argument, NULL, 'c'},
    {"eof", no_argument, NULL, 'E'},
    {"help", no_argument, NULL, 'h'},
    {"input-file", required_argument, NULL, 'f'},
    {"itm-sync", no_argument, NULL, 'n'},
    {"server", required_argument, NULL, 's'},
    {"sbcolour", required_argument, NULL, 'S'},
    {"sbcolor", required_argument, NULL, 'S'},
    {"tpiu", required_argument, NULL, 't'},
    {"verbose", required_argument, NULL, 'v'},
    {"version", no_argument, NULL, 'V'},
    {"size", required_argument, NULL, 'z'},
    {NULL, no_argument, NULL, 0}
};
// ====================================================================================================
bool _processOptions( int argc, char *argv[], struct RunTime *r )

{
    int c, optionIndex = 0;

    while ( ( c = getopt_long ( argc, argv, "c:Ef:hns:S:t:v:Vz:", _longOptions, &optionIndex ) ) != -1 )
        switch ( c )
        {
            // ------------------------------------
            case 'c':
                r->app->chan = atoi( optarg );
                break;

            // ------------------------------------
            case 'E':
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
            case 'n':
                r->options->forceITMSync = false;
                break;

            // ------------------------------------
            case 'V':
                _printVersion();
                return false;

            // ------------------------------------
            case 's':
                r->options->server = optarg;

                // See if we have an optional port number too
                char *a = optarg;

                while ( ( *a ) && ( *a != ':' ) )
                {
                    a++;
                }

                if ( *a == ':' )
                {
                    *a = 0;
                    r->options->port = atoi( ++a );
                }

                if ( !r->options->port )
                {
                    r->options->port = NWCLIENT_SERVER_PORT;
                }

                break;

            // ------------------------------------
            case 'S':
                r->app->sbcolour = strtol( optarg, NULL, 0 );
                break;

            // ------------------------------------
            case 't':
                r->options->tpiuChannel = atoi( optarg );
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
            case 'z':
                r->app->scale = atof( optarg );
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
    _printVersion();

    genericsReport( V_INFO, "App Channel    : Data=%d, Control=%d" EOL, r->app->chan, r->app->chan + 1 );
    genericsReport( V_INFO, "SB Colour      : 0x%x" EOL, r->app->sbcolour );
    genericsReport( V_INFO, "Relative Scale : %1.2f:1" EOL, r->app->scale );

    if ( r->options->port )
    {
        genericsReport( V_INFO, "NW SERVER H&P  : %s:%d" EOL, r->options->server, r->options->port );
    }

    if ( r->options->tpiuChannel )
    {
        genericsReport( V_INFO, "Use/Strip TPIU : True, channel %d" EOL, r->options->tpiuChannel );
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

    if ( ( r->options->file ) && ( r->options->port ) )
    {
        genericsReport( V_ERROR, "Cannot specify file and port or NW Server at same time" EOL );
        return false;
    }

    return true;
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Externally available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
int main( int argc, char *argv[] )

{
    bool alreadyReported = false;

    if ( !_processOptions( argc, argv, &_r ) )
    {
        exit( -1 );
    }

    /* Reset the TPIU handler before we start */
    TPIUDecoderInit( &_r.t );
    ITMDecoderInit( &_r.i, _r.options->forceITMSync );

    if ( SDL_Init( SDL_INIT_VIDEO ) < 0 )
    {
        genericsExit( -1, "Could not initailise SDL" );
    }

    while ( true )
    {
        struct Stream *stream = NULL;

        while ( true )
        {
            stream = _tryOpenStream( &_r );

            if ( stream != NULL )
            {
                if ( alreadyReported )
                {
                    genericsReport( V_INFO, "Connected" EOL );
                    alreadyReported = false;
                }

                break;
            }

            if ( !alreadyReported )
            {
                genericsReport( V_INFO, EOL "No connection" EOL );
                alreadyReported = true;
            }

            if ( _r.options->fileTerminate )
            {
                break;
            }

            /* Checking every 100ms for a connection is quite often enough */
            usleep( 10000 );
        }

        if ( stream != NULL )
        {
            if ( !_feedStream( stream, &_r ) )
            {
                break;
            }
        }

        stream->close( stream );
        free( stream );

        if ( _r.options->fileTerminate )
        {
            break;
        }
    }

    SDL_Quit();
}
// ====================================================================================================
