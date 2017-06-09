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

#define ADDR2LINE "arm-none-eabi-addr2line"

#define SERVER_PORT 3443           // Server port definition 
#define TRANSFER_SIZE (64)
#define TOP_UPDATE_INTERVAL (1000) // Interval between each on screen update
#define MAX_STRING_LENGTH (256)    // Maximum length that will be output from a fifo for a single event

// Codeline entry returned from addr2line
struct CodeLine
{
  char *file;
  char *function;
  int line;
};

// Structure for Hashmap of visited/observed addresses

struct visitedAddr {
  uint32_t addr;
  uint32_t visits;
  UT_hash_handle hh;
};

static struct visitedAddr *addresses=NULL;

// Record for options, either defaults or from command line
struct
{
  /* Config information */
  BOOL verbose;
  BOOL useTPIU;
  uint32_t tpiuITMChannel;

  uint32_t hwOutputs;

  /* Addr2line config */
  char *addr2line;
  char *elffile;

  /* Source information */
  int port;
  char *server;

} options = {.addr2line=ADDR2LINE,  .tpiuITMChannel=1, .port=SERVER_PORT, .server="localhost"};

#define MAX_IP_PACKET_LEN (1500)

struct
{
  /* The decoders and the packets from them */
  struct ITMDecoder i;
  struct ITMPacket h;
  struct TPIUDecoder t;
  struct TPIUPacket p;
  
  /* The interface to the addr2line utility */
  FILE *alhandle;
} _r;
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Handler for individual message types from SWO
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
void _handleException(struct ITMDecoder *i, struct ITMPacket *p)

{
  if (!(options.hwOutputs&(1<<HWEVENT_EXCEPTION)))
    {
      return;
    }

  uint32_t exceptionNumber=((p->d[1]&0x01)<<8)|p->d[0];
  uint32_t eventType=p->d[1]>>4;

  const char *exNames[]={"Thread","Reset","NMI","HardFault","MemManage","BusFault","UsageFault","UNKNOWN_7",
      "UNKNOWN_8", "UNKNOWN_9", "UNKNOWN_10", "SVCall", "Debug Monitor", "UNKNOWN_13", "PendSV", "SysTick"};
  const char *exEvent[]={"Enter","Exit","Resume"};
  fprintf(stdout,"%d,%s,%s\n",HWEVENT_EXCEPTION,exEvent[eventType],exNames[exceptionNumber]);
}
// ====================================================================================================
void _handleDWTEvent(struct ITMDecoder *i, struct ITMPacket *p)

{
  if (!(options.hwOutputs&(1<<HWEVENT_DWT)))
    {
      return;
    }

  uint32_t event=p->d[1]&0x2F;
  const char *evName[]={"CPI","Exc","Sleep","LSU","Fold","Cyc"};

  for (uint32_t i=0; i<6; i++)
    {
      if (event&(1<<i))
	{
	  fprintf(stdout,"%d,%s\n",HWEVENT_DWT,evName[event]);
	}
    }
}
// ====================================================================================================
struct CodeLine *_lookup(uint32_t addr)

{
  static char fileContents[MAX_STRING_LENGTH];
  static char functionContents[MAX_STRING_LENGTH];
  static struct CodeLine l = {.file=fileContents, .function=functionContents};
  char *c=l.file;

  if (!_r.alhandle) return NULL;

  if (fprintf(_r.alhandle,"0x%08x\n",addr)<=0)
    {
      _r.alhandle=NULL;
      return NULL;
    }

  if (!fgets(l.function,MAX_STRING_LENGTH,_r.alhandle))
    {
      _r.alhandle=NULL;
      return NULL;
    }
  c=l.function;

  /* Remove newline from the end */
  while ((*c) && (*c!='\n') && (*c!='\r')) c++;
  *c=0;

  if (!fgets(l.file,MAX_STRING_LENGTH,_r.alhandle))
    {
      _r.alhandle=NULL;
      return NULL;
    }

  /* Spin through looking for the colon that represents the start of the line number */
  c=l.file;
  while ((*c!=':') && (*c)) c++;

  if (!*c)
    return NULL;

  *c++=0;
  l.line=atoi(c);
  return(&l);
}
// ====================================================================================================
uint64_t _timestamp(void) 

{
  struct timeval te; 
  gettimeofday(&te, NULL); // get current time
  uint64_t milliseconds = te.tv_sec*1000LL + te.tv_usec/1000; // caculate milliseconds
  return milliseconds;
}
// ====================================================================================================
int _addresses_sort_fn(void *a, void *b)

{
  if ((((struct visitedAddr *)a)->addr) < (((struct visitedAddr *)b)->addr)) return -1;
  if ((((struct visitedAddr *)a)->addr) > (((struct visitedAddr *)b)->addr)) return 1;
  return 0;
}
// ====================================================================================================
struct reportLine

{
  char function[MAX_STRING_LENGTH];
  uint64_t count;
  UT_hash_handle hh;
};
// ====================================================================================================
int _report_sort_fn(void *a, void *b)

