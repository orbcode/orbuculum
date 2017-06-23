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
#define TOP_UPDATE_INTERVAL (1000LL)         /* Interval between each on screen update */
#define MAX_STRING_LENGTH (256)              /* Maximum length that will be output from a fifo for a single event */

/* Special tag values for unreal locations */
#define SLEEPING ((void *)0xFFFFFFFF)
#define UNKNOWN ((void *)0xFFFFFFFE)

struct visitedAddr                           /* Structure for Hashmap of visited/observed addresses */
{
    uint32_t addr;
    uint32_t visits;
    UT_hash_handle hh;
};

#define MAX_IP_PACKET_LEN (1500)

struct SymbolTableEntry                    /* Record for a function in the source file */
{
    uint32_t address;
    char *name;
};

static struct visitedAddr *addresses = NULL;
uint32_t sleeps;

/* ---------- CONFIGURATION ----------------- */

struct                                      /* Record for options, either defaults or from command line */
{
    /* Config information */
    BOOL verbose;
    BOOL useTPIU;
    uint32_t tpiuITMChannel;

    uint32_t hwOutputs;

    /* Target program config */
    char *elffile;

    /* Source information */
    int port;
    char *server;

} options = {.tpiuITMChannel = 1, .port = SERVER_PORT, .server = "localhost"};

/* ----------- LIVE STATE ----------------- */
struct
{
    /* The decoders and the packets from them */
    struct ITMDecoder i;
    struct ITMPacket h;
    struct TPIUDecoder t;
    struct TPIUPacket p;

    uint32_t numSymbols;
    struct SymbolTableEntry *symbols;
} _r;

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Internally available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
int _addrCompare( const void *a, const void *b )

/* Embedded compare function for searching and sorting */

{
    return ( ( ( const struct SymbolTableEntry * )a )->address - ( ( const struct SymbolTableEntry * )b )->address );
}


BOOL _loadElfSymbols( void )

