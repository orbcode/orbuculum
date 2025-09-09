#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "generics.h"
#include "symbols.h"
#include "rtos_support.h"
#include <rtos/rtx5.h>
#include <output_handler.h>
#include "uthash.h"


struct rtx5_private {
    uint32_t osRtxInfo;
    uint32_t thread_run_curr;
    uint32_t pendSV_Handler;
    uint32_t osRtxThreadListPut;
};

/* Hash for unresolved function addresses */
struct unresolvedFunc {
    uint32_t addr;
    UT_hash_handle hh;
};
static struct unresolvedFunc *unresolvedFuncs = NULL;

/* Sort functions for threads */
static int cpu_usage_sort_desc(void *a, void *b) 
{
    struct rtosThread *ta = (struct rtosThread *)a;
    struct rtosThread *tb = (struct rtosThread *)b;
    if (tb->accumulated_time_us > ta->accumulated_time_us) return 1;
    if (tb->accumulated_time_us < ta->accumulated_time_us) return -1;
    return 0;
}

static int max_cpu_sort_desc(void *a, void *b) 
{
    struct rtosThread *ta = (struct rtosThread *)a;
    struct rtosThread *tb = (struct rtosThread *)b;
    if (tb->max_cpu_percent > ta->max_cpu_percent) return 1;
    if (tb->max_cpu_percent < ta->max_cpu_percent) return -1;
    return 0;
}

static int tcb_addr_sort_asc(void *a, void *b) 
{
    struct rtosThread *ta = (struct rtosThread *)a;
    struct rtosThread *tb = (struct rtosThread *)b;
    if (ta->tcb_addr > tb->tcb_addr) return 1;
    if (ta->tcb_addr < tb->tcb_addr) return -1;
    return 0;
}

static int name_sort_asc(void *a, void *b) 
{
    struct rtosThread *ta = (struct rtosThread *)a;
    struct rtosThread *tb = (struct rtosThread *)b;
    return strcmp(ta->name, tb->name);
}

static int func_sort_asc(void *a, void *b) 
{
    struct rtosThread *ta = (struct rtosThread *)a;
    struct rtosThread *tb = (struct rtosThread *)b;
    const char *fa = ta->entry_func_name ? ta->entry_func_name : "";
    const char *fb = tb->entry_func_name ? tb->entry_func_name : "";
    return strcmp(fa, fb);
}

static int priority_sort_desc(void *a, void *b) 
{
    struct rtosThread *ta = (struct rtosThread *)a;
    struct rtosThread *tb = (struct rtosThread *)b;
    if (tb->priority > ta->priority) return 1;
    if (tb->priority < ta->priority) return -1;
    return 0;
}

static int switches_sort_desc(void *a, void *b) 
{
    struct rtosThread *ta = (struct rtosThread *)a;
    struct rtosThread *tb = (struct rtosThread *)b;
    if (tb->context_switches > ta->context_switches) return 1;
    if (tb->context_switches < ta->context_switches) return -1;
    return 0;
}

/* Helper function to find osRtxInfo address from ELF file */
static uint32_t findOsRtxInfoAddress(const char *elfFile)
{
    if (!elfFile) return 0;
    
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "arm-none-eabi-objdump -t %s 2>/dev/null | grep 'osRtxInfo$'", elfFile);
    
    FILE *fp = popen(cmd, "r");
    if (!fp) return 0;
    
    char line[256];
    uint32_t address = 0;
    
    if (fgets(line, sizeof(line), fp)) {
        /* Parse line like: "20004000 g     O RW_KERNEL	000000a4 .hidden osRtxInfo" */
        char *endptr;
        address = strtoul(line, &endptr, 16);
        if (endptr == line) address = 0; /* Conversion failed */
    }
    
    pclose(fp);
    return address;
}


/* Initialize RTOS tracking */
struct rtosState *rtosDetectAndInit(struct SymbolSet *symbols, const char *requested_type, int options_telnetPort, uint32_t cpu_freq)
{
    if (!requested_type) return NULL;
    
    struct rtosState *rtos = calloc(1, sizeof(struct rtosState));
    if (!rtos) return NULL;
    
    rtos->cpu_freq = cpu_freq;
    rtos->telnet_port = options_telnetPort;
    
