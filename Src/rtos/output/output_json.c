#include <output_handler.h>
#include <output_json.h>
#include <cJSON.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <rtos_support.h>
#include <uthash.h>
#include <generics.h>
#include <rtos/exceptions.h>

static cJSON *json_root = NULL;
static cJSON *json_profile_array = NULL;
static cJSON *json_exception_array = NULL;

static void send_udp_json(OutputConfig *config, const char *json_str) 
{
    if (config->udp_socket >= 0 && config->udp_dest && json_str) 
    {
        int result = sendto(config->udp_socket, json_str, strlen(json_str), 0,
                           (struct sockaddr *)config->udp_dest, sizeof(struct sockaddr_in));
        if (result < 0)
        {
            genericsReport(V_ERROR, "UDP send failed: %s" EOL, strerror(errno));
        }
    }
}

static void output_json_object(OutputConfig *config, cJSON *obj) 
{
    if (!obj) 
        return;
    
    char *json_str = cJSON_PrintUnformatted(obj);
    if (!json_str) 
        return;
    
    if (config->mode == OUTPUT_JSON_FILE && config->file) 
    {
        fprintf(config->file, "%s\n", json_str);
        fflush(config->file);
    } 
    else if (config->mode == OUTPUT_JSON_UDP) 
    {
        size_t len = strlen(json_str);
        char *buffer = malloc(len + 2);
        if (buffer)
        {
            snprintf(buffer, len + 2, "%s\n", json_str);
            send_udp_json(config, buffer);
            free(buffer);
        }
    }
    
    free(json_str);
}

void output_json_start_frame(OutputConfig *config, IntervalOutput *interval) 
{
    if (json_root) 
    {
        cJSON_Delete(json_root);
        json_root = NULL;
    }
    
    json_root = cJSON_CreateObject();
    if (!json_root) 
        return;
    
    cJSON_AddNumberToObject(json_root, "timestamp", interval->timestamp);
    cJSON_AddNumberToObject(json_root, "interval_us", interval->interval_us);
    cJSON_AddNumberToObject(json_root, "interval_ticks", interval->interval_ticks);
    cJSON_AddNumberToObject(json_root, "ticks_per_ms", interval->ticks_per_ms);
    cJSON_AddNumberToObject(json_root, "total_samples", interval->total_samples);
    
    json_profile_array = cJSON_CreateArray();
    json_exception_array = cJSON_CreateArray();
    
    if (json_profile_array)
        cJSON_AddItemToObject(json_root, "profile", json_profile_array);
    if (json_exception_array)
        cJSON_AddItemToObject(json_root, "exceptions", json_exception_array);
}

void output_json_profile_entry(OutputConfig *config, ProfileOutput *entry) 
{
    if (!json_profile_array || !entry) 
        return;
    
    cJSON *item = cJSON_CreateObject();
    if (!item) 
        return;
    
    cJSON_AddStringToObject(item, "function", entry->function ? entry->function : "");
    if (entry->filename)
        cJSON_AddStringToObject(item, "filename", entry->filename);
    if (entry->line > 0)
        cJSON_AddNumberToObject(item, "line", entry->line);
    cJSON_AddNumberToObject(item, "count", entry->count);
    cJSON_AddNumberToObject(item, "percentage", entry->percentage);
    
    cJSON_AddItemToArray(json_profile_array, item);
}

