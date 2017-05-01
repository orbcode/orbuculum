/*
 * SWO Splitter for Blackmagic Probe and TTL Serial Interfaces
 * ===========================================================
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

#define VERSION "0.12"
#define PRINT_EXPERIMENTAL

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
#include <libusb.h>
#include <stdint.h>
#include <limits.h>
#include <termios.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>


#include "git_version_info.h"
#include "generics.h"
#include "tpiuDecoder.h"
#include "itmDecoder.h"

/* Server port definition */
#define SERVER_PORT 3443

/* Descriptor information for BMP */
#define VID       (0x1d50)
#define PID       (0x6018)
#define INTERFACE (5)
#define ENDPOINT  (0x85)

#define TRANSFER_SIZE (64)
#define NUM_CHANNELS  32

#define MAX_STRING_LENGTH (100)   // Maximum length that will be output from a fifo for a single event

// Information for an individual channel
struct SWchannel
{
  char *chanName;   /* Filename to be used for the fifo */
  char *presFormat; /* Format of data presentation to be used */
};

// Record for options, either defaults or from command line
struct
{
  /* Config information */
  BOOL verbose;
  BOOL useTPIU;
  uint32_t tpiuITMChannel;

  /* Sink information */
  struct SWchannel channel[NUM_CHANNELS];
  char *chanPath;

  /* Source information */
  char *port;
  char *file;
  int speed;
} options = {.chanPath="", .speed=115200, .tpiuITMChannel=1};


// Runtime state
struct channelRuntime
{
  int handle;
  pthread_t thread;
  char *fifoName;
};

struct nwClient

{
  int handle;
  pthread_t thread;
  struct nwClient *nextClient;
  struct nwClient *prevClient;
};

struct nwClientParams

{
  struct nwClient *client;
  int portNo;
  int listenHandle;
};

#define MAX_IP_PACKET_LEN (1500)

struct
{
  struct channelRuntime c[NUM_CHANNELS];
  struct nwClient *firstClient;
  pthread_t ipThread;
  struct ITMDecoder i;
  struct ITMPacket h;
  struct TPIUDecoder t;
  struct TPIUPacket p;
} _r;

/* Structure for parameters passed to a task thread */
struct _runThreadParams 

{
  int portNo;
  int listenHandle;
};

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Mechanism for handling the set of Fifos
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================


static void *_runFifo(void *arg)

/* This is the control loop for the channel fifos */

{
  struct _runThreadParams *params=(struct _runThreadParams *)arg;
  struct ITMPacket p;

  char constructString[MAX_STRING_LENGTH];
  int fifo;
  int readDataLen, writeDataLen;

  if (mkfifo(_r.c[params->portNo].fifoName,0666)<0)
    {
      return NULL;
    }

  /* Don't kill this sub-process when any reader or writer evaporates */
  signal(SIGPIPE, SIG_IGN);

  while (1)
    {
      /* This is the child */
      fifo=open(_r.c[params->portNo].fifoName,O_WRONLY);

      while (1)
	{
	  /* ....get the packet */
	  readDataLen=read(params->listenHandle, &p, sizeof(struct ITMPacket));

	  if (readDataLen!=sizeof(struct ITMPacket))
	    {
	      return NULL;
	    }

	  writeDataLen=snprintf(constructString,MAX_STRING_LENGTH,options.channel[params->portNo].presFormat,(*(uint32_t *)p.d));
	  if (write(fifo,constructString,(writeDataLen<MAX_STRING_LENGTH)?writeDataLen:MAX_STRING_LENGTH)<=0)
	    {
	      break;
	    }
	}
      close(fifo);
    }
  return NULL;
}
// ====================================================================================================
static BOOL _makeFifoTasks(void)

/* Create each sub-process that will handle a port */

{
  struct _runThreadParams *params;
  int f[2];

  /* Cycle through channels and create a fifo for each one that is enabled */
  for (int t=0; t<NUM_CHANNELS; t++)
    {
      if (options.channel[t].chanName)
	{
	  if (pipe(f)<0)
	    return FALSE;
	  fcntl(f[1],F_SETFL,O_NONBLOCK);
	  _r.c[t].handle=f[1];

	  params=(struct _runThreadParams *)malloc(sizeof(struct _runThreadParams));
	  params->listenHandle=f[0];
	  params->portNo=t;

	  _r.c[params->portNo].fifoName=(char *)malloc(strlen(options.channel[t].chanName)+strlen(options.chanPath)+2);
	  strcpy(_r.c[params->portNo].fifoName,options.chanPath);
	  strcat(_r.c[params->portNo].fifoName,options.channel[t].chanName);

	  if (pthread_create(&(_r.c[t].thread),NULL,&_runFifo,params))
	    {
	      return FALSE;
	    }
	}
    }
  return TRUE;
}
// ====================================================================================================
static void _removeFifoTasks(void)

