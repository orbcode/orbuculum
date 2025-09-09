#ifndef OUTPUT_FTRACE_H
#define OUTPUT_FTRACE_H

#include <output_handler.h>

struct rtosThread;

void output_ftrace_start_frame(OutputConfig *config, IntervalOutput *interval);
void output_ftrace_profile_entry(OutputConfig *config, ProfileOutput *entry);
void output_ftrace_exception_entry(OutputConfig *config, ExceptionOutput *exception);
void output_ftrace_stats(OutputConfig *config, StatsOutput *stats);
void output_ftrace_rtos_info(OutputConfig *config, void *rtos_data);
void output_ftrace_end_frame(OutputConfig *config);
void output_ftrace_thread_switch(OutputConfig *config, struct rtosThread *prev, struct rtosThread *next, uint64_t timestamp_us);

#endif