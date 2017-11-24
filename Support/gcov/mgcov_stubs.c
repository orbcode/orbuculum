/*
 * coverage_stubs.c
 *
 *  These stubs are needed to generate coverage from an embedded target.
 */
#include <stdio.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include "mgcov_stubs.h"
// ============================================================================================
/* prototype */
void __gcov_flush(void);
// ============================================================================================
void mgcov_static_init(void)

/* call the coverage initializers if not done by startup code */

{
  void (**p)(void);
  extern uint32_t __init_array_start, __init_array_end; /* linker defined symbols, array of function pointers */
  uint32_t beg = (uint32_t)&__init_array_start;
  uint32_t end = (uint32_t)&__init_array_end-sizeof(p);

  while(beg<end) {
    p = (void(**)(void))beg; /* get function pointer */
    (*p)(); /* call constructor */
    beg += sizeof(p); /* next pointer */
  }
}
// ============================================================================================
void mgcov_report(void) {
  __gcov_flush();
}
// ============================================================================================
