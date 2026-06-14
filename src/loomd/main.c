#include "control_service.h"
#include "evdi_device.h"
#include "evdi_logging.h"
#include "event_loop.h"
#include "logging.h"
#include "loom_settings.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static void print_usage(const char *argv0)
{
    printf("Usage: %s [OPTIONS]\n", argv0);
    printf("\n");
    printf("Options:\n");
    printf("  --config PATH       Load daemon settings from PATH\n");
    printf("  --device N          Open /dev/dri/cardN instead of auto-detecting EVDI\n");
    printf("  --no-capture        Connect and log events, but do not register a framebuffer\n");
    printf("  --dump-frame PATH   Dump the first non-empty captured frame (default: frame.raw)\n");
    printf("  --no-dump-frame     Disable raw frame dump\n");
    printf("  -h, --help          Show this help\n");
}

int main(int argc, char **argv)
{
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
        } else if (strcmp(argv[i], "--device") == 0) {
            if (i + 1 >= argc) {
                log_error("--device requires a card number");
                return 2;
            }
            if (!loom_settings_set_value(&settings, "device", argv[++i])) {
                log_error("invalid --device value");
                return 2;
            }
        } else if (strcmp(argv[i], "--no-capture") == 0) {
            settings.capture_enabled = false;
        } else if (strcmp(argv[i], "--dump-frame") == 0) {
            if (i + 1 >= argc) {
                log_error("--dump-frame requires a path");
                return 2;
            }
            settings.dump_frame = true;
            snprintf(settings.dump_path, sizeof(settings.dump_path), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--no-dump-frame") == 0) {
            settings.dump_frame = false;
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

    EvdiDevice device;
    if (!evdi_device_open(&device, settings.device_index)) {
        return 1;
    }

    device.capture_enabled = settings.capture_enabled;
    device.dump_frame = settings.dump_frame;
    device.dump_path = settings.dump_path;
    device.mode_width = settings.mode_width;
    device.mode_height = settings.mode_height;
    device.mode_refresh = settings.mode_refresh;
    device.pixel_area_limit = settings.pixel_area_limit;
    device.pixel_per_second_limit = settings.pixel_per_second_limit;

    evdi_device_connect(&device);

    LoomControlService control_service;
    control_service_start(&control_service, &settings, &device);

    const int rc = event_loop_run(&device, &control_service);
    control_service_stop(&control_service);
    evdi_device_close(&device);

    return rc;
}
