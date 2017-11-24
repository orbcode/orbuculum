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

#ifndef _FILEWRITER_PROT_H_
#define _FILEWRITER_PROT_H_

// Structure of the command byte;
// NN CCC FFF
//
// NN  - Number of bytes following in this frame (0,1 or 2)
//       NN=3 is reserved
// CCC - Command
// FFF - File Number
//

#define FW_CHANNEL    (29)   // ITM Channel to be used
#define FW_MAX_FILES  (8)    // Number of files we support

#define FW_MAX_SEND   (3)    // Maximum number of bytes in a single ITM frame

/* Masks and shifts to get the correct bits out of the command word */
#define FW_FILEID(x)  ((x)&7)
#define FW_GET_FILEID(x) FW_FILEID(x)

#define FW_COMMAND(x) (((x)&7)<<3)
#define FW_MASK_COMMAND(x) ((x)&FW_COMMAND(7))

#define FW_BYTES(x)   (((x)&3)<<6)
#define FW_GET_BYTES(x) (((x)>>6)&3)

/* The various commands that are available */
#define FW_CMD_NULL   FW_COMMAND(0)
#define FW_CMD_OPENA  FW_COMMAND(1)
#define FW_CMD_OPENE  FW_COMMAND(2)
#define FW_CMD_CLOSE  FW_COMMAND(3)
#define FW_CMD_ERASE  FW_COMMAND(4)
#define FW_CMD_WRITE  FW_COMMAND(5)

#endif
