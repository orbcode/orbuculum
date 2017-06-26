/*
 * SWO Dumper for Blackmagic Probe and TTL Serial Interfaces
 * =========================================================
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
 */

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <elf.h>
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
#include "uthash.h"
#include "git_version_info.h"
#include "generics.h"
#include "tpiuDecoder.h"
#include "itmDecoder.h"

#define SERVER_PORT 3443                     /* Server port definition */
#define MAX_IP_PACKET_LEN (1500)             /* Maximum packet we might receive */
#define MAX_STRING_LENGTH (256)              /* Maximum length that will be output from a fifo for a single event */

#define DEFAULT_OUTFILE "/dev/stdout"
#define DEFAULT_TIMELEN 10000

/* ---------- CONFIGURATION ----------------- */

struct                                      /* Record for options, either defaults or from command line */
{
    /* Config information */
    BOOL verbose;
    BOOL useTPIU;
    uint32_t tpiuITMChannel;

  /* File to output dump to */
  char *outfile;

  /* Do we need to write syncronously */
  BOOL writeSync;

  /* How long to dump */
  uint32_t timelen;
  
    /* Source information */
    int port;
    char *server;
} options = {
  .useTPIU=TRUE,
  .tpiuITMChannel=1,
  .outfile = DEFAULT_OUTFILE,
  .timelen = DEFAULT_TIMELEN,
  .port = SERVER_PORT,
  .server = "localhost"
};

/* ----------- LIVE STATE ----------------- */
struct
{
    /* The decoders and the packets from them */
    struct ITMDecoder i;
    struct ITMPacket h;
    struct TPIUDecoder t;
    struct TPIUPacket p;
} _r;

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Internally available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
uint64_t _timestamp( void )

{
    struct timeval te;
    gettimeofday( &te, NULL ); // get current time
    uint64_t milliseconds = te.tv_sec * 1000LL + te.tv_usec / 1000; // caculate milliseconds
    return milliseconds;
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
            case TPIU_EV_SYNCED:
                ITMDecoderForceSync( &_r.i, TRUE );
                break;

            // ------------------------------------
            case TPIU_EV_RXING:
            case TPIU_EV_NONE:
                break;

            // ------------------------------------
            case TPIU_EV_UNSYNCED:
                ITMDecoderForceSync( &_r.i, FALSE );
                break;

            // ------------------------------------
            case TPIU_EV_RXEDPACKET:
                if ( !TPIUGetPacket( &_r.t, &_r.p ) )
                {
                    fprintf( stderr, "TPIUGetPacket fell over\n" );
                }

                for ( uint32_t g = 0; g < _r.p.len; g++ )
                {
                    if ( _r.p.packet[g].s == options.tpiuITMChannel )
                    {
		      ITMPump( &_r.i, _r.p.packet[g].d );
                        continue;
                    }

                    if ( ( _r.p.packet[g].s != 0 ) && ( options.verbose ) )
                    {
                        if ( options.verbose )
                        {
                            printf( "Unknown TPIU channel %02x\n", _r.p.packet[g].s );
                        }
                    }
                }

                break;

            // ------------------------------------
            case TPIU_EV_ERROR:
                fprintf( stderr, "****ERROR****\n" );
                break;
                // ------------------------------------
        }
    }
    else
    {
        /* There's no TPIU in use, so this goes straight to the ITM layer */
      ITMPump( &_r.i, c );
    }
}
// ====================================================================================================
void _printHelp( char *progName )

{
    printf( "Useage: %s <htv> <-i channel> <-p port> <-s server>\n", progName );
    printf( "        h: This help\n" );
    printf( "        i: <channel> Set ITM Channel in TPIU decode (defaults to 1)\n" );
    printf( "        l: <timelen> Length of time in ms to record from point of acheiving sync (defaults to %dmS)\n", options.timelen );
    printf( "        o: <filename> to be used for dump file (defaults to %s)\n", options.outfile );
    printf( "        p: <Port> to use\n" );
    printf( "        s: <Server> to use\n" );
    printf( "        t: Use TPIU decoder\n" );
    printf( "        v: Verbose mode (this will intermingle SWO info with the output flow when using stdout)\n" );
    printf( "        w: Write syncronously to the output file after every packet\n" );
}
// ====================================================================================================
int _processOptions( int argc, char *argv[] )

