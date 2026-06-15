#include "control_service.h"
#include "display_manager.h"
#include "evdi_device.h"
#include "evdi_logging.h"
#include "event_loop.h"
#include "logging.h"
#include "loom_settings.h"

#include <stdbool.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

static void print_usage(const char *argv0)
{
    printf("Usage: %s [OPTIONS]\n", argv0);
    printf("\n");
    printf("Options:\n");
    printf("  --config PATH       Load daemon settings from PATH\n");
    printf("  -h, --help          Show this help\n");
}

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);

    LoomSettings settings;
    char default_config_path[512];
    const char *config_path = loom_settings_default_user_path(default_config_path,
                                                              sizeof(default_config_path));
    bool config_path_explicit = false;

    loom_settings_defaults(&settings);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0) {
            if (i + 1 >= argc) {
                log_error("--config requires a path");
                return 2;
            }
            config_path = argv[++i];
            config_path_explicit = true;
        }
    }

    if (!loom_settings_load(&settings, config_path)) {
        return 1;
    }
    if (config_path_explicit) {
        log_info("loaded settings from %s", config_path);
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0) {
            i++;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            log_error("unknown option: %s", argv[i]);
            print_usage(argv[0]);
            return 2;
        }
    }

    evdi_logging_install();

    struct evdi_lib_version version;
    evdi_get_lib_version(&version);
    log_info("libevdi version %d.%d.%d",
             version.version_major,
             version.version_minor,
             version.version_patchlevel);

    LoomDisplayManager display_manager;
    if (!display_manager_load_settings(&display_manager, &settings)) {
        log_error("failed to load display profiles");
        return 1;
    }
    log_info("loaded %zu display profile(s)", display_manager.session_count);
    display_manager_start_enabled(&display_manager);

    LoomControlService control_service;
    control_service_start(&control_service, &settings, &display_manager, config_path);

    const int rc = event_loop_run(&display_manager, &control_service);
    control_service_stop(&control_service);
    display_manager_stop_all(&display_manager);

    return rc;
}
