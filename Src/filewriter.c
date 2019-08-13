/*
 * Filewriter for ITM channels
 * ===========================
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

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "itmDecoder.h"
#include "generics.h"
#include "fileWriter.h"

enum fwState { FW_STATE_CLOSED, FW_STATE_GETNAMEA, FW_STATE_GETNAMEE, FW_STATE_UNLINK, FW_STATE_OPEN };

#define MAX_CONCAT_FILENAMELEN 4096
#define MAX_FILENAMELEN 1024
#define MAX_STRLEN 4096

static struct
{
    struct
    {
        enum fwState s;                     /* Current state of the handle */
        FILE        *f;                     /* Handle for the handle */
        char         name[MAX_FILENAMELEN]; /* Filename */
    } file[FW_MAX_FILES];

    char            *basedir;     /* Where we are going to put everything */
    bool             initialised; /* Have we been initialised? */
} _f;

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Internal Routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
void _processCompleteName( uint32_t n )

/* We got the whole name from the remote end, so process it */

{
    char workingName[MAX_CONCAT_FILENAMELEN] = { 0 };

    /* Concat strings */
    if ( _f.basedir )
    {
        strncpy( workingName, _f.basedir, MAX_CONCAT_FILENAMELEN );
        strncat( workingName, _f.file[n].name, MAX_CONCAT_FILENAMELEN );
    }
    else
    {
        strncpy( workingName, _f.file[n].name, MAX_CONCAT_FILENAMELEN );
    }

    genericsReport( V_DEBUG, "Complete name to work with is [%s]" EOL, workingName );

    /* OK, now decide what to do... */
    switch ( _f.file[n].s )
    {
        // -----------------------
        case FW_STATE_GETNAMEA:     // This is a file append operation
            _f.file[n].f = fopen( workingName, "ab+" );

            if ( _f.file[n].f )
            {
                genericsReport( V_INFO, "File [%s] opened for append" EOL, workingName, n );
                _f.file[n].s = FW_STATE_OPEN;
            }
            else
            {
                genericsReport( V_WARN, "Failed to open [%s] for append" EOL, workingName );
                memset( _f.file[n].name, 0, MAX_FILENAMELEN );
                _f.file[n].s = FW_STATE_CLOSED;
            }

            break;

        // -----------------------
        case FW_STATE_GETNAMEE:     // This is a file replacement operation
            _f.file[n].f = fopen( workingName, "wb+" );

            if ( _f.file[n].f )
            {
                genericsReport( V_INFO, "File [%s] opened for write" EOL, workingName, n );
                _f.file[n].s = FW_STATE_OPEN;
            }
            else
            {
                genericsReport( V_WARN, "Failed to open [%s] for write" EOL, workingName );
                memset( _f.file[n].name, 0, MAX_FILENAMELEN );
                _f.file[n].s = FW_STATE_CLOSED;
            }

            break;

        // -----------------------
        case FW_STATE_UNLINK:     // this is a file delete operation
            if ( !unlink( workingName ) )
            {
                genericsReport( V_INFO, "Removed file [%s]" EOL, workingName );
            }
            else
            {
                genericsReport( V_WARN, "Failed to remove file [%s]" EOL, workingName );
            }

            memset( _f.file[n].name, 0, MAX_FILENAMELEN );
            _f.file[n].s = FW_STATE_CLOSED;
            break;

        // -----------------------
        default:
            genericsReport( V_WARN, "Unexpected state %d while getting filename" EOL, _f.file[n].s );
            break;
            // -----------------------
    }
}
// ====================================================================================================
void _handleNameBytes( uint32_t n, uint8_t h, uint8_t *d )

/* Collect the name of the file we're going to do something with */

{
    /* Spin through the received bytes and append them to the filename string */
    while ( h-- )
    {
        if ( *d )
        {
            if ( strlen( _f.file[n].name ) < MAX_FILENAMELEN - 2 )
            {
                _f.file[n].name[strlen( _f.file[n].name )] = *d;
            }
            else
            {
                genericsReport( V_WARN, "Attempt to write an overlong filename [%s]" EOL, _f.file[n].name );
            }

            d++;
        }
        else
        {
            genericsReport( V_DEBUG, "Got complete name [%s]" EOL, _f.file[n].name );
            _processCompleteName( n );
            break;
        }
    }
}
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Externally available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
bool filewriterProcess( struct ITMPacket *p )