    if (strcasecmp(requested_type, "rtx5") == 0 || 
        strcasecmp(requested_type, "rtxv5") == 0) {
        rtos->type = RTOS_RTX5;
        rtos->name = "RTX5";
        rtos->ops = rtx5GetOps();
        
        if (rtos->ops && rtos->ops->init) {
            if (rtos->ops->init(rtos, symbols) < 0) {
                genericsReport(V_ERROR, "Failed to initialize RTX5" EOL);
                free(rtos);
                return NULL;
            }
        }
        
        /* Verify target match if telnet is configured */
        if (rtos->ops && rtos->ops->verify_target_match && options_telnetPort > 0) {
            int verify_result = rtos->ops->verify_target_match(rtos, symbols);
            if (verify_result == RTOS_VERIFY_MISMATCH) {
                /* Only exit on real mismatch */
                free(rtos);
                return NULL;
            } else if (verify_result == RTOS_VERIFY_NO_CONNECTION) {
                genericsReport(V_INFO, "RTOS verification pending - telnet not ready yet" EOL);
            }
        }
        
        /* Initialize timing for first thread detection */
        rtos->last_switch_time = genericsTimestampuS();
        
        if (rtos->priv) {
            struct rtx5_private *priv = (struct rtx5_private*)rtos->priv;
            if (priv->thread_run_curr && options_telnetPort > 0) {
                genericsReport(V_INFO, "Configuring DWT for address 0x%08X via telnet" EOL, priv->thread_run_curr);
                rtosConfigureDWT(priv->thread_run_curr);
            } else if (options_telnetPort <= 0) {
                genericsReport(V_WARN, "Telnet not configured, DWT not auto-configured" EOL);
            }
        }
        
        rtos->enabled = true;
    } else {
        genericsReport(V_ERROR, "Unknown RTOS type: %s" EOL, requested_type);
        free(rtos);
        return NULL;
    }
    
    return rtos;
}

void rtosFree(struct rtosState *rtos)
{
    if (!rtos) return;
    
    struct rtosThread *thread, *tmp;
    HASH_ITER(hh, rtos->threads, thread, tmp) {
        HASH_DEL(rtos->threads, thread);
        free(thread);
    }
    
    if (rtos->ops && rtos->ops->cleanup) {
        rtos->ops->cleanup(rtos);
    }
    
    free(rtos);
}

/* Lookup pointer as string in symbols */
const char *rtosLookupPointerAsString(struct SymbolSet *symbols, uint32_t ptr_value)
{
    if (!ptr_value || !symbols) return NULL;
    
    struct nameEntry n;
    if (SymbolLookup(symbols, ptr_value, &n)) {
        const char *name = SymbolFunction(symbols, n.functionindex);
        if (name && name[0] != '\0' && name[0] != '.') {
            return name;
        }
    }
    
    return NULL;
}

/* Lookup pointer as function in symbols */
const char *rtosLookupPointerAsFunction(struct SymbolSet *symbols, uint32_t ptr_value)
{
    if (!symbols || !ptr_value || ptr_value == 0xFFFFFFFF) return NULL;
    // Intentar con &~1 (limpiar Thumb)
    const char *name = rtosLookupPointerAsString(symbols, ptr_value & ~1);
    if (name) return name;
    // Try with original address
    name = rtosLookupPointerAsString(symbols, ptr_value);
    if (name) return name;
    // Try with address -1 (in case Thumb is at +1)
    if (ptr_value > 0) {
        name = rtosLookupPointerAsString(symbols, ptr_value - 1);
        if (name) return name;
    }
    // Symbol not found
    struct unresolvedFunc *uf;
    HASH_FIND_INT(unresolvedFuncs, &ptr_value, uf);
    if (!uf) {
        uf = malloc(sizeof(struct unresolvedFunc));
        uf->addr = ptr_value;
        HASH_ADD_INT(unresolvedFuncs, addr, uf);
        genericsReport(V_WARN, "No symbol found for function at 0x%08X" EOL, ptr_value);
    }
    return "Unknown Function";
}

