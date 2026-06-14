#include "evdi_device.h"
#include "event_loop.h"
#include "logging.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *argv0)
{
    printf("Usage: %s [OPTIONS]\n", argv0);
    printf("\n");
    printf("Options:\n");
    printf("  --device N          Open /dev/dri/cardN instead of auto-detecting EVDI\n");
    printf("  --no-capture        Connect and log events, but do not register a framebuffer\n");
    printf("  --dump-frame PATH   Dump the first non-empty captured frame (default: frame.raw)\n");
    printf("  --no-dump-frame     Disable raw frame dump\n");
    printf("  -h, --help          Show this help\n");
}

int main(int argc, char **argv)
{
    int requested_device = -1;
    bool capture_enabled = true;
    bool dump_frame = true;
    const char *dump_path = "frame.raw";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--device") == 0) {
            if (i + 1 >= argc) {
                log_error("--device requires a card number");
                return 2;
            }
            requested_device = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--no-capture") == 0) {
            capture_enabled = false;
        } else if (strcmp(argv[i], "--dump-frame") == 0) {
            if (i + 1 >= argc) {
                log_error("--dump-frame requires a path");
                return 2;
            }
            dump_frame = true;
            dump_path = argv[++i];
        } else if (strcmp(argv[i], "--no-dump-frame") == 0) {
            dump_frame = false;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            log_error("unknown option: %s", argv[i]);
            print_usage(argv[0]);
            return 2;
        }
    }

    logging_install_evdi_logger();

    struct evdi_lib_version version;
    evdi_get_lib_version(&version);
    log_info("libevdi version %d.%d.%d",
             version.version_major,
             version.version_minor,
             version.version_patchlevel);

    EvdiDevice device;
    if (!evdi_device_open(&device, requested_device)) {
        return 1;
    }

    device.capture_enabled = capture_enabled;
    device.dump_frame = dump_frame;
    device.dump_path = dump_path;

    evdi_device_connect(&device);
    const int rc = event_loop_run(&device);
    evdi_device_close(&device);

    return rc;
}

