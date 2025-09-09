#ifndef OUTPUT_HANDLER_H
#define OUTPUT_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef enum {
    OUTPUT_CONSOLE,
    OUTPUT_JSON_FILE,
    OUTPUT_JSON_UDP,
    OUTPUT_FTRACE,
    OUTPUT_DISABLED
} OutputMode;

typedef struct {
    OutputMode mode;
    FILE *file;
    int udp_socket;
    struct sockaddr_in *udp_dest;
    bool mono;
    uint32_t cutscreen;
} OutputConfig;

typedef struct {
    uint32_t exception_num;
    const char *exception_name;
    uint64_t visits;
    uint32_t max_depth;
    int64_t total_time;
    int64_t min_time;
    int64_t max_time;
    int64_t max_wall_time;
    float util_percent;
    int64_t ave_time;
} ExceptionOutput;

typedef struct {
    uint32_t overflow;
    uint32_t sync_count;
    uint32_t error_count;
    uint32_t sw_packets;
    uint32_t ts_packets;
    uint32_t hw_packets;
} StatsOutput;

typedef struct {
    uint64_t timestamp;
    uint64_t interval_us;
    uint64_t interval_ticks;
    uint64_t ticks_per_ms;
    uint32_t total_samples;
} IntervalOutput;

typedef struct {
    const char *filename;
    const char *function;
    uint32_t line;
    uint64_t count;
    float percentage;
} ProfileOutput;

void output_init(OutputConfig *config);
void output_cleanup(OutputConfig *config);

void output_start_frame(OutputConfig *config, IntervalOutput *interval);
void output_profile_entry(OutputConfig *config, ProfileOutput *entry);
void output_exception_entry(OutputConfig *config, ExceptionOutput *exception);
void output_stats(OutputConfig *config, StatsOutput *stats);
void output_rtos_info(OutputConfig *config, void *rtos_data);
void output_end_frame(OutputConfig *config);

struct rtosThread;
void output_thread_switch(OutputConfig *config, struct rtosThread *prev, struct rtosThread *next, uint64_t timestamp_us);

void output_clear_screen(OutputConfig *config);
void output_status_line(OutputConfig *config, const char *format, ...);

#endif