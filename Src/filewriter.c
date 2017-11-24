/*
 * Filewriter for ITM channels
 * ===========================
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

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "itmDecoder.h"
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
    enum FWverbLevel v;           /* How loud do we want to be? */
} _f;

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Internal Routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
void _report( enum FWverbLevel l, const char *fmt, ... )

/* Debug reporting stream */

{
    static char op[MAX_STRLEN];

    if ( l <= _f.v )
    {
        va_list va;
        va_start( va, fmt );
        vsnprintf( op, MAX_STRLEN, fmt, va );
        va_end( va );
        fputs( op, stdout );
    }
}
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

    _report( FW_V_DEBUG, "Complete name to work with is [%s]" EOL, workingName );

    /* OK, now decide what to do... */
    switch ( _f.file[n].s )
    {
        // -----------------------
        case FW_STATE_GETNAMEA:     // This is a file append operation
            _f.file[n].f = fopen( workingName, "ab+" );

            if ( _f.file[n].f )
            {
                _report( FW_V_INFO, "File [%s] opened for append on descriptor %d" EOL, workingName, n );
                _f.file[n].s = FW_STATE_OPEN;
            }
            else
            {
                _report( FW_V_WARN, "Failed to open [%s] for append" EOL, workingName );
                memset( _f.file[n].name, 0, MAX_FILENAMELEN );
                _f.file[n].s = FW_STATE_CLOSED;
            }

            break;

        // -----------------------
        case FW_STATE_GETNAMEE:     // This is a file replacement operation
            _f.file[n].f = fopen( workingName, "wb+" );

            if ( _f.file[n].f )
            {
                _report( FW_V_INFO, "File [%s] opened for write on descriptor %d" EOL, workingName, n );
                _f.file[n].s = FW_STATE_OPEN;
            }
            else
            {
                _report( FW_V_WARN, "Failed to open [%s] for write" EOL, workingName );
                memset( _f.file[n].name, 0, MAX_FILENAMELEN );
                _f.file[n].s = FW_STATE_CLOSED;
            }

            break;

        // -----------------------
        case FW_STATE_UNLINK:     // this is a file delete operation
            if ( !unlink( workingName ) )
            {
                _report( FW_V_INFO, "Removed file [%s]" EOL, workingName );
            }
            else
            {
                _report( FW_V_WARN, "Failed to remove file [%s]" EOL, workingName );
            }

            memset( _f.file[n].name, 0, MAX_FILENAMELEN );
            _f.file[n].s = FW_STATE_CLOSED;
            break;

        // -----------------------
        default:
            _report( FW_V_WARN, "Unexpected state %d while getting filename" EOL, _f.file[n].s );
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
                _report( FW_V_WARN, "Attempt to write an overlong filename [%s]" EOL, _f.file[n].name );
            }

            d++;
        }
        else
        {
            _report( FW_V_DEBUG, "Got complete name [%s]" EOL, _f.file[n].name );
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
            _report( FW_V_DEBUG, "Attempt to open or create file" EOL );

            if ( _f.file[FW_GET_FILEID( c )].f )
            {
                /* There was a file open, close it */
                _report( FW_V_WARN, "Attempt to write to descriptor %d while open writing %s" EOL, FW_GET_FILEID( c ),
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
                _report( FW_V_INFO, "Attempt to close descriptor %d while not open" EOL, FW_GET_FILEID( c ) );
            }
            else
            {
                _report( FW_V_DEBUG, "Close descriptor %d (%s)" EOL, FW_GET_FILEID( c ), _f.file[FW_GET_FILEID( c )].name );
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
                _report( FW_V_WARN, "Attempt to use open descriptor %d to erase a file" EOL, FW_GET_FILEID( c ) );
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
                _report( FW_V_WARN, "Request for write on descriptor %d while file closed" EOL, FW_GET_FILEID( c ) );
            }
            else
            {
                if ( _f.file[FW_GET_FILEID( c )].s != FW_STATE_OPEN )
                {
                    _handleNameBytes( FW_GET_FILEID( c ), FW_GET_BYTES( c ), &( p->d[1] ) );
                }
                else
                {
                    _report( FW_V_DEBUG, "Wrote %d bytes on descriptor %d" EOL, FW_GET_BYTES( c ), FW_GET_FILEID( c ) );
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
bool filewriterInit( char *basedir, enum FWverbLevel vSet )

/* Initialise the filewriter */

{
    _f.initialised = true;
    _f.basedir     = basedir;
    _f.v           = vSet;
    _report( FW_V_DEBUG, "Filewriter initialised" EOL );
    return true;
}
// ====================================================================================================
