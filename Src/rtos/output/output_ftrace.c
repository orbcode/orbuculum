#include <output_handler.h>
#include <generics.h>
#include <rtos_support.h>
#include <stdio.h>
#include <inttypes.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static uint64_t base_timestamp_us = 0;
static bool first_switch = true;
static struct rtosThread *last_thread = NULL;
static int cpu_id = 0;

void output_ftrace_start_frame(OutputConfig *config, IntervalOutput *interval)
{
    if (!config || !config->file)
    {
        return;
    }
    
    if (first_switch && interval)
    {
        base_timestamp_us = interval->timestamp;
    }
}

void output_ftrace_thread_switch(OutputConfig *config, struct rtosThread *prev, struct rtosThread *next, uint64_t timestamp_us)
{
    genericsReport(V_DEBUG, "ftrace: thread_switch called - conf...ile=%p, next=%p\n", config, config ? config->file : NULL, next);

    if (!config || !config->file || !next)
    {
        genericsReport(V_ERROR, "ftrace: thread_switch validation failed - config=%p, file=%p, next=%p\n",
                       config, config ? config->file : NULL, next);
        return;
    }

    if (first_switch)
    {
        base_timestamp_us = timestamp_us;
        first_switch = false;

        fprintf(config->file, "# tracer: nop\n");
        fprintf(config->file, "#\n");
        fprintf(config->file, "# entries-in-buffer/entries-written: 0/0   #P:1\n");
        fprintf(config->file, "#\n");
        fprintf(config->file, "#                                _-----=> irqs-off\n");
        fprintf(config->file, "#                               / _----=> need-resched\n");
        fprintf(config->file, "#                              | / _---=> hardirq/softirq\n");
        fprintf(config->file, "#                              || / _--=> preempt-depth\n");
        fprintf(config->file, "#                              ||| /     delay\n");
        fprintf(config->file, "#           TASK-PID     CPU#  ||||   TIMESTAMP  FUNCTION\n");
        fprintf(config->file, "#              | |         |   ||||      |         |\n");
    }

    double t = (timestamp_us - base_timestamp_us) / 1000000.0;

    const char *p_base = "unknown";
    const char *p_entry = NULL;
    uintptr_t p_tcb = 0;
    int p_prio = 0;
    unsigned p_pid = 0;

    if (prev)
    {
        p_base = (prev->name && prev->name[0]) ? prev->name : "unknown";
        p_entry = prev->entry_func_name;
        p_tcb = prev->tcb_addr;
        p_prio = prev->priority;
        p_pid = (unsigned)(uint32_t)p_tcb;
    }

    const char *n_base = (next->name && next->name[0]) ? next->name : "unknown";
    const char *n_entry = next->entry_func_name;
    uintptr_t n_tcb = next->tcb_addr;
    int n_prio = next->priority;
    unsigned n_pid = (unsigned)(uint32_t)n_tcb;

    char p_name[128];
    char n_name[128];

    if (p_entry && p_entry[0])
        snprintf(p_name, sizeof(p_name), "%s|%s", p_base, p_entry);
    else
        snprintf(p_name, sizeof(p_name), "%s", p_base);

    if (n_entry && n_entry[0])
        snprintf(n_name, sizeof(n_name), "%s|%s", n_base, n_entry);
    else
        snprintf(n_name, sizeof(n_name), "%s", n_base);

    fprintf(config->file,
            "%16s-%u [%03d] .... %12.6f: sched_switch: prev_comm=%s prev_pid=%u prev_prio=%d prev_state=%c ==> next_comm=%s next_pid=%u next_prio=%d\n",
            p_name, p_pid, cpu_id, t,
            p_name, p_pid, p_prio, 'S',
            n_name, n_pid, n_prio);

    fflush(config->file);

    last_thread = next;
}



void output_ftrace_profile_entry(OutputConfig *config, ProfileOutput *entry)
{
}

void output_ftrace_exception_entry(OutputConfig *config, ExceptionOutput *exception)
{
}

void output_ftrace_stats(OutputConfig *config, StatsOutput *stats)
{
}

void output_ftrace_rtos_info(OutputConfig *config, void *rtos_data)
{
}

void output_ftrace_end_frame(OutputConfig *config)
{
    if (config && config->file)
    {
        fflush(config->file);
    }
}

void output_ftrace_status_line(OutputConfig *config, const char *format, va_list args)
{
}