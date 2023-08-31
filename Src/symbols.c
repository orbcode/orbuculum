/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Symbol recovery from elf file
 * =============================
 *
 */

#include <stdio.h>
#include <assert.h>
#include <ctype.h>
#include "generics.h"
#include "symbols.h"
#if defined(WIN32)
    #include <Windows.h>
    #include <io.h>
    #include <fcntl.h>
#endif

#define MAX_LINE_LEN (4096)
#define ELF_RELOAD_DELAY_TIME 1000000   /* Time before elf reload will be attempted when its been lost */

#define OBJDUMP "arm-none-eabi-objdump"
#define OBJENVNAME "OBJDUMP"

#define SOURCE_INDICATOR "sRc##"
#define SYM_NOT_FOUND (0xffffffff)

#define NO_FUNCTION_TXT (char*)"No Function Name"
#define NO_FILE_TXT     (char*)"No Source"

//#define GPTI_DEBUG 1                 /* Define this for objdump data collection state machine trace */

#ifdef GPTI_DEBUG
    #define GTPIP(...) { fprintf(stderr, __VA_ARGS__); }
#else
    #define GTPIP(...) {}
#endif

enum LineType { LT_NULL, LT_NOISE, LT_PROC_LABEL, LT_LABEL, LT_SOURCE, LT_ASSEMBLY, LT_FILEANDLINE, LT_NEWLINE, LT_ERROR };
enum ProcessingState {PS_IDLE, PS_GET_SOURCE, PS_GET_ASSY} ps = PS_IDLE;

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Internal Routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
static uint32_t _getFileEntryIdx( struct SymbolSet *s, char *filename )

/* Get index to file entry in the files table, or SYM_NOT_FOUND */

{
    uint32_t i = 0;

    while ( ( i < s->fileCount ) && ( strcmp( s->files[i].name, filename ) ) ) {
        i++;
    }

    return ( i < s->fileCount ) ? i : SYM_NOT_FOUND;
}
// ====================================================================================================
static uint32_t _getOrAddFileEntryIdx( struct SymbolSet *s, char *filename )

/* Return index to file entry in the files table, or create an entry and return that */

{
    char *fl = filename;
    char *d  = s->deleteMaterial;

    /* Scan forwards past any delete material on the front */
    while ( ( d ) && ( *d ) && ( *fl == *d ) ) {
        d++;
        fl++;
    }

    /* If we didn't manage to delete everything then don't delete anything */
    if ( fl - filename != ( long int )strlen( s->deleteMaterial ) ) {
        fl = filename;
    }


    uint32_t f = _getFileEntryIdx( s, fl );

    if ( SYM_NOT_FOUND == f ) {
        /* Doesn't exist, so create it */
        s->files = ( struct fileEntry * )realloc( s->files, sizeof( struct fileEntry ) * ( s->fileCount + 1 ) );
        f = s->fileCount;
        memset( &( s->files[f] ), 0, sizeof( struct fileEntry ) );
        s->files[f].name = strdup( fl );
        s->fileCount++;
    }

    return f;
}
// ====================================================================================================
static uint32_t _getFunctionEntryIdx( struct SymbolSet *s, char *function )

/* Get index to file entry in the functions table, or SYM_NOT_FOUND */

{
    uint32_t i = 0;

    while ( ( i < s->functionCount ) && ( strcmp( s->functions[i].name, function ) ) ) {
        i++;
    }

    return ( i < s->functionCount ) ? i : SYM_NOT_FOUND;
}
// ====================================================================================================
// Strdup leak is deliberately ignored. That is the central purpose of this code!
#pragma GCC diagnostic push
#if !defined(__clang__)
    #pragma GCC diagnostic ignored "-Wanalyzer-malloc-leak"
#endif
static uint32_t _getOrAddFunctionEntryIdx( struct SymbolSet *s, char *function )

/* Return index to file entry in the functions table, or create an entry and return that */

{
    uint32_t f = _getFunctionEntryIdx( s, function );

    if ( SYM_NOT_FOUND == f ) {
        /* Doesn't exist, so create it */
        s->functions = ( struct functionEntry * )realloc( s->functions, sizeof( struct functionEntry ) * ( s->functionCount + 1 ) );
        f = s->functionCount;
        memset( &( s->functions[f] ), 0, sizeof( struct functionEntry ) );
        s->functions[f].name = strdup( function );
        MEMCHECK( s->functions[f].name, 0 );
        s->functionCount++;
    }

    return f;
}
#pragma GCC diagnostic pop
// ====================================================================================================
static struct sourceLineEntry *_AddSourceLineEntry( struct SymbolSet *s )

/* Add an entry to the sources table, and return a pointer to created entry */

{
    struct sourceLineEntry *src;
    s->sources = ( struct sourceLineEntry * )realloc( s->sources, sizeof( struct sourceLineEntry ) * ( s->sourceCount + 1 ) );
    src = &s->sources[s->sourceCount];
    memset( src, 0, sizeof( struct sourceLineEntry ) );
    s->sourceCount++;
    return src;
}

