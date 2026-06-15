#ifndef LOOMD_METRICS_H
#define LOOMD_METRICS_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#define LOOM_PROCESS_METRICS_MAX 64

typedef struct HostMetricsSampler {
    unsigned long long last_total;
    unsigned long long last_idle;
    bool has_cpu_sample;
} HostMetricsSampler;

typedef struct ProcessMetricsSampler {
    pid_t pid;
    unsigned long long last_proc_ticks;
    unsigned long long last_total_ticks;
    unsigned long long last_gpu_ns;
    long long last_time_ns;
    bool has_sample;
} ProcessMetricsSampler;

void host_metrics_sampler_init(HostMetricsSampler *sampler);
void host_metrics_append_text(HostMetricsSampler *sampler, char *buffer, size_t buffer_size);
void process_metrics_sampler_init(ProcessMetricsSampler *sampler);
void process_metrics_append_text(ProcessMetricsSampler *sampler,
                                 pid_t pid,
                                 const char *role,
                                 const char *display_id,
                                 char *buffer,
                                 size_t buffer_size);
void process_metrics_collect_tree(pid_t root_pid, pid_t *pids, size_t *count, size_t max_count);
bool process_metrics_is_descendant(pid_t pid, pid_t ancestor_pid);

#endif