{
    int32_t fd;
    Elf32_Ehdr elfHdr;
    Elf32_Shdr *sHdr;
    int32_t sTab;

    /* Embedded function to get the section ------------- */
    char *__alloc_and_read_section( int32_t fd, Elf32_Shdr sh )
    {
        char *buff = malloc( sh.sh_size );

        if ( buff )
        {
            lseek( fd, ( off_t )sh.sh_offset, SEEK_SET );
            read( fd, ( void * )buff, sh.sh_size );
        }

        return buff;
    }
    /* ------------------------------------------------- */

    fd = open( options.elffile, O_RDONLY );

    if ( fd < 0 )
    {
        fprintf( stderr, "Couldn't open ELF file\n" );
        return FALSE;
    }

    read( fd, &elfHdr, sizeof( elfHdr ) );

    if ( strncmp( ( char * )elfHdr.e_ident, "\177ELF", 4 ) )
    {
        fprintf( stderr, "%s is not an ELF file\n", options.elffile );
        return FALSE;
    }

    /* Collect the headers */
    sHdr = malloc( elfHdr.e_shentsize * elfHdr.e_shnum );
    lseek( fd, ( off_t )elfHdr.e_shoff, SEEK_SET );

    for ( uint32_t i = 0; i < elfHdr.e_shnum; i++ )
    {
        read( fd, ( void * )&sHdr[i], elfHdr.e_shentsize );
    }

    /* Now find the symbol table for import */
    for ( sTab = 0; sTab < elfHdr.e_shnum; sTab++ )
    {
        if ( sHdr[sTab].sh_type == SHT_SYMTAB )
        {
            break;
        }
    }

    if ( sTab == elfHdr.e_shnum )
    {
        fprintf( stderr, "Couldn't find symbol table in ELF file\n" );
        free( sHdr );
        close( fd );
        return FALSE;
    }


    Elf32_Sym *sym_tbl = ( Elf32_Sym * )__alloc_and_read_section( fd, sHdr[sTab] );
    char *str_tbl = __alloc_and_read_section( fd, sHdr[sHdr[sTab].sh_link] );
    uint32_t symbol_count = ( sHdr[sTab].sh_size / sizeof( Elf32_Sym ) );

    _r.symbols = ( struct SymbolTableEntry * )malloc( symbol_count * sizeof( struct SymbolTableEntry ) );
    _r.numSymbols = 0;

    for ( uint32_t i = 0; i < symbol_count; i++ )
    {
        if ( STT_FUNC == ELF32_ST_TYPE( sym_tbl[i].st_info ) )
        {
            _r.symbols[_r.numSymbols].address = sym_tbl[i].st_value;
            _r.symbols[_r.numSymbols].name = strdup( str_tbl + sym_tbl[i].st_name );
            _r.numSymbols++;
        }
    }

    /* Put the found addresses into order */
    qsort( _r.symbols, _r.numSymbols, sizeof( struct SymbolTableEntry ), _addrCompare );

    /* ....get rid of the working memory we used */
    free( str_tbl );
    free( sym_tbl );
    free( sHdr );
    close( fd );
    return TRUE;
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Handler for individual message types from SWO
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
void _handleException( struct ITMDecoder *i, struct ITMPacket *p )

{
    if ( !( options.hwOutputs & ( 1 << HWEVENT_EXCEPTION ) ) )
    {
        return;
    }

    uint32_t exceptionNumber = ( ( p->d[1] & 0x01 ) << 8 ) | p->d[0];
    uint32_t eventType = p->d[1] >> 4;

    const char *exNames[] = {"Thread", "Reset", "NMI", "HardFault", "MemManage", "BusFault", "UsageFault", "UNKNOWN_7",
                             "UNKNOWN_8", "UNKNOWN_9", "UNKNOWN_10", "SVCall", "Debug Monitor", "UNKNOWN_13", "PendSV", "SysTick"
                            };
    const char *exEvent[] = {"Unknown", "Enter", "Exit", "Resume"};
    fprintf( stdout, "%d,%s,%s\n", HWEVENT_EXCEPTION, exEvent[eventType], exNames[exceptionNumber] );
}
// ====================================================================================================
void _handleDWTEvent( struct ITMDecoder *i, struct ITMPacket *p )

{
    if ( !( options.hwOutputs & ( 1 << HWEVENT_DWT ) ) )
    {
        return;
    }

    uint32_t event = p->d[1] & 0x2F;
    const char *evName[] = {"CPI", "Exc", "Sleep", "LSU", "Fold", "Cyc"};

    for ( uint32_t i = 0; i < 6; i++ )
    {
        if ( event & ( 1 << i ) )
        {
            fprintf( stdout, "%d,%s\n", HWEVENT_DWT, evName[event] );
        }
    }
}
// ====================================================================================================
struct SymbolTableEntry *_lookup( uint32_t addr )

/* Lookup function for address to function */

{
    uint32_t imax = _r.numSymbols;
    uint32_t imin = 0;
    uint32_t imid;

    if ( ( addr < _r.symbols[imin].address ) || ( addr > _r.symbols[imax - 1].address ) )
    {
        return FALSE;
    }

    while ( imax >= imin )
    {
        imid = imin + ( ( imax - imin ) >> 1 );

        if ( ( addr >= _r.symbols[imid].address ) && ( addr < _r.symbols[imid + 1].address ) )
        {
            return &_r.symbols[imid];
        }

        if ( _r.symbols[imid].address < addr )
        {
            imin = imid + 1;
        }
        else
        {
            imax = imid - 1;
        }
    }

    return NULL;
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
    if ( ( ( ( struct visitedAddr * )a )->addr ) < ( ( ( struct visitedAddr * )b )->addr ) )
    {
        return -1;
    }

    if ( ( ( ( struct visitedAddr * )a )->addr ) > ( ( ( struct visitedAddr * )b )->addr ) )
    {
        return 1;
    }

    return 0;
}
// ====================================================================================================
struct reportLine

{
    struct SymbolTableEntry *s;
    uint64_t count;
    UT_hash_handle hh;
};
// ====================================================================================================
int _report_sort_fn( void *a, void *b )

{
    if ( ( ( ( struct reportLine * )a )->count ) < ( ( ( struct reportLine * )b )->count ) )
    {
        return 1;
    }

    if ( ( ( ( struct reportLine * )a )->count ) > ( ( ( struct reportLine * )b )->count ) )
    {
        return -1;
    }

    return 0;
}
// ====================================================================================================
void outputTop( void )

/* Produce the output */

{
    struct SymbolTableEntry *l = NULL;

    struct reportLine *report = NULL;
    struct reportLine *current = NULL;

    uint32_t total = 0;
    uint32_t unknown = 0;
    uint64_t samples = 0;
    uint32_t percentage;

    /* Put the address into order */
    HASH_SORT( addresses, _addresses_sort_fn );

    /* Now merge them together */
    for ( struct visitedAddr *a = addresses; a != NULL; a = a->hh.next )
    {
        if ( a->visits )
        {
            if ( !( ( l ) && ( l->address > a->addr ) ) )
            {
                l = _lookup( a->addr );
            }


            if ( !l )
            {
                unknown += a->visits;
                a->visits = 0;
            }
            else
            {
                if ( ( current ) && ( l == current->s ) )
                {
                    /* This is another line in the same function */
                    current->count += a->visits;
                    total += a->visits;
                    a->visits = 0;
                }
                else
                {
                    if ( current )
                    {
                        HASH_ADD_INT( report, count, current );
                    }

                    current = ( struct reportLine * )calloc( 1, sizeof( struct reportLine ) );

                    /* Reset these for the next iteration */
                    current->count = a->visits;
                    current->s = l;
                    total += a->visits;
                    a->visits = 0;
                }
            }
        }
    }

    /* If there's one left then add it in */
    if ( current )
    {
        HASH_ADD_INT( report, count, current );
    }

    /* Add entries for Sleeping and unknown, if there are any of either */
    if ( sleeps )
    {
        current = ( struct reportLine * )calloc( 1, sizeof( struct reportLine ) );
        /* Reset these for the next iteration */
        current->count = sleeps;
        total += sleeps;
        sleeps = 0;
        current->s = SLEEPING;
        HASH_ADD_INT( report, count, current );
    }

    if ( unknown )
    {
        current = ( struct reportLine * )calloc( 1, sizeof( struct reportLine ) );
        /* Reset these for the next iteration */
        current->count = unknown;
        total += unknown;
        current->s = UNKNOWN;
        HASH_ADD_INT( report, count, current );
    }

    HASH_SORT( report, _report_sort_fn );
    struct reportLine *n;
    fprintf( stdout, "\033[2J\033[;H" );

    if ( total )
    {
        for ( struct reportLine *r = report; r != NULL; r = n )
        {
            percentage = ( r->count * 10000 ) / total;

            if ( r->count )
            {
                fprintf( stdout, "%3d.%02d%% %8ld ", percentage / 100, percentage % 100, r->count );

                if ( r->s == UNKNOWN )
                {
                    fprintf( stdout, "** UNKNOWN **\n" );
                }
                else if ( r->s == SLEEPING )
                {
                    fprintf( stdout, "** SLEEPING **\n" );
                }
                else
                {
                    fprintf( stdout, "%s\n", r->s->name );
                }

            }

            samples += r->count;
            n = r->hh.next;
            free( r );
        }

        fprintf( stdout, "-----------------\n" );
        fprintf( stdout, "        %8ld Samples\n", samples );
    }
    else
    {
      if (options.verbose)
	{
	  fprintf( stdout, "No samples\n");
	}
    }

    if (options.verbose)
      {
	fprintf( stdout,"         Ovf=%3d  ITMSync=%3d TPIUSync=%3d ITMErrors=%3d\n",
		 ITMDecoderGetStats(&_r.i)->overflow,
		 ITMDecoderGetStats(&_r.i)->syncCount,
		 TPIUDecoderGetStats(&_r.t)->syncCount,
		 ITMDecoderGetStats(&_r.i)->ErrorPkt);
      }

    /* ... and we are done with the report now, get rid of it */
    HASH_CLEAR( hh, report );
}
// ====================================================================================================
void _handlePCSample( struct ITMDecoder *i, struct ITMPacket *p )

{
    uint32_t pc;

    if ( p->len == 1 )
    {
        /* This is a sleep packet */
        sleeps++;
    }
    else
    {
        pc = ( p->d[3] << 24 ) | ( p->d[2] << 16 ) | ( p->d[1] << 8 ) | ( p->d[0] );

        struct visitedAddr *a;
        HASH_FIND_INT( addresses, &pc, a );

        if ( a )
        {
            a->visits++;
        }
        else
        {
            /* Create a new record for this address */
            a = ( struct visitedAddr * )malloc( sizeof( struct visitedAddr ) );
            a->visits = 1;
            a->addr = pc;
            HASH_ADD_INT( addresses, addr, a );
        }
    }
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
                fprintf( stdout, "ITM Lost Sync (%d)\n", ITMDecoderGetStats( &_r.i )->lostSyncCount );
            }

            break;

        // ------------------------------------
        case ITM_EV_SYNCED:
            if ( options.verbose )
            {
                fprintf( stdout, "ITM In Sync (%d)\n", ITMDecoderGetStats( &_r.i )->syncCount );
            }

            break;

        // ------------------------------------
        case ITM_EV_OVERFLOW:
            if ( options.verbose )
            {
                fprintf( stdout, "ITM Overflow (%d)\n", ITMDecoderGetStats( &_r.i )->overflow );
            }

            break;

        // ------------------------------------
        case ITM_EV_ERROR:
            if ( options.verbose )
            {
                fprintf( stdout, "ITM Error\n" );
            }

            break;

        // ------------------------------------
        case ITM_EV_TS_PACKET_RXED:
            break;

        // ------------------------------------
        case ITM_EV_SW_PACKET_RXED:
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
            case TPIU_EV_SYNCED:
                if ( options.verbose )
                {
                    printf( "TPIU In Sync (%d)\n", TPIUDecoderGetStats( &_r.t )->syncCount );
                }

                ITMDecoderForceSync( &_r.i, TRUE );
                break;

            // ------------------------------------
            case TPIU_EV_RXING:
            case TPIU_EV_NONE:
                break;

            // ------------------------------------
            case TPIU_EV_UNSYNCED:
                printf( "TPIU Lost Sync (%d)\n", TPIUDecoderGetStats( &_r.t )->lostSync );
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
                        _itmPumpProcess( _r.p.packet[g].d );
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
        _itmPumpProcess( c );
    }
}
// ====================================================================================================
void _printHelp( char *progName )

