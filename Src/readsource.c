
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "generics.h"

#define BLOCKSIZE    (65536)
#define MAX_LINE_LEN (4095)

// ====================================================================================================
// ====================================================================================================
// ====================================================================================================
// Externally Available routines
// ====================================================================================================
// ====================================================================================================
// ====================================================================================================

#if defined(WIN32)

char *readsourcefile( char *path, size_t *l )
{
    *l = 0;
    FILE *f = fopen( path, "r" );

    if ( f == NULL )
    {
        return NULL;
    }

    char *retBuffer = ( char * )malloc( BLOCKSIZE );

    size_t insize = fread( retBuffer, 1, BLOCKSIZE, f );
    *l = insize;

    while ( !feof( f ) )
    {
        retBuffer = ( char * )realloc( retBuffer, *l + BLOCKSIZE );

        if ( !retBuffer )
        {
            genericsExit( -1, "Out of memory" EOL );
        }

        insize = fread( &retBuffer[*l], 1, BLOCKSIZE, f );
        *l += insize;
    }

    fclose( f );

    retBuffer = ( char * )realloc( retBuffer, *l + 1 );

    if ( !retBuffer )
    {
        genericsExit( -1, "Out of memory" EOL );
    }

    retBuffer[*l] = '\0';

    return retBuffer;
}

#else

char *readsourcefile( char *path, size_t *l )

/* Return either a malloced buffer containing the source file, or NULL if the file isn't available */

{
    FILE *fd = NULL;
    static bool prettyPrinterTested = false;
    char commandLine[MAX_LINE_LEN];
    bool isProcess = true;

    char *retBuffer = ( char * )malloc( BLOCKSIZE );
    size_t insize = 0;

    if ( !prettyPrinterTested )
    {
        /* Try and grab the file via a prettyprinter. If that doesn't work, grab it via cat */
        if ( getenv( "ORB_PRETTYPRINTER" ) )
        {
            /* We have an environment variable containing the prettyprinter...lets use that */
            snprintf( commandLine, MAX_LINE_LEN, "%s %s", getenv( "ORB_PRETTYPRINTER" ), path );
        }
        else
        {
            /* No environment variable, use the default */
            snprintf( commandLine, MAX_LINE_LEN, "source-highlight -f esc -o STDOUT -i %s 2>/dev/null", path );
        }

        fd = popen( commandLine, "r" );

        /* Perform a single read...this will lead to a zero length read if the command wasn't valid */
        insize = fread( retBuffer, 1, BLOCKSIZE, fd );
    }

    if ( !insize )
    {
        if ( fd )
        {
            pclose( fd );
        }

        isProcess = false;
        prettyPrinterTested = true;

        if ( ( fd = fopen( path, "r" ) ) )
        {
            insize = fread( retBuffer, 1, BLOCKSIZE, fd );
        }
    }

    /* Record what we managed to read, by whichever mechanism */
    *l = insize;

    /* There may be more file to read...if so, then let's do it */
    while ( insize == BLOCKSIZE )
    {
        /* Make another block available */
        retBuffer = ( char * )realloc( retBuffer, *l + BLOCKSIZE );

        if ( !retBuffer )
        {
            genericsExit( -1, "Out of memory" EOL );
        }

        insize = fread( &retBuffer[*l], 1, BLOCKSIZE, fd );
        *l += insize;
    }

    /* Make sure we terminate with a 0 .. there will always be space for this */
    retBuffer[( *l )++] = 0;

    /* Close the process or file, depending on what it was that actually got opened */
    if ( fd )
    {
        isProcess ? pclose( fd ) : fclose( fd );
    }

    /* Resize the memory to what was actually read in */
    if ( *l )
    {
        retBuffer = ( char * )realloc( retBuffer, *l );

        if ( !retBuffer )
        {
            genericsExit( -1, "Out of memory" EOL );
        }
    }
    else
    {
        free( retBuffer );
        retBuffer = NULL;
    }

    return retBuffer;
}

#endif

// ====================================================================================================
