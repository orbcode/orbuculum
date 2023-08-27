/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Generic Routines
 * ================
 *
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#ifdef WIN32
    #include <Windows.h>
#else
    #include <libgen.h>
    #include <unistd.h>
#endif

#include "generics.h"

#if defined(WIN32)
    #define _POSIX_ARG_MAX 4096
#endif

#define MAX_STRLEN (_POSIX_ARG_MAX) // Maximum length of debug string

/* Flag indicating if active screen handling is in use */
bool _screenHandling;

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
static int _htoi( char c )

{
    if ( ( c >= '0' ) && ( c <= '9' ) )
    {
        return c - '0';
    }

    if ( ( c >= 'a' ) && ( c <= 'f' ) )
    {
        return c - 'a' + 10;
    }

    return 0;
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
char *genericsGetBaseDirectory( void )
{
#ifdef WIN32
    size_t currentSize = MAX_PATH;
    char *exePath = (char*)malloc( currentSize );

    while ( true )
    {
        DWORD n = GetModuleFileNameA( NULL, exePath, currentSize );

        if ( n < ( currentSize - 1 ) )
        {
            break;
        }

        currentSize *= 2;
        exePath = (char*)realloc( exePath, currentSize );
    }

    char *dirPath = (char*)malloc( currentSize );
    char drive[_MAX_DRIVE];
    _splitpath_s( exePath, drive, sizeof( drive ), dirPath, currentSize, NULL, 0, NULL, 0 );
    free( exePath );

    char *concatPath = (char*)malloc( strlen( drive ) + strlen( dirPath ) + 1 );
    *concatPath = '\0';
    strcat( concatPath, drive );
    strcat( concatPath, dirPath );
    free( dirPath );
    return concatPath;
#else
    ssize_t currentSize = 256;
    char *exePath = ( char * )malloc( currentSize );

    if ( !exePath )
    {
        return NULL;
    }

    while ( true )
    {
        ssize_t n = readlink( "/proc/self/exe", exePath, currentSize - 1 );

        if ( n == -1 )
        {
            // Failed to resolve path to current executable, let's hope it is not needed to correctly resolve orbtrace path
            // https://stackoverflow.com/a/933996/995351
            strcpy( exePath, "" );
            return exePath;
        }

        if ( n < ( currentSize - 1 ) )
        {
            // readlink does not insert null terminator
            exePath[n] = 0;
            break;
        }

        currentSize *= 2;
        exePath = ( char * )realloc( exePath, currentSize );
    }

    const char *dirPath = dirname( exePath );
    char *path = ( char * )malloc( strlen( dirPath ) + 2 );
    *path = 0;
    strcat( path, dirPath );
    strcat( path, "/" );
    free( exePath );
    return path;
#endif
}
// ====================================================================================================
void genericsPrintf( const char *fmt, ... )

/* Print to output stream */

{
    static char op[MAX_STRLEN];
    char *p = op;

    va_list va;
    va_start( va, fmt );
    vsnprintf( op, MAX_STRLEN, fmt, va );
    va_end( va );

    while ( *p )
    {
        if ( *p != CMD_ALERT[0] )
        {
            putc( *p++, stderr );
        }
        else
        {
            p++;

            switch ( *p )
            {
                case '0'...'9':
                case 'a'...'f':
                    if ( _screenHandling )
                    {
                        fprintf( stderr, CC_COLOUR, _htoi( *p ) > 7, _htoi( *p ) & 7 );
                    }

                    p++;
                    break;

                case 'u':
                    if ( _screenHandling )
                    {
                        fprintf( stderr, CC_PREV_LN );
                    }

                    p++;
                    break;

                case 'U':
                    if ( _screenHandling )
                    {
                        fprintf( stderr, CC_CLR_LN );
                    }

                    p++;
                    break;

                case 'r':
                    if ( _screenHandling )
                    {
                        fprintf( stderr, CC_RES );
                    }

                    p++;
                    break;

                case 'z':
                    /* We'll take a flyer on it being vt100 compatible */
                    fprintf( stderr, CC_CLEAR_SCREEN );
                    p++;
                    break;

                default:
                    break;
            }
        }
    }

    fflush( stdout );
}
// ====================================================================================================
void genericsReport( enum verbLevel l, const char *fmt, ... )

/* Debug reporting stream */

{
    static char op[MAX_STRLEN];
    static const char *colours[V_MAX_VERBLEVEL] = {C_VERB_ERROR, C_VERB_WARN, C_VERB_INFO, C_VERB_DEBUG};

    if ( l <= lstore )
    {
        fflush( stdout );
        genericsPrintf( colours[l] );
        va_list va;
        va_start( va, fmt );
        vsnprintf( op, MAX_STRLEN, fmt, va );
        va_end( va );
        genericsPrintf( "%s", op );
        genericsPrintf( C_RESET );
        fflush( stderr );
    }
}
// ====================================================================================================
void genericsExit( int status, const char *fmt, ... )

{
    static char op[MAX_STRLEN];

    fflush( stdout );
    va_list va;
    va_start( va, fmt );
    vsnprintf( op, MAX_STRLEN, fmt, va );
    va_end( va );
    genericsPrintf( C_VERB_ERROR );
    genericsPrintf( op );
    genericsPrintf( C_RESET );
    fflush( stderr );

    exit( status );
}
// ====================================================================================================
void genericsScreenHandling( bool screenHandling )

{
    _screenHandling = screenHandling;
}
// ====================================================================================================
