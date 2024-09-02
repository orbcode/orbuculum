/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Network support
 * ===============
 *
 */

#ifndef _NW_
#define _NW_

#include "generics.h"

#ifdef __cplusplus
extern "C" {
#endif

// ====================================================================================================

#define OFCLIENT_SERVER_PORT (3402)           /* orbflow server port definition */
#define NWCLIENT_SERVER_PORT (3443)           /* legacy server port definition */
#define LEGACY_SERVER_PORT_OFS (NWCLIENT_SERVER_PORT-OFCLIENT_SERVER_PORT)

#define TRANSFER_SIZE (65536*4)

#define DEFAULT_ITM_STREAM 1
#define DEFAULT_ETM_STREAM 2
// ====================================================================================================

#ifdef __cplusplus
}
#endif
#endif