/* Resolve thread info from pointers */
bool rtosResolveThreadInfo(struct rtosThread *thread, struct SymbolSet *symbols,
                          uint32_t name_ptr, uint32_t func_ptr)
{
    if (!thread || !symbols) return false;
    
    bool resolved = false;
    
    if (name_ptr && !thread->entry_func_name) {
        const char *name = rtosLookupPointerAsString(symbols, name_ptr);
        if (name) {
            strncpy(thread->name, name, sizeof(thread->name) - 1);
            thread->name[sizeof(thread->name) - 1] = '\0';
            resolved = true;
        }
    }
    
    if (func_ptr && !thread->entry_func_name) {
        const char *func = rtosLookupPointerAsFunction(symbols, func_ptr);
        if (func) {
            thread->entry_func = func_ptr & ~1;
            thread->entry_func_name = func;
            resolved = true;
        }
    }
    
    return resolved;
}

static const char* rtosGetThreadName(struct rtosState *rtos, uint32_t tcb_addr)
{
    if (!rtos || !tcb_addr) return "NULL";
    struct rtosThread *thread;
    HASH_FIND_INT(rtos->threads, &tcb_addr, thread);
    return thread ? thread->name : "UNKNOWN";
}

/* Handle DWT match with ITM timestamp */
void rtosHandleDWTMatchWithTimestamp(struct rtosState *rtos, struct SymbolSet *symbols,
                       uint32_t comp_num, uint32_t address, uint32_t value, 
                       uint64_t itm_timestamp, int options_telnetPort)
{
    if (!rtos || !rtos->enabled) return;
    
    /* ITM timestamps come from CYCCNT which is 32-bit, handle as 32-bit to detect wraparound */
    uint32_t current_cyccnt = (uint32_t)(itm_timestamp & 0xFFFFFFFF);
    
    struct rtosThread *thread;
    HASH_FIND_INT(rtos->threads, &value, thread);
    
    if (!thread) {
        thread = calloc(1, sizeof(struct rtosThread));
        if (!thread) return;
        thread->tcb_addr = value;
        HASH_ADD_INT(rtos->threads, tcb_addr, thread);
        rtos->thread_count++;
        
        if (options_telnetPort > 0) {
            int read_result = 0;
            if (rtos->ops && rtos->ops->read_thread_info) {
                read_result = rtos->ops->read_thread_info(rtos, symbols, thread, value);
            } else {
                strcpy(thread->name, "UNKNOWN");
                thread->priority = 0;
            }
            
            if (read_result < 0) {
                genericsReport(V_DEBUG, "Failed to read thread info for TCB=0x%08X - removing from tracking\n", value);
                HASH_DEL(rtos->threads, thread);
                free(thread);
                return;
            } else if (read_result > 0) {
                genericsReport(V_INFO, "Thread reuse detected for TCB=0x%08X - clearing cache\n", value);
                rtosClearMemoryCacheForTCB(value);
            }
            
            genericsReport(V_INFO, "New thread detected: TCB=0x%08X, Name='%s', Func=0x%08X/%s, Prio=%d\n", 
                thread->tcb_addr, thread->name, thread->entry_func, 
                thread->entry_func_name ? thread->entry_func_name : "-", thread->priority);
        } else {
            strcpy(thread->name, "UNNAMED");
            thread->priority = 0;
        }
    }
    
    if (rtos->current_thread) {
        struct rtosThread *prev_thread;
        uint32_t prev_tcb = rtos->current_thread;
        HASH_FIND_INT(rtos->threads, &prev_tcb, prev_thread);
        if (prev_thread && rtos->last_cyccnt != 0) {
            /* Calculate delta handling 32-bit wraparound */
            uint32_t delta_cycles;
            if (current_cyccnt >= rtos->last_cyccnt) {
                /* Normal case: no wraparound */
                delta_cycles = current_cyccnt - rtos->last_cyccnt;
            } else {
                /* Wraparound occurred */
                delta_cycles = (0xFFFFFFFF - rtos->last_cyccnt) + current_cyccnt + 1;
            }
            
            if (delta_cycles > 0 && delta_cycles < 0x80000000) {
                uint32_t delta_time_us = rtos->cpu_freq > 0 ? delta_cycles / (rtos->cpu_freq / 1000000) : 0;
                
                if (delta_time_us > 10000) {
                    genericsReport(V_INFO, "Long timeslice: %u us (%u cycles) for TCB=0x%08X (%s)\n", 
                        delta_time_us, delta_cycles, prev_thread->tcb_addr, prev_thread->name);
                }
                
                prev_thread->accumulated_time_us += delta_time_us;
                prev_thread->accumulated_cycles += delta_cycles;
                genericsReport(V_DEBUG, "Thread TCB=0x%08X ran for %u us, total=%" PRIu64 " us\n", 
                    prev_thread->tcb_addr, delta_time_us, prev_thread->accumulated_time_us);
            }
        }
    }
    
    thread->last_scheduled_us = current_cyccnt;  /* Store current CYCCNT value */
    
    if (rtos->current_thread != value) {
        /* Only count as a context switch if it's actually a different thread */
        thread->context_switches++;
        thread->window_switches++;
        
        struct rtosThread *prev_thread = NULL;
        if (rtos->current_thread) {
            HASH_FIND_INT(rtos->threads, &rtos->current_thread, prev_thread);
        }
        
        genericsReport(V_DEBUG, "Context switch: 0x%08X (%s) → 0x%08X (%s)\n", 
            rtos->current_thread, 
            rtos->current_thread ? rtosGetThreadName(rtos, rtos->current_thread) : "NULL",
            thread->tcb_addr, thread->name);
        
        if (rtos->output_config) {
            genericsReport(V_DEBUG, "Calling output_thread_switch with output_config=%p\n", rtos->output_config);
            output_thread_switch((OutputConfig *)rtos->output_config, prev_thread, thread, itm_timestamp);
        } else {
            genericsReport(V_DEBUG, "No output_config set for RTOS, skipping thread_switch output\n");
        }
        
        rtos->current_thread = value;
        /* Update last CYCCNT for the new thread */
        rtos->last_cyccnt = current_cyccnt;
    } else if (current_cyccnt != rtos->last_cyccnt) {
        /* Same thread but new timestamp - update for accurate timing */
        rtos->last_cyccnt = current_cyccnt;
    }
}

