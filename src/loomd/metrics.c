#include "metrics.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static size_t appendf(char *buffer, size_t buffer_size, size_t used, const char *format, ...)
{
    if (used >= buffer_size) {
        return used;
    }

    va_list args;
    va_start(args, format);
    int written = vsnprintf(buffer + used, buffer_size - used, format, args);
    va_end(args);
    if (written < 0) {
        return used;
    }
    size_t next = used + (size_t)written;
    return next >= buffer_size ? buffer_size - 1 : next;
}

void host_metrics_sampler_init(HostMetricsSampler *sampler)
{
    memset(sampler, 0, sizeof(*sampler));
}

void process_metrics_sampler_init(ProcessMetricsSampler *sampler)
{
    memset(sampler, 0, sizeof(*sampler));
}

static bool read_cpu_totals(unsigned long long *total, unsigned long long *idle)
{
    FILE *file = fopen("/proc/stat", "r");
    if (!file) {
        return false;
    }

    char label[8];
    unsigned long long user = 0;
    unsigned long long nice = 0;
    unsigned long long system = 0;
    unsigned long long idle_ticks = 0;
    unsigned long long iowait = 0;
    unsigned long long irq = 0;
    unsigned long long softirq = 0;
    unsigned long long steal = 0;
    int parsed = fscanf(file,
                        "%7s %llu %llu %llu %llu %llu %llu %llu %llu",
                        label,
                        &user,
                        &nice,
                        &system,
                        &idle_ticks,
                        &iowait,
                        &irq,
                        &softirq,
                        &steal);
    fclose(file);
    if (parsed < 8 || strcmp(label, "cpu") != 0) {
        return false;
    }

    *idle = idle_ticks + iowait;
    *total = user + nice + system + idle_ticks + iowait + irq + softirq + steal;
    return true;
}

static bool read_cpu_total_ticks(unsigned long long *total)
{
    unsigned long long idle = 0;
    return read_cpu_totals(total, &idle);
}

static long long monotonic_ns(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long long)now.tv_sec * 1000000000LL + now.tv_nsec;
}

static bool read_process_stat(pid_t pid,
                              unsigned long long *proc_ticks,
                              unsigned long long *rss_mib)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid);
    FILE *file = fopen(path, "r");
    if (!file) {
        return false;
    }

    char line[4096];
    bool ok = fgets(line, sizeof(line), file) != NULL;
    fclose(file);
    if (!ok) {
        return false;
    }

    char *end_comm = strrchr(line, ')');
    if (!end_comm || end_comm[1] != ' ') {
        return false;
    }

    char *token = end_comm + 2;
    unsigned long long utime = 0;
    unsigned long long stime = 0;
    long long rss_pages = 0;
    int field = 3;
    while (token && *token != '\0') {
        char *next = strchr(token, ' ');
        if (next) {
            *next = '\0';
        }

        if (field == 14) {
            utime = strtoull(token, NULL, 10);
        } else if (field == 15) {
            stime = strtoull(token, NULL, 10);
        } else if (field == 24) {
            rss_pages = strtoll(token, NULL, 10);
            break;
        }

        if (!next) {
            break;
        }
        token = next + 1;
        while (*token == ' ') {
            token++;
        }
        field++;
    }

    if (utime == 0 && stime == 0 && rss_pages == 0) {
        return false;
    }

    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        page_size = 4096;
    }

    *proc_ticks = utime + stime;
    *rss_mib = rss_pages > 0
                   ? (unsigned long long)((rss_pages * (long long)page_size) / (1024LL * 1024LL))
                   : 0;
    return true;
}

static bool parse_drm_engine_line(const char *line, unsigned long long *value)
{
    if (strncmp(line, "drm-engine-", strlen("drm-engine-")) != 0) {
        return false;
    }

    const char *colon = strchr(line, ':');
    if (!colon) {
        return false;
    }

    errno = 0;
    unsigned long long parsed = strtoull(colon + 1, NULL, 10);
    if (errno != 0) {
        return false;
    }

    *value = parsed;
    return true;
}

