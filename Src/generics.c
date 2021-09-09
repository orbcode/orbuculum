/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Generic Routines
 * ================
 *
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <sys/time.h>
#include "generics.h"

#define MAX_STRLEN (4096) // Maximum length of debug string

// ====================================================================================================
char *genericsEscape( char *str )

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
char *genericsUnescape( char *str )

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
enum verbLevel genericsGetReportLevel( void )

{
    return lstore;
}
// ====================================================================================================
uint64_t genericsTimestampuS( void )

{
    struct timeval te;
    gettimeofday( &te, NULL ); // get current time
    return ( te.tv_sec * 1000000LL + te.tv_usec ); // caculate microseconds
}
// ====================================================================================================
uint32_t genericsTimestampmS( void )

{
    struct timeval te;
    gettimeofday( &te, NULL ); // get current time
    return ( te.tv_sec * 1000LL + ( te.tv_usec / 1000 ) );
}
// ====================================================================================================
void genericsPrintf( const char *fmt, ... )

/* Print to output stream */

{
    static char op[MAX_STRLEN];

    va_list va;
    va_start( va, fmt );
    vsnprintf( op, MAX_STRLEN, fmt, va );
    va_end( va );
    fputs( op, stdout );
}
// ====================================================================================================
const char *genericsBasename( const char *n )

/* Find basename of given path...returns empty string if path ends with / */

{
    const char *p = n + strlen( n );

    while ( ( p != n ) && ( *( p - 1 ) != '/' ) )
    {
        p--;
    }

    return p;
}
// ====================================================================================================
const char *genericsBasenameN( const char *n, int c )

/* Find basename + c path elements of given path...returns path if path ends with / */

{
    const char *p = n + strlen( n );

    while ( ( p != n ) && ( c ) && ( *( p - 1 ) != '/' ) )
    {
        p--;

        if ( *p == '/' )
        {
            c--;
        }
    }

    return p;
}
// ====================================================================================================
void genericsReport( enum verbLevel l, const char *fmt, ... )

/* Debug reporting stream */

{
    static char op[MAX_STRLEN];
    static char *colours[V_MAX_VERBLEVEL] = {C_VERB_ERROR, C_VERB_WARN, C_VERB_INFO, C_VERB_DEBUG};

    if ( l <= lstore )
    {
        fputs( colours[l], stderr );
        va_list va;
        va_start( va, fmt );
        vsnprintf( op, MAX_STRLEN, fmt, va );
        va_end( va );
        fputs( op, stderr );
        fputs( C_RESET, stderr );
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
    fputs( C_VERB_ERROR, stderr );
    fputs( op, stderr );
    fputs( C_RESET, stderr );

    exit( status );
}
// ====================================================================================================
