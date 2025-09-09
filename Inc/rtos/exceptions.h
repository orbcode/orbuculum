#ifndef EXCEPTIONS_H
#define EXCEPTIONS_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_EXCEPTIONS      (512)
#define NO_EXCEPTION        (0xFFFFFFFF)

struct exceptionRecord {
    uint64_t visits;
    int64_t totalTime;
    int64_t minTime;
    int64_t maxTime;
    int64_t entryTime;
    int64_t maxWallTime;
    int64_t thisTime;
    int64_t stealTime;
    uint32_t prev;
    uint32_t maxDepth;
};

struct exceptionStats {
    struct exceptionRecord er[MAX_EXCEPTIONS];
    uint32_t exceptionActive;
    int64_t timeStamp;
    int64_t lastReportTicks;
};

const char* exceptionGetName(uint32_t exceptionNum);
void exceptionInit(struct exceptionStats *stats);
void exceptionEnter(struct exceptionStats *stats, uint32_t exceptionNum, int64_t timestamp);
void exceptionExit(struct exceptionStats *stats, int64_t timestamp);
void exceptionReset(struct exceptionStats *stats);
bool exceptionIsActive(struct exceptionStats *stats);

#endif