// ====================================================================================================
static int _compareLines( const void *a, const void *b )

/* Compare two lines for ordinal value (used for bsort/bsearch) */

{
    struct sourceLineEntry *sa = ( struct sourceLineEntry * )a;
    struct sourceLineEntry *sb = ( struct sourceLineEntry * )b;

    /* Is it before the start of this line? */
    if ( sa->startAddr < sb->startAddr ) {
        return -1;
    }

    /* Is it after the end of this line? */
    if ( sa->startAddr > sb->endAddr ) {
        return 1;
    }

    /* Hmm...match must be on this line then */
    return 0;
}

// ====================================================================================================
static void _sortLines( struct SymbolSet *s )

/* Sort lines into ordinal value as defined by _compareLines */

{
    qsort( s->sources, s->sourceCount, sizeof( struct sourceLineEntry ), _compareLines );
}

// ====================================================================================================
static bool _find_symbol( struct SymbolSet *s, uint32_t workingAddr,
                          uint32_t *fileindex, uint32_t *functionindex, uint32_t *pline,
                          uint16_t *linesInBlock, const char **psource,
                          const struct assyLineEntry **assy,
                          uint32_t *assyLine )

/* Find symbol and return pointers to contents */

{
    struct sourceLineEntry needle = { .startAddr = workingAddr, .endAddr = workingAddr };
    struct sourceLineEntry *found = ( struct sourceLineEntry * )bsearch( &needle, s->sources, s->sourceCount, sizeof( struct sourceLineEntry ), _compareLines );

    if ( found ) {
        *pline         = found->lineNo;
        *linesInBlock  = found->linesInBlock;
        *psource       = found->lineText;
        *assy          = found->assy;
        *fileindex     = found->fileIdx;
        *functionindex = found->functionIdx;

        /* If there is assembly then match the line too */
        for ( *assyLine = 0; *assyLine < found->assyLines; ( *assyLine )++ ) {
            if ( ( *assy )[*assyLine].addr == workingAddr ) {
                break;
            }
        }

        /* If the assembly line wasn't found then indicate that */
        if ( *assyLine == found->assyLines ) {
            *assyLine = ASSY_NOT_FOUND;
        }

        return true;
    }

    return false;
}
// ====================================================================================================
static enum LineType _getLineType( char *sourceLine, char *p1, char *p2, char *p3, char *p4 )

/* Analyse line returned by objdump and categorise it. If objdump output is misinterpreted, this is the first place to check */

{
    /* If it's empty, something is badly wrong */
    if ( !*sourceLine ) {
        return LT_NULL;
    }

    /* If it starts with a source tag, it's unambigious */
    if ( !strncmp( sourceLine, SOURCE_INDICATOR, strlen( SOURCE_INDICATOR ) ) ) {
        return LT_SOURCE;
    }

    /* If it has an address followed by pairs of hex digits then it's a data block and we're not interested */
    if ( 1 == sscanf( sourceLine, " %*[0-9a-fA-F]: %[0-9a-fA-F]", p1 ) ) {
        /* Match is two digits followed by a whitespace */
        if ( ( isxdigit( *p1 ) ) && ( isxdigit( *( p1 + 1 ) ) ) && ( !( *( p1 + 2 ) ) ) ) {
            return LT_NOISE;
        }
    }

    /* If it has something with <xxx> in it, it's a proc label (function) */
    if ( 2 == sscanf( sourceLine, "%[0-9a-fA-F] <%[^>]>", p1, p2 ) ) {
        return LT_PROC_LABEL;
    }

    /* If it has (): on the end it's a label */
    if ( 2 == sscanf( sourceLine, "%[^(]()%c", p1, p2 ) ) {
        if ( *p2 == ':' ) {
            return LT_LABEL;
        }
    }

    /* If it starts with a space and has specific fields, it's a 16 or 32 bit assembly line */
    if ( 4 == sscanf( sourceLine, " %[0-9a-fA-F]: %[0-9a-fA-F]%*1[ ]%[0-9a-fA-F] %[^\n]", p1, p2, p3, p4 ) ) {
        return LT_ASSEMBLY;
    }

    *p2 = 0;

    if ( 3 == sscanf( sourceLine, " %[0-9a-fA-F]: %[0-9a-fA-F] %[^\n]", p1, p3, p4 ) ) {
        return LT_ASSEMBLY;
    }

    /* If it contains text:num then it's a file and line */
    if ( 2 == sscanf( sourceLine, "%[^:]:%[0-9]", p1, p2 ) ) {
        return LT_FILEANDLINE;
    }

    /* Alternative form for Windows; If it contains Drive:text:num then it's a file and line */
    if ( 3 == sscanf( sourceLine, "%2c%[^:]:%[0-9]", p1, &p1[2], p2 ) ) {
        return LT_FILEANDLINE;
    }

    /* If it contains nothing other than newline then its a newline */
    if ( ( *sourceLine == '\n' ) || ( *sourceLine == '\r' ) ) {
        return LT_NEWLINE;
    }

    /* ...otherwise we consider it junk */
    return LT_NOISE;
}
// ====================================================================================================
static bool _getDest( char *assy, uint32_t *dest )