{
    int c;

    while ( ( c = getopt ( argc, argv, "hti:l:o:p:s:vw" ) ) != -1 )
        switch ( c )
        {
            case 'o':
                options.outfile = optarg;
                break;

	case 'l':
	  options.timelen = atoi(optarg);
                break;

            case 'w':
                options.writeSync = TRUE;
                break;

            case 'v':
                options.verbose = 1;
                break;

            case 't':
                options.useTPIU = TRUE;
                break;

            case 'i':
                options.tpiuITMChannel = atoi( optarg );
                break;

            /* Source information */
            case 'p':
                options.port = atoi( optarg );
                break;

            case 's':
                options.server = optarg;
                break;

            case 'h':
                _printHelp( argv[0] );
                return FALSE;

            case '?':
                if ( optopt == 'b' )
                {
                    fprintf ( stderr, "Option '%c' requires an argument.\n", optopt );
                }
                else if ( !isprint ( optopt ) )
                {
                    fprintf ( stderr, "Unknown option character `\\x%x'.\n", optopt );
                }

                return FALSE;

            default:
                fprintf( stderr, "Unknown option %c\n", optopt );
                return FALSE;
        }

    if ( ( options.useTPIU ) && ( !options.tpiuITMChannel ) )
    {
        fprintf( stderr, "TPIU set for use but no channel set for ITM output\n" );
        return FALSE;
    }

    if ( options.verbose )
    {
        fprintf( stdout, "orbdump V" VERSION " (Git %08X %s, Built " BUILD_DATE ")\n", GIT_HASH, ( GIT_DIRTY ? "Dirty" : "Clean" ) );

        fprintf( stdout, "Verbose   : TRUE\n" );
        fprintf( stdout, "Server    : %s:%d\n", options.server, options.port );
	fprintf( stdout, "Rec Length: %dmS\n", options.timelen);
	fprintf( stdout, "Sync Write: %s\n", options.writeSync?"TRUE":"FALSE");

        if ( options.useTPIU )
        {
            fprintf( stdout, "Using TPIU: TRUE (ITM on channel %d)\n", options.tpiuITMChannel );
        }
    }

    return TRUE;
}
// ====================================================================================================
int main( int argc, char *argv[] )

{
    int sockfd;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    uint8_t cbw[MAX_IP_PACKET_LEN];
    uint64_t firstTime=0;
    size_t octetsRxed=0;
    FILE *opFile;

    ssize_t readLength, t;
    int flag = 1;

    BOOL haveSynced=FALSE;
    
    if ( !_processOptions( argc, argv ) )
    {
        exit( -1 );
    }

    /* Reset the TPIU handler before we start */
    TPIUDecoderInit( &_r.t );
    ITMDecoderInit( &_r.i );

    sockfd = socket( AF_INET, SOCK_STREAM, 0 );
    setsockopt( sockfd, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof( flag ) );

    if ( sockfd < 0 )
    {
        fprintf( stderr, "Error creating socket\n" );
        return -1;
    }

    
    /* Now open the network connection */
    bzero( ( char * ) &serv_addr, sizeof( serv_addr ) );
    server = gethostbyname( options.server );

    if ( !server )
    {
        fprintf( stderr, "Cannot find host\n" );
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    bcopy( ( char * )server->h_addr,
           ( char * )&serv_addr.sin_addr.s_addr,
           server->h_length );
    serv_addr.sin_port = htons( options.port );

    if ( connect( sockfd, ( struct sockaddr * ) &serv_addr, sizeof( serv_addr ) ) < 0 )
    {
        fprintf( stderr, "Could not connect\n" );
        return -1;
    }

    /* .... and the file to dump it into */
    opFile=fopen(options.outfile,"wb");
    if (!opFile)
      {
	fprintf( stderr, "Could not open output file for writing\n");
	return -2;
      }

    if (options.verbose)
      {
	fprintf( stdout,"Waiting for sync\n");
      }

    /* Start the process of collecting the data */
    while (( readLength = read( sockfd, cbw, MAX_IP_PACKET_LEN ) ) > 0 )
      {
	if ((firstTime != 0) && ( (_timestamp()-firstTime)>options.timelen) )
	  {
	    /* This packet arrived at the end of the window...finish the write process */
	    break;
	  }
	
        uint8_t *c = cbw;

	t = readLength;
        while ( t-- )
        {
            _protocolPump( *c++ );
        }

        /* Check to make sure there's not an unexpected TPIU in here */
        if ( ITMDecoderGetStats( &_r.i )->tpiuSyncCount )
        {
            fprintf( stderr, "Got a TPIU sync while decoding ITM...did you miss a -t option?\n" );
            break;
        }

	/* ... now check if we've acheived sync so can write frames */
	if (!haveSynced)
	  {
	    if (!ITMDecoderIsSynced( &_r.i ))
	      {
		continue;
	      }
	    haveSynced=TRUE;
	    /* Fill in the time to start from */
	    firstTime = _timestamp();
	    if (options.verbose)
	      {
		fprintf(stdout,"Started recording\n");
	      }
	  }

	octetsRxed+=fwrite(cbw,1,readLength,opFile);

	if (!ITMDecoderIsSynced( &_r.i ))
	  {
	    fprintf( stderr, "Warning:Sync lost while writing output\n");
	  }
	
	if (options.writeSync)
	  {
	    sync();
	  }
    }

    close( sockfd );
    fclose( opFile );

    if ( readLength<= 0 )
    {
        fprintf( stderr, "Network Read failed\n" );
	return -2;
    }

    if (options.verbose)
      {
	fprintf( stdout,"Wrote %ld bytes of data\n",octetsRxed);
      }
    return 0;
}
// ====================================================================================================