void output_json_exception_entry(OutputConfig *config, ExceptionOutput *exception) 
{
    if (config->mode == OUTPUT_JSON_UDP) 
    {
        cJSON *item = cJSON_CreateObject();
        if (!item) 
        return;
        
        cJSON_AddNumberToObject(item, "ex", 1);
        cJSON_AddNumberToObject(item, "num", exception->exception_num);
        cJSON_AddStringToObject(item, "name", exception->exception_name ? exception->exception_name : "");
        cJSON_AddNumberToObject(item, "count", exception->visits);
        cJSON_AddNumberToObject(item, "maxd", exception->max_depth);
        cJSON_AddNumberToObject(item, "total", exception->total_time);
        cJSON_AddNumberToObject(item, "pct", exception->util_percent);
        cJSON_AddNumberToObject(item, "ave", exception->ave_time);
        cJSON_AddNumberToObject(item, "min", exception->min_time);
        cJSON_AddNumberToObject(item, "max", exception->max_time);
        cJSON_AddNumberToObject(item, "maxwall", exception->max_wall_time);
        
        output_json_object(config, item);
        cJSON_Delete(item);
    } 
    else if (json_exception_array) 
    {
        cJSON *item = cJSON_CreateObject();
        if (!item) 
        return;
        
        cJSON_AddNumberToObject(item, "num", exception->exception_num);
        cJSON_AddStringToObject(item, "name", exception->exception_name ? exception->exception_name : "");
        cJSON_AddNumberToObject(item, "visits", exception->visits);
        cJSON_AddNumberToObject(item, "max_depth", exception->max_depth);
        cJSON_AddNumberToObject(item, "total_time", exception->total_time);
        cJSON_AddNumberToObject(item, "util_percent", exception->util_percent);
        cJSON_AddNumberToObject(item, "ave_time", exception->ave_time);
        cJSON_AddNumberToObject(item, "min_time", exception->min_time);
        cJSON_AddNumberToObject(item, "max_time", exception->max_time);
        cJSON_AddNumberToObject(item, "max_wall_time", exception->max_wall_time);
        
        cJSON_AddItemToArray(json_exception_array, item);
    }
}

void output_json_stats(OutputConfig *config, StatsOutput *stats) 
{
    if (!json_root || !stats) 
        return;
    
    cJSON *stats_obj = cJSON_CreateObject();
    if (!stats_obj) 
        return;
    
    cJSON_AddNumberToObject(stats_obj, "overflow", stats->overflow);
    cJSON_AddNumberToObject(stats_obj, "sync_count", stats->sync_count);
    cJSON_AddNumberToObject(stats_obj, "error_count", stats->error_count);
    cJSON_AddNumberToObject(stats_obj, "sw_packets", stats->sw_packets);
    cJSON_AddNumberToObject(stats_obj, "ts_packets", stats->ts_packets);
    cJSON_AddNumberToObject(stats_obj, "hw_packets", stats->hw_packets);
    
    cJSON_AddItemToObject(json_root, "stats", stats_obj);
}