/* Return destination address if this assembly instruction is a precomputed jump */
/* The jump destination is the second field in the assembly string */

{
    int r = sscanf( assy, "%*[^\t]\t%x", dest );

    if ( r == 1 ) {
        return r;
    }

    /* Check for cbz destination format (i.e. with a register in front of the destination address) */
    return ( 1 == sscanf( assy, "%*[^\t]\tr%*[0-7],%x", dest ) );
}

#if defined(WIN32)
static FILE *_openProcess( char *commandLine, PROCESS_INFORMATION *processInfo, FILE **errorOutput )
{
    HANDLE readingPipe;
    HANDLE processOutPipe;

    HANDLE errorReadingPipe;
    HANDLE processErrorPipe;

    {
        // Create pipe for process' stdout
        SECURITY_ATTRIBUTES pipeSecurity;
        pipeSecurity.nLength = sizeof( pipeSecurity );
        pipeSecurity.bInheritHandle = true;
        pipeSecurity.lpSecurityDescriptor = NULL;
        CreatePipe( &readingPipe, &processOutPipe, &pipeSecurity, 0 );

        // only write side of pipe should be inherited
        SetHandleInformation( readingPipe, HANDLE_FLAG_INHERIT, 0 );
    }

    {
        // Create pipe for process' stderr
        SECURITY_ATTRIBUTES pipeSecurity;
        pipeSecurity.nLength = sizeof( pipeSecurity );
        pipeSecurity.bInheritHandle = true;
        pipeSecurity.lpSecurityDescriptor = NULL;
        CreatePipe( &errorReadingPipe, &processErrorPipe, &pipeSecurity, 1 * 1024 * 1024 );

        // only write side of pipe should be inherited
        SetHandleInformation( errorReadingPipe, HANDLE_FLAG_INHERIT, 0 );
    }

    memset( processInfo, 0, sizeof( PROCESS_INFORMATION ) );

    {
        STARTUPINFOA startupInfo;
        memset( &startupInfo, 0, sizeof( startupInfo ) );
        startupInfo.cb = sizeof( startupInfo );
        startupInfo.hStdOutput = processOutPipe;
        startupInfo.hStdInput = GetStdHandle( STD_INPUT_HANDLE );
        startupInfo.hStdError = processErrorPipe;
        startupInfo.dwFlags = STARTF_USESTDHANDLES;

        BOOL success = CreateProcessA(
                                   NULL,
                                   commandLine,
                                   NULL,
                                   NULL,
                                   true,
                                   DETACHED_PROCESS,
                                   NULL,
                                   NULL,
                                   &startupInfo,
                                   processInfo
                       );

        // Close handle passed to child process
        CloseHandle( processOutPipe );
        CloseHandle( processErrorPipe );

        if ( !success ) {
            CloseHandle( readingPipe );
            CloseHandle( errorReadingPipe );
            return NULL;
        }
    }

    int fd = _open_osfhandle( ( intptr_t )readingPipe, _O_RDONLY );

    if ( fd == -1 ) {
        CloseHandle( readingPipe );
        CloseHandle( errorReadingPipe );
        return NULL;
    }

    FILE *f = _fdopen( fd, "r" );

    if ( f == NULL ) {
        _close( fd );
        return NULL;
    }

    int errorFd = _open_osfhandle( ( intptr_t )errorReadingPipe, _O_RDONLY );

    if ( errorFd == -1 ) {
        fclose( f );
        CloseHandle( errorReadingPipe );
        return NULL;
    }

    *errorOutput = _fdopen( errorFd, "r" );

    if ( *errorOutput == NULL ) {
        _close( errorFd );
        fclose( f );
        return NULL;
    }

    return f;
}
#endif

// ====================================================================================================
static enum symbolErr _getTargetProgramInfo( struct SymbolSet *s )

/* Analyse line returned by objdump and categorise it, putting results into correct structures. */
/* If objdump output is misinterpreted, this is the second place to check */

