#ifndef OUTPUT_JSON_H
#define OUTPUT_JSON_H

#include <output_handler.h>

struct rtosState;
struct rtosThread;
struct exceptionRecord;

void output_json_start_frame(OutputConfig *config, IntervalOutput *interval);
void output_json_profile_entry(OutputConfig *config, ProfileOutput *entry);
void output_json_exception_entry(OutputConfig *config, ExceptionOutput *exception);
void output_json_stats(OutputConfig *config, StatsOutput *stats);
void output_json_rtos_info(OutputConfig *config, void *rtos_data);
void output_json_rtos_threads(OutputConfig *config, struct rtosState *rtos, uint64_t window_time_us, bool itm_overflow);
void output_json_exceptions(OutputConfig *config, struct exceptionRecord *exceptions, uint32_t max_exceptions, uint64_t timeStamp, uint64_t lastReportTicks);
void output_json_end_frame(OutputConfig *config);

#endif