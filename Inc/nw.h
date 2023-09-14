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

#define OTCLIENT_SERVER_PORT (3402)           /* ORBTrace COBS server port definition */
#define NWCLIENT_SERVER_PORT (3443)           /* Server port definition */
#define TRANSFER_SIZE (65536*4)

#define DEFAULT_ITM_STREAM 1
#define DEFAULT_ETM_STREAM 2
// ====================================================================================================

#ifdef __cplusplus
}
#endif
#endif