{
    FILE *f;                                    /* Connection to objdum process */
    char line[MAX_LINE_LEN];                    /* Line read from objdump process */
    char commandLine[MAX_LINE_LEN];             /* Command line used to run objdump */
    uint32_t existingTextLen;                   /* Line under construction */

    char label[MAX_LINE_LEN];                   /* Any cached label */
    uint32_t lineNo = 0;                        /* Any cached line number */
    bool startAddrSet = false;                  /* If to attach start address to function */

    char p1[MAX_LINE_LEN];
    char p2[MAX_LINE_LEN];
    char p3[MAX_LINE_LEN];
    char p4[MAX_LINE_LEN];                      /* Elements returned by line analysis */
    enum LineType lt;                           /* Line type returned from anaylsis */

    uint32_t fileEntryIdx;                      /* Index into file entry table */
    uint32_t functionEntryIdx;                  /* Index into function entry table */
    uint32_t nullFileEntry;                     /* Tag for when we don't have a filename */
    struct sourceLineEntry *sourceEntry = NULL; /* pointer to current source entry */

    if ( stat( s->elfFile, &s->st ) != 0 ) {
        return SYMBOL_NOELF;
    }

    if ( getenv( OBJENVNAME ) ) {
        snprintf( commandLine, MAX_LINE_LEN, "%s -Sl%s --source-comment=" SOURCE_INDICATOR " %s %s", getenv( OBJENVNAME ),  s->demanglecpp ? " -C" : "", s->elfFile, s->odoptions );
    } else {
        snprintf( commandLine, MAX_LINE_LEN, OBJDUMP " -Sl%s --source-comment=" SOURCE_INDICATOR " %s %s",  s->demanglecpp ? " -C" : "", s->elfFile, s->odoptions );
    }

    printf( "\n\n%s\n\n", commandLine );
#if defined(WIN32)
    PROCESS_INFORMATION processInfo;
    FILE *errorOut;
    f = _openProcess( commandLine, &processInfo, &errorOut );
#else
    f = popen( commandLine, "r" );
#endif

    if ( !f ) {
        return SYMBOL_NOOBJDUMP;
    }


    /* Create the null entries */
    functionEntryIdx = _getOrAddFunctionEntryIdx( s, NO_FUNCTION_TXT );
    nullFileEntry    = fileEntryIdx = _getOrAddFileEntryIdx( s, NO_FILE_TXT );

    while ( !feof( f ) ) {
        fgets( line, MAX_LINE_LEN, f );

        lt = _getLineType( line, p1, p2, p3, p4 );

        if ( lt == LT_ERROR ) {
            pclose( f );
            return SYMBOL_UNSPECIFIED;
        }

        GTPIP( "**************** %s", line );

    repeat_process: // In case we need to process the state machine more than once

#ifdef GPTI_DEBUG

        switch ( ps ) {
            case PS_IDLE:
                GTPIP( "IDLE(" );
                break;

            case PS_GET_SOURCE:
                GTPIP( "GET_SOURCE(" );
                break;

            case PS_GET_ASSY:
                GTPIP( "GET_ASSY(" );
                break;
        }

        switch ( lt ) {
            case LT_NOISE:
                GTPIP( "NOISE" );
                break;

            case LT_PROC_LABEL:
                GTPIP( "PROC_LABEL" );
                break;

            case LT_LABEL:
                GTPIP( "LABEL" );
                break;

            case LT_SOURCE:
                GTPIP( "SOURCE" );
                break;

            case LT_ASSEMBLY:
                GTPIP( "ASSEMBLY" );
                break;

            case LT_FILEANDLINE:
                GTPIP( "FILEANDLINE" );
                break;

            case LT_NEWLINE:
                GTPIP( "NEWLINE" );
                break;

            case LT_ERROR:
                GTPIP( "ERROR" );
                break;
        }

        GTPIP( ")" EOL );
#endif

        switch ( ps ) {
            case PS_IDLE: /* Waiting for name of function ================================== */
                switch ( lt ) {
                    case LT_NOISE: /* ------------------------------------------------------ */
                        break;

                    case LT_SOURCE: /* ----------------------------------------------------- */
                        ps = PS_GET_SOURCE;
                        sourceEntry = _AddSourceLineEntry( s );
                        sourceEntry->lineNo = lineNo;
                        sourceEntry->functionIdx = functionEntryIdx;
                        sourceEntry->fileIdx = fileEntryIdx;
                        startAddrSet = false;
                        GTPIP( "Switching to get source" EOL );
                        goto repeat_process;
                        break;

                    case LT_ASSEMBLY: /* --------------------------------------------------- */
                        /* This happens when there is no corresponding source code */
                        sourceEntry = _AddSourceLineEntry( s );
                        sourceEntry->functionIdx = functionEntryIdx;
                        sourceEntry->fileIdx = fileEntryIdx;
                        startAddrSet = false;
                        GTPIP( "Straight to Assembly" EOL );
                        ps = PS_GET_ASSY;
                        goto repeat_process;
                        break;

                    case LT_PROC_LABEL: /* ------------------------------------------------- */
                        functionEntryIdx = _getOrAddFunctionEntryIdx( s, p2 );
                        s->functions[functionEntryIdx].startAddr = strtoul( p1, NULL, 16 );
                        GTPIP( "Got function name [%08x %s]" EOL, s->functions[functionEntryIdx].startAddr, s->functions[functionEntryIdx].name );
                        break;

                    case LT_FILEANDLINE: /* ------------------------------------------------ */
                        fileEntryIdx = _getOrAddFileEntryIdx( s, p1 );
                        s->functions[functionEntryIdx].fileEntryIdx = fileEntryIdx;
                        lineNo = strtoul( p2, NULL, 10 );
                        GTPIP( "Got filename and line [%d %s]" EOL, lineNo, p1 );
                        break;

                    case LT_LABEL: /* -----------------------------------------------------  */
                        strcpy( label, p1 );
                        GTPIP( "Got label [%s]" EOL, label );
                        break;

                    case LT_ERROR: /* ------------------------------------------------------ */
                    case LT_NEWLINE:
                    default:
                        GTPIP( "Unhandled" EOL );
                        break;
                }

                break;

            case PS_GET_SOURCE: /* Collecting source ========================================== */
                switch ( lt ) {
                    case LT_NOISE: /* ------------------------------------------------------ */
                        break;

                    case LT_SOURCE: /* ----------------------------------------------------- */
                        /* Bundle source lines together, separated by the newlines */
                        existingTextLen = sourceEntry->lineText ? strlen( sourceEntry->lineText ) : 0;
                        sourceEntry->functionIdx = functionEntryIdx;
                        sourceEntry->lineNo = lineNo;

                        // Add this to source line repository
                        if ( s->recordSource ) {
                            sourceEntry->lineText = ( char * )realloc( sourceEntry->lineText, strlen( line ) - strlen( SOURCE_INDICATOR ) + existingTextLen + 1 );
                            strcpy( &sourceEntry->lineText[existingTextLen], &line[strlen( SOURCE_INDICATOR )] );
                            sourceEntry->linesInBlock++;
                            GTPIP( "Got Source [%s]", &line[strlen( SOURCE_INDICATOR )] );
                        }

                        break;

                    case LT_ASSEMBLY: /* --------------------------------------------------- */
                        ps = PS_GET_ASSY;
                        goto repeat_process;
                        break;

                    case LT_PROC_LABEL: /* ------------------------------------------------- */
                    case LT_ERROR:
                    case LT_LABEL:
                    case LT_NEWLINE:
                    case LT_FILEANDLINE:
                    default:
                        GTPIP( "Unhandled" EOL );
                        break;
                }

                break;

            case PS_GET_ASSY: /* Waiting for assembly ========================================= */
                switch ( lt ) {
                    case LT_NOISE: /* ------------------------------------------------------ */
                        break;

                    case LT_PROC_LABEL: /* ------------------------------------------------- */
                    case LT_FILEANDLINE: /* ------------------------------------------------ */
                    case LT_NEWLINE: /* ---------------------------------------------------- */
                        ps = PS_IDLE;

                        /* For all these cases we would expect a new file and line, so reset those */
                        fileEntryIdx = nullFileEntry;
                        lineNo = 0;
                        goto repeat_process;
                        break;

                    case LT_ASSEMBLY: /* --------------------------------------------------- */
                        sourceEntry->endAddr = strtoul( p1, NULL, 16 );
                        s->functions[functionEntryIdx].endAddr = sourceEntry->endAddr;

                        /* If we're recording entries and this is not seomthing we explicitly want to ignore */
                        if ( ( s->recordAssy )  && ( !strstr( p4, ".word" ) ) && ( !strstr( p4, ".short" ) ) ) {
                            sourceEntry->assy = ( struct assyLineEntry * )realloc( sourceEntry->assy, sizeof( struct assyLineEntry ) * ( sourceEntry->assyLines + 1 ) );
                            sourceEntry->assy[sourceEntry->assyLines].addr = sourceEntry->endAddr;
                            sourceEntry->assy[sourceEntry->assyLines].is4Byte = ( ( *p2 ) != 0 );
                            sourceEntry->assy[sourceEntry->assyLines].codes = strtoul( p3, NULL, 16 );

                            if ( *p2 ) {
                                sourceEntry->assy[sourceEntry->assyLines].codes |= ( strtoul( p2, NULL, 16 ) << 16 );
                            }

                            sourceEntry->assy[sourceEntry->assyLines].lineText           = strdup( line );
                            sourceEntry->assy[sourceEntry->assyLines].isJump             = false;
                            sourceEntry->assy[sourceEntry->assyLines].isReturn           = false;
                            sourceEntry->assy[sourceEntry->assyLines].isSubCall          = false;
                            sourceEntry->assy[sourceEntry->assyLines].jumpdest  = NO_DESTADDRESS;

                            /* Just hook the assy pointer to the location in the line where the assembly itself starts */
                            sourceEntry->assy[sourceEntry->assyLines].assy = strstr( sourceEntry->assy[sourceEntry->assyLines].lineText, p4 );

                            /* Record the label is there was one */
                            sourceEntry->assy[sourceEntry->assyLines].label = *label ? strdup( label ) : NULL;
                            GTPIP( "%08x %x [%s]" EOL, sourceEntry->assy[sourceEntry->assyLines].addr,
                                   sourceEntry->assy[sourceEntry->assyLines].codes,
                                   sourceEntry->assy[sourceEntry->assyLines].lineText );

#define MASKED_COMPARE(mask,compare) (((sourceEntry->assy[sourceEntry->assyLines].codes)&(mask))==(compare))

                            /* For ETM4 we need to know direct and indirect branches, cos they are the only instructions                */
                            /* that will get traced. So let's label those...Per definition in ARM IHI0064H.a ID20820 Appendix F         */
                            if (
                                        MASKED_COMPARE( 0xffffff03, 0x00004700 ) || /* BL, BLX rx */
                                        MASKED_COMPARE( 0xfffff500, 0x0000b100 ) || /* CBNZ/CBZ   */
                                        MASKED_COMPARE( 0xfffff000, 0x0000d000 ) || /* B          */
                                        MASKED_COMPARE( 0xffffffef, 0x0000bf20 ) || /* WFE/WFI    */

                                        /* 32 bit matches */
                                        MASKED_COMPARE( 0xffd08000, 0xe8908000 ) || /* LDM */
                                        MASKED_COMPARE( 0xffd08000, 0xe9908000 ) || /* LDMDB */
                                        MASKED_COMPARE( 0xfe10f000, 0xf810f000 ) || /* LDR to PC */
                                        MASKED_COMPARE( 0xf8008000, 0xf0008000 )  /* Branches and misc control */
                            ) {
                                sourceEntry->assy[sourceEntry->assyLines].etm4branch = true;
                            } else {
                                sourceEntry->assy[sourceEntry->assyLines].etm4branch = false;
                            }

                            /* The only way a subroutine will be called from gcc (See gcc source code file gcc/config/arm/thumb2.md) is */
                            /* via blx reg, blxns reg. In theory it could also be done via direct manipulation of R15, but fortunately  */
                            /* gcc doesn't pull tricks like that. It _will_ tail chain (with BX) though.                                */
                            /* Also see https://gcc.gnu.org/onlinedocs/gccint/Machine-Desc.html                                         */

                            /* Mark if this is a subroutine call (BL/BLX) */
                            if (
                                        MASKED_COMPARE( 0xf800D000, 0xf000D000 ) ||  /* BL Encoding T1 */
                                        MASKED_COMPARE( 0xffffff80, 0x00004780 )     /* BLX rx */
                            ) {
                                sourceEntry->assy[sourceEntry->assyLines].isSubCall = true;
                                _getDest( sourceEntry->assy[sourceEntry->assyLines].assy, &sourceEntry->assy[sourceEntry->assyLines].jumpdest );
                            }

                            /* Returns are selected via the function output_return_instruction in arm.c in the gcc source.              */
                            /* Mark if instruction is a return (i.e. PC popped from stack)                                              */
                            if (
                                        MASKED_COMPARE( 0xffd0a000, 0xe8908000 ) ||  /* LDM including PC */
                                        MASKED_COMPARE( 0xffffff00, 0x0000bd00 ) ||  /* POP PC Encoding T1 */
                                        MASKED_COMPARE( 0xffff8000, 0xe8bd8000 ) ||  /* POP PC Encoding T2 */
                                        MASKED_COMPARE( 0xffffffff, 0xf85dfb04 ) ||  /* POP PC Encoding T3 */
                                        MASKED_COMPARE( 0xffffff00, 0x0000bd00 ) ||
                                        MASKED_COMPARE( 0xffff8000, 0xe8bd8000 ) ||
                                        MASKED_COMPARE( 0xffffffff, 0x000047f0 ) ||  /* BLX LR */
                                        MASKED_COMPARE( 0xffffffff, 0x00004770 )     /* BX LR */
                            ) {
                                sourceEntry->assy[sourceEntry->assyLines].isReturn = true;
                            }

                            /* Finally, if this is a jump that might be taken, then get the jump destination */
                            /* This is done by checking if the opcode is a valid jump in either 16 or 32 bit world */
                            if (
                                        MASKED_COMPARE( 0xfff00000, 0xE8D00000 ) || /* TBB (T1) */
                                        MASKED_COMPARE( 0xfffff800, 0x0000e000 ) || /* Bc Label (T2) */
                                        MASKED_COMPARE( 0xfffff500, 0x0000b100 ) || /* CBNZ/CBZ      */
                                        MASKED_COMPARE( 0xfffff000, 0x0000d000 ) || /* Bc Label (T1) */
                                        MASKED_COMPARE( 0xf800d000, 0xf0009000 ) || /* Bc Label (T4) */
                                        ( MASKED_COMPARE( 0xf800d000, 0xf0008000 ) &&
                                          ( !( MASKED_COMPARE( 0x03800000, 0x03800000 ) ) ) ) /* Bc Label (T3) (Excludes AL condition ) */
                            ) {
                                sourceEntry->assy[sourceEntry->assyLines].isJump = true;

                                if ( !_getDest( sourceEntry->assy[sourceEntry->assyLines].assy, &sourceEntry->assy[sourceEntry->assyLines].jumpdest ) ) {
                                    GTPIP( "Failed to get jump destination for text %s " EOL, sourceEntry->assy[sourceEntry->assyLines].assy );
                                }
                            }

                            sourceEntry->assyLines++;
                        }

                        *label = 0;

                        if ( !startAddrSet ) {
                            startAddrSet = true;
                            sourceEntry->startAddr = sourceEntry->endAddr;
                        }

                        break;

                    case LT_LABEL: /* ------------------------------------------------------ */
                        strcpy( label, p1 );
                        GTPIP( "Got label [%s]" EOL, label );
                        break;

                    case LT_SOURCE:  /* ---------------------------------------------------- */
                    case LT_ERROR:
                    default:
                        GTPIP( "Unhandled" EOL );
                        break;
                }

                break;
        }
    }

#if defined(WIN32)
    WaitForSingleObject( processInfo.hProcess, INFINITE );

    DWORD exitCode = 1;

    if ( !GetExitCodeProcess( processInfo.hProcess, &exitCode ) ) {
        exitCode = 1;
    }

    CloseHandle( processInfo.hThread );
    CloseHandle( processInfo.hProcess );

    if ( exitCode != 0 ) {
        while ( !feof( errorOut ) ) {
            fgets( line, MAX_LINE_LEN, errorOut );
            fputs( line, stderr );
        }

        return SYMBOL_NOOBJDUMP;
    }

    fclose( errorOut );
#else

    if ( 0 != pclose( f ) ) {
        /* Something went wrong in the subprocess */
        return SYMBOL_NOOBJDUMP;
    }

#endif

    _sortLines( s );
    return SYMBOL_OK;
}
// ====================================================================================================
const char *SymbolFilename( struct SymbolSet *s, uint32_t index )