/* Handle DWT match - legacy function that uses system time */
void rtosHandleDWTMatch(struct rtosState *rtos, struct SymbolSet *symbols,
                       uint32_t comp_num, uint32_t address, uint32_t value, int options_telnetPort)
{
    /* Handle thread switch from DWT watchpoint */
    if (!rtos || !rtos->enabled) return;
    
    /* Get current timestamp in microseconds */
    uint64_t current_time_us = genericsTimestampuS();
    
    /* Value is the thread TCB address that just became active */
    struct rtosThread *thread;
    HASH_FIND_INT(rtos->threads, &value, thread);
    
    if (!thread) {
        /* New thread - create and read memory info ONLY ONCE */
        thread = calloc(1, sizeof(struct rtosThread));
        if (!thread) return;
        thread->tcb_addr = value;
        HASH_ADD_INT(rtos->threads, tcb_addr, thread);
        rtos->thread_count++;
        
        /* ONLY read memory for NEW threads */
        if (options_telnetPort > 0) {
            /* Delegate to specific RTOS to read thread details */
            int read_result = 0;
            if (rtos->ops && rtos->ops->read_thread_info) {
                read_result = rtos->ops->read_thread_info(rtos, symbols, thread, value);
            } else {
                /* Generic fallback if no specific handler */
                strcpy(thread->name, "UNKNOWN");
                thread->priority = 0;
            }
            
            /* Check read result */
            if (read_result < 0) {
                /* Read failed - remove the thread from hash as it's invalid */
                genericsReport(V_WARN, "Failed to read thread info for TCB=0x%08X - removing from tracking\n", value);
                HASH_DEL(rtos->threads, thread);
                free(thread);
                return;
            } else if (read_result > 0) {
                /* Thread was reused - clear cache for this TCB */
                genericsReport(V_INFO, "Thread reuse detected for TCB=0x%08X - clearing cache\n", value);
                rtosClearMemoryCacheForTCB(value);
            }
            
            genericsReport(V_INFO, "New thread detected: TCB=0x%08X, Name='%s', Func=0x%08X/%s, Prio=%d\n", 
                thread->tcb_addr, thread->name, thread->entry_func, 
                thread->entry_func_name ? thread->entry_func_name : "-", thread->priority);
        } else {
            strcpy(thread->name, "UNNAMED");
            thread->priority = 0;
        }
    }
    
    if (rtos->current_thread && rtos->last_switch_time > 0) {
        struct rtosThread *prev_thread;
        uint32_t prev_tcb = rtos->current_thread;
        HASH_FIND_INT(rtos->threads, &prev_tcb, prev_thread);
        if (prev_thread) {
            uint64_t delta_time_us = current_time_us - rtos->last_switch_time;
            
            if (delta_time_us > 10000) {
                genericsReport(V_INFO, "Long timeslice: %" PRIu64 " us for TCB=0x%08X (%s)\n", 
                    delta_time_us, prev_thread->tcb_addr, prev_thread->name);
            }
            
            prev_thread->accumulated_time_us += delta_time_us;
            genericsReport(V_DEBUG, "Thread TCB=0x%08X ran for %" PRIu64 " us, total=%" PRIu64 " us\n", 
                prev_thread->tcb_addr, delta_time_us, prev_thread->accumulated_time_us);
        }
    }
    
    thread->last_scheduled_us = current_time_us;
    thread->context_switches++;
    thread->window_switches++;
    
    if (rtos->current_thread != value) {
        struct rtosThread *prev_thread = NULL;
        if (rtos->current_thread) {
            HASH_FIND_INT(rtos->threads, &rtos->current_thread, prev_thread);
        }
        
        genericsReport(V_DEBUG, "Context switch: 0x%08X (%s) → 0x%08X (%s)\n", 
            rtos->current_thread, 
            rtos->current_thread ? rtosGetThreadName(rtos, rtos->current_thread) : "NULL",
            thread->tcb_addr, thread->name);
        
        if (rtos->output_config) {
            output_thread_switch((OutputConfig *)rtos->output_config, prev_thread, thread, current_time_us);
        }
    }
    
    rtos->current_thread = value;
    rtos->last_switch_time = current_time_us;
}

