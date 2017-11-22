/*
 * SWO Top for Blackmagic Probe and TTL Serial Interfaces
 * ======================================================
 *
 * Copyright (C) 2017  Dave Marples  <dave@marples.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * This code uses the bdf library. It originally used libdwarf, but that
 * was painful. Unfortunately bdf isn't well documented so you've got to
 * go through the binutils source to find your way around it.  The bdf
 * dependencies are well partitioned into the _lookup routine and it's
 * supporting cast.
 *
 */

#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <elf.h>
#include <bfd.h>
#if defined OSX
    #include <libusb.h>
#else
    #if defined LINUX
        #include <libusb-1.0/libusb.h>
    #else
        #error "Unknown OS"
    #endif
#endif
#include <stdint.h>
#include <limits.h>
#include <termios.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "generics.h"
#include "uthash.h"
#include "git_version_info.h"
#include "generics.h"
#include "tpiuDecoder.h"
#include "itmDecoder.h"
#include "symbols.h"

#define CUTOFF              (10)             /* Default cutoff at 0.1% */
#define SERVER_PORT         (3443)           /* Server port definition */
#define TRANSFER_SIZE       (4096)           /* Maximum packet we might receive */
#define TOP_UPDATE_INTERVAL (1000LL)         /* Interval between each on screen update */


struct visitedAddr                           /* Structure for Hashmap of visited/observed addresses */
{
    uint64_t visits;
    struct nameEntry *n;

    UT_hash_handle hh;
};

struct reportLine

{
    uint64_t count;
    struct nameEntry *n;
};


/* ---------- CONFIGURATION ----------------- */
struct                                       /* Record for options, either defaults or from command line */
{
    bool verbose;                            /* Talk more.... */
    bool useTPIU;                            /* Are we decoding via the TPIU? */
    uint32_t tpiuITMChannel;                 /* What channel? */
    bool forceITMSync;                       /* Must ITM start synced? */

    uint32_t hwOutputs;                      /* What hardware outputs are enabled */

    char *deleteMaterial;                    /* Material to delete off filenames for target */

    char *elffile;                           /* Target program config */

    char *outfile;                           /* File to output historic information */

    uint32_t cutscreen;                      /* Cut screen output after specified number of lines */
    uint32_t maxRoutines;                    /* Historic information to emit */
    uint32_t maxHistory;
    bool lineDisaggregation;                 /* Aggregate per line or per function? */

    int port;                                /* Source information */
    char *server;

} options =
{
    .forceITMSync = true,
    .useTPIU = false,
    .tpiuITMChannel = 1,
    .outfile = NULL,
    .lineDisaggregation = false,
    .maxRoutines = 8,
    .maxHistory = 30,
    .port = SERVER_PORT,
    .server = "localhost"
};

/* ----------- LIVE STATE ----------------- */
struct
{
    struct ITMDecoder i;                    /* The decoders and the packets from them */
    struct ITMPacket h;
    struct TPIUDecoder t;
    struct TPIUPacket p;

    struct SymbolSet *s;                    /* Symbols read from elf */
    struct nameEntry *n;                    /* Current table of recognised names */

    struct visitedAddr *addresses;         /* Addresses we received in the SWV */

    uint32_t interrupts;
    uint32_t sleeps;
    uint32_t notFound;
} _r;

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Internally available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================



// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Handler for individual message types from SWO
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
void _handleException( struct ITMDecoder *i, struct ITMPacket *p )

{

}
// ====================================================================================================
void _handleDWTEvent( struct ITMDecoder *i, struct ITMPacket *p )

{

}
// ====================================================================================================
void _handleSW( struct ITMDecoder *i )

{

}
// ====================================================================================================
uint64_t _timestamp( void )

{
    struct timeval te;
    gettimeofday( &te, NULL ); // get current time
    uint64_t milliseconds = te.tv_sec * 1000LL + te.tv_usec / 1000; // caculate milliseconds
    return milliseconds;
}
// ====================================================================================================
int _addresses_sort_fn( void *a, void *b )

{
    if ( ( ( ( struct visitedAddr * )a )->n->addr ) < ( ( ( struct visitedAddr * )b )->n->addr ) )
    {
        return -1;
    }

    if ( ( ( ( struct visitedAddr * )a )->n->addr ) > ( ( ( struct visitedAddr * )b )->n->addr ) )
    {
        return 1;
    }

    return 0;
}
// ====================================================================================================
int _report_sort_fn( const void *a, const void *b )

{
    return ( ( struct reportLine * )b )->count - ( ( struct reportLine * )a )->count;
}
// ====================================================================================================
void outputTop( void )