{
  if ((((struct reportLine *)a)->count) < (((struct reportLine *)b)->count)) return 1;
  if ((((struct reportLine *)a)->count) > (((struct reportLine *)b)->count)) return -1;
  return 0;
}
// ====================================================================================================
void outputTop(void)

{
  struct CodeLine *l;

  struct reportLine *report=NULL;
  struct reportLine *current=NULL;

  uint32_t total=0;
  uint32_t unknown=0;
  uint32_t percentage;

  HASH_SORT(addresses, _addresses_sort_fn);
  for (struct visitedAddr *a=addresses; a!=NULL; a=a->hh.next)
    {
      l=_lookup(a->addr);
      if (!strcmp(l->function,"??"))
	{
	  unknown+=a->visits;
	  total+=a->visits;
	}
	else
	  {
	    if ((current) && (!strcmp(l->function,current->function)))
	      {
		/* This is another line in the same function */
		current->count+=a->visits;
		total+=a->visits;
	      }
	    else
	      {
		if (current)
		  {
		    HASH_ADD_INT(report, count, current);
		  }

		current=(struct reportLine *)calloc(1,sizeof(struct reportLine));

		/* Reset these for the next iteration */
		current->count=a->visits;
		total+=a->visits;
		strcpy(current->function,l->function);
	      }
	  }
    }

  /* If there's one left then add it in */
  if (current)
    {
      HASH_ADD_INT(report, count, current);
    }

  HASH_SORT(report, _report_sort_fn);
  struct reportLine *n;
  fprintf(stdout,"\033[2J\033[;H");

  if (total)
    {
      for (struct reportLine *r=report; r!=NULL; r=n)
	{
	  percentage=(r->count*10000)/total;
	  fprintf(stdout,"%3d.%02d%% %s\n",percentage/100, percentage%100,r->function);
	  n=r->hh.next;
	  free(r);
	}
      percentage=(unknown*10000)/total;
      fprintf(stdout,"%3d.%02d%% Unknown (%d Total Samples)\n",percentage/100, percentage%100,total);
    }
}
// ====================================================================================================
void _handlePCSample(struct ITMDecoder *i, struct ITMPacket *p)

{
  uint32_t pc=(p->d[3]<<24)|(p->d[2]<<16)|(p->d[1]<<8)|(p->d[0]);
  struct visitedAddr *a;
  HASH_FIND_INT( addresses, &pc, a );
  if (a)
    {
      a->visits++;
    }
  else
    {
      /* Create a new record for this address */
      a=(struct visitedAddr *)malloc(sizeof(struct visitedAddr));
      a->visits=1;
      a->addr=pc;
      HASH_ADD_INT(addresses, addr, a);
    }
}
// ====================================================================================================
void _handleHW(struct ITMDecoder *i)

{
  struct ITMPacket p;
  ITMGetPacket(i, &p);

  switch (p.srcAddr)
    {
      // --------------
    case 0: /* DWT Event */
      break;
      // --------------
    case 1: /* Exception */
      _handleException(i, &p);
      break;
      // --------------
    case 2: /* PC Counter Sample */
      _handlePCSample(i,&p);
      break;
      // --------------
    default:
      break;
      // --------------
    }
}
// ====================================================================================================
void _itmPumpProcess(char c)

