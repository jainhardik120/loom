#include "loom_control.h"
#include "loom_settings.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void usage(const char *argv0)
{
    printf("Usage: %s COMMAND [ARGS]\n", argv0);
    printf("\n");
    printf("Commands:\n");
    printf("  status                         Query live loomd over D-Bus\n");
    printf("  get KEY                        Get a live loomd setting over D-Bus\n");
    printf("  set KEY VALUE                  Set a live loomd setting over D-Bus\n");
    printf("  display list                   List runtime display sessions\n");
    printf("  display add ID [NAME]          Add and start a display profile\n");
    printf("  display remove ID              Remove a display profile\n");
    printf("  display pause ID               Temporarily disconnect a display\n");
    printf("  display resume ID              Reconnect a paused display\n");
    printf("  display set ID KEY VALUE       Set a display profile value\n");
    printf("  usb list                       List USB identities visible to loomd\n");
    printf("  metrics [--watch|-w] [ID]      Show live resource and stream metrics\n");
    printf("  metrics --raw                  Print raw metrics text from loomd\n");
    printf("  settings                       Print effective local settings file values\n");
    printf("  settings get KEY               Print one setting\n");
    printf("  settings set KEY VALUE         Update the local settings file\n");
    printf("  settings path                  Print the local settings file path\n");
}

static bool load_user_settings(LoomSettings *settings, char *path, size_t path_size)
{
    loom_settings_defaults(settings);
    loom_settings_default_user_path(path, path_size);
    return loom_settings_load(settings, path);
}

static bool key_value(const char *line, const char *key, char *out, size_t out_size)
{
    size_t key_len = strlen(key);
    const char *cursor = line;
    while ((cursor = strstr(cursor, key)) != NULL) {
        if ((cursor == line || cursor[-1] == ' ') && cursor[key_len] == '=') {
            const char *value = cursor + key_len + 1;
            const char *end = NULL;
            if (*value == '"') {
                value++;
                end = strchr(value, '"');
            } else {
                end = strpbrk(value, " \n");
            }
            if (!end) {
                end = value + strlen(value);
            }
            size_t len = (size_t)(end - value);
            if (len >= out_size) {
                len = out_size - 1;
            }
            memcpy(out, value, len);
            out[len] = '\0';
            return true;
        }
        cursor += key_len;
    }
    snprintf(out, out_size, "-");
    return false;
}

static const char *line_with_prefix(const char *text, const char *prefix)
{
    size_t prefix_len = strlen(prefix);
    const char *line = text;
    while (line && *line) {
        if (strncmp(line, prefix, prefix_len) == 0) {
            return line;
        }
        line = strchr(line, '\n');
        if (line) {
            line++;
        }
    }
    return NULL;
}

static void bytes_to_mib(const char *bytes_text, char *out, size_t out_size)
{
    unsigned long long bytes = 0;
    if (sscanf(bytes_text, "%llu", &bytes) != 1) {
        snprintf(out, out_size, "-");
        return;
    }
    snprintf(out, out_size, "%.1f", (double)bytes / (1024.0 * 1024.0));
}

static bool display_matches_filter(const char *line, const char *filter_id)
{
    if (!filter_id || filter_id[0] == '\0') {
        return true;
    }
    char id[128];
    return key_value(line, "id", id, sizeof(id)) && strcmp(id, filter_id) == 0;
}

