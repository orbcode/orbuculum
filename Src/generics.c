/*
 * Generic Routines
 * ================
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


#include <limits.h>
#include "generics.h"

// ====================================================================================================
char *GenericsEscape( char *str )

{
    static char workingBuffer[_POSIX_ARG_MAX];
    char *d = workingBuffer;
    char *s = str;

    do
    {
        switch ( *s )
        {
            case 0:
                break;

            case '\a': /* BEL */
                *d++ = '\\';
                *d++ = 'a';
                break;

            case '\b': /* BACKSPACE */
                *d++ = '\\';
                *d++ = 'b';
                break;

            case '\f': /* FORMFEED */
                *d++ = '\\';
                *d++ = 'f';
                break;

            case '\n': /* NL */
                *d++ = '\\';
                *d++ = 'n';
                break;

            case '\r': /* CR */
                *d++ = '\\';
                *d++ = 'r';
                break;

            case '\t': /* TAB */
                *d++ = '\\';
                *d++ = 't';
                break;

            case '\v': /* VTAB */
                *d++ = '\\';
                *d++ = 'v';
                break;

            default:
                *d++ = *s;
        }
    }
    while ( ( *s++ ) && ( d - workingBuffer < ( _POSIX_ARG_MAX - 1 ) ) );

    *d = 0;
    return workingBuffer;
}
// ====================================================================================================
char *GenericsUnescape( char *str )

{
    static char workingBuffer[_POSIX_ARG_MAX];
    char *d = workingBuffer;
    char *s = str;

    do
    {
        if ( *s == '\\' )
        {
            /* This is an escape....put the correct code in its place */
            s++;

            switch ( *s )
            {
                case 0:
                    break;

                case 'a': /* BEL */
                    *d++ = '\a';
                    break;

                case 'b': /* BACKSPACE */
                    *d++ = '\b';
                    break;

                case 'f': /* FORMFEED */
                    *d++ = '\f';
                    break;

                case 'n': /* NL */
                    *d++ = '\n';
                    break;

                case 'r': /* CR */
                    *d++ = '\r';
                    break;

                case 't': /* TAB */
                    *d++ = '\t';
                    break;

                case 'v': /* VTAB */
                    *d++ = '\v';
                    break;

                case '0'...'7': /* Direct octal number for ASCII Code */
                    *d = 0;

                    while ( ( *s >= '0' ) && ( *s <= '7' ) )
                    {
                        *d = ( ( *d ) * 8 ) + ( ( *s++ ) - '0' );
                    }

                    d++;
                    break;

                default:
                    *d++ = *s;
            }
        }
        else
        {
            *d++ = *s;
        }
    }
    while ( ( *s++ ) && ( d - workingBuffer < ( _POSIX_ARG_MAX - 1 ) ) );

    *d = 0;
    return workingBuffer;
}
// ====================================================================================================
