#ifndef OUTPUT_CONSOLE_H
#define OUTPUT_CONSOLE_H

#include <output_handler.h>
#include <stdarg.h>

struct rtosState;

void output_console_start_frame(OutputConfig *config, IntervalOutput *interval);
void output_console_profile_entry(OutputConfig *config, ProfileOutput *entry);
void output_console_exception_entry(OutputConfig *config, ExceptionOutput *exception);
void output_console_stats(OutputConfig *config, StatsOutput *stats);
void output_console_rtos_info(OutputConfig *config, void *rtos_data);
void output_console_rtos_threads(OutputConfig *config, struct rtosState *rtos, uint64_t window_time_us, bool itm_overflow, const char *sort_by);
void output_console_end_frame(OutputConfig *config);
void output_console_clear_screen(OutputConfig *config);
void output_console_status_line(OutputConfig *config, const char *format, va_list args);
void output_console_exception_header(OutputConfig *config);
void output_console_exception_footer(OutputConfig *config);
void output_console_no_exceptions(OutputConfig *config);
void output_console_status_indicators(OutputConfig *config, bool overflow, bool sw_changed, bool ts_changed, bool hw_changed);
void output_console_interval_info(OutputConfig *config, uint64_t interval_ms, uint64_t interval_ticks, uint64_t ticks_per_ms, bool has_ticks);
void output_console_sort_options(OutputConfig *config, bool show_options);
void output_console_message(OutputConfig *config, const char *message);

#endif