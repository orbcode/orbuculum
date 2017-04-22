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

#define VERSION "0.11"

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


#include "git_version_info.h"
#include "generics.h"
#include "tpiuDecoder.h"
#include "itmDecoder.h"

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

struct
{
  struct channelRuntime c[NUM_CHANNELS];
  struct ITMDecoder i;
  struct ITMSWPacket h;
  struct TPIUDecoder t;
  struct TPIUPacket p;
} _r;

/* Structure for parameters passed to a fifo thread */
struct _runFifoParams 

{
  int portNo;
  int listenHandle;
};

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Internals
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
static void *_runFifo(void *arg)

/* This is the control loop for the channel fifos */

{
  struct _runFifoParams *params=(struct _runFifoParams *)arg;
  struct ITMSWPacket p;

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
	  readDataLen=read(params->listenHandle, &p, sizeof(struct ITMSWPacket));

	  if (readDataLen!=sizeof(struct ITMSWPacket))
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
  struct _runFifoParams *params;
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

	  params=(struct _runFifoParams *)malloc(sizeof(struct _runFifoParams));
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
// Handlers for each message type
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
void _handleSW(struct ITMDecoder *i)

{
  struct ITMSWPacket p;

  if (ITMGetSWPacket(i, &p))
    {
      if ((p.srcAddr<NUM_CHANNELS) && (_r.c[p.srcAddr].handle))
	{
	  write(_r.c[p.srcAddr].handle,&p,sizeof(struct ITMSWPacket));
	}
    }
}
// ====================================================================================================
void _handleHW(struct ITMDecoder *i)

{
  struct ITMSWPacket p;

  if (ITMGetSWPacket(i, &p))
    {
      printf("HW %02x\n",p.srcAddr);
    }
}
// ====================================================================================================
void _handleXTN(struct ITMDecoder *i)

{
  struct ITMSWPacket p;

  if (ITMGetSWPacket(i, &p))
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
  struct ITMSWPacket p;

  if (ITMGetSWPacket(i, &p))
    {
      printf("Timestamp (len=%d)\n",p.len);
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

  /* Using the exit construct rather than return ensures the atexit gets called */
  if (options.port)
    exit(serialFeeder());

  if (options.file)
    exit(fileFeeder());

  exit(usbFeeder());
}
// ====================================================================================================