/* Destroy the per-port sub-processes */

{
  for (int t=0; t<NUM_CHANNELS; t++)
    {
      if (_r.c[t].handle>0)
	{
	  close(_r.c[t].handle);
	  unlink(_r.c[t].fifoName);
	}
    }
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Network server implementation for raw SWO feed
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
static void *_client(void *args)

/* Handle an individual network client account */

{
  struct nwClientParams *params=(struct nwClientParams *)args;
  int readDataLen;
  uint8_t maxTransitPacket[MAX_IP_PACKET_LEN];

  while (1)
    {
      readDataLen=read(params->listenHandle, &maxTransitPacket, MAX_IP_PACKET_LEN);
      if (write(params->portNo,&maxTransitPacket,readDataLen)<0)
	{
	  /* This port went away, so remove it */
	  close(params->portNo);
	  close(params->listenHandle);
	  if (params->client->prevClient)
	    {
	      params->client->prevClient->nextClient=params->client->nextClient;
	    }
	  else
	    {
	      _r.firstClient=params->client->nextClient;
	    }

	  if (params->client->nextClient)
	    {
	      params->client->nextClient->prevClient=params->client->prevClient;
	    }
	  return NULL;
	}
    }
}
// ====================================================================================================
static void *_listenTask(void *arg)

{
  uint32_t *sockfd=(uint32_t *)arg;
  uint32_t newsockfd;
  socklen_t clilen;
  struct sockaddr_in cli_addr;
  int f[2];                               /* File descriptor set for pipe */
  struct nwClientParams *params;

  while (1)
    {
      listen(*sockfd,5);
      clilen = sizeof(cli_addr);
      newsockfd = accept(*sockfd,(struct sockaddr *) &cli_addr, &clilen);

      /* We got a new connection - spawn a thread to handle it */
      if (!pipe(f))
	{
	  params=(struct nwClientParams *)malloc(sizeof(struct nwClientParams));

	  params->client = (struct nwClient *)malloc(sizeof(struct nwClient));
	  params->client->handle=f[1];
	  params->listenHandle=f[0];
	  params->portNo=newsockfd;

	  if (!pthread_create(&(params->client->thread),NULL,&_client,params))
	    {
	      /* Auto-cleanup for this thread */
	      pthread_detach(params->client->thread);

	      /* Hook into linked list */
	      params->client->nextClient=_r.firstClient;
	      params->client->prevClient=NULL;
	      if (params->client->nextClient)
		{
		  params->client->nextClient->prevClient=params->client;
		}
	      _r.firstClient=params->client;
	    }
	}
    }
}
// ====================================================================================================
static BOOL _makeServerTask(void)

/* Creating the listening server thread */

{
  int sockfd;
  struct sockaddr_in serv_addr;
  int flag=1;

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof(flag));
  if (sockfd < 0) 
    {
      fprintf(stderr,"Error opening socket\n");
      return FALSE;
    }

  bzero((char *) &serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(SERVER_PORT);
  if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) 
    {
      fprintf(stderr,"Error on binding\n");
      return FALSE;
    }

  /* We have the listening socket - spawn a thread to handle it */
  if (pthread_create(&(_r.ipThread),NULL,&_listenTask,&sockfd))
    {
      fprintf(stderr,"Failed to create listening thread\n");
      return FALSE;
    }
  return TRUE;
}
// ====================================================================================================
static void _sendToClients(uint32_t len, uint8_t *buffer)

{
  struct nwClient *n=_r.firstClient;

  while (n)
    {
      write(n->handle,buffer,len);
      n=n->nextClient;
    }
}
// ====================================================================================================
void _handleException(struct ITMDecoder *i, struct ITMPacket *p)