/* Produce the output */

{
    struct nameEntry *n;
    struct visitedAddr *a;


    uint32_t reportLines = 0;
    struct reportLine *report = NULL;

    uint32_t total = 0;
    uint64_t samples = 0;
    uint64_t dispSamples = 0;
    uint32_t percentage;
    uint32_t totPercent = 0;

    FILE *p = NULL;

    /* Put the address into order of the file and function names */
    HASH_SORT( _r.addresses, _addresses_sort_fn );

    /* Now merge them together */
    for ( a = _r.addresses; a != NULL; a = a->hh.next )
    {
        if ( !a->visits )
        {
            continue;
        }

        if ( ( reportLines == 0 ) ||
                ( strcmp( report[reportLines - 1].n->filename, a->n->filename ) ) ||
                ( strcmp( report[reportLines - 1].n->function, a->n->function ) ) ||
                ( ( report[reportLines - 1].n->line != a->n->line ) && ( options.lineDisaggregation ) ) )
        {
            /* Make room for a report line */
            reportLines++;
            report = ( struct reportLine * )realloc( report, sizeof( struct reportLine ) * ( reportLines ) );
            report[reportLines - 1].n = a->n;
            report[reportLines - 1].count = 0;
        }

        report[reportLines - 1].count += a->visits;
        total += a->visits;
        a->visits = 0;
    }


    /* Now fold in any sleeping entries */
    report = ( struct reportLine * )realloc( report, sizeof( struct reportLine ) * ( reportLines + 1 ) );

    uint32_t addr = SLEEPING;
    HASH_FIND_INT( _r.addresses, &addr, a );

    if ( a )
    {
        n = a->n;
    }
    else
    {
        n = ( struct nameEntry * )malloc( sizeof( struct nameEntry ) );
    }

    n->filename = "";
    n->function = "** SLEEPING **";
    n->addr = 0;
    n->line = 0;

    report[reportLines].n = n;
    report[reportLines].count = _r.sleeps;
    reportLines++;
    total += _r.sleeps;
    _r.sleeps = 0;

    /* Now put the whole thing into order of number of samples */
    qsort( report, reportLines, sizeof( struct reportLine ), _report_sort_fn );

    if ( options.outfile )
    {
        p = fopen( options.outfile, "w" );
    }

    fprintf( stdout, "\033[2J\033[;H" );

    if ( total )
    {
        for ( uint32_t n = 0; n < reportLines; n++ )
        {
            percentage = ( report[n].count * 10000 ) / total;
            samples += report[n].count;

            if ( report[n].count )
            {
                if ( ( percentage >= CUTOFF ) && ( ( !options.cutscreen ) || ( n < options.cutscreen ) ) )
                {
                    fprintf( stdout, "%3d.%02d%% %8ld ", percentage / 100, percentage % 100, report[n].count );

                    dispSamples += report[n].count;

                    if ( ( options.lineDisaggregation ) && ( report[n].n->line ) )
                    {
                        fprintf( stdout, "%s::%d" EOL, report[n].n->function, report[n].n->line );
                    }
                    else
                    {
                        fprintf( stdout, "%s" EOL, report[n].n->function );
                    }

                    totPercent += percentage;
                }

                if ( ( p )  && ( n < options.maxRoutines ) )
                {
                    if ( !options.lineDisaggregation )
                    {
                        fprintf( p, "%s,%3d.%02d" EOL, report[n].n->function, percentage / 100, percentage % 100 );
                    }
                    else
                    {
                        fprintf( p, "%s::%d,%3d.%02d" EOL, report[n].n->function, report[n].n->line, percentage / 100, percentage % 100 );
                    }
                }
            }
        }
    }

    fprintf( stdout, "-----------------" EOL );

    if ( samples == dispSamples )
    {
        fprintf( stdout, "%3d.%02d%% %8ld Samples" EOL, totPercent / 100, totPercent % 100, samples );
    }
    else
    {
        fprintf( stdout, "%3d.%02d%% %8ld of %ld Samples" EOL, totPercent / 100, totPercent % 100, dispSamples, samples );
    }

    if ( p )
    {
        fclose( p );
        p = NULL;
    }

    if ( options.verbose )
    {
        fprintf( stdout, "         Ovf=%3d  ITMSync=%3d TPIUSync=%3d ITMErrors=%3d" EOL,
                 ITMDecoderGetStats( &_r.i )->overflow,
                 ITMDecoderGetStats( &_r.i )->syncCount,
                 TPIUDecoderGetStats( &_r.t )->syncCount,
                 ITMDecoderGetStats( &_r.i )->ErrorPkt );
    }

    /* ... and we are done with the report now, get rid of it */
    free( report );
}
// ====================================================================================================
void _handlePCSample( struct ITMDecoder *i, struct ITMPacket *p )