/* External sort method selection */

/* Helper structure for column widths */
struct ColumnWidths {
    int name;
    int address;  /* Fixed at 10 for 0xXXXXXXXX */
    int function;
    int priority;
    int time;
    int cpu;
    int max;
    int switches;
};

/* Print horizontal separator line for table */
static void printTableSeparator(FILE *f, struct ColumnWidths *widths)
{
    fprintf(f, "|");
    for (int i = 0; i < widths->name + 2; i++) fprintf(f, "-");
    fprintf(f, "|");
    for (int i = 0; i < widths->address + 2; i++) fprintf(f, "-");
    fprintf(f, "|");
    for (int i = 0; i < widths->function + 2; i++) fprintf(f, "-");
    fprintf(f, "|");
    for (int i = 0; i < widths->priority + 2; i++) fprintf(f, "-");
    fprintf(f, "|");
    for (int i = 0; i < widths->time + 2; i++) fprintf(f, "-");
    fprintf(f, "|");
    for (int i = 0; i < widths->cpu + 2; i++) fprintf(f, "-");
    fprintf(f, "|");
    for (int i = 0; i < widths->max + 2; i++) fprintf(f, "-");
    fprintf(f, "|");
    for (int i = 0; i < widths->switches + 2; i++) fprintf(f, "-");
    fprintf(f, "|\n");
}

/* Get function string for a thread */
static void getThreadFunctionString(struct rtosThread *thread, char *buf, size_t bufsize)
{
    if (thread->entry_func_name && thread->entry_func) {
        snprintf(buf, bufsize, "%s", thread->entry_func_name);
    } else if (thread->entry_func && thread->entry_func != 0xFFFFFFFF) {
        snprintf(buf, bufsize, "0x%08X", thread->entry_func);
    } else {
        strcpy(buf, "-");
    }
}

/* Calculate column widths based on thread data */
static void calculateColumnWidths(struct rtosState *rtos, struct ColumnWidths *widths)
{
    struct rtosThread *thread, *tmp;
    
    /* Initialize with header widths */
    widths->name = strlen("Thread Name");
    widths->address = 10;  /* Fixed for 0xXXXXXXXX format */
    widths->function = strlen("Function");
    widths->priority = strlen("Priority");
    widths->time = strlen("Time(ms)");
    widths->cpu = 7;  /* Fixed width for XXX.XXX format */
    widths->max = 7;  /* Fixed width for XXX.XXX format */
    widths->switches = strlen("Switches");
    
    /* Calculate actual maximum widths from thread data */
    HASH_ITER(hh, rtos->threads, thread, tmp) {
        /* Thread name */
        int len = strlen(thread->name);
        if (len > widths->name) widths->name = len;
        
        /* Function name */
        char func_str[64];
        getThreadFunctionString(thread, func_str, sizeof(func_str));
        len = strlen(func_str);
        if (len > widths->function) widths->function = len;
        
        /* Priority name */
        const char *pri_name = rtx5GetPriorityName(thread->priority);
        len = strlen(pri_name);
        if (len > widths->priority) widths->priority = len;
        
        /* Time */
        char time_str[32];
        snprintf(time_str, sizeof(time_str), "%llu", (unsigned long long)(thread->accumulated_time_us / 1000));
        len = strlen(time_str);
        if (len > widths->time) widths->time = len;
        
        /* Switches */
        char switches_str[32];
        snprintf(switches_str, sizeof(switches_str), "%llu", (unsigned long long)thread->window_switches);
        len = strlen(switches_str);
        if (len > widths->switches) widths->switches = len;
    }
    
    /* Add padding for readability */
    widths->name += 2;
    widths->function += 2;
    widths->priority += 2;
    widths->time += 2;
    widths->switches += 2;
}

