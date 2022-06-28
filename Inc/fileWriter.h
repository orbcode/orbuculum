/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Filewriter Module
 * =================
 *
 */

#ifndef _FILEWRITER_H_
#define _FILEWRITER_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "generics.h"
#include "fileWriterProtocol.h"
#include "msgDecoder.h"

// ====================================================================================================
bool filewriterProcess( struct swMsg *m );
bool filewriterInit( char *basedir );
// ====================================================================================================

#ifdef __cplusplus
}
#endif

#endif
