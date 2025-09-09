#include <exceptions.h>
#include <string.h>
#include <stdio.h>

static const char *ExceptionNames[] = {
    [0] = "None",
    [1] = "Reset",
    [2] = "NMI",
    [3] = "HardFault",
    [4] = "MemManage",
    [5] = "BusFault",
    [6] = "UsageFault",
    [7] = "Reserved",
    [8] = "Reserved",
    [9] = "Reserved",
    [10] = "Reserved",
    [11] = "SVCall",
    [12] = "DebugMonitor",
    [13] = "Reserved",
    [14] = "PendSV",
    [15] = "SysTick",
};

const char* exceptionGetName(uint32_t exceptionNum)
{
    static char irqName[30];
    
    if (exceptionNum < 16) {
        return ExceptionNames[exceptionNum];
    }
    
    snprintf(irqName, sizeof(irqName), "IRQ %d", exceptionNum - 16);
    return irqName;
}

void exceptionInit(struct exceptionStats *stats)
{
    memset(stats, 0, sizeof(struct exceptionStats));
    stats->exceptionActive = NO_EXCEPTION;
}

void exceptionEnter(struct exceptionStats *stats, uint32_t exceptionNum, int64_t timestamp)
{
    if (exceptionNum >= MAX_EXCEPTIONS) {
        return;
    }
    
    stats->er[exceptionNum].prev = stats->exceptionActive;
    stats->er[exceptionNum].entryTime = timestamp;
    stats->er[exceptionNum].thisTime = 0;
    stats->er[exceptionNum].stealTime = 0;
    stats->er[exceptionNum].maxDepth = 
        (stats->exceptionActive != NO_EXCEPTION && 
         stats->er[exceptionNum].maxDepth < stats->er[stats->exceptionActive].maxDepth + 1) ?
        stats->er[stats->exceptionActive].maxDepth + 1 : stats->er[exceptionNum].maxDepth;
    
    stats->exceptionActive = exceptionNum;
}

void exceptionExit(struct exceptionStats *stats, int64_t timestamp)
{
    if (stats->exceptionActive == NO_EXCEPTION) {
        return;
    }
    
    uint32_t e = stats->exceptionActive;
    int64_t exTime = timestamp - stats->er[e].entryTime - stats->er[e].stealTime;
    
    stats->er[e].visits++;
    stats->er[e].totalTime += exTime;
    stats->er[e].thisTime = exTime;
    
    if (stats->er[e].minTime > exTime || stats->er[e].minTime == 0) {
        stats->er[e].minTime = exTime;
    }
    
    if (stats->er[e].maxTime < exTime) {
        stats->er[e].maxTime = exTime;
    }
    
    int64_t wallTime = timestamp - stats->er[e].entryTime;
    if (stats->er[e].maxWallTime < wallTime) {
        stats->er[e].maxWallTime = wallTime;
    }
    
    stats->exceptionActive = stats->er[e].prev;
    
    if (stats->exceptionActive != NO_EXCEPTION) {
        stats->er[stats->exceptionActive].stealTime += wallTime;
    }
}

void exceptionReset(struct exceptionStats *stats)
{
    for (uint32_t e = 0; e < MAX_EXCEPTIONS; e++) {
        memset(&stats->er[e], 0, sizeof(struct exceptionRecord));
    }
    stats->exceptionActive = NO_EXCEPTION;
}

bool exceptionIsActive(struct exceptionStats *stats)
{
    return stats->exceptionActive != NO_EXCEPTION;
}