void output_json_rtos_threads(OutputConfig *config, struct rtosState *rtos, uint64_t window_time_us, bool itm_overflow)
{
    if (!config || !rtos || !rtos->enabled || !rtos->threads)
        return;
        
    struct rtosThread *thread, *tmp;
    uint64_t active_accum_us = 0;
    uint64_t total_accum_us = 0;
    bool has_idle_concept = false;
    
    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    
    cJSON *threads_array = cJSON_CreateArray();
    if (!threads_array)
    {
        cJSON_Delete(root);
        return;
    }
    
    HASH_ITER(hh, rtos->threads, thread, tmp) 
    {
        cJSON *thread_obj = cJSON_CreateObject();
        if (!thread_obj) continue;
        
        uint32_t cpu_pct = 0;
        if (window_time_us > 0)
        {
            uint64_t temp = (uint64_t)thread->accumulated_time_us * 10000ULL;
            cpu_pct = (uint32_t)(temp / window_time_us);
            if (cpu_pct > 10000) cpu_pct = 10000;
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
        
        char tcb_str[20];
        snprintf(tcb_str, sizeof(tcb_str), "0x%08X", thread->tcb_addr);
        cJSON_AddStringToObject(thread_obj, "tcb", tcb_str);
        cJSON_AddStringToObject(thread_obj, "name", thread->name);
        cJSON_AddStringToObject(thread_obj, "func", thread->entry_func_name ? thread->entry_func_name : "unknown");
        cJSON_AddNumberToObject(thread_obj, "prio", thread->priority);
        cJSON_AddNumberToObject(thread_obj, "time_ms", thread->accumulated_time_us / 1000);
        
        char cpu_str[16];
        snprintf(cpu_str, sizeof(cpu_str), "%.3f", cpu_pct / 100.0);
        cJSON_AddRawToObject(thread_obj, "cpu", cpu_str);
        
        char max_str[16];
        snprintf(max_str, sizeof(max_str), "%.3f", thread->max_cpu_percent / 100.0);
        cJSON_AddRawToObject(thread_obj, "max", max_str);
        cJSON_AddNumberToObject(thread_obj, "switches", thread->window_switches);
        
        cJSON_AddItemToArray(threads_array, thread_obj);
    }
    
    cJSON_AddItemToObject(root, "threads", threads_array);
    
    if (window_time_us > 0 && has_idle_concept)
    {
        uint64_t temp = (uint64_t)active_accum_us * 10000ULL;
        uint32_t cpu_usage_pct = (uint32_t)(temp / window_time_us);
        
        cJSON_AddNumberToObject(root, "interval_ms", window_time_us / 1000);
        
        char cpu_usage_str[16];
        snprintf(cpu_usage_str, sizeof(cpu_usage_str), "%.3f", cpu_usage_pct / 100.0);
        cJSON_AddRawToObject(root, "cpu_usage", cpu_usage_str);
        
        char cpu_max_str[16];
        snprintf(cpu_max_str, sizeof(cpu_max_str), "%.3f", rtos->max_cpu_usage / 100.0);
        cJSON_AddRawToObject(root, "cpu_max", cpu_max_str);
        if (rtos->cpu_freq > 0)
        {
            cJSON_AddNumberToObject(root, "cpu_freq", rtos->cpu_freq);
        }
        cJSON_AddBoolToObject(root, "overflow", itm_overflow);
    }
    
    output_json_object(config, root);
    cJSON_Delete(root);
}

void output_json_exceptions(OutputConfig *config, struct exceptionRecord *exceptions, uint32_t max_exceptions, uint64_t timeStamp, uint64_t lastReportTicks)
{
    if (!config || !exceptions)
        return;
    
    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    
    cJSON *exceptions_array = cJSON_CreateArray();
    if (!exceptions_array)
    {
        cJSON_Delete(root);
        return;
    }
    
    for (uint32_t e = 0; e < max_exceptions; e++)
    {
        if (exceptions[e].visits)
        {
            cJSON *exc_obj = cJSON_CreateObject();
            if (!exc_obj) continue;
            
            const char* exceptionName = exceptionGetName(e);
            float util_percent = (lastReportTicks && timeStamp > lastReportTicks) ? 
                                ((float)exceptions[e].totalTime / (timeStamp - lastReportTicks) * 100.0f) : 0;
            
            cJSON_AddNumberToObject(exc_obj, "num", e);
            cJSON_AddStringToObject(exc_obj, "name", exceptionName);
            cJSON_AddNumberToObject(exc_obj, "count", exceptions[e].visits);
            cJSON_AddNumberToObject(exc_obj, "maxd", exceptions[e].maxDepth);
            cJSON_AddNumberToObject(exc_obj, "total", exceptions[e].totalTime);
            cJSON_AddNumberToObject(exc_obj, "pct", util_percent);
            cJSON_AddNumberToObject(exc_obj, "ave", exceptions[e].visits ? exceptions[e].totalTime / exceptions[e].visits : 0);
            cJSON_AddNumberToObject(exc_obj, "min", exceptions[e].minTime);
            cJSON_AddNumberToObject(exc_obj, "max", exceptions[e].maxTime);
            cJSON_AddNumberToObject(exc_obj, "maxwall", exceptions[e].maxWallTime);
            
            cJSON_AddItemToArray(exceptions_array, exc_obj);
        }
    }
    
    cJSON_AddItemToObject(root, "exceptions", exceptions_array);
    output_json_object(config, root);
    cJSON_Delete(root);
}

void output_json_rtos_info(OutputConfig *config, void *rtos_data) 
{
    /* Legacy function - kept for compatibility */
}

void output_json_end_frame(OutputConfig *config) 
{
    if (!json_root) 
        return;
    
    if (config->mode == OUTPUT_JSON_FILE) 
    {
        output_json_object(config, json_root);
    }
    
    cJSON_Delete(json_root);
    json_root = NULL;
    json_profile_array = NULL;
    json_exception_array = NULL;
}