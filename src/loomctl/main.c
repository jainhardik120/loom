#include "loom_control.h"
#include "loom_settings.h"

#include <stdio.h>
#include <string.h>

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