/* Handle an ITM frame targetted at the filewriter */

{
    uint8_t c = p->d[0]; /* Extract the control word for convinience */

    switch ( FW_MASK_COMMAND( c ) )
    {
        // -----------------------
        case FW_CMD_OPENA:     // Open file for appending write
        case FW_CMD_OPENE:     // Open file for empty write (i.e. flush and write)
            genericsReport( V_DEBUG, "Attempt to open or create file" EOL );

            if ( _f.file[FW_GET_FILEID( c )].f )
            {
                /* There was a file open, close it */
                genericsReport( V_WARN, "Attempt to write to descriptor %d while open writing %s" EOL, FW_GET_FILEID( c ),
                                _f.file[FW_GET_FILEID( c )].name );
                fclose( _f.file[FW_GET_FILEID( c )].f );
                _f.file[FW_GET_FILEID( c )].f = NULL;
                _f.file[FW_GET_FILEID( c )].f = NULL;
            }

            memset( _f.file[FW_GET_FILEID( c )].name, 0, MAX_FILENAMELEN );

            /* Start collecting the name */
            if ( FW_MASK_COMMAND( c ) == FW_CMD_OPENA )
            {
                _f.file[FW_GET_FILEID( c )].s = FW_STATE_GETNAMEA;
            }
            else
            {
                _f.file[FW_GET_FILEID( c )].s = FW_STATE_GETNAMEE;
            }

            _handleNameBytes( FW_GET_FILEID( c ), FW_GET_BYTES( c ), &( p->d[1] ) );
            break;

        // -----------------------

        case FW_CMD_CLOSE:     // Close file
            if ( !_f.file[FW_GET_FILEID( c )].f )
            {
                /* There was no file open, complain */
                genericsReport( V_DEBUG, "Attempt to close descriptor %d while not open" EOL, FW_GET_FILEID( c ) );
            }
            else
            {
                genericsReport( V_INFO, "Close %s" EOL,  _f.file[FW_GET_FILEID( c )].name );
                fclose( _f.file[FW_GET_FILEID( c )].f );
                _f.file[FW_GET_FILEID( c )].f = NULL;
                memset( _f.file[FW_GET_FILEID( c )].name, 0, MAX_FILENAMELEN );
                _f.file[FW_GET_FILEID( c )].s = FW_STATE_CLOSED;
            }

            break;

        // -----------------------

        case FW_CMD_ERASE:     // Erase file
            if ( _f.file[FW_GET_FILEID( c )].s != FW_STATE_CLOSED )
            {
                genericsReport( V_WARN, "Attempt to use open descriptor %d to erase a file" EOL, FW_GET_FILEID( c ) );
            }
            else
            {
                memset( _f.file[FW_GET_FILEID( c )].name, 0, MAX_FILENAMELEN );
                _f.file[FW_GET_FILEID( c )].s = FW_STATE_UNLINK;
                _handleNameBytes( FW_GET_FILEID( c ), FW_GET_BYTES( c ), &( p->d[1] ) );
            }

            break;

        // -----------------------

        case FW_CMD_WRITE:     // Write to file
            if ( _f.file[FW_GET_FILEID( c )].s == FW_STATE_CLOSED )
            {
                genericsReport( V_WARN, "Request for write on descriptor %d while file closed" EOL, FW_GET_FILEID( c ) );
            }
            else
            {
                if ( _f.file[FW_GET_FILEID( c )].s != FW_STATE_OPEN )
                {
                    _handleNameBytes( FW_GET_FILEID( c ), FW_GET_BYTES( c ), &( p->d[1] ) );
                }
                else
                {
                    genericsReport( V_DEBUG, "Wrote %d bytes on descriptor %d" EOL, FW_GET_BYTES( c ), FW_GET_FILEID( c ) );
                    fwrite( &( p->d[1] ), 1, FW_GET_BYTES( c ), _f.file[FW_GET_FILEID( c )].f );
                }
            }

            break;

        // -----------------------

        default:
        case FW_CMD_NULL:
            break;
    }

    return true;
}
// ====================================================================================================
bool filewriterInit( char *basedir )

/* Initialise the filewriter */

{
    _f.initialised = true;
    _f.basedir     = basedir;
    genericsReport( V_DEBUG, "Filewriter initialised" EOL );
    return true;
}
// ====================================================================================================
