/*
 * Generic Routines
 * ================
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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include "generics.h"

#define MAX_STRLEN (4096) // Maximum length of debug string

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
static enum verbLevel lstore = V_WARN;

void genericsSetReportLevel( enum verbLevel lset )

{
    lstore = lset;
}
// ====================================================================================================
void genericsReport( enum verbLevel l, const char *fmt, ... )

/* Debug reporting stream */

{
    static char op[MAX_STRLEN];

    if ( l <= lstore )
    {
        va_list va;
        va_start( va, fmt );
        vsnprintf( op, MAX_STRLEN, fmt, va );
        va_end( va );
        fputs( op, stderr );
    }
}
// ====================================================================================================
void genericsExit( int status, const char *fmt, ... )

{
    static char op[MAX_STRLEN];

    va_list va;
    va_start( va, fmt );
    vsnprintf( op, MAX_STRLEN, fmt, va );
    va_end( va );
    fputs( op, stderr );

    exit( status );
}
// ====================================================================================================