static unsigned long long read_process_gpu_ns(pid_t pid, bool *available)
{
    *available = false;

    char dir_path[64];
    snprintf(dir_path, sizeof(dir_path), "/proc/%ld/fdinfo", (long)pid);
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return 0;
    }

    unsigned long long total = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        FILE *file = fopen(path, "r");
        if (!file) {
            continue;
        }

        char line[256];
        while (fgets(line, sizeof(line), file)) {
            unsigned long long engine_ns = 0;
            if (parse_drm_engine_line(line, &engine_ns)) {
                total += engine_ns;
                *available = true;
            }
        }
        fclose(file);
    }

    closedir(dir);
    return total;
}

static bool read_memory_mib(unsigned long long *total_mib,
                            unsigned long long *available_mib,
                            unsigned long long *used_mib)
{
    FILE *file = fopen("/proc/meminfo", "r");
    if (!file) {
        return false;
    }

    char key[64];
    unsigned long long value = 0;
    char unit[16];
    unsigned long long total_kib = 0;
    unsigned long long available_kib = 0;
    while (fscanf(file, "%63s %llu %15s", key, &value, unit) == 3) {
        if (strcmp(key, "MemTotal:") == 0) {
            total_kib = value;
        } else if (strcmp(key, "MemAvailable:") == 0) {
            available_kib = value;
        }
    }
    fclose(file);

    if (total_kib == 0 || available_kib == 0) {
        return false;
    }

    *total_mib = total_kib / 1024;
    *available_mib = available_kib / 1024;
    *used_mib = (total_kib - available_kib) / 1024;
    return true;
}

static bool read_first_line(const char *path, char *buffer, size_t buffer_size)
{
    FILE *file = fopen(path, "r");
    if (!file) {
        return false;
    }
    bool ok = fgets(buffer, (int)buffer_size, file) != NULL;
    fclose(file);
    if (!ok) {
        return false;
    }
    buffer[strcspn(buffer, "\n")] = '\0';
    return true;
}

static bool read_status_name_ppid(pid_t pid, char *name, size_t name_size, pid_t *ppid)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%ld/status", (long)pid);
    FILE *file = fopen(path, "r");
    if (!file) {
        return false;
    }

    bool has_name = false;
    bool has_ppid = false;
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "Name:", 5) == 0) {
            char parsed[128];
            if (sscanf(line + 5, "%127s", parsed) == 1) {
                snprintf(name, name_size, "%s", parsed);
                has_name = true;
            }
        } else if (strncmp(line, "PPid:", 5) == 0) {
            long parsed = 0;
            if (sscanf(line + 5, "%ld", &parsed) == 1) {
                *ppid = (pid_t)parsed;
                has_ppid = true;
            }
        }
    }
    fclose(file);
    return has_name && has_ppid;
}

static bool pid_seen(const pid_t *pids, size_t count, pid_t pid)
{
    for (size_t i = 0; i < count; i++) {
        if (pids[i] == pid) {
            return true;
        }
    }
    return false;
}

void process_metrics_collect_tree(pid_t root_pid, pid_t *pids, size_t *count, size_t max_count)
{
    if (!pids || !count || max_count == 0) {
        return;
    }

    *count = 0;
    if (root_pid <= 0) {
        return;
    }

    pids[(*count)++] = root_pid;
    for (size_t parent_index = 0; parent_index < *count && *count < max_count; parent_index++) {
        pid_t parent_pid = pids[parent_index];
        DIR *dir = opendir("/proc");
        if (!dir) {
            return;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL && *count < max_count) {
            char *end = NULL;
            long parsed_pid = strtol(entry->d_name, &end, 10);
            if (end == entry->d_name || *end != '\0' || parsed_pid <= 0) {
                continue;
            }

            char name[128] = "";
            pid_t ppid = -1;
            if (!read_status_name_ppid((pid_t)parsed_pid, name, sizeof(name), &ppid)) {
                continue;
            }

            if (ppid == parent_pid && !pid_seen(pids, *count, (pid_t)parsed_pid)) {
                pids[(*count)++] = (pid_t)parsed_pid;
            }
        }

        closedir(dir);
    }
}