{
  switch (ITMPump(&_r.i,c))
    {
    case ITM_EV_NONE:
      break;

    case ITM_EV_UNSYNCED:
      if (options.verbose)
	{
	  fprintf(stdout,"ITM Unsynced\n");
	}
      break;

    case ITM_EV_SYNCED:
      if (options.verbose)
	{
	  fprintf(stdout,"ITM Synced\n");
	}
      break;

    case ITM_EV_OVERFLOW:
      if (options.verbose)
	{
	  fprintf(stdout,"ITM Overflow\n");
	}
      break;

    case ITM_EV_ERROR:
      if (options.verbose)
	{
	  fprintf(stdout,"ITM Error\n");
	}
      break;

    case ITM_EV_HW_PACKET_RXED:
      _handleHW(&_r.i);
      break;

    default:
      break;
    }
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Protocol pump for decoding messages
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
void _protocolPump(uint8_t c)

{
  if (options.useTPIU)
    {
      switch (TPIUPump(&_r.t,c))
	{
	case TPIU_EV_SYNCED:
	  ITMDecoderForceSync(&_r.i, TRUE);
	  break;

	case TPIU_EV_RXING:
	case TPIU_EV_NONE: 
	  break;

	case TPIU_EV_UNSYNCED:
	  ITMDecoderForceSync(&_r.i, FALSE);
	  break;

	case TPIU_EV_RXEDPACKET:
	  if (!TPIUGetPacket(&_r.t, &_r.p))
	    {
	      fprintf(stderr,"TPIUGetPacket fell over\n");
	    }

	  for (uint32_t g=0; g<_r.p.len; g++)
	    {
	      if (_r.p.packet[g].s == options.tpiuITMChannel)
		{
		  _itmPumpProcess(_r.p.packet[g].d);
		  continue;
		}

	      if ((_r.p.packet[g].s != 0) && (options.verbose))
		{
		  if (options.verbose)
		    {
		      printf("Unknown TPIU channel %02x\n",_r.p.packet[g].s);
		    }
		}
	    }
	  break;

	case TPIU_EV_ERROR:
	  fprintf(stderr,"****ERROR****\n");
	  break;
	}
    }
  else
    {
      _itmPumpProcess(c);
    }
}
// ====================================================================================================
void _printHelp(char *progName)

{
  printf("Useage: %s <htv> <-i channel> <-p port> <-s server>\n",progName);
  printf("        a: <Path/Filename> of addr2line to use (Defaults to %s)\n",ADDR2LINE);
  printf("        e: <ElfFile> to use for symbols\n");
  printf("        h: This help\n");
  printf("        i: <channel> Set ITM Channel in TPIU decode (defaults to 1)\n");
  printf("        p: <Port> to use\n");
  printf("        s: <Server> to use\n");
  printf("        t: Use TPIU decoder\n");
  printf("        v: Verbose mode (this will intermingle state info with the output flow)\n");
}
// ====================================================================================================
int _processOptions(int argc, char *argv[])

{
  int c;
#define DELIMITER ','

  while ((c = getopt (argc, argv, "a:e:hti:p:s:v")) != -1)
    switch (c)
      {
	/* Config Information */
      case 'a':
	options.addr2line = optarg;
	break;
      case 'e':
	options.elffile = optarg;
	break;
      case 'v':
        options.verbose = 1;
        break;
      case 't':
	options.useTPIU=TRUE;
	break;
      case 'i':
	options.tpiuITMChannel=atoi(optarg);
	break;

	/* Source information */
      case 'p':
	options.port=atoi(optarg);
	break;
      case 's':
	options.server=optarg;
	break;
      case 'h':
	_printHelp(argv[0]);
	return FALSE;
      case '?':
        if (optopt == 'b')
          fprintf (stderr, "Option '%c' requires an argument.\n", optopt);
        else if (!isprint (optopt))
	    fprintf (stderr,"Unknown option character `\\x%x'.\n", optopt);
	return FALSE;
      default:
        return FALSE;
      }

  if ((options.useTPIU) && (!options.tpiuITMChannel))
    {
      fprintf(stderr,"TPIU set for use but no channel set for ITM output\n");
      return FALSE;
    }

  if (options.verbose)
    {
      fprintf(stdout,"orbtop V" VERSION " (Git %08X %s, Built " BUILD_DATE ")\n", GIT_HASH,(GIT_DIRTY?"Dirty":"Clean"));

      fprintf(stdout,"Verbose   : TRUE\n");
      fprintf(stdout,"Addr2line : %s\n",options.addr2line);
      fprintf(stdout,"Server    : %s:%d",options.server,options.port);    
      if (options.useTPIU)
	{
	  fprintf(stdout,"Using TPIU: TRUE (ITM on channel %d)\n",options.tpiuITMChannel);
	}

    }
  return TRUE;
}
// ====================================================================================================
int main(int argc, char *argv[])

{
  int sockfd;
  struct sockaddr_in serv_addr;
  struct hostent *server;
  unsigned char cbw[TRANSFER_SIZE];
  char constructString[MAX_STRING_LENGTH];
  uint64_t lastTime;

  ssize_t t;
  int flag=1;

  /* Fill in a time to start from */
  lastTime=_timestamp();

  if (!_processOptions(argc,argv))
    {
      exit(-1);
    }

  /* Reset the TPIU handler before we start */
  TPIUDecoderInit(&_r.t);
  ITMDecoderInit(&_r.i);

  /* Now create a connection to addr2line...we can use cbw as a temporary buffer */
  snprintf(constructString,MAX_STRING_LENGTH,"%s -fse %s",options.addr2line,options.elffile);

  _r.alhandle=popen(constructString, "r+"); // the logger app is another application

  /* A lookup of address 0 is unlikely to match a function, but it should be a valid response */
  if (!_lookup(0))
    {
      fprintf(stderr,"Could not perform popen\n");
      exit(-1);
    }

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof(flag));
  if (sockfd < 0) 
    {
      fprintf(stderr,"Error creating socket\n");
      return -1;
    }

  /* Now open the network connection */
  bzero((char *) &serv_addr, sizeof(serv_addr));
  server = gethostbyname(options.server);

  if (!server)
    {
      fprintf(stderr,"Cannot find host\n");
      return -1;
    }

  serv_addr.sin_family = AF_INET;
  bcopy((char *)server->h_addr, 
	(char *)&serv_addr.sin_addr.s_addr,
	server->h_length);
  serv_addr.sin_port = htons(options.port);
  if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0) 
    {
      fprintf(stderr,"Could not connect\n");
      return -1;
    }

  while ((t=read(sockfd,cbw,TRANSFER_SIZE))>0)
    {
      unsigned char *c=cbw;
      while (t--)
	_protocolPump(*c++);

      if (_timestamp()-lastTime>TOP_UPDATE_INTERVAL)
	{
	  lastTime=_timestamp();
	  outputTop();
	}
    }
  if (options.verbose)
    {
      fprintf(stderr,"Read failed\n");
    }
  close(sockfd);
  return -2;
}
// ====================================================================================================
