#include <output_handler.h>
#include <output_console.h>
#include <generics.h>
#include <stdarg.h>
#include <inttypes.h>
#include <rtos_support.h>
#include <itmDecoder.h>

#define CUTOFF 10

static uint32_t printed_lines = 0;

void output_console_clear_screen(OutputConfig *config) 
{
    if (!config->mono)
    {
        genericsFPrintf(stdout, CLEAR_SCREEN);
    }
    printed_lines = 0;
}

void output_console_start_frame(OutputConfig *config, IntervalOutput *interval) 
{
    output_console_clear_screen(config);
}

void output_console_profile_entry(OutputConfig *config, ProfileOutput *entry) 
{
    if (!entry) return;
    
    uint32_t percentage_int = (uint32_t)(entry->percentage * 100);
    
    if (!config->mono) 
    {
        fprintf(stdout, C_DATA "%3d.%02d%% " C_SUPPORT " %7" PRIu64 " ",
                percentage_int / 100, percentage_int % 100, entry->count);
        
        if (entry->filename) 
        {
            fprintf(stdout, C_CONTEXT "%s" C_RESET "::", entry->filename);
        }
        
        if (entry->line > 0) 
        {
            fprintf(stdout, C_SUPPORT2 "%s" C_RESET "::" C_CONTEXT "%d\n",
                    entry->function, entry->line);
        } else {
            fprintf(stdout, C_SUPPORT2 "%s" C_RESET "\n", entry->function);
        }
    } else {
        fprintf(stdout, "%3d.%02d%%  %7" PRIu64 " ",
                percentage_int / 100, percentage_int % 100, entry->count);
        
        if (entry->filename) 
        {
            fprintf(stdout, "%s::", entry->filename);
        }
        
        if (entry->line > 0) 
        {
            fprintf(stdout, "%s::%d\n", entry->function, entry->line);
        } else {
            fprintf(stdout, "%s\n", entry->function);
        }
    }
    
}

void output_console_exception_header(OutputConfig *config)
{
    genericsFPrintf(stdout, "\n=== Exception Statistics ===\n");
    genericsFPrintf(stdout, "|-------------------|----------|-------|-------------|-------|------------|------------|------------|------------|\n");
    genericsFPrintf(stdout, "| Exception         |   Count  | MaxD  | TotalTicks  |   %%   |  AveTicks  |  minTicks  |  maxTicks  |  maxWall   |\n");
    genericsFPrintf(stdout, "|-------------------|----------|-------|-------------|-------|------------|------------|------------|------------|\n");
}

void output_console_exception_entry(OutputConfig *config, ExceptionOutput *exception) 
{
    if (!config->mono) 
    {
        genericsFPrintf(stdout, "| " C_DATA "%-17s" C_RESET " | " C_DATA "%8" PRIu64 C_RESET 
                " | " C_DATA "%5" PRIu32 C_RESET " | " C_DATA "%11" PRId64 C_RESET 
                " | " C_DATA "%5.1f" C_RESET " | " C_DATA "%10" PRId64 C_RESET 
                " | " C_DATA "%10" PRId64 C_RESET " | " C_DATA "%10" PRId64 C_RESET 
                " | " C_DATA "%10" PRId64 C_RESET " |\n",
                exception->exception_name,
                exception->visits, exception->max_depth, exception->total_time,
                exception->util_percent, exception->ave_time,
                exception->min_time, exception->max_time, exception->max_wall_time);
    } else {
        genericsFPrintf(stdout, "| %-17s | %8" PRIu64 " | %5" PRIu32 
                " | %11" PRId64 " | %5.1f | %10" PRId64 " | %10" PRId64 
                " | %10" PRId64 " | %10" PRId64 " |\n",
                exception->exception_name,
                exception->visits, exception->max_depth, exception->total_time,
                exception->util_percent, exception->ave_time,
                exception->min_time, exception->max_time, exception->max_wall_time);
    }
}

