/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Generic Routines
 * ================
 *
 */

#ifndef _GENERICS_
#define _GENERICS_

#include <stdbool.h>
#include <limits.h>
#include <stdint.h>
#include <errno.h>

#if defined LINUX
    #define EOL "\n"
#else
    #define EOL "\n\r"
#endif
#ifdef __cplusplus
extern "C" {
#endif

/* Error return codes .. may already be defined by ncurses */
#ifndef OK
#define OK         0
#endif

#ifndef ERR
#define ERR       -1
#endif

#ifdef SCREEN_HANDLING
#define CLEAR_SCREEN  "\033[2J\033[;H"
#define C_PREV_LN     "\033[1F"
#define C_CLR_LN      "\033[K"
#else
#define CLEAR_SCREEN  ""
#define C_PREV_LN ""
#define C_CLR_LN  ""
#endif

#define C_RES     "\033[0m"
#define C_RED     "\033[0;31m"
#define C_GREEN   "\033[0;32m"
#define C_BROWN   "\033[0;33m"
#define C_BLUE    "\033[0;34m"
#define C_PURPLE  "\033[0;35m"
#define C_CYAN    "\033[0;36m"
#define C_GRAY    "\033[0;37m"
#define C_LRED    "\033[1;31m"
#define C_LGREEN  "\033[1;32m"
#define C_YELLOW  "\033[1;33m"
#define C_LBLUE   "\033[1;34m"
#define C_LPURPLE "\033[1;35m"
#define C_LCYAN   "\033[1;36m"
#define C_WHITE   "\033[1;37m"
#define C_MONO    ""

#define ALWAYS_INLINE inline __attribute__((always_inline))

#define MEMCHECK(x,y) if ( NULL == (x))                 \
    {                                   \
        genericsExit( ENOMEM,"Out of memory at %s::%d" EOL, __FILE__,__LINE__); \
        return y;                         \
    }

// ====================================================================================================
enum verbLevel {V_ERROR, V_WARN, V_INFO, V_DEBUG, V_MAX_VERBLEVEL};

typedef void ( *genericsReportCB )( enum verbLevel l, const char *fmt, ... );

char *genericsEscape( char *str );
char *genericsUnescape( char *str );
uint64_t genericsTimestampuS( void );
uint32_t genericsTimestampmS( void );
void genericsSetReportLevel( enum verbLevel lset );
void genericsPrintf( const char *fmt, ... );
const char *genericsBasename( const char *n );
const char *genericsBasenameN( const char *n, int c );
void genericsReport( enum verbLevel l, const char *fmt, ... );
void genericsExit( int status, const char *fmt, ... );
// ====================================================================================================
#ifdef __cplusplus
}
#endif

#endif