{
    switch ( index ) {
        default:
            if ( index < s->fileCount ) {
                return s->files[index].name;
            }

            return "NONE";
    }
}
// ====================================================================================================
const char *SymbolFunction( struct SymbolSet *s, uint32_t index )

{
    switch ( index ) {
        case FN_SLEEPING:
            return FN_SLEEPING_STR;

        case INTERRUPT:
            return FN_INTERRUPT_STR;

        default:
            if ( index < s->functionCount ) {
                return s->functions[index].name;
            }

            return "NONE";
    }
}
// ====================================================================================================
bool SymbolLookup( struct SymbolSet *s, uint32_t addr, struct nameEntry *n )

/* Lookup function for address to line, and hence to function */

{
    const char *source   = NULL;
    uint32_t fileindex;
    uint32_t functionindex;
    const struct assyLineEntry *assy = NULL;
    uint32_t assyLine;
    uint16_t linesInBlock;
    uint32_t line;

    memset( n, 0, sizeof( struct nameEntry ) );
    assert( s );

    if ( ( addr & SPECIALS_MASK ) == SPECIALS_MASK ) {
        /* Address is some sort of interrupt */
        n->line = 0;
        n->fileindex = n->functionindex = n->addr = INTERRUPT;
        return true;
    }

    if ( _find_symbol( s, addr, &fileindex, &functionindex, &line, &linesInBlock, &source, &assy, &assyLine ) ) {
        n->fileindex = fileindex;
        n->functionindex = functionindex;
        n->source   = source ? source : "";
        n->assy     = assy;
        n->assyLine = assyLine;
        n->addr = addr;
        n->line = line;
        n->linesInBlock = linesInBlock;
        return true;
    }


    n->fileindex = n->functionindex = n->line = 0;
    n->source   = "";
    n->assy     = NULL;
    n->addr = SYM_NOT_FOUND;
    return false;
}
// ====================================================================================================
void SymbolSetDelete( struct SymbolSet **s )