bool process_metrics_is_descendant(pid_t pid, pid_t ancestor_pid)
{
    if (pid <= 0 || ancestor_pid <= 0 || pid == ancestor_pid) {
        return pid == ancestor_pid;
    }

    for (int depth = 0; depth < 64; depth++) {
        char name[128] = "";
        pid_t ppid = -1;
        if (!read_status_name_ppid(pid, name, sizeof(name), &ppid)) {
            return false;
        }
        if (ppid == ancestor_pid) {
            return true;
        }
        if (ppid <= 1 || ppid == pid) {
            return false;
        }
        pid = ppid;
    }

    return false;
}

static int read_int_file(const char *path, int fallback)
{
    char value[32];
    if (!read_first_line(path, value, sizeof(value))) {
        return fallback;
    }
    int parsed = fallback;
    if (sscanf(value, "%d", &parsed) != 1) {
        return fallback;
    }
    return parsed;
}

void host_metrics_append_text(HostMetricsSampler *sampler, char *buffer, size_t buffer_size)
{
    size_t used = strlen(buffer);

    unsigned long long total = 0;
    unsigned long long idle = 0;
    double cpu_percent = -1.0;
    if (read_cpu_totals(&total, &idle)) {
        if (sampler->has_cpu_sample && total > sampler->last_total) {
            unsigned long long total_delta = total - sampler->last_total;
            unsigned long long idle_delta = idle - sampler->last_idle;
            if (total_delta > 0 && idle_delta <= total_delta) {
                cpu_percent = 100.0 * (double)(total_delta - idle_delta) / (double)total_delta;
            }
        }
        sampler->last_total = total;
        sampler->last_idle = idle;
        sampler->has_cpu_sample = true;
    }

    unsigned long long mem_total = 0;
    unsigned long long mem_available = 0;
    unsigned long long mem_used = 0;
    if (!read_memory_mib(&mem_total, &mem_available, &mem_used)) {
        mem_total = 0;
        mem_available = 0;
        mem_used = 0;
    }

    used = appendf(buffer,
                   buffer_size,
                   used,
                   "host cpu_percent=%.1f mem_used_mib=%llu mem_total_mib=%llu mem_available_mib=%llu\n",
                   cpu_percent,
                   mem_used,
                   mem_total,
                   mem_available);

    char gpu_lines[2048] = "";
    size_t gpu_used = 0;
    DIR *dir = opendir("/sys/class/drm");
    if (!dir) {
        used = appendf(buffer, buffer_size, used, "gpu_count=0\n");
        return;
    }

    struct dirent *entry;
    int gpu_count = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "card", 4) != 0 || strchr(entry->d_name, '-')) {
            continue;
        }

        char path[512];
        char driver[64] = "unknown";
        snprintf(path, sizeof(path), "/sys/class/drm/%s/device/uevent", entry->d_name);
        FILE *uevent = fopen(path, "r");
        if (uevent) {
            char line[128];
            while (fgets(line, sizeof(line), uevent)) {
                if (strncmp(line, "DRIVER=", 7) == 0) {
                    char *value = line + 7;
                    value[strcspn(value, "\n")] = '\0';
                    size_t len = strlen(value);
                    if (len >= sizeof(driver)) {
                        len = sizeof(driver) - 1;
                    }
                    memcpy(driver, value, len);
                    driver[len] = '\0';
                    break;
                }
            }
            fclose(uevent);
        }

        snprintf(path, sizeof(path), "/sys/class/drm/%s/device/gpu_busy_percent", entry->d_name);
        int busy = read_int_file(path, -1);
        gpu_used = appendf(gpu_lines,
                           sizeof(gpu_lines),
                           gpu_used,
                           "gpu card=%s driver=%s busy_percent=%d\n",
                           entry->d_name,
                           driver,
                           busy);
        gpu_count++;
    }
    closedir(dir);

    used = appendf(buffer, buffer_size, used, "gpu_count=%d\n", gpu_count);
    (void)appendf(buffer, buffer_size, used, "%s", gpu_lines);
}