{
  uint32_t exceptionNumber=((p->d[1]&0x01)<<8)|p->d[0];
  uint32_t eventType=p->d[1]>>4;

  const char *exNames[]={"Thread","Reset","NMI","HardFault","MemManage","BusFault","UsageFault","UNKNOWN_7",
      "UNKNOWN_8", "UNKNOWN_9", "UNKNOWN_10", "SVCall", "Debug Monitor", "UNKNOWN_13", "PendSV", "SysTick"};
  const char *exEvent[]={"Enter","Exit","Resume"};
#ifdef PRINT_EXPERIMENTAL
  printf("%lld,%s,%s\n",i->timeStamp,exEvent[eventType],exNames[exceptionNumber]);
#endif
}
// ====================================================================================================
void _handleDWTEvent(struct ITMDecoder *i, struct ITMPacket *p)

{
  uint32_t event=p->d[1]&0x2F;
  const char *evName[]={"CPI","Exc","Sleep","LSU","Fold","Cyc"};

#ifdef PRINT_EXPERIMENTAL
  printf("%lld,",i->timeStamp);
  for (uint32_t i=0; i<6; i++)
    {
      if (event&(1<<i))
	{
	  printf("%s ",evName[event]);
	}
    }
  printf("\n");
#endif
}
// ====================================================================================================
void _handlePCSample(struct ITMDecoder *i, struct ITMPacket *p)

{
  uint32_t pc=(p->d[3]<<24)|(p->d[2]<<16)|(p->d[1]<<8)|(p->d[0]);
}
// ====================================================================================================
void _handleDataRWWP(struct ITMDecoder *i, struct ITMPacket *p)

{
  uint32_t comp=(p->d[0]&0x30)>>4; 
  BOOL isWrite = ((p->d[0]&0x08)!=0);
  uint32_t data;
  
  switch(p->len)
    {
    case 1:
      data=p->d[1];
      break;
    case 2:
      data=(p->d[1])|((p->d[2])<<8);
      break;
  default:
    data=(p->d[1])|((p->d[2])<<8)|((p->d[3])<<16)|((p->d[4])<<24);
    break;
    }
#ifdef PRINT_EXPERIMENTAL
  printf("Watchpoint %d %s 0x%x\n",comp,isWrite?"Write":"Read",data);
#endif
}
// ====================================================================================================
void _handleDataAccessWP(struct ITMDecoder *i, struct ITMPacket *p)

{
  uint32_t comp=(p->d[0]&0x30)>>4; 
  uint32_t data=(p->d[1])|((p->d[2])<<8)|((p->d[3])<<16)|((p->d[4])<<24);
#ifdef PRINT_EXPERIMENTAL
  printf("Access Watchpoint %d 0x%08x\n",comp,data);
#endif
}
// ====================================================================================================
void _handleDataOffsetWP(struct ITMDecoder *i, struct ITMPacket *p)

