#include <stdio.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "filewriter-client.h"

// ============================================================================================
// Stub routines that get replaced by nosys.specs if not needed
// This file binds them to the filewriter primitives
// ============================================================================================
void _exit(int status) {
  (void)status;
  for(;;){} /* does not return */
}
// ============================================================================================
int _write(int file, const char *ptr, int len)

{
  return fwWrite(ptr, 1, len, file);
  return len; 
}
// ============================================================================================
int _open (const char *ptr, int mode)

{
  return fwOpenFile(ptr, ((mode&O_APPEND)!=0));
}
// ============================================================================================
int _close(int file)

{
  return fwClose(file);
}
// ============================================================================================
int _fstat(int file, struct stat *st)

{
  (void)file;
  (void)st;
  st->st_mode = S_IFCHR;
  return 0;
}
// ============================================================================================
int _getpid(void)

{
  return 1;
}
// ============================================================================================
int _isatty(int file)

{
    errno = EBADF;
    return 0;
}
// ============================================================================================
int _kill(int pid, int sig)

{
  (void)pid;
  (void)sig;
  errno = EINVAL;
  return (-1);
}
// ============================================================================================
int _lseek(int file, int ptr, int dir)

{
  (void)file;
  (void)ptr;
  (void)dir;
  return 0; /* return offset in file */
}
// ============================================================================================
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
__attribute__((naked)) static unsigned char *get_stackpointer(void) {
  __asm volatile (
    "mrs r0, msp   \r\n"
    "bx lr         \r\n"
  );
}
#pragma GCC diagnostic pop
// ============================================================================================
void *_sbrk(int incr) {
  extern char __HeapLimit; /* Defined by the linker file */
  static char *heap_end = 0;
  char *prev_heap_end;
  char *stack;

  if (heap_end==0) {
    heap_end = &__HeapLimit;
  }
  prev_heap_end = heap_end;
  stack = get_stackpointer();

  if (heap_end+incr > stack) {
    _write (STDERR_FILENO, "Heap and stack collision\n", 25);
    errno = ENOMEM;
    return  (void *)-1;
  }
  heap_end += incr;
  return (void *)prev_heap_end;
}
// ============================================================================================
int _read(int file, char *ptr, int len)

{
  (void)file;
  (void)ptr;
  (void)len;
  return 0; /* zero means end of file */
}
// ============================================================================================