/* Print table header */
static void printTableHeader(FILE *f, struct ColumnWidths *widths)
{
    /* Print top separator first */
    printTableSeparator(f, widths);
    
    /* Then print column headers */
    fprintf(f, "| %-*s | %-*s | %-*s | %-*s | %*s | %*s | %*s | %*s |\n",
            widths->name, "Thread Name",
            widths->address, "Address",
            widths->function, "Function",
            widths->priority, "Priority",
            widths->time, "Time(ms)",
            widths->cpu, "CPU%",
            widths->max, "Max%",
            widths->switches, "Switches");
}

/* Print a single thread row */
static void printThreadRow(FILE *f, struct ColumnWidths *widths, struct rtosThread *thread, 
                           struct rtosState *rtos, uint64_t window_time_us)
{
    /* Calculate CPU percentage - use cycles if available, otherwise time */
    uint32_t pct = 0;
    if (window_time_us > 0) {
        if (thread->accumulated_cycles > 0 && rtos->total_cycles > 0) {
            /* Calculate from cycles */
            pct = (thread->accumulated_cycles * 10000) / rtos->total_cycles;
        } else if (thread->accumulated_time_us > 0) {
            /* Fallback to time-based calculation */
            pct = (thread->accumulated_time_us * 10000) / window_time_us;
        }
        if (pct > 10000) pct = 10000;  /* Cap at 100% */
    }
    
    /* Update maximum if needed */
    if (pct > thread->max_cpu_percent) {
        thread->max_cpu_percent = pct;
    }
    
    /* Get thread info strings */
    char func_str[64];
    getThreadFunctionString(thread, func_str, sizeof(func_str));
    const char *pri_name = rtx5GetPriorityName(thread->priority);
    
    if (rtos->cpu_freq == 0) {
        fprintf(f, "| %-*s | 0x%08X | %-*s | %-*s | %*s | %*.3f | %*.3f | %*" PRIu64 " |\n",
            widths->name, thread->name,
            thread->tcb_addr,
            widths->function, func_str,
            widths->priority, pri_name,
            widths->time, "NA",
            widths->cpu, pct / 100.0,
            widths->max, thread->max_cpu_percent / 100.0,
            widths->switches, thread->window_switches);
    } else {
        uint64_t time_ms = (thread->accumulated_cycles * 1000) / rtos->cpu_freq;
        fprintf(f, "| %-*s | 0x%08X | %-*s | %-*s | %*" PRIu64 " | %*.3f | %*.3f | %*" PRIu64 " |\n",
            widths->name, thread->name,
            thread->tcb_addr,
            widths->function, func_str,
            widths->priority, pri_name,
            widths->time, time_ms,
            widths->cpu, pct / 100.0,
            widths->max, thread->max_cpu_percent / 100.0,
            widths->switches, thread->window_switches);
    }
}