/* Delete existing symbol set, by means of deleting all memory-allocated components of it first */

{
    if ( *s ) {
        free( ( *s )->elfFile );

        /* Free off any files dynamic memory we allocated */
        if ( ( *s )->files ) {
            for ( uint32_t i = 0; i < ( *s )->fileCount; i++ ) {
                if ( ( *s )->files[i].name ) {
                    free( ( *s )->files[i].name );
                }
            }

            free( ( *s )->files );
        }

        /* Free off any functions dynamic memory we allocated */
        if ( ( *s )->functions ) {
            for ( uint32_t i = 0; i < ( *s )->functionCount; i++ ) {
                if ( ( *s )->functions[i].name ) {
                    free( ( *s )->functions[i].name );
                }
            }

            free( ( *s )->functions );
        }

        /* Free off any sources dynamic memory we allocated */
        if ( ( *s )->sources ) {
            for ( uint32_t i = 0; i < ( *s )->sourceCount; i++ ) {
                if ( ( *s )->sources[i].lineText ) {
                    free( ( *s )->sources[i].lineText );
                }

                /* For any source line, free off it's assembly if there is some */
                if ( ( *s )->sources[i].assy ) {
                    if ( ( *s )->sources[i].assy->label ) {
                        free( ( *s )->sources[i].assy->label );
                    }

                    for ( uint32_t j = 0; j < ( *s )->sources[i].assyLines; j++ ) {
                        if ( ( *s )->sources[i].assy[j].lineText ) {
                            free( ( *s )->sources[i].assy[j].lineText );
                        }
                    }

                    free( ( *s )->sources[i].assy );
                }
            }

            free( ( *s )->sources );
        }

        if ( ( *s )->deleteMaterial ) {
            free( ( *s )->deleteMaterial );
        }

        if ( ( *s )->odoptions ) {
            free( ( *s )->odoptions );
        }

        free( *s );
        *s = NULL;
    }
}
// ====================================================================================================
bool SymbolSetValid( struct SymbolSet **s, char *filename )

