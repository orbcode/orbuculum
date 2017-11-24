/* avoid issues with libbfd expecting to have
 * an automake config.h
 *
 * see:
 * https://sourceware.org/bugzilla/show_bug.cgi?id=14243
 * https://github.com/mlpack/mlpack/pull/575/commits/eedd99da548a92a05788a69fae05fb5024f079c0
 * https://github.com/Grive/grive/issues/204
 */

#ifndef PACKAGE
# define PACKAGE
#ifndef PACKAGE_VERSION
    #define PACKAGE_VERSION
    #include <bfd.h>
    #undef PACKAGE_VERSION
#else
    #include <bfd.h>
#endif
# undef PACKAGE
#else
#ifndef PACKAGE_VERSION
    #define PACKAGE_VERSION
    #include <bfd.h>
    #undef PACKAGE_VERSION
#else
    #include <bfd.h>
#endif
#endif

