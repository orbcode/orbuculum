#ifndef TELNET_CLIENT_H
#define TELNET_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

int telnet_connect(int port);
void telnet_disconnect(void);
bool telnet_is_connected(void);

uint32_t telnet_read_memory_word(uint32_t address);
char* telnet_read_memory_string(uint32_t address, char *buffer, size_t maxlen);
void telnet_clear_cache_for_tcb(uint32_t tcb_addr);
void telnet_configure_dwt(uint32_t watch_address);
void telnet_configure_exception_trace(bool enable);

#endif