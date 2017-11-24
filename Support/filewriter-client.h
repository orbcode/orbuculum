#ifndef FILEWRITER_H_
#define FILEWRITER_H_

#include <stdint.h>
#include <stdbool.h>

// ============================================================================================
uint8_t fwOpenFile(const char *n, bool forAppend);
uint32_t fwWrite(const char *ptr, size_t size, size_t nmemb, uint32_t h);
uint32_t fwClose(uint32_t h);
void fwSeek(uint32_t h, uint32_t lcn);
void fwDeleteFile(const char *ptr);

void fwInit(void);
// ============================================================================================

#endif /* FILEWRITER_H_ */
