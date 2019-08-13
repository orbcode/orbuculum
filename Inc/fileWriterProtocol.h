/*
 * Filewriter Module
 * =================
 *
 * Copyright (C) 2017, 2019  Dave Marples  <dave@marples.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * * Neither the names Orbtrace, Orbuculum nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
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