void process_metrics_append_text(ProcessMetricsSampler *sampler,
                                 pid_t pid,
                                 const char *role,
                                 const char *display_id,
                                 char *buffer,
                                 size_t buffer_size)
{
    size_t used = strlen(buffer);
    if (pid <= 0) {
        (void)appendf(buffer,
                      buffer_size,
                      used,
                      "process role=%s display=%s pid=%ld alive=false cpu_percent=-1.0 rss_mib=0 gpu_percent=-1.0 gpu_available=false\n",
                      role ? role : "unknown",
                      display_id ? display_id : "-",
                      (long)pid);
        return;
    }

    if (sampler->pid != pid) {
        process_metrics_sampler_init(sampler);
        sampler->pid = pid;
    }

    unsigned long long total_ticks = 0;
    unsigned long long proc_ticks = 0;
    unsigned long long rss_mib = 0;
    char name[128] = "unknown";
    pid_t ppid = -1;
    (void)read_status_name_ppid(pid, name, sizeof(name), &ppid);
    bool alive = read_cpu_total_ticks(&total_ticks) &&
                 read_process_stat(pid, &proc_ticks, &rss_mib);

    bool gpu_available = false;
    unsigned long long gpu_ns = read_process_gpu_ns(pid, &gpu_available);
    long long now_ns = monotonic_ns();

    double cpu_percent = -1.0;
    double gpu_percent = -1.0;
    if (alive && sampler->has_sample && total_ticks > sampler->last_total_ticks) {
        unsigned long long proc_delta = proc_ticks - sampler->last_proc_ticks;
        unsigned long long total_delta = total_ticks - sampler->last_total_ticks;
        long cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
        if (cpu_count <= 0) {
            cpu_count = 1;
        }
        double elapsed_one_cpu_ticks = (double)total_delta / (double)cpu_count;
        if (elapsed_one_cpu_ticks > 0.0) {
            cpu_percent = 100.0 * (double)proc_delta / elapsed_one_cpu_ticks;
        }
    }

    if (gpu_available && sampler->has_sample && now_ns > sampler->last_time_ns &&
        gpu_ns >= sampler->last_gpu_ns) {
        unsigned long long gpu_delta = gpu_ns - sampler->last_gpu_ns;
        long long time_delta = now_ns - sampler->last_time_ns;
        if (time_delta > 0) {
            gpu_percent = 100.0 * (double)gpu_delta / (double)time_delta;
        }
    }

    if (alive) {
        sampler->last_total_ticks = total_ticks;
        sampler->last_proc_ticks = proc_ticks;
        sampler->last_gpu_ns = gpu_ns;
        sampler->last_time_ns = now_ns;
        sampler->has_sample = true;
    } else {
        process_metrics_sampler_init(sampler);
    }

    (void)appendf(buffer,
                  buffer_size,
                  used,
                  "process role=%s name=%s display=%s pid=%ld ppid=%ld alive=%s cpu_percent=%.1f rss_mib=%llu gpu_percent=%.1f gpu_available=%s\n",
                  role ? role : "unknown",
                  name,
                  display_id ? display_id : "-",
                  (long)pid,
                  (long)ppid,
                  alive ? "true" : "false",
                  cpu_percent,
                  alive ? rss_mib : 0,
                  gpu_percent,
                  gpu_available ? "true" : "false");
}
