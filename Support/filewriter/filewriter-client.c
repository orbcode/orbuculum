/*
 * A simple filewriter talking to an Orbuculum session at the other end.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "stm32f4xx.h"
#include "filewriter-client.h"
#include "fileWriterProtocol.h"

static bool isInUse[FW_MAX_FILES];
static bool _initialised;
// ============================================================================================
// ============================================================================================
// ============================================================================================
// Internal Routines
// ============================================================================================
// ============================================================================================
// ============================================================================================
int32_t _getHandle(void)

/* Find a spare handle to use */

{
    uint32_t t;
    for ( t=0; ((t<FW_MAX_FILES) && (isInUse[t])); t++) {}

    return (t<FW_MAX_FILES)?t:-1;
}
// ============================================================================================
void _releaseHandle(uint32_t h)

/* Release a handle that is in use */

{
    if (h<FW_MAX_FILES)
	isInUse[h]=false;
}
// ============================================================================================
void _sendMsg(uint32_t cmd, uint32_t id, uint32_t *len, const uint8_t *d)

/* Send a message to the Orbuculum session */

{
    if (!((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) && /* Trace enabled */
         (ITM->TCR & ITM_TCR_ITMENA_Msk) && /* ITM enabled */
         (ITM->TER & (1ul << FW_CHANNEL) ) /* ITM Port c enabled */
        ))
	return;

    uint32_t c=0;
    uint32_t l;

    if (*len<FW_MAX_SEND)
	{
	    l=*len;
	    *len=0;
	}
    else
	{
	    l=FW_MAX_SEND;
	    *len-=FW_MAX_SEND;
	}

    /* Calculate the command tag */
    cmd=(cmd|FW_BYTES(l)|FW_FILEID(id));

    /* Pack in the individual bytes */
    for (uint32_t b=0; b<l; b++) c|=(*d++)<<(b*8);

    /* ...and send it out */
    while (ITM->PORT[FW_CHANNEL].u32 == 0); // Port available?
    ITM->PORT[FW_CHANNEL].u32 = (c<<8)|cmd; // Write data
}
// ============================================================================================
// ============================================================================================
// ============================================================================================
// Externally Available Routines
// ============================================================================================
// ============================================================================================
// ============================================================================================
uint8_t fwOpenFile(const char *n, bool forAppend)

/* Open a file for append or rewrite */

{
  if (!_initialised) fwInit();
  int32_t handle=_getHandle();
    if (handle>=0)
	{

    uint32_t l=strlen(n)+1;  // +1 ensures terminating 0 is sent

    /* Send indication that we're opening a file */
    _sendMsg(forAppend?FW_CMD_OPENA:FW_CMD_OPENE, handle, &l, n);

    /* Now send any remaining filename */
    while (l)
	{
	    n+=FW_MAX_SEND;
	    _sendMsg(FW_CMD_WRITE, handle, &l, n);
	}
	}
    return handle;
}
// ============================================================================================
uint32_t fwWrite(const char *ptr, size_t size, uint32_t nmemb, uint32_t h)

/* Write to an open file */

{
  nmemb*=size;
  uint32_t r = nmemb;
  
    while (nmemb)
	{
	    _sendMsg(FW_CMD_WRITE, 0, &nmemb, ptr);
	    ptr+=FW_MAX_SEND;
	}
    return r;
}
// ============================================================================================
uint32_t fwClose(uint32_t h)

/* Close an open file */

{
    if (h>=FW_MAX_FILES)
	return false;

    _sendMsg(FW_CMD_CLOSE, h, 0, NULL);
    _releaseHandle(h);

    return true;
}
// ============================================================================================
bool fwDeleteFile(const char *ptr)

/* Delete a file */

{
  if (!_initialised) fwInit();
    int32_t handle=_getHandle();
    if (handle<0)
	{
	return false;
	}
    uint32_t l=strlen(ptr)+1;  // +1 ensures terminating 0 is sent

    /* Send indication that we're deleting a file */
    _sendMsg(FW_CMD_ERASE, handle, &l, ptr);

    /* Now send any remaining filename */
    while (l)
	{
	    ptr+=FW_MAX_SEND;
	    _sendMsg(FW_CMD_WRITE, handle, &l, ptr);
	}

    _releaseHandle(handle);
    return 0;
}
// ============================================================================================
void fwInit(void)

/* Initialise the filewriter */

{
    /* Make sure everything is closed at the other end */
    for (uint32_t t=0; t<FW_MAX_FILES; t++) fwClose(t);
    _initialised=true;
}
// ============================================================================================