{
    printf( "Useage: %s <htv> <-i channel> <-p port> <-s server>\n", progName );
    printf( "        e: <ElfFile> to use for symbols\n" );
    printf( "        h: This help\n" );
    printf( "        i: <channel> Set ITM Channel in TPIU decode (defaults to 1)\n" );
    printf( "        p: <Port> to use\n" );
    printf( "        s: <Server> to use\n" );
    printf( "        t: Use TPIU decoder\n" );
    printf( "        v: Verbose mode (this will intermingle state info with the output flow)\n" );
}
// ====================================================================================================
int _processOptions( int argc, char *argv[] )

{
    int c;
#define DELIMITER ','

    while ( ( c = getopt ( argc, argv, "a:e:hti:p:s:v" ) ) != -1 )
        switch ( c )
        {
            /* Config Information */
            case 'e':
                options.elffile = optarg;
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
        fprintf( stdout, "orbtop V" VERSION " (Git %08X %s, Built " BUILD_DATE ")\n", GIT_HASH, ( GIT_DIRTY ? "Dirty" : "Clean" ) );

        fprintf( stdout, "Verbose   : TRUE\n" );
        fprintf( stdout, "Server    : %s:%d", options.server, options.port );

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
    uint64_t lastTime;

    ssize_t t;
    int flag = 1;

    /* Fill in a time to start from */
    lastTime = _timestamp();

    if ( !_processOptions( argc, argv ) )
    {
        exit( -1 );
    }

    if ( !options.elffile )
    {
        fprintf( stderr, "Elf File not specified\n" );
        exit( -2 );
    }


    if ( !_loadElfSymbols() )
    {
        exit( -3 );
    }

    /* Reset the TPIU handler before we start */
    TPIUDecoderInit( &_r.t );
    ITMDecoderInit( &_r.i );

    /* Because we are connecting over IP, we only get synced packets, so force receivers into sync */
    TPIUDecoderForceSync( &_r.t, 0 );
    ITMDecoderForceSync( &_r.i, TRUE );

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

    while ( ( t = read( sockfd, cbw, MAX_IP_PACKET_LEN ) ) > 0 )
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
        }

        /* Check to make sure there's not an unexpected TPIU in here */
        if ( ITMDecoderGetStats( &_r.i )->tpiuSyncCount )
        {
            fprintf( stderr, "Got a TPIU sync while decoding ITM...did you miss a -t option?\n" );
            break;
        }
    }

    if ( ( options.verbose ) && ( !ITMDecoderGetStats( &_r.i )->tpiuSyncCount ) )
    {
        fprintf( stderr, "Read failed\n" );
    }

    close( sockfd );
    return -2;
}
// ====================================================================================================