/* Check if current symbol set remains valid */

{
    struct stat n;

    if ( 0 != stat( filename, &n ) ) {
        /* We can't even stat the file, assume it's invalid */
        SymbolSetDelete( s );
        return false;
    }

    /* We check filesize, modification time and status change time for any differences */
    if ( ( !( *s ) ) ||
         ( memcmp( &n.st_size, &( ( *s )->st.st_size ), sizeof( off_t ) ) ) ||

#ifdef OSX
         ( memcmp( &n.st_mtimespec, &( ( *s )->st.st_mtimespec ), sizeof( struct timespec ) ) ) ||
         ( memcmp( &n.st_ctimespec, &( ( *s )->st.st_ctimespec ), sizeof( struct timespec ) ) )
#elif WIN32
         ( memcmp( &n.st_mtime, &( ( *s )->st.st_mtime ), sizeof( n.st_mtime ) ) ) ||
         ( memcmp( &n.st_ctime, &( ( *s )->st.st_ctime ), sizeof( n.st_ctime ) ) )
#else
         ( memcmp( &n.st_mtim, &( ( *s )->st.st_mtim ), sizeof( struct timespec ) ) ) ||
         ( memcmp( &n.st_ctim, &( ( *s )->st.st_ctim ), sizeof( struct timespec ) ) )
#endif
       ) {
        SymbolSetDelete( s );
        return false;
    } else {
        return true;
    }
}
// ====================================================================================================
// Malloc leak is deliberately ignored. That is the central purpose of this code!
#pragma GCC diagnostic push
#if !defined(__clang__)
    #pragma GCC diagnostic ignored "-Wanalyzer-malloc-leak"