static void print_metrics_table(const char *metrics, const char *filter_id)
{
    time_t now = time(NULL);
    struct tm local_time;
    char timestamp[32] = "-";
    if (localtime_r(&now, &local_time)) {
        strftime(timestamp, sizeof(timestamp), "%H:%M:%S", &local_time);
    }

    printf("Loom metrics %s\n", timestamp);

    printf("\nProcesses\n");
    printf("%-16s %-16s %-18s %8s %8s %9s %8s\n",
           "Role",
           "Name",
           "Display",
           "PID",
           "CPU",
           "RSS MiB",
           "GPU");
    const char *line = metrics;
    bool saw_process = false;
    while (line && *line) {
        if (strncmp(line, "process ", 8) == 0) {
            char role[32], name[64], display[64], pid[32], cpu[32], rss[32], gpu[32], gpu_available[32];
            key_value(line, "role", role, sizeof(role));
            key_value(line, "name", name, sizeof(name));
            key_value(line, "display", display, sizeof(display));
            key_value(line, "pid", pid, sizeof(pid));
            key_value(line, "cpu_percent", cpu, sizeof(cpu));
            key_value(line, "rss_mib", rss, sizeof(rss));
            key_value(line, "gpu_percent", gpu, sizeof(gpu));
            key_value(line, "gpu_available", gpu_available, sizeof(gpu_available));
            if (strncmp(cpu, "-1", 2) == 0) {
                snprintf(cpu, sizeof(cpu), "warmup");
            } else {
                size_t len = strlen(cpu);
                if (len + 1 < sizeof(cpu)) {
                    cpu[len] = '%';
                    cpu[len + 1] = '\0';
                }
            }
            if (strcmp(gpu_available, "true") != 0 || strncmp(gpu, "-1", 2) == 0) {
                snprintf(gpu, sizeof(gpu), "n/a");
            } else {
                size_t len = strlen(gpu);
                if (len + 1 < sizeof(gpu)) {
                    gpu[len] = '%';
                    gpu[len + 1] = '\0';
                }
            }
            printf("%-16s %-16s %-18s %8s %8s %9s %8s\n",
                   role,
                   name,
                   display,
                   pid,
                   cpu,
                   rss,
                   gpu);
            saw_process = true;
        }
        line = strchr(line, '\n');
        if (line) {
            line++;
        }
    }
    if (!saw_process) {
        printf("%-16s %-16s %-18s %8s %8s %9s %8s\n", "-", "-", "-", "-", "-", "-", "-");
    }

    printf("\nDisplays\n");
    printf("%-18s %-13s %-13s %8s %10s %10s %10s %10s %12s\n",
           "ID",
           "State",
           "Mode",
           "Raw FPS",
           "AU FPS",
           "Enc kbps",
           "USB Mbps",
           "USB ms",
           "USB MiB");
    line = metrics;
    bool saw_display = false;
    while (line && *line) {
        if (strncmp(line, "display ", 8) == 0 && display_matches_filter(line, filter_id)) {
            char id[64], state[32], mode[32], raw_fps[32], au_fps[32], enc_kbps[32], usb_mbps[32], usb_ms[32];
            char usb_bytes[32], usb_mib[32];
            key_value(line, "id", id, sizeof(id));
            key_value(line, "state", state, sizeof(state));
            key_value(line, "mode", mode, sizeof(mode));
            key_value(line, "raw_fps", raw_fps, sizeof(raw_fps));
            key_value(line, "encoded_au_fps", au_fps, sizeof(au_fps));
            key_value(line, "encoded_kbps", enc_kbps, sizeof(enc_kbps));
            key_value(line, "usb_mbps", usb_mbps, sizeof(usb_mbps));
            key_value(line, "avg_usb_write_ms", usb_ms, sizeof(usb_ms));
            key_value(line, "usb_bytes_total", usb_bytes, sizeof(usb_bytes));
            bytes_to_mib(usb_bytes, usb_mib, sizeof(usb_mib));
            printf("%-18s %-13s %-13s %8s %10s %10s %10s %10s %12s\n",
                   id,
                   state,
                   mode,
                   raw_fps,
                   au_fps,
                   enc_kbps,
                   usb_mbps,
                   usb_ms,
                   usb_mib);
            saw_display = true;
        }
        line = strchr(line, '\n');
        if (line) {
            line++;
        }
    }
    if (!saw_display) {
        printf("%-18s %-13s %-13s %8s %10s %10s %10s %10s %12s\n",
               "-",
               "-",
               "-",
               "-",
               "-",
               "-",
               "-",
               "-",
               "-");
    }

    const char *android = line_with_prefix(metrics, "android_metrics ");
    char available[32] = "-";
    char reason[128] = "-";
    if (android) {
        key_value(android, "available", available, sizeof(available));
        key_value(android, "reason", reason, sizeof(reason));
    }
    printf("\nAndroid telemetry: available=%s reason=%s\n", available, reason);
}