void output_console_stats(OutputConfig *config, StatsOutput *stats) 
{
    static uint32_t last_overflow = 0;
    static uint32_t last_sync = 0;
    static uint32_t last_errors = 0;
    
    genericsReport( V_INFO, "         Ovf=%3d (+%d)  ITMSync=%3d (+%d)  ITMErrors=%3d (+%d)\n",
            stats->overflow, stats->overflow - last_overflow,
            stats->sync_count, stats->sync_count - last_sync,
            stats->error_count, stats->error_count - last_errors);
    
    last_overflow = stats->overflow;
    last_sync = stats->sync_count;
    last_errors = stats->error_count;
}

void output_console_rtos_threads(OutputConfig *config, struct rtosState *rtos, uint64_t window_time_us, bool itm_overflow, const char *sort_by)
{
    if (!rtos || !rtos->enabled || !rtos->threads)
        return;
        
    rtosDumpThreadInfo(rtos, stdout, window_time_us, itm_overflow, sort_by);
}

void output_console_rtos_info(OutputConfig *config, void *rtos_data) 
{
}

void output_console_end_frame(OutputConfig *config) 
{
    if (!config->mono) 
    {
        fprintf(stdout, "\n" C_RESET);
    } else {
        fprintf(stdout, "\n");
    }
    fflush(stdout);
}

void output_console_status_line(OutputConfig *config, const char *format, va_list args) 
{
    vfprintf(stdout, format, args);
    fflush(stdout);
}

void output_console_exception_footer(OutputConfig *config)
{
    genericsFPrintf(stdout, "|-------------------|----------|-------|-------------|-------|------------|------------|------------|------------|\n");
}

void output_console_no_exceptions(OutputConfig *config)
{
    genericsFPrintf(stdout, "| No exceptions detected yet...                                                                                      |\n");
}

void output_console_status_indicators(OutputConfig *config, bool overflow, bool sw_changed, bool ts_changed, bool hw_changed)
{
    genericsFPrintf(stdout, EOL C_RESET "[%s%s%s%s" C_RESET "] ",
            overflow ? C_OVF_IND "V" : C_RESET "-",
            sw_changed ? C_SOFT_IND "S" : C_RESET "-",
            ts_changed ? C_TSTAMP_IND "T" : C_RESET "-",
            hw_changed ? C_HW_IND "H" : C_RESET "-");
}

void output_console_interval_info(OutputConfig *config, uint64_t interval_ms, uint64_t interval_ticks, uint64_t ticks_per_ms, bool has_ticks)
{
    if (has_ticks && ticks_per_ms > 0)
    {
        genericsFPrintf(stdout, "Interval = " C_DATA "%" PRIu64 "ms " C_RESET "/ " C_DATA "%" PRIu64 C_RESET " (~" C_DATA "%" PRIu64 C_RESET " Ticks/ms)" EOL,
                interval_ms, interval_ticks, ticks_per_ms);
    }
    else
    {
        genericsFPrintf(stdout, C_RESET "Interval = " C_DATA "%" PRIu64 C_RESET "ms" EOL, interval_ms);
    }
}

void output_console_sort_options(OutputConfig *config, bool show_options)
{
    if (show_options)
    {
        genericsFPrintf(stdout, C_RESET "Sort: " C_SUPPORT "[t]" C_RESET "cb " C_SUPPORT "[c]" C_RESET "pu "
                C_SUPPORT "[m]" C_RESET "ax " C_SUPPORT "[n]" C_RESET "ame "
                C_SUPPORT "[f]" C_RESET "unc " C_SUPPORT "[p]" C_RESET "riority "
                C_SUPPORT "[s]" C_RESET "witches " C_CYAN "| " C_SUPPORT "[r]" C_RESET "eset max" C_RESET EOL);
    }
}

void output_console_message(OutputConfig *config, const char *message)
{
    genericsFPrintf(stdout, "%s", message);
}