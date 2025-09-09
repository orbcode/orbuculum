#ifndef OPTIONS_H
#define OPTIONS_H

#include <stdint.h>
#include <stdbool.h>

enum Protocol { 
    PROT_OFLOW, 
    PROT_ITM, 
    PROT_UNKNOWN 
};

typedef struct {
    uint32_t tag;
    bool outputExceptions;
    bool forceITMSync;
    char *file;
    uint32_t hwOutputs;
    char *elffile;
    char *odoptions;
    char *json;
    char *ftrace;
    bool mono;
    int paceDelay;
    bool demangle;
    int64_t displayInterval;
    int port;
    char *server;
    enum Protocol protocol;
    char *rtos;
    char *rtosSort;
    int telnetPort;
    int udpPort;
    uint32_t cpuFreq;
    bool cpuFreqSpecified;
} ProgramOptions;

int options_parse(int argc, char *argv[], ProgramOptions *opts);
void options_print_help(const char *progName);
ProgramOptions* options_get_defaults(void);

#endif