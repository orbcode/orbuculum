/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Network Server support
 * ======================
 *
 */

#ifndef _NW_CLIENT_
#define _NW_CLIENT_

#include "generics.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <semaphore.h>
#include "nw.h"
// ====================================================================================================

struct nwclientsHandle;

// ====================================================================================================

void nwclientSend( struct nwclientsHandle *h, uint32_t len, uint8_t *buffer );

void nwclientShutdown( struct nwclientsHandle *h );
bool nwclientShutdownComplete( struct nwclientsHandle *h );
struct nwclientsHandle *nwclientStart( int port );

// ====================================================================================================
#ifdef __cplusplus
}
#endif
#endif