#endif
enum symbolErr SymbolSetCreate( struct SymbolSet **ss, const char *filename, const char *deleteMaterial,
                                bool demanglecpp, bool recordSource, bool recordAssy, const char *objdumpOptions )

/* Create new symbol set by reading from elf file, if it's there and stable */

{
    struct stat statbuf, newstatbuf;
    struct SymbolSet *s;
    enum symbolErr  ret = SYMBOL_UNSPECIFIED;

    s = ( struct SymbolSet * )calloc( sizeof( struct SymbolSet ), 1 );
    MEMCHECK( s, SYMBOL_OOM );

    s->odoptions        = strdup( objdumpOptions ? objdumpOptions : "" );
    MEMCHECK( s->odoptions, SYMBOL_OOM );
    s->elfFile          = strdup( filename );
    MEMCHECK( s->elfFile, SYMBOL_OOM );
    s->deleteMaterial   = strdup( deleteMaterial ? deleteMaterial : "" );
    MEMCHECK( s->deleteMaterial, SYMBOL_OOM );
    s->recordSource     = recordSource;
    s->demanglecpp      = demanglecpp;
    s->recordAssy       = recordAssy;


    /* Make sure this file is stable before trying to load it */
    if ( ( stat( filename, &statbuf ) != 0 ) || !( statbuf.st_mode & S_IFREG ) ) {
        ret = SYMBOL_NOELF;
    } else {
        /* There is at least a file here */

        while ( 1 ) {
            usleep( ELF_RELOAD_DELAY_TIME );

            if ( stat( filename, &newstatbuf ) == 0 ) {
                /* We check filesize, modification time and status change time for any differences */
                if (
                            ( !newstatbuf.st_size ) ||
                            ( memcmp( &statbuf.st_size, &newstatbuf.st_size, sizeof( off_t ) ) ) ||
#ifdef OSX
                            ( memcmp( &statbuf.st_mtimespec, &newstatbuf.st_mtimespec, sizeof( struct timespec ) ) ) ||
                            ( memcmp( &statbuf.st_ctimespec, &newstatbuf.st_ctimespec, sizeof( struct timespec ) ) )
#elif WIN32
                            ( memcmp( &statbuf.st_mtime, &newstatbuf.st_mtime, sizeof( statbuf.st_mtime ) ) ) ||
                            ( memcmp( &statbuf.st_ctime, &newstatbuf.st_ctime, sizeof( statbuf.st_ctime ) ) )
#else
                            ( memcmp( &statbuf.st_mtim, &newstatbuf.st_mtim, sizeof( struct timespec ) ) ) ||
                            ( memcmp( &statbuf.st_ctim, &newstatbuf.st_ctim, sizeof( struct timespec ) ) )
#endif
                ) {
                    /* Make this the version we check next time around */
                    memcpy( &statbuf, &newstatbuf, sizeof( struct stat ) );
                    continue;
                } else {
                    break;
                }
            }
        }

        /* File is stable, let's grab stuff from it */
        ret =  _getTargetProgramInfo( s );
    }

    if ( ret != SYMBOL_OK ) {
        SymbolSetDelete( &s );
        s = NULL;
    }

    *ss = s;
    return ret;
}
#pragma GCC diagnostic pop
// ====================================================================================================