{
  uint32_t comp=(p->d[0]&0x30)>>4; 
  uint32_t offset=(p->d[1])|((p->d[2])<<8);
#ifdef PRINT_EXPERIMENTAL
  printf("Offset Watchpoint %d 0x????%04x\n",comp,offset);
#endif
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Handlers for each message type
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
void _handleSW(struct ITMDecoder *i)

{
  struct ITMPacket p;

  if (ITMGetPacket(i, &p))
    {
      if ((p.srcAddr<NUM_CHANNELS) && (_r.c[p.srcAddr].handle))
	{
	  write(_r.c[p.srcAddr].handle,&p,sizeof(struct ITMPacket));
	}
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
      if ((p.d[0]&0xC4) == 0x84)
	{
	  _handleDataRWWP(i, &p);
	}
      else
	if ((p.d[0]&0xCF) == 0x47)
	  {
	    _handleDataAccessWP(i, &p);
	  }
	else
	  if ((p.d[0]&0xCF) == 0x4E)	  
	    {
	      _handleDataOffsetWP(i, &p);
	    }
      break;
      // --------------
    }
}
// ====================================================================================================
void _handleXTN(struct ITMDecoder *i)

{
  struct ITMPacket p;

  if (ITMGetPacket(i, &p))
    {
      printf("XTN len=%d (%02x)\n",p.len,p.d[0]);
    }
  else
    {
      printf("GET FAILED\n");
    }
}
// ====================================================================================================
void _handleTS(struct ITMDecoder *i)

{
  struct ITMPacket p;
  uint32_t stamp=0;

  if (ITMGetPacket(i, &p))
    {
      if (!(p.d[0]&0x80))
	{
	  stamp=p.d[0]>>4;
	}
      else
	{
	  i->timeStatus=(p.d[0]&0x30) >> 4;
	  stamp=(p.d[1])&0x7f;
	  if (p.len>2)
	    {
	      stamp|=(p.d[2])<<7;
	      if (p.len>3)
		{
		  stamp|=(p.d[3]&0x7F)<<14;
		  if (p.len>4)
		    {
		      stamp|=(p.d[4]&0x7f)<<21;
		    }
		}
	    }
	}

      i->timeStamp+=stamp;
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

    case ITM_EV_TS_PACKET_RXED:
      _handleTS(&_r.i);
      break;

    case ITM_EV_SW_PACKET_RXED:
      _handleSW(&_r.i);
      break;

    case ITM_EV_HW_PACKET_RXED:
      _handleHW(&_r.i);
      break;

    case ITM_EV_XTN_PACKET_RXED:
      _handleXTN(&_r.i);
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

	  _sendToClients(_r.p.len, (uint8_t *)_r.p.packet);

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
void intHandler(int dummy) 

{
  exit(0);
}
// ====================================================================================================
void _printHelp(char *progName)

{
  printf("Useage: %s <dhnv> <b basedir> <p port> <s speed>\n",progName);
  printf("        b: <basedir> for channels\n");
  printf("        c: <Number>,<Name>,<Format> of channel to populate (repeat per channel)\n");
  printf("        h: This help\n");
  printf("        f: <filename> Take input from specified file\n");
  printf("        i: <channel> Set ITM Channel in TPIU decode (defaults to 1)\n");
  printf("        p: <serialPort> to use\n");
  printf("        s: <serialSpeed> to use\n");
  printf("        t: Use TPIU decoder\n");
  printf("        v: Verbose mode\n");
}
// ====================================================================================================
int _processOptions(int argc, char *argv[])

{
  int c;
  char *chanConfig;
  uint chan;
  char *chanIndex;
#define DELIMITER ','

  while ((c = getopt (argc, argv, "vts:i:p:f:hc:b:")) != -1)
    switch (c)
      {
	/* Config Information */
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
	options.port=optarg;
	break;
      case 'f':
	options.file=optarg;
	break;
      case 's':
	options.speed=atoi(optarg);
	break;
      case 'h':
	_printHelp(argv[0]);
	return FALSE;

	/* Individual channel setup */
      case 'c':
	chanIndex=chanConfig=strdup(optarg);
	chan=atoi(optarg);
	if (chan>=NUM_CHANNELS)
	  {
	    fprintf(stderr,"Channel index out of range\n");
	    return FALSE;
	  }

	/* Scan for start of filename */
	while ((*chanIndex) && (*chanIndex!=DELIMITER)) chanIndex++;
	if (!*chanIndex)
	  {
	    fprintf(stderr,"No filename for channel %d\n",chan);
	    return FALSE;
	  }
	options.channel[chan].chanName=++chanIndex;

	/* Scan for format */
	while ((*chanIndex) && (*chanIndex!=DELIMITER)) chanIndex++;	
	if (!*chanIndex)
	  {
	    fprintf(stderr,"No output format for channel %d\n",chan);
	    return FALSE;
	  }
	*chanIndex++=0;
	options.channel[chan].presFormat=strdup(GenericsUnescape(chanIndex));
	break;

      case 'b':
        options.chanPath = optarg;
        break;

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

#ifdef PRINT_EXPERIMENTAL
  printf("*****Experimental and part developed features enabled\n");
#endif

  if (options.verbose)
    {
      fprintf(stdout,"Orbuculum V" VERSION " (Git %08X %s, Built " BUILD_DATE ")\n", GIT_HASH,(GIT_DIRTY?"Dirty":"Clean"));

      fprintf(stdout,"Verbose   : TRUE\n");
      fprintf(stdout,"BasePath  : %s\n",options.chanPath);

      if (options.port)
	{
	  fprintf(stdout,"Serial Port: %s\nSerial Speed: %d\n",options.port,options.speed);    
	}

      if (options.useTPIU)
	{
	  fprintf(stdout,"Using TPIU: TRUE (ITM on channel %d)\n",options.tpiuITMChannel);
	}
      if (options.file)
	{
	  fprintf(stdout,"Input File: %s\n",options.file);
	}
      fprintf(stdout,"Channels  :\n");
      for (int g=0; g<NUM_CHANNELS; g++)
	{
	  if (options.channel[g].chanName)
	    {
	      fprintf(stdout,"        %02d [%s] [%s]\n",g,GenericsEscape(options.channel[g].presFormat),options.channel[g].chanName);
	    }
	}
    }
  if ((options.file) && (options.port))
    {
      fprintf(stdout,"Cannot specify file and port at same time\n");
      return FALSE;
    }

  return TRUE;
}
// ====================================================================================================
int usbFeeder(void)

{

  unsigned char cbw[TRANSFER_SIZE];
  libusb_device_handle *handle;
  libusb_device *dev;
  int size;

  while (1)
    {
      if (libusb_init(NULL) < 0)
	{
	  fprintf(stderr,"Failed to initalise USB interface\n");
	  return (-1);
	}

      while (!(handle = libusb_open_device_with_vid_pid(NULL, VID, PID)))
	{
	  usleep(500000);
	}

      if (!(dev = libusb_get_device(handle)))
	continue;

      if (libusb_kernel_driver_active(handle, 0))
	{
	  libusb_detach_kernel_driver(handle, 0);
	}
      
      if (libusb_claim_interface (handle, INTERFACE)<0)
	continue;

      while (0==libusb_bulk_transfer(handle, ENDPOINT, cbw, TRANSFER_SIZE, &size, 10))
	{
	  unsigned char *c=cbw;
	  while (size--)
	    {
	      _protocolPump(*c++);
	    }
	}
      libusb_close(handle);
    }
}
// ====================================================================================================
int serialFeeder(void)

{
  int f;
  unsigned char cbw[TRANSFER_SIZE];
  ssize_t t;
  struct termios settings;

  while (1)
    {
      while ((f=open(options.port,O_RDONLY))<0)
	{
	  if (options.verbose)
	    {
	      fprintf(stderr,"Can't open serial port\n");
	    }
	  usleep(500000);
	}

      if (options.verbose)
	{
	  fprintf(stderr,"Port opened\n");
	}

      if (tcgetattr(f, &settings) <0)
	{
	  perror("tcgetattr");
	  return(-3);
	}

      if (cfsetspeed(&settings, options.speed)<0)
	{
	  perror("Setting input speed");
	  return -3;
	}
      settings.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
      settings.c_cflag &= ~PARENB; /* no parity */
      settings.c_cflag &= ~CSTOPB; /* 1 stop bit */
      settings.c_cflag &= ~CSIZE;
      settings.c_cflag |= CS8 | CLOCAL; /* 8 bits */
      settings.c_oflag &= ~OPOST; /* raw output */

      if (tcsetattr(f, TCSANOW, &settings)<0)
	{
	  fprintf(stderr,"Unsupported baudrate\n");
	  exit(-3);
	}

      tcflush(f, TCOFLUSH);

      while ((t=read(f,cbw,TRANSFER_SIZE))>0)
	{
	  unsigned char *c=cbw;
	  while (t--)
	    _protocolPump(*c++);
	}
      if (options.verbose)
	{
	  fprintf(stderr,"Read failed\n");
	}
      close(f);
    }
}
// ====================================================================================================
int fileFeeder(void)

{
  int f;
  unsigned char cbw[TRANSFER_SIZE];
  ssize_t t;

  if ((f=open(options.file,O_RDONLY))<0)
    {
      if (options.verbose)
	{
	  fprintf(stderr,"Can't open file %s\n",options.file);
	}
      exit(-4);
    }

  if (options.verbose)
    {
      fprintf(stdout,"Reading from file\n");
    }

  while ((t=read(f,cbw,TRANSFER_SIZE))>0)
    {
      unsigned char *c=cbw;
      while (t--)
	{
	  _protocolPump(*c++);
	}
    }
  if (options.verbose)
    {
      fprintf(stdout,"File read\b");
    }
  close(f);
  return TRUE;
}
// ====================================================================================================
int main(int argc, char *argv[])

{
  if (!_processOptions(argc,argv))
    {
      exit(-1);
    }

  atexit(_removeFifoTasks);

  /* Reset the TPIU handler before we start */
  TPIUDecoderInit(&_r.t,!(options.file==NULL));
  ITMDecoderInit(&_r.i,!(options.file==NULL));

  /* This ensures the atexit gets called */
  signal(SIGINT, intHandler);
  if (!_makeFifoTasks())
    {
      fprintf(stderr,"Failed to make channel devices\n");
      exit(-1);
    }

  if (!_makeServerTask())
    {
      fprintf(stderr,"Failed to make network server\n");
      exit(-1);
    }

  /* Using the exit construct rather than return ensures the atexit gets called */
  if (options.port)
    exit(serialFeeder());

  if (options.file)
    exit(fileFeeder());

  exit(usbFeeder());
}
// ====================================================================================================
