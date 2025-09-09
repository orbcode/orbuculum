/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * RTOS Support for Orbuculum
 * ==========================
 *
 * Generic RTOS thread tracking support
 */

#ifndef _RTOS_SUPPORT_H_
#define _RTOS_SUPPORT_H_

#include <stdint.h>
#include <stdbool.h>
#include "uthash.h"

/* RTOS Thread Entry */
struct rtosThread {
    /* Thread identification */
    uint32_t tcb_addr;              /* Thread control block address (key) */
    char name[64];                  /* Thread name (from memory or "UNNAMED") */
    uint32_t entry_func;            /* Thread entry function address */
    const char *entry_func_name;    /* Thread entry function name from symbols */
    int8_t priority;                /* Thread priority (signed for RTX5) */
    uint32_t name_ptr;              /* Puntero al nombre en memoria (debug) */

    /* Execution time tracking */
    uint64_t accumulated_time_us;   /* Accumulated execution time in current window (microseconds) */
    uint64_t accumulated_cycles;    /* Accumulated CPU cycles in current window */
    uint64_t last_scheduled_us;     /* Timestamp when thread became active (microseconds) */
    uint64_t context_switches;      /* Total number of times scheduled (all time) */
    uint64_t window_switches;       /* Number of switches in current window */
    uint32_t max_cpu_percent;       /* Maximum CPU usage seen (in 0.001% units) */
    
    /* Thread state tracking for reuse detection */
    uint32_t name_hash;             /* Hash of name for change detection */
    uint32_t func_hash;             /* Hash of function for change detection */
    
    UT_hash_handle hh;              /* Hash handle */
};

/* RTOS types */
enum rtosType {
    RTOS_NONE = 0,
    RTOS_RTX5,
    RTOS_FREERTOS,
    RTOS_THREADX,
    RTOS_UNKNOWN
};

/* RTOS verification result codes */
enum rtosVerifyResult {
    RTOS_VERIFY_SUCCESS = 0,        /* Target matches ELF */
    RTOS_VERIFY_NO_CONNECTION = 1,  /* Cannot connect to target (telnet down) */
    RTOS_VERIFY_MISMATCH = -1,      /* Target does not match ELF */
    RTOS_VERIFY_ERROR = -2          /* Other error */
};

/* Forward declarations */
struct rtosState;
struct rtosThread;
struct SymbolSet;
struct rtosDetection;

/* RTOS Operations - Virtual table for RTOS-specific operations */
struct rtosOps {
    /* Read thread information from target memory */
    int (*read_thread_info)(struct rtosState *rtos, 
                           struct SymbolSet *symbols,
                           struct rtosThread *thread, 
                           uint32_t tcb_addr);
    
    /* Get priority name string */
    const char* (*get_priority_name)(int8_t priority);
    
    /* Detect RTOS from symbols */
    bool (*detect)(struct SymbolSet *symbols, struct rtosDetection *result);
    
    /* Initialize RTOS-specific data */
    int (*init)(struct rtosState *rtos, struct SymbolSet *symbols);
    
    /* Cleanup RTOS-specific data */
    void (*cleanup)(struct rtosState *rtos);
    
    /* Get thread state name */
    const char* (*get_state_name)(uint8_t state);
    
    /* Check if a thread is the idle thread (optional) */
    bool (*is_idle_thread)(struct rtosThread *thread);
    
    /* Verify RTOS version match between ELF and target (optional) */
    int (*verify_target_match)(struct rtosState *rtos, struct SymbolSet *symbols);
};

/* RTOS State */
struct rtosState {
    enum rtosType type;                     /* Type of RTOS detected */
    bool enabled;                           /* Is RTOS tracking active? */
    const char *name;                       /* RTOS name string */
    
    /* Operations table */
    const struct rtosOps *ops;              /* RTOS-specific operations */
    
    /* Current state */
    uint32_t current_thread;                /* Currently executing thread TCB */
    uint64_t last_switch_time;              /* Timestamp of last context switch (system time) */
    uint32_t last_cyccnt;                   /* Last CYCCNT value (32-bit wrapping counter) */
    uint64_t total_cycles;                  /* Total accumulated cycles (handles wrapping) */
    uint32_t cpu_freq;                      /* CPU frequency in Hz for time calculations */
    
    /* Thread tracking */
    struct rtosThread *threads;             /* Hash table of all threads */
    uint32_t thread_count;                  /* Number of threads detected */
    uint32_t max_cpu_usage;                 /* Maximum CPU usage seen (in 0.01% units) */
    
    /* RTOS-specific private data */
    void *priv;                             /* Private data for RTOS implementation */
    
    /* Configuration */
    int telnet_port;                        /* Telnet port for GDB connection */
    
    /* Output configuration for real-time events */
    void *output_config;     /* Output handler for thread switches (OutputConfig*) */
};

/* RTOS detection result */
struct rtosDetection {
    enum rtosType type;
    const char *name;
    uint32_t confidence;    /* 0-100% confidence in detection */
    const char *reason;     /* Why this RTOS was detected */
};

/* Function declarations */

/* Main RTOS detection and initialization */
struct rtosState *rtosDetectAndInit(struct SymbolSet *symbols, const char *requested_type, int options_telnetPort, uint32_t cpu_freq);

/* Register RTOS implementations */
void rtosRegisterRTX5(void);
void rtosRegisterFreeRTOS(void);
void rtosRegisterThreadX(void);
void rtosFree(struct rtosState *rtos);

/* Thread name/function lookup from symbols */
const char *rtosLookupPointerAsString(struct SymbolSet *symbols, uint32_t ptr_value);
const char *rtosLookupPointerAsFunction(struct SymbolSet *symbols, uint32_t ptr_value);
bool rtosResolveThreadInfo(struct rtosThread *thread, struct SymbolSet *symbols,
                          uint32_t name_ptr, uint32_t func_ptr);


/* DWT handling */
void rtosHandleDWTMatch(struct rtosState *rtos, struct SymbolSet *symbols,
                       uint32_t comp_num, uint32_t address, uint32_t value, int options_telnetPort);

void rtosHandleDWTMatchWithTimestamp(struct rtosState *rtos, struct SymbolSet *symbols,
                       uint32_t comp_num, uint32_t address, uint32_t value, 
                       uint64_t itm_timestamp, int options_telnetPort);

/* Output functions */
void rtosDumpThreadInfo(struct rtosState *rtos, FILE *f, uint64_t window_time_us, bool itm_overflow, const char *sort_order);

/* Thread metrics update functions */
void rtosUpdateThreadCpuMetrics(struct rtosState *rtos, uint64_t window_time_us);
void rtosResetThreadCounters(struct rtosState *rtos);

/* Memory reading functions (implemented in orbtop_rtos.c) */
uint32_t rtosReadMemoryWord(uint32_t address);
char *rtosReadMemoryString(uint32_t address, char *buffer, size_t maxlen);

/* DWT configuration via telnet */
void rtosConfigureDWT(uint32_t watch_address);

/* Cache management */
void rtosClearMemoryCacheForTCB(uint32_t tcb_addr);

#endif /* _RTOS_SUPPORT_H_ */