/* Dump thread info */
void rtosDumpThreadInfo(struct rtosState *rtos, FILE *f, uint64_t window_time_us, bool itm_overflow, const char *sort_order)
{
    if (!rtos || !rtos->threads || !f) return;
    
    struct rtosThread *thread, *tmp;
    struct ColumnWidths widths;
    
    /* Check if RTOS has idle concept */
    bool has_idle_concept = (rtos->ops && rtos->ops->is_idle_thread);
    
    /* Calculate dynamic column widths */
    calculateColumnWidths(rtos, &widths);
    
    /* Print header */
    fprintf(f, "\n=== RTOS Thread Statistics (%s) ===\n", rtos->name);
    
    /* Print table header */
    printTableHeader(f, &widths);
    
    /* Print separator line */
    printTableSeparator(f, &widths);
    
    /* FIRST: Account for the currently running thread's time since last switch */
    /* NOTE: Skip this when using ITM timestamps as they're already accounted for in rtosHandleDWTMatchWithTimestamp */
    if (rtos->current_thread && rtos->last_switch_time > 0 && rtos->last_cyccnt == 0) {
        /* Only do final accounting if we're NOT using ITM timestamps (last_cyccnt would be > 0 if using ITM) */
        struct rtosThread *current;
        HASH_FIND_INT(rtos->threads, &rtos->current_thread, current);
        if (current) {
            uint64_t current_time_us = genericsTimestampuS();
            uint64_t delta_time_us = current_time_us - rtos->last_switch_time;
            current->accumulated_time_us += delta_time_us;
            genericsReport(V_DEBUG, "Final accounting: Thread TCB=0x%08X ran for %" PRIu64 " us in window tail\n", 
                current->tcb_addr, delta_time_us);
            /* Update timestamp so we don't double-count in next window */
            rtos->last_switch_time = current_time_us;
        }
    }
    
    uint64_t total_time_us = 0;
    
    /* Calculate total accumulated time (should equal window_time_us approximately) */
    HASH_ITER(hh, rtos->threads, thread, tmp) {
        total_time_us += thread->accumulated_time_us;
    }
    
    /* If no time recorded yet, use window time for calculations */
    if (total_time_us == 0) {
        total_time_us = window_time_us;
    }
    
    /* Sort threads based on selected method - update cpu_usage_sort to use accumulated_time_us */
    if (!sort_order || strcmp(sort_order, "cpu") == 0) {
        HASH_SORT(rtos->threads, cpu_usage_sort_desc);
    } else if (strcmp(sort_order, "maxcpu") == 0) {
        HASH_SORT(rtos->threads, max_cpu_sort_desc);
    } else if (strcmp(sort_order, "tcb") == 0) {
        HASH_SORT(rtos->threads, tcb_addr_sort_asc);
    } else if (strcmp(sort_order, "name") == 0) {
        HASH_SORT(rtos->threads, name_sort_asc);
    } else if (strcmp(sort_order, "func") == 0) {
        HASH_SORT(rtos->threads, func_sort_asc);
    } else if (strcmp(sort_order, "priority") == 0) {
        HASH_SORT(rtos->threads, priority_sort_desc);
    } else if (strcmp(sort_order, "switches") == 0) {
        HASH_SORT(rtos->threads, switches_sort_desc);
    } else {
        /* Default to CPU usage */
        HASH_SORT(rtos->threads, cpu_usage_sort_desc);
    }
    
    /* Track idle thread separately */
    struct rtosThread *idle_thread = NULL;
    
    /* Display each thread (except idle) */
    HASH_ITER(hh, rtos->threads, thread, tmp) {
        /* Skip invalid TCBs */
        if (thread->tcb_addr == 0x00000000 || thread->tcb_addr == 0xFFFFFFFF) {
            genericsReport(V_DEBUG, "Skipping invalid TCB: 0x%08X\n", thread->tcb_addr);
            continue;
        }
        
        /* Skip idle thread if RTOS can identify it - save for later */
        if (has_idle_concept && rtos->ops->is_idle_thread(thread)) {
            idle_thread = thread;
            continue;
        }
        
        /* Print regular thread row */
        printThreadRow(f, &widths, thread, rtos, window_time_us);
    }
    
    /* Print separator before idle thread if present */
    if (idle_thread) {
        printTableSeparator(f, &widths);
        printThreadRow(f, &widths, idle_thread, rtos, window_time_us);
    }
    
    /* Calculate CPU percentages */
    uint64_t total_accum_us = 0;
    uint64_t active_accum_us = 0;
    uint64_t total_cycles = 0;
    uint64_t active_cycles = 0;
    
    HASH_ITER(hh, rtos->threads, thread, tmp) {
        total_accum_us += thread->accumulated_time_us;
        total_cycles += thread->accumulated_cycles;
        
        /* If RTOS can identify idle threads, calculate active CPU separately */
        if (has_idle_concept && !rtos->ops->is_idle_thread(thread)) {
            active_accum_us += thread->accumulated_time_us;
            active_cycles += thread->accumulated_cycles;
        }
    }
    
    /* Store total cycles for percentage calculation */
    rtos->total_cycles = total_cycles;
    
    if (window_time_us > 0) {
        uint32_t display_pct;
        printTableSeparator(f, &widths);
        
        if (has_idle_concept) {
            /* Show active CPU (non-idle threads only) */
            display_pct = (active_accum_us * 10000) / window_time_us;
            
            /* Update max CPU usage if needed */
            if (display_pct > rtos->max_cpu_usage) {
                rtos->max_cpu_usage = display_pct;
            }
            
            fprintf(f, "Interval: %" PRIu64 " ms, CPU Usage: %.3f%%,  Max: %.3f%%, CPU Freq: ",
                window_time_us/1000, display_pct / 100.0, rtos->max_cpu_usage / 100.0);
            if (rtos->cpu_freq > 0) {
                fprintf(f, "%uHz", rtos->cpu_freq);
            } else {
                fprintf(f, "NA");
            }
        } else {
            /* Show total for RTOS without idle concept */
            display_pct = (total_accum_us * 10000) / window_time_us;
            fprintf(f, "Window: %" PRIu64 " ms, Total CPU: %.3f%%", 
                window_time_us/1000, display_pct / 100.0);
        }
        
        /* Show warning based on ITM overflow or percentage */
        /* For warnings, always use total (should be ~100%) */
        uint32_t total_pct = (total_accum_us * 10000) / window_time_us;
        if (itm_overflow) {
            fprintf(f, " [ITM OVERFLOW DETECTED!]");
        } else if (total_pct < 9500) {
            fprintf(f, " [WARNING: Low total - possible lost DWT events]");
        } else if (total_pct > 10500) {
            fprintf(f, " [WARNING: High total - timing issue?]");
        }
        fprintf(f, "\n");
    }
}

