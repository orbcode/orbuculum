/*
 * Filewriter Module
 * =================
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

#ifndef _FILEWRITER_H_
#define _FILEWRITER_H_

#include <stdint.h>
#include <stdbool.h>
#include "generics.h"
#include "fileWriterProtocol.h"
#include "itmDecoder.h"

enum FWverbLevel {FW_V_ERROR, FW_V_WARN, FW_V_INFO, FW_V_DEBUG};

// ====================================================================================================
bool filewriterProcess( struct ITMPacket *p );
bool filewriterInit( char *basedir, enum FWverbLevel );
// ====================================================================================================
#endif