{
    struct visitedAddr *a;
    uint32_t pc;

    if ( p->len == 1 )
    {
        /* This is a sleep packet */
        _r.sleeps++;
    }
    else
    {
        pc = ( p->d[3] << 24 ) | ( p->d[2] << 16 ) | ( p->d[1] << 8 ) | ( p->d[0] );

        HASH_FIND_INT( _r.addresses, &pc, a );

        if ( a )
        {
            a->visits++;
        }
        else
        {
            struct nameEntry n;

            /* Find a matching name record if there is one */
            SymbolLookup( _r.s, pc, &n, options.deleteMaterial );

            /* This is a new entry - record it */

            a = ( struct visitedAddr * )calloc( 1, sizeof( struct visitedAddr ) );
            a->visits = 1;

            a->n = ( struct nameEntry * )malloc( sizeof( struct nameEntry ) );
            memcpy( a->n, &n, sizeof( struct nameEntry ) );
            HASH_ADD_INT( _r.addresses, n->addr, a );
        }
    }
}
// ====================================================================================================
void _flushHash(void)

{
  struct visitedAddr *a;
  UT_hash_handle hh;
  
  for ( a = _r.addresses; a != NULL; a = hh.next )
    {
      hh=a->hh;
      free(a);
    }

  _r.addresses=NULL;
}
// ====================================================================================================
void _handleHW( struct ITMDecoder *i )

{
    struct ITMPacket p;
    ITMGetPacket( i, &p );

    switch ( p.srcAddr )
    {
        // --------------
        case 0: /* DWT Event */
            break;

        // --------------
        case 1: /* Exception */
            _handleException( i, &p );
            break;

        // --------------
        case 2: /* PC Counter Sample */
            _handlePCSample( i, &p );
            break;

        // --------------
        default:
            break;
            // --------------
    }
}
// ====================================================================================================
void _itmPumpProcess( char c )

/* Handle individual characters into the itm decoder */

