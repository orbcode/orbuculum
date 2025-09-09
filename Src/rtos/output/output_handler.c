#include <output_handler.h>
#include <output_console.h>
#include <output_json.h>
#include <output_ftrace.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

typedef void (*start_frame_fn)(OutputConfig*, IntervalOutput*);
typedef void (*profile_entry_fn)(OutputConfig*, ProfileOutput*);
typedef void (*exception_entry_fn)(OutputConfig*, ExceptionOutput*);
typedef void (*stats_fn)(OutputConfig*, StatsOutput*);
typedef void (*rtos_info_fn)(OutputConfig*, void*);
typedef void (*end_frame_fn)(OutputConfig*);
typedef void (*thread_switch_fn)(OutputConfig*, struct rtosThread*, struct rtosThread*, uint64_t);

static const start_frame_fn start_frame_handlers[] = {
    [OUTPUT_CONSOLE] = output_console_start_frame,
    [OUTPUT_JSON_FILE] = output_json_start_frame,
    [OUTPUT_JSON_UDP] = output_json_start_frame,
    [OUTPUT_FTRACE] = output_ftrace_start_frame,
    [OUTPUT_DISABLED] = NULL
};

static const profile_entry_fn profile_handlers[] = {
    [OUTPUT_CONSOLE] = output_console_profile_entry,
    [OUTPUT_JSON_FILE] = output_json_profile_entry,
    [OUTPUT_JSON_UDP] = output_json_profile_entry,
    [OUTPUT_FTRACE] = output_ftrace_profile_entry,
    [OUTPUT_DISABLED] = NULL
};

static const exception_entry_fn exception_handlers[] = {
    [OUTPUT_CONSOLE] = output_console_exception_entry,
    [OUTPUT_JSON_FILE] = output_json_exception_entry,
    [OUTPUT_JSON_UDP] = output_json_exception_entry,
    [OUTPUT_FTRACE] = output_ftrace_exception_entry,
    [OUTPUT_DISABLED] = NULL
};

static const stats_fn stats_handlers[] = {
    [OUTPUT_CONSOLE] = output_console_stats,
    [OUTPUT_JSON_FILE] = output_json_stats,
    [OUTPUT_JSON_UDP] = output_json_stats,
    [OUTPUT_FTRACE] = output_ftrace_stats,
    [OUTPUT_DISABLED] = NULL
};

static const rtos_info_fn rtos_handlers[] = {
    [OUTPUT_CONSOLE] = output_console_rtos_info,
    [OUTPUT_JSON_FILE] = output_json_rtos_info,
    [OUTPUT_JSON_UDP] = output_json_rtos_info,
    [OUTPUT_FTRACE] = output_ftrace_rtos_info,
    [OUTPUT_DISABLED] = NULL
};

static const end_frame_fn end_frame_handlers[] = {
    [OUTPUT_CONSOLE] = output_console_end_frame,
    [OUTPUT_JSON_FILE] = output_json_end_frame,
    [OUTPUT_JSON_UDP] = output_json_end_frame,
    [OUTPUT_FTRACE] = output_ftrace_end_frame,
    [OUTPUT_DISABLED] = NULL
};

static const thread_switch_fn thread_switch_handlers[] = {
    [OUTPUT_CONSOLE] = NULL,
    [OUTPUT_JSON_FILE] = NULL,
    [OUTPUT_JSON_UDP] = NULL,
    [OUTPUT_FTRACE] = output_ftrace_thread_switch,
    [OUTPUT_DISABLED] = NULL
};

void output_init(OutputConfig *config) 
{
    if (!config) 
        return;
    
    if (config->mode == OUTPUT_JSON_UDP && config->udp_socket < 0) {
        config->udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (config->udp_socket >= 0 && config->udp_dest) {
            config->udp_dest->sin_family = AF_INET;
            config->udp_dest->sin_addr.s_addr = inet_addr("127.0.0.1");
        }
    }
}

void output_cleanup(OutputConfig *config) 
{
    if (!config) 
        return;
    
    if (config->file && config->file != stdout && config->file != stderr) {
        fclose(config->file);
        config->file = NULL;
    }
    
    if (config->udp_socket >= 0) {
        close(config->udp_socket);
        config->udp_socket = -1;
    }
}

void output_start_frame(OutputConfig *config, IntervalOutput *interval) 
{
    if (!config || config->mode == OUTPUT_DISABLED || config->mode >= sizeof(start_frame_handlers)/sizeof(start_frame_handlers[0])) 
        return;
    
    start_frame_fn handler = start_frame_handlers[config->mode];
    if (handler) 
        handler(config, interval);
}

void output_profile_entry(OutputConfig *config, ProfileOutput *entry) 
{
    if (!config || config->mode == OUTPUT_DISABLED || config->mode >= sizeof(profile_handlers)/sizeof(profile_handlers[0])) 
        return;
    
    profile_entry_fn handler = profile_handlers[config->mode];
    if (handler) 
        handler(config, entry);
}

void output_exception_entry(OutputConfig *config, ExceptionOutput *exception) 
{
    if (!config || config->mode == OUTPUT_DISABLED || config->mode >= sizeof(exception_handlers)/sizeof(exception_handlers[0])) 
        return;
    
    exception_entry_fn handler = exception_handlers[config->mode];
    if (handler) 
        handler(config, exception);
}

void output_stats(OutputConfig *config, StatsOutput *stats) 
{
    if (!config || config->mode == OUTPUT_DISABLED || config->mode >= sizeof(stats_handlers)/sizeof(stats_handlers[0])) 
        return;
    
    stats_fn handler = stats_handlers[config->mode];
    if (handler) 
        handler(config, stats);
}

void output_rtos_info(OutputConfig *config, void *rtos_data) 
{
    if (!config || config->mode == OUTPUT_DISABLED || config->mode >= sizeof(rtos_handlers)/sizeof(rtos_handlers[0])) 
        return;
    
    rtos_info_fn handler = rtos_handlers[config->mode];
    if (handler) 
        handler(config, rtos_data);
}

void output_end_frame(OutputConfig *config) 
{
    if (!config || config->mode == OUTPUT_DISABLED || config->mode >= sizeof(end_frame_handlers)/sizeof(end_frame_handlers[0])) 
        return;
    
    end_frame_fn handler = end_frame_handlers[config->mode];
    if (handler) 
        handler(config);
}

void output_clear_screen(OutputConfig *config) 
{
    if (!config || config->mode != OUTPUT_CONSOLE) 
        return;
    output_console_clear_screen(config);
}

void output_status_line(OutputConfig *config, const char *format, ...) 
{
    if (!config || config->mode != OUTPUT_CONSOLE) 
        return;
    
    va_list args;
    va_start(args, format);
    output_console_status_line(config, format, args);
    va_end(args);
}

void output_thread_switch(OutputConfig *config, struct rtosThread *prev, struct rtosThread *next, uint64_t timestamp_us)
{
    if (!config || config->mode == OUTPUT_DISABLED || config->mode >= sizeof(thread_switch_handlers)/sizeof(thread_switch_handlers[0]))
    {
        return;
    }
    
    thread_switch_fn handler = thread_switch_handlers[config->mode];
    if (handler)
    {
        handler(config, prev, next, timestamp_us);
    }
}