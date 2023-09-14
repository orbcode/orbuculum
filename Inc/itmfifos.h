/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * ITM Fifo Interface
 * ==================
 *
 */

#ifndef _ITMFIFOS_
#define _ITMFIFOS_

#include "tpiuDecoder.h"
#include "itmDecoder.h"
#include "cobs.h"

#include "generics.h"

#ifdef __cplusplus
extern "C" {
#endif

// ====================================================================================================
#define NUM_CHANNELS  32                     /* Number of channels defined */
#define HW_CHANNEL    (NUM_CHANNELS)         /* Make the hardware fifo on the end of the software ones */
#define HWFIFO_NAME "hwevent"                /* Name for the hardware channel */

struct Channel;
struct itmfifosHandle;

enum Prot { PROT_COBS, PROT_ITM, PROT_TPIU, PROT_UNKNOWN };

/* Fifos running */
void itmfifoForceSync( struct itmfifosHandle *f, bool synced );                  /* Force sync status */
void itmfifoProtocolPump( struct itmfifosHandle *f, uint8_t *c, int len );       /* Send undecoded data to the fifo */

/* Getters and setters */
void itmfifoSetChannel( struct itmfifosHandle *f, int chan, char *n, char *s );
void itmfifoSetChanPath( struct itmfifosHandle *f, char *s );
void itmfifoSetProtocol( struct itmfifosHandle *f, enum Prot p );
void itmfifoSetForceITMSync( struct itmfifosHandle *f, bool s );
void itmfifoSettag( struct itmfifosHandle *f, int tag );
char *itmfifoGetChannelName( struct itmfifosHandle *f, int chan );
char *itmfifoGetChannelFormat( struct itmfifosHandle *f, int chan );
char *itmfifoGetChanPath( struct itmfifosHandle *f );
enum Prot itmfifoGetProtocol( struct itmfifosHandle *f );
struct TPIUCommsStats *itmfifoGetCommsStats( struct itmfifosHandle *f );
struct ITMDecoderStats *itmfifoGetITMDecoderStats( struct itmfifosHandle *f );
bool itmfifoGetForceITMSync( struct itmfifosHandle *f );
int itmfifoGettag( struct itmfifosHandle *f );
void itmfifoUsePermafiles( struct itmfifosHandle *f, bool usePermafilesSet );

/* Filewriting */
void itmfifoFilewriter( struct itmfifosHandle *f, bool useFilewriter, char *workingPath );

/* Fifos management */
bool itmfifoCreate( struct itmfifosHandle *f );                                  /* Create the fifo set */
void itmfifoShutdown( struct itmfifosHandle *f );                                /* Destroy the fifo set */
struct itmfifosHandle *itmfifoInit( bool forceITMSyncSet, enum Prot p, int tag );
// ====================================================================================================
#ifdef __cplusplus
}
#endif
#endif
