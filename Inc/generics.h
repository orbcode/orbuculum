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
#include <stdio.h>
#include <errno.h>

#define EOL "\n"
#ifdef __cplusplus
extern "C" {
#endif

/* Error return codes .. may already be defined by ncurses */
typedef int errcode;
#ifndef OK
#define OK         0
#endif

#ifndef ERR
#define ERR       -1
#endif

#define CN_RED     1
#define CN_GREEN   2
#define CN_BROWN   3
#define CN_BLUE    4
#define CN_PURPLE  5
#define CN_CYAN    6
#define CN_GRAY    7
#define CN_LRED    9
#define CN_LGREEN  10
#define CN_YELLOW  11
#define CN_LBLUE   12
#define CN_LPURPLE 13
#define CN_LCYAN   14
#define CN_WHITE   15

#define CMD_ALERT    "\001"
#define C_RED        CMD_ALERT "1"
#define C_GREEN      CMD_ALERT "2"
#define C_BROWN      CMD_ALERT "3"
#define C_BLUE       CMD_ALERT "4"
#define C_PURPLE     CMD_ALERT "5"
#define C_CYAN       CMD_ALERT "6"
#define C_GRAY       CMD_ALERT "7"
#define C_LRED       CMD_ALERT "9"
#define C_LGREEN     CMD_ALERT "a"
#define C_YELLOW     CMD_ALERT "b"
#define C_LBLUE      CMD_ALERT "c"
#define C_LPURPLE    CMD_ALERT "d"
#define C_LCYAN      CMD_ALERT "e"
#define C_WHITE      CMD_ALERT "f"
#define C_RESET      CMD_ALERT "r"
#define CLEAR_SCREEN CMD_ALERT "z"
#define C_PREV_LN    CMD_ALERT "u"
#define C_CLR_LN     CMD_ALERT "U"

/* The actual control codes that do the work */
#define CC_CLEAR_SCREEN  "\033[H\033[2J\033[3J"
#define CC_PREV_LN       "\033[1F"
#define CC_CLR_LN        "\033[K"
#define CC_COLOUR        "\033[%d;3%dm"
#define CC_RES           "\033[0m"

#define ALWAYS_INLINE inline __attribute__((always_inline))

#define MEMCHECK(x,y) if ( NULL == (x))                 \
    {                                   \
        genericsExit( ENOMEM,"Out of memory at %s::%d" EOL, __FILE__,__LINE__); \
        return y;                         \
    }

#define MEMCHECKV(x) if ( NULL == (x))  \
    {                                   \
        genericsExit( ENOMEM,"Out of memory at %s::%d" EOL, __FILE__,__LINE__); \
    }

/* Memory sizes */
typedef uint32_t symbolMemaddr;
typedef unsigned char *symbolMemptr;

// ====================================================================================================
enum verbLevel {V_ERROR, V_WARN, V_INFO, V_DEBUG, V_MAX_VERBLEVEL};

typedef void ( *genericsReportCB )( enum verbLevel l, const char *fmt, ... );

char *genericsEscape( char *str );
char *genericsUnescape( char *str );
uint64_t genericsTimestampuS( void );
uint32_t genericsTimestampmS( void );
bool genericsSetReportLevel( enum verbLevel lset );
enum verbLevel genericsGetReportLevel( void );
void genericsFPrintf( FILE *stream, const char *fmt, ... );
char *genericsGetBaseDirectory( void );
const char *genericsBasename( const char *n );
const char *genericsBasenameN( const char *n, int c );
void genericsReport( enum verbLevel l, const char *fmt, ... );
void genericsExit( int status, const char *fmt, ... );

void genericsScreenHandling( bool screenHandling );

// ====================================================================================================
#ifdef __cplusplus
}
#endif

#endif