void rtosUpdateThreadCpuMetrics(struct rtosState *rtos, uint64_t window_time_us)
{
    if (!rtos || !rtos->enabled || !rtos->threads || window_time_us == 0)
        return;
    
    struct rtosThread *thread, *tmp;
    uint64_t active_accum_us = 0;
    uint64_t total_accum_us = 0;
    bool has_idle_concept = false;
    
    /* Calculate CPU percentages and update max values for all threads */
    HASH_ITER(hh, rtos->threads, thread, tmp) 
    {
        /* Calculate CPU percentage with proper scaling to avoid overflow */
        uint32_t cpu_pct = 0;
        if (window_time_us > 0)
        {
            uint64_t temp = (uint64_t)thread->accumulated_time_us * 10000ULL;
            cpu_pct = (uint32_t)(temp / window_time_us);
            if (cpu_pct > 10000) cpu_pct = 10000;  /* Cap at 100% */
        }
        
        /* Update max CPU percentage */
        if (cpu_pct > thread->max_cpu_percent)
        {
            thread->max_cpu_percent = cpu_pct;
        }
        
        /* Track totals for overall CPU usage calculation */
        total_accum_us += thread->accumulated_time_us;
        
        /* Check if this is an idle thread using RTOS-specific method */
        bool is_idle = false;
        if (rtos->ops && rtos->ops->is_idle_thread)
        {
            is_idle = rtos->ops->is_idle_thread(thread);
        }
        
        if (!is_idle)
        {
            active_accum_us += thread->accumulated_time_us;
        }
        else
        {
            has_idle_concept = true;
        }
    }
    
    /* Update overall CPU usage max if we have idle thread concept */
    if (has_idle_concept)
    {
        uint64_t temp = (uint64_t)active_accum_us * 10000ULL;
        uint32_t cpu_usage_pct = (uint32_t)(temp / window_time_us);
        if (cpu_usage_pct > 10000) cpu_usage_pct = 10000;
        
        if (cpu_usage_pct > rtos->max_cpu_usage)
        {
            rtos->max_cpu_usage = cpu_usage_pct;
        }
    }
}

void rtosResetThreadCounters(struct rtosState *rtos)
{
    if (!rtos || !rtos->enabled || !rtos->threads)
        return;
    
    struct rtosThread *thread, *tmp;
    HASH_ITER(hh, rtos->threads, thread, tmp) 
    {
        thread->accumulated_time_us = 0;
        thread->accumulated_cycles = 0;
        thread->window_switches = 0;
    }
}