int main(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        usage(argv[0]);
        return argc < 2 ? 2 : 0;
    }

    if (strcmp(argv[1], "status") == 0) {
        return loom_control_status();
    }

    if (strcmp(argv[1], "get") == 0) {
        if (argc != 3) {
            usage(argv[0]);
            return 2;
        }
        return loom_control_get_setting(argv[2]);
    }

    if (strcmp(argv[1], "set") == 0) {
        if (argc != 4) {
            usage(argv[0]);
            return 2;
        }
        return loom_control_set_setting(argv[2], argv[3]);
    }

    if (strcmp(argv[1], "display") == 0) {
        if (argc < 3) {
            usage(argv[0]);
            return 2;
        }
        if (strcmp(argv[2], "list") == 0) {
            char displays[4096];
            int rc = loom_control_list_displays_text(displays, sizeof(displays));
            if (rc == 0) {
                printf("%s", displays);
            }
            return rc;
        }
        if (strcmp(argv[2], "add") == 0) {
            if (argc < 4 || argc > 5) {
                usage(argv[0]);
                return 2;
            }
            const char *name = argc == 5 ? argv[4] : argv[3];
            int rc = loom_control_add_display(argv[3], name, "usb_accessory", 1920, 1200, 30);
            if (rc == 0) {
                printf("ok\n");
            }
            return rc;
        }
        if (strcmp(argv[2], "remove") == 0) {
            if (argc != 4) {
                usage(argv[0]);
                return 2;
            }
            int rc = loom_control_remove_display(argv[3]);
            if (rc == 0) {
                printf("ok\n");
            }
            return rc;
        }
        if (strcmp(argv[2], "pause") == 0) {
            if (argc != 4) {
                usage(argv[0]);
                return 2;
            }
            int rc = loom_control_pause_display(argv[3]);
            if (rc == 0) {
                printf("ok\n");
            }
            return rc;
        }
        if (strcmp(argv[2], "resume") == 0) {
            if (argc != 4) {
                usage(argv[0]);
                return 2;
            }
            int rc = loom_control_resume_display(argv[3]);
            if (rc == 0) {
                printf("ok\n");
            }
            return rc;
        }
        if (strcmp(argv[2], "set") == 0) {
            if (argc != 6) {
                usage(argv[0]);
                return 2;
            }
            int rc = loom_control_set_display_setting(argv[3], argv[4], argv[5]);
            if (rc == 0) {
                printf("ok\n");
            }
            return rc;
        }
    }

    if (strcmp(argv[1], "usb") == 0) {
        if (argc != 3 || strcmp(argv[2], "list") != 0) {
            usage(argv[0]);
            return 2;
        }
        char devices[4096];
        int rc = loom_control_list_usb_devices_text(devices, sizeof(devices));
        if (rc == 0) {
            printf("%s", devices);
        }
        return rc;
    }

    if (strcmp(argv[1], "metrics") == 0) {
        bool watch = false;
        bool raw = false;
        const char *filter_id = NULL;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--watch") == 0 || strcmp(argv[i], "-w") == 0) {
                watch = true;
            } else if (strcmp(argv[i], "--raw") == 0) {
                raw = true;
            } else if (!filter_id) {
                filter_id = argv[i];
            } else {
                usage(argv[0]);
                return 2;
            }
        }

        if (raw && watch) {
            usage(argv[0]);
            return 2;
        }

        do {
            char metrics[16384];
            int rc = loom_control_metrics_text(metrics, sizeof(metrics));
            if (rc != 0) {
                return rc;
            }
            if (watch) {
                printf("\033[H\033[J");
            }
            if (raw) {
                printf("%s", metrics);
            } else {
                print_metrics_table(metrics, filter_id);
            }
            fflush(stdout);
            if (watch) {
                sleep(1);
            }
        } while (watch);
        return 0;
    }

    if (strcmp(argv[1], "settings") == 0) {
        LoomSettings settings;
        char path[512];
        if (!load_user_settings(&settings, path, sizeof(path))) {
            return 1;
        }

        if (argc == 2) {
            loom_settings_print(&settings);
            return 0;
        }

        if (strcmp(argv[2], "path") == 0) {
            printf("%s\n", path);
            return 0;
        }

        if (strcmp(argv[2], "get") == 0) {
            if (argc != 4) {
                usage(argv[0]);
                return 2;
            }
            char value[256];
            if (!loom_settings_get_value(&settings, argv[3], value, sizeof(value))) {
                fprintf(stderr, "unknown setting: %s\n", argv[3]);
                return 2;
            }
            printf("%s\n", value);
            return 0;
        }

        if (strcmp(argv[2], "set") == 0) {
            if (argc != 5) {
                usage(argv[0]);
                return 2;
            }
            if (!loom_settings_set_value(&settings, argv[3], argv[4])) {
                fprintf(stderr, "invalid setting or value: %s=%s\n", argv[3], argv[4]);
                return 2;
            }
            if (!loom_settings_save(&settings, path)) {
                return 1;
            }
            printf("%s\n", path);
            return 0;
        }
    }

    usage(argv[0]);
    return 2;
}