{
    switch ( ITMPump( &_r.i, c ) )
    {
        // ------------------------------------
        case ITM_EV_NONE:
            break;

        // ------------------------------------
        case ITM_EV_UNSYNCED:
            if ( options.verbose )
            {
                fprintf( stdout, "ITM Lost Sync (%d)" EOL, ITMDecoderGetStats( &_r.i )->lostSyncCount );
            }

            break;

        // ------------------------------------
        case ITM_EV_SYNCED:
            if ( options.verbose )
            {
                fprintf( stdout, "ITM In Sync (%d)" EOL, ITMDecoderGetStats( &_r.i )->syncCount );
            }

            break;

        // ------------------------------------
        case ITM_EV_OVERFLOW:
            if ( options.verbose )
            {
                fprintf( stdout, "ITM Overflow (%d)" EOL, ITMDecoderGetStats( &_r.i )->overflow );
            }

            break;

        // ------------------------------------
        case ITM_EV_ERROR:
            if ( options.verbose )
            {
                fprintf( stdout, "ITM Error" EOL );
            }

            break;

        // ------------------------------------
        case ITM_EV_TS_PACKET_RXED:
            break;

        // ------------------------------------
        case ITM_EV_SW_PACKET_RXED:
            _handleSW( &_r.i );
            break;

        // ------------------------------------
        case ITM_EV_HW_PACKET_RXED:
            _handleHW( &_r.i );
            break;

        // ------------------------------------
        case ITM_EV_XTN_PACKET_RXED:
            break;
            // ------------------------------------
    }
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Protocol pump for decoding messages
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
void _protocolPump( uint8_t c )

/* Top level protocol pump */

{
    if ( options.useTPIU )
    {
        switch ( TPIUPump( &_r.t, c ) )
        {
            // ------------------------------------
            case TPIU_EV_NEWSYNC:
                if ( options.verbose )
                {
                    printf( "TPIU In Sync (%d)" EOL, TPIUDecoderGetStats( &_r.t )->syncCount );
                }

            case TPIU_EV_SYNCED:
                ITMDecoderForceSync( &_r.i, true );
                break;

            // ------------------------------------
            case TPIU_EV_RXING:
            case TPIU_EV_NONE:
                break;

            // ------------------------------------
            case TPIU_EV_UNSYNCED:
                printf( "TPIU Lost Sync (%d)" EOL, TPIUDecoderGetStats( &_r.t )->lostSync );
                ITMDecoderForceSync( &_r.i, false );
                break;

            // ------------------------------------
            case TPIU_EV_RXEDPACKET:
                if ( !TPIUGetPacket( &_r.t, &_r.p ) )
                {
                    fprintf( stderr, "TPIUGetPacket fell over" EOL );
                }

                for ( uint32_t g = 0; g < _r.p.len; g++ )
                {
                    if ( _r.p.packet[g].s == options.tpiuITMChannel )
                    {
                        _itmPumpProcess( _r.p.packet[g].d );
                        continue;
                    }

                    if ( ( _r.p.packet[g].s != 0 ) && ( options.verbose ) )
                    {
                        if ( options.verbose )
                        {
                            fprintf( stdout, "Unknown TPIU channel %02x" EOL, _r.p.packet[g].s );
                        }
                    }
                }

                break;

            // ------------------------------------
            case TPIU_EV_ERROR:
                fprintf( stderr, "****ERROR****" EOL );
                break;
                // ------------------------------------
        }
    }
    else
    {
        /* There's no TPIU in use, so this goes straight to the ITM layer */
        _itmPumpProcess( c );
    }
}
// ====================================================================================================
void _printHelp( char *progName )

{
    fprintf( stdout, "Useage: %s <htv> <-e ElfFile> <-m MaxHistory> <-o filename> -r <routines> <-i channel> <-p port> <-s server>" EOL, progName );
    fprintf( stdout, "        c: <num> Cut screen output after number of lines" EOL );
    fprintf( stdout, "        d: <DeleteMaterial> to take off front of filenames" EOL );
    fprintf( stdout, "        e: <ElfFile> to use for symbols" EOL );
    fprintf( stdout, "        h: This help" EOL );
    fprintf( stdout, "        i: <channel> Set ITM Channel in TPIU decode (defaults to 1)" EOL );
    fprintf( stdout, "        l: Aggregate per line rather than per function" EOL );
    fprintf( stdout, "        m: <MaxHistory> to record in history file (default %d intervals)" EOL, options.maxHistory );
    fprintf( stdout, "        n: Enforce sync requirement for ITM (i.e. ITM needs to issue syncs)" EOL );
    fprintf( stdout, "        o: <filename> to be used for output history file" EOL );
    fprintf( stdout, "        r: <routines> to record in history file (default %d routines)" EOL, options.maxRoutines );
    fprintf( stdout, "        s: <Server>:<Port> to use" EOL );
    fprintf( stdout, "        t: Use TPIU decoder" EOL );
    fprintf( stdout, "        v: Verbose mode (this will intermingle state info with the output flow)" EOL );

}
// ====================================================================================================
int _processOptions( int argc, char *argv[] )

{
    int c;

    while ( ( c = getopt ( argc, argv, "c:d:e:hi:lm:no:r:s:tv" ) ) != -1 )
        switch ( c )
        {
            // ------------------------------------
            case 'c':
                options.cutscreen = atoi( optarg );
                break;

            // ------------------------------------
            case 'e':
                options.elffile = optarg;
                break;

            // ------------------------------------
            case 'd':
                options.deleteMaterial = optarg;
                break;

            // ------------------------------------
            case 'l':
                options.lineDisaggregation = true;
                break;

            // ------------------------------------
            case 'r':
                options.maxRoutines = atoi( optarg );
                break;

            // ------------------------------------
            case 'm':
                options.maxHistory = atoi( optarg );
                break;

            // ------------------------------------
            case 'n':
                options.forceITMSync = false;
                break;

            // ------------------------------------
            case 'o':
                options.outfile = optarg;
                break;

            // ------------------------------------
            case 'v':
                options.verbose = 1;
                break;

            // ------------------------------------
            case 't':
                options.useTPIU = true;
                break;

            // ------------------------------------
            case 'i':
                options.tpiuITMChannel = atoi( optarg );
                break;

            // ------------------------------------
            case 's':
                options.server = optarg;

                // See if we have an optional port number too
                char *a = optarg;

                while ( ( *a ) && ( *a != ':' ) )
                {
                    a++;
                }

                if ( *a == ':' )
                {
                    *a = 0;
                    options.port = atoi( ++a );
                }

                if ( !options.port )
                {
                    options.port = SERVER_PORT;
                }

                break;

            // ------------------------------------
            case 'h':
                _printHelp( argv[0] );
                return false;

            // ------------------------------------
            case '?':
                if ( optopt == 'b' )
                {
                    fprintf ( stderr, "Option '%c' requires an argument." EOL, optopt );
                }
                else if ( !isprint ( optopt ) )
                {
                    fprintf ( stderr, "Unknown option character `\\x%x'." EOL, optopt );
                }

                return false;

            // ------------------------------------
            default:
                fprintf( stderr, "Unknown option %c" EOL, optopt );
                return false;
                // ------------------------------------
        }

    if ( ( options.useTPIU ) && ( !options.tpiuITMChannel ) )
    {
        fprintf( stderr, "TPIU set for use but no channel set for ITM output" EOL );
        return false;
    }

    if ( !options.elffile )
    {
        fprintf( stderr, "Elf File not specified" EOL );
        exit( -2 );
    }

    if ( options.verbose )
    {
        fprintf( stdout, "orbtop V" VERSION " (Git %08X %s, Built " BUILD_DATE ")" EOL, GIT_HASH, ( GIT_DIRTY ? "Dirty" : "Clean" ) );

        fprintf( stdout, "Verbose     : true" EOL );
        fprintf( stdout, "Server      : %s:%d" EOL, options.server, options.port );
        fprintf( stdout, "Delete Mat  : %s" EOL, options.deleteMaterial ? options.deleteMaterial : "None" );
        fprintf( stdout, "Elf File    : %s" EOL, options.elffile );
        fprintf( stdout, "ForceSync   : %s" EOL, options.forceITMSync ? "true" : "false" );

        if ( options.useTPIU )
        {
            fprintf( stdout, "Using TPIU  : true (ITM on channel %d)" EOL, options.tpiuITMChannel );
        }
    }

    return true;
}
// ====================================================================================================
int main( int argc, char *argv[] )

{
    int sockfd;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    uint8_t cbw[TRANSFER_SIZE];
    uint64_t lastTime;

    ssize_t t;
    int flag = 1;

    /* Fill in a time to start from */
    lastTime = _timestamp();

    if ( !_processOptions( argc, argv ) )
    {
        exit( -1 );
    }

    /* Get the symbols from file */
    _r.s = SymbolSetCreate( options.elffile );

    if ( !_r.s )
    {
        exit( -3 );
    }

    /* Reset the TPIU handler before we start */
    TPIUDecoderInit( &_r.t );
    ITMDecoderInit( &_r.i, options.forceITMSync );

    sockfd = socket( AF_INET, SOCK_STREAM, 0 );
    setsockopt( sockfd, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof( flag ) );

    if ( sockfd < 0 )
    {
        perror( "Error creating socket\n" );
        return -1;
    }

    /* Now open the network connection */
    bzero( ( char * ) &serv_addr, sizeof( serv_addr ) );
    server = gethostbyname( options.server );

    if ( !server )
    {
        perror( "Cannot find host" );
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    bcopy( ( char * )server->h_addr,
           ( char * )&serv_addr.sin_addr.s_addr,
           server->h_length );
    serv_addr.sin_port = htons( options.port );

    if ( connect( sockfd, ( struct sockaddr * ) &serv_addr, sizeof( serv_addr ) ) < 0 )
    {
        perror( "Could not connect" );
        return -1;
    }

    while ( ( t = read( sockfd, cbw, TRANSFER_SIZE ) ) > 0 )
    {
        uint8_t *c = cbw;

        while ( t-- )
        {
            _protocolPump( *c++ );
        }

        if ( _timestamp() - lastTime > TOP_UPDATE_INTERVAL )
        {
            lastTime = _timestamp();
            outputTop();
	    if (!SymbolSetCheckValidity( &_r.s, options.elffile ))
	      {
		/* Make sure old references are invalidated */
		_flushHash();
		
		if (options.verbose)
		  {
		    fprintf(stdout,"Reload %s" EOL,options.elffile );
		  }

		if (!_r.s)
		  {
		    /* Its possible the file was in the process of being written, so wait before testing again */
		    usleep(1000000);
		    if (!SymbolSetCheckValidity( &_r.s, options.elffile ))
		      {
			fprintf( stderr,"Elf file was lost" EOL );
			return -1;
		      }
		  }
	      }
        }

        /* Check to make sure there's not an unexpected TPIU in here */
        if ( ITMDecoderGetStats( &_r.i )->tpiuSyncCount )
        {
            fprintf( stderr, "Got a TPIU sync while decoding ITM...did you miss a -t option?" EOL );
            break;
        }
    }

    if ( ( options.verbose ) && ( !ITMDecoderGetStats( &_r.i )->tpiuSyncCount ) )
    {
        fprintf( stderr, "Read failed" EOL );
    }

    close( sockfd );
    return -2;
}
// ====================================================================================================
