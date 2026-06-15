#include "loom_settings.h"

#include "logging.h"
#include "loom_protocol.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static bool parse_bool(const char *value, bool *out)
{
    if (strcmp(value, "1") == 0 || strcmp(value, "true") == 0 || strcmp(value, "on") == 0 ||
        strcmp(value, "yes") == 0) {
        *out = true;
        return true;
    }
    if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0 || strcmp(value, "off") == 0 ||
        strcmp(value, "no") == 0) {
        *out = false;
        return true;
    }
    return false;
}

static char *trim(char *text)
{
    while (isspace((unsigned char)*text)) {
        text++;
    }

    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }
    return text;
}

static bool parse_int(const char *value, int *out)
{
    char *end = NULL;
    errno = 0;
    long parsed = strtol(value, &end, 10);
    if (errno != 0 || end == value || *trim(end) != '\0') {
        return false;
    }
    *out = (int)parsed;
    return true;
}

static bool ensure_parent_dir(const char *path)
{
    char copy[512];
    snprintf(copy, sizeof(copy), "%s", path);
    char *slash = strrchr(copy, '/');
    if (!slash) {
        return true;
    }
    *slash = '\0';
    if (copy[0] == '\0') {
        return true;
    }

    char partial[512] = "";
    char *cursor = copy;
    if (*cursor == '/') {
        snprintf(partial, sizeof(partial), "/");
        cursor++;
    }

    while (*cursor) {
        char *next = strchr(cursor, '/');
        if (next) {
            *next = '\0';
        }
        if (strlen(partial) + strlen(cursor) + 2 >= sizeof(partial)) {
            return false;
        }
        if (strcmp(partial, "/") != 0 && partial[0] != '\0') {
            strcat(partial, "/");
        }
        strcat(partial, cursor);
        if (mkdir(partial, 0755) != 0 && errno != EEXIST) {
            return false;
        }
        if (!next) {
            break;
        }
        *next = '/';
        cursor = next + 1;
    }
    return true;
}

static void display_profile_defaults(LoomDisplayProfile *profile, const char *id)
{
    memset(profile, 0, sizeof(*profile));
    snprintf(profile->id, sizeof(profile->id), "%s", id);
    snprintf(profile->name, sizeof(profile->name), "%s", id);
    profile->enabled = true;
    profile->paused = false;
    profile->auto_connect = true;
    profile->mode_width = 1920;
    profile->mode_height = 1200;
    profile->mode_refresh = 30;
    snprintf(profile->stream_transport, sizeof(profile->stream_transport), "usb_accessory");
    snprintf(profile->stream_host, sizeof(profile->stream_host), "127.0.0.1");
    profile->stream_port = 27183;
    profile->stream_bitrate_kbps = 12000;
    profile->stream_fps = 30;
}

void loom_settings_defaults(LoomSettings *settings)
{
    memset(settings, 0, sizeof(*settings));
    settings->device_index = -1;
    settings->capture_enabled = true;
    settings->dump_frame = true;
    snprintf(settings->dump_path, sizeof(settings->dump_path), "frame.raw");
    settings->mode_width = 1920;
    settings->mode_height = 1080;
    settings->mode_refresh = 60;
    settings->pixel_area_limit = (uint32_t)(settings->mode_width * settings->mode_height);
    settings->pixel_per_second_limit = settings->pixel_area_limit * (uint32_t)settings->mode_refresh;
    settings->stream_enabled = false;
    snprintf(settings->stream_transport, sizeof(settings->stream_transport), "tcp");
    snprintf(settings->stream_host, sizeof(settings->stream_host), "127.0.0.1");
    settings->stream_port = 27183;
    settings->stream_bitrate_kbps = 8000;
    settings->stream_fps = 30;
    settings->display_count = 0;
}

bool loom_display_profile_set_value(LoomDisplayProfile *profile, const char *key, const char *value)
{
    int parsed_int = 0;
    bool parsed_bool = false;

    if (strcmp(key, "id") == 0) {
        snprintf(profile->id, sizeof(profile->id), "%s", value);
        return true;
    }
    if (strcmp(key, "name") == 0) {
        snprintf(profile->name, sizeof(profile->name), "%s", value);
        return true;
    }
    if (strcmp(key, "enabled") == 0) {
        if (!parse_bool(value, &parsed_bool)) {
            return false;
        }
        profile->enabled = parsed_bool;
        return true;
    }
    if (strcmp(key, "paused") == 0) {
        if (!parse_bool(value, &parsed_bool)) {
            return false;
        }
        profile->paused = parsed_bool;
        return true;
    }
    if (strcmp(key, "auto_connect") == 0) {
        if (!parse_bool(value, &parsed_bool)) {
            return false;
        }
        profile->auto_connect = parsed_bool;
        return true;
    }
    if (strcmp(key, "mode_width") == 0) {
        if (!parse_int(value, &parsed_int)) {
            return false;
        }
        profile->mode_width = parsed_int;
        return true;
    }
    if (strcmp(key, "mode_height") == 0) {
        if (!parse_int(value, &parsed_int)) {
            return false;
        }
        profile->mode_height = parsed_int;
        return true;
    }
    if (strcmp(key, "mode_refresh") == 0) {
        if (!parse_int(value, &parsed_int)) {
            return false;
        }
        profile->mode_refresh = parsed_int;
        return true;
    }
    if (strcmp(key, "stream_transport") == 0 || strcmp(key, "transport") == 0) {
        if (strcmp(value, "tcp") != 0 && strcmp(value, "usb_accessory") != 0) {
            return false;
        }
        snprintf(profile->stream_transport, sizeof(profile->stream_transport), "%s", value);
        return true;
    }
    if (strcmp(key, "stream_host") == 0) {
        snprintf(profile->stream_host, sizeof(profile->stream_host), "%s", value);
        return true;
    }
    if (strcmp(key, "stream_port") == 0) {
        if (!parse_int(value, &parsed_int)) {
            return false;
        }
        profile->stream_port = parsed_int;
        return true;
    }
    if (strcmp(key, "stream_bitrate_kbps") == 0) {
        if (!parse_int(value, &parsed_int)) {
            return false;
        }
        profile->stream_bitrate_kbps = parsed_int;
        return true;
    }
    if (strcmp(key, "stream_fps") == 0) {
        if (!parse_int(value, &parsed_int)) {
            return false;
        }
        profile->stream_fps = parsed_int;
        return true;
    }
    if (strcmp(key, "usb_serial") == 0) {
        snprintf(profile->usb_serial, sizeof(profile->usb_serial), "%s", value);
        return true;
    }
    return false;
}

bool loom_display_profile_get_value(const LoomDisplayProfile *profile,
                                    const char *key,
                                    char *buffer,
                                    size_t buffer_size)
{
    if (strcmp(key, "id") == 0) {
        snprintf(buffer, buffer_size, "%s", profile->id);
    } else if (strcmp(key, "name") == 0) {
        snprintf(buffer, buffer_size, "%s", profile->name);
    } else if (strcmp(key, "enabled") == 0) {
        snprintf(buffer, buffer_size, "%s", profile->enabled ? "true" : "false");
    } else if (strcmp(key, "paused") == 0) {
        snprintf(buffer, buffer_size, "%s", profile->paused ? "true" : "false");
    } else if (strcmp(key, "auto_connect") == 0) {
        snprintf(buffer, buffer_size, "%s", profile->auto_connect ? "true" : "false");
    } else if (strcmp(key, "mode_width") == 0) {
        snprintf(buffer, buffer_size, "%d", profile->mode_width);
    } else if (strcmp(key, "mode_height") == 0) {
        snprintf(buffer, buffer_size, "%d", profile->mode_height);
    } else if (strcmp(key, "mode_refresh") == 0) {
        snprintf(buffer, buffer_size, "%d", profile->mode_refresh);
    } else if (strcmp(key, "stream_transport") == 0 || strcmp(key, "transport") == 0) {
        snprintf(buffer, buffer_size, "%s", profile->stream_transport);
    } else if (strcmp(key, "stream_host") == 0) {
        snprintf(buffer, buffer_size, "%s", profile->stream_host);
    } else if (strcmp(key, "stream_port") == 0) {
        snprintf(buffer, buffer_size, "%d", profile->stream_port);
    } else if (strcmp(key, "stream_bitrate_kbps") == 0) {
        snprintf(buffer, buffer_size, "%d", profile->stream_bitrate_kbps);
    } else if (strcmp(key, "stream_fps") == 0) {
        snprintf(buffer, buffer_size, "%d", profile->stream_fps);
    } else if (strcmp(key, "usb_serial") == 0) {
        snprintf(buffer, buffer_size, "%s", profile->usb_serial);
    } else {
        return false;
    }
    return true;
}

bool loom_settings_set_value(LoomSettings *settings, const char *key, const char *value)
{
    int parsed_int = 0;
    bool parsed_bool = false;

    if (strncmp(key, "display.", 8) == 0) {
        const char *rest = key + 8;
        char *end = NULL;
        errno = 0;
        unsigned long index = strtoul(rest, &end, 10);
        if (errno != 0 || end == rest || *end != '.' || index >= LOOM_MAX_DISPLAY_PROFILES) {
            return false;
        }
        if (index >= settings->display_count) {
            for (size_t i = settings->display_count; i <= index; i++) {
                char id[32];
                snprintf(id, sizeof(id), "display%zu", i);
                display_profile_defaults(&settings->displays[i], id);
            }
            settings->display_count = index + 1;
        }
        return loom_display_profile_set_value(&settings->displays[index], end + 1, value);
    }
    if (strcmp(key, "display_count") == 0) {
        if (!parse_int(value, &parsed_int) || parsed_int < 0 ||
            parsed_int > LOOM_MAX_DISPLAY_PROFILES) {
            return false;
        }
        for (size_t i = settings->display_count; i < (size_t)parsed_int; i++) {
            char id[32];
            snprintf(id, sizeof(id), "display%zu", i);
            display_profile_defaults(&settings->displays[i], id);
        }
        settings->display_count = (size_t)parsed_int;
        return true;
    }
    if (strcmp(key, "device") == 0 || strcmp(key, "device_index") == 0) {
        if (!parse_int(value, &parsed_int)) {
            return false;
        }
        settings->device_index = parsed_int;
        return true;
    }
    if (strcmp(key, "capture") == 0 || strcmp(key, "capture_enabled") == 0) {
        if (!parse_bool(value, &parsed_bool)) {
            return false;
        }
        settings->capture_enabled = parsed_bool;
        return true;
    }
    if (strcmp(key, "dump_frame") == 0) {
        if (!parse_bool(value, &parsed_bool)) {
            return false;
        }
        settings->dump_frame = parsed_bool;
        return true;
    }
    if (strcmp(key, "dump_path") == 0) {
        snprintf(settings->dump_path, sizeof(settings->dump_path), "%s", value);
        return true;
    }
    if (strcmp(key, "mode_width") == 0) {
        if (!parse_int(value, &parsed_int)) {
            return false;
        }
        settings->mode_width = parsed_int;
        settings->pixel_area_limit = (uint32_t)(settings->mode_width * settings->mode_height);
        settings->pixel_per_second_limit = settings->pixel_area_limit * (uint32_t)settings->mode_refresh;
        return true;
    }
    if (strcmp(key, "mode_height") == 0) {
        if (!parse_int(value, &parsed_int)) {
            return false;
        }
        settings->mode_height = parsed_int;
        settings->pixel_area_limit = (uint32_t)(settings->mode_width * settings->mode_height);
        settings->pixel_per_second_limit = settings->pixel_area_limit * (uint32_t)settings->mode_refresh;
        return true;
    }
    if (strcmp(key, "mode_refresh") == 0) {
        if (!parse_int(value, &parsed_int)) {
            return false;
        }
        settings->mode_refresh = parsed_int;
        settings->pixel_per_second_limit = settings->pixel_area_limit * (uint32_t)settings->mode_refresh;
        return true;
    }
    if (strcmp(key, "pixel_area_limit") == 0) {
        if (!parse_int(value, &parsed_int)) {
            return false;
        }
        settings->pixel_area_limit = (uint32_t)parsed_int;
        return true;
    }
    if (strcmp(key, "pixel_per_second_limit") == 0) {
        if (!parse_int(value, &parsed_int)) {
            return false;
        }
        settings->pixel_per_second_limit = (uint32_t)parsed_int;
        return true;
    }
    if (strcmp(key, "stream_enabled") == 0 || strcmp(key, "stream") == 0) {
        if (!parse_bool(value, &parsed_bool)) {
            return false;
        }
        settings->stream_enabled = parsed_bool;
        return true;
    }
    if (strcmp(key, "stream_transport") == 0) {
        if (strcmp(value, "tcp") != 0 && strcmp(value, "usb_accessory") != 0) {
            return false;
        }
        snprintf(settings->stream_transport, sizeof(settings->stream_transport), "%s", value);
        return true;
    }
    if (strcmp(key, "stream_host") == 0) {
        snprintf(settings->stream_host, sizeof(settings->stream_host), "%s", value);
        return true;
    }
    if (strcmp(key, "stream_port") == 0) {
        if (!parse_int(value, &parsed_int)) {
            return false;
        }
        settings->stream_port = parsed_int;
        return true;
    }
    if (strcmp(key, "stream_bitrate_kbps") == 0) {
        if (!parse_int(value, &parsed_int)) {
            return false;
        }
        settings->stream_bitrate_kbps = parsed_int;
        return true;
    }
    if (strcmp(key, "stream_fps") == 0) {
        if (!parse_int(value, &parsed_int)) {
            return false;
        }
        settings->stream_fps = parsed_int;
        return true;
    }

    return false;
}

bool loom_settings_get_value(const LoomSettings *settings,
                             const char *key,
                             char *buffer,
                             size_t buffer_size)
{
    if (strncmp(key, "display.", 8) == 0) {
        const char *rest = key + 8;
        char *end = NULL;
        errno = 0;
        unsigned long index = strtoul(rest, &end, 10);
        if (errno != 0 || end == rest || *end != '.' || index >= settings->display_count) {
            return false;
        }
        return loom_display_profile_get_value(&settings->displays[index],
                                              end + 1,
                                              buffer,
                                              buffer_size);
    }
    if (strcmp(key, "display_count") == 0) {
        snprintf(buffer, buffer_size, "%zu", settings->display_count);
    } else if (strcmp(key, "device") == 0 || strcmp(key, "device_index") == 0) {
        snprintf(buffer, buffer_size, "%d", settings->device_index);
    } else if (strcmp(key, "capture") == 0 || strcmp(key, "capture_enabled") == 0) {
        snprintf(buffer, buffer_size, "%s", settings->capture_enabled ? "true" : "false");
    } else if (strcmp(key, "dump_frame") == 0) {
        snprintf(buffer, buffer_size, "%s", settings->dump_frame ? "true" : "false");
    } else if (strcmp(key, "dump_path") == 0) {
        snprintf(buffer, buffer_size, "%s", settings->dump_path);
    } else if (strcmp(key, "mode_width") == 0) {
        snprintf(buffer, buffer_size, "%d", settings->mode_width);
    } else if (strcmp(key, "mode_height") == 0) {
        snprintf(buffer, buffer_size, "%d", settings->mode_height);
    } else if (strcmp(key, "mode_refresh") == 0) {
        snprintf(buffer, buffer_size, "%d", settings->mode_refresh);
    } else if (strcmp(key, "pixel_area_limit") == 0) {
        snprintf(buffer, buffer_size, "%u", settings->pixel_area_limit);
    } else if (strcmp(key, "pixel_per_second_limit") == 0) {
        snprintf(buffer, buffer_size, "%u", settings->pixel_per_second_limit);
    } else if (strcmp(key, "stream_enabled") == 0 || strcmp(key, "stream") == 0) {
        snprintf(buffer, buffer_size, "%s", settings->stream_enabled ? "true" : "false");
    } else if (strcmp(key, "stream_transport") == 0) {
        snprintf(buffer, buffer_size, "%s", settings->stream_transport);
    } else if (strcmp(key, "stream_host") == 0) {
        snprintf(buffer, buffer_size, "%s", settings->stream_host);
    } else if (strcmp(key, "stream_port") == 0) {
        snprintf(buffer, buffer_size, "%d", settings->stream_port);
    } else if (strcmp(key, "stream_bitrate_kbps") == 0) {
        snprintf(buffer, buffer_size, "%d", settings->stream_bitrate_kbps);
    } else if (strcmp(key, "stream_fps") == 0) {
        snprintf(buffer, buffer_size, "%d", settings->stream_fps);
    } else {
        return false;
    }
    return true;
}

bool loom_settings_load(LoomSettings *settings, const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file) {
        return errno == ENOENT;
    }

    char line[512];
    int line_number = 0;
    while (fgets(line, sizeof(line), file)) {
        line_number++;
        char *content = trim(line);
        if (content[0] == '\0' || content[0] == '#') {
            continue;
        }

        char *equals = strchr(content, '=');
        if (!equals) {
            log_warn("ignoring invalid config line %d in %s", line_number, path);
            continue;
        }
        *equals = '\0';
        char *key = trim(content);
        char *value = trim(equals + 1);
        if (!loom_settings_set_value(settings, key, value)) {
            log_warn("ignoring unknown or invalid config key '%s' in %s", key, path);
        }
    }

    fclose(file);
    return true;
}

bool loom_settings_save(const LoomSettings *settings, const char *path)
{
    if (!ensure_parent_dir(path)) {
        log_error("failed to create parent directory for %s", path);
        return false;
    }

    FILE *file = fopen(path, "w");
    if (!file) {
        log_error("failed to open %s: %s", path, strerror(errno));
        return false;
    }

    fprintf(file, "display_count=%zu\n", settings->display_count);
    for (size_t i = 0; i < settings->display_count; i++) {
        const LoomDisplayProfile *display = &settings->displays[i];
        fprintf(file, "display.%zu.id=%s\n", i, display->id);
        fprintf(file, "display.%zu.name=%s\n", i, display->name);
        fprintf(file, "display.%zu.enabled=%s\n", i, display->enabled ? "true" : "false");
        fprintf(file, "display.%zu.paused=%s\n", i, display->paused ? "true" : "false");
        fprintf(file, "display.%zu.auto_connect=%s\n", i, display->auto_connect ? "true" : "false");
        fprintf(file, "display.%zu.mode_width=%d\n", i, display->mode_width);
        fprintf(file, "display.%zu.mode_height=%d\n", i, display->mode_height);
        fprintf(file, "display.%zu.mode_refresh=%d\n", i, display->mode_refresh);
        fprintf(file, "display.%zu.stream_transport=%s\n", i, display->stream_transport);
        fprintf(file, "display.%zu.stream_host=%s\n", i, display->stream_host);
        fprintf(file, "display.%zu.stream_port=%d\n", i, display->stream_port);
        fprintf(file, "display.%zu.stream_bitrate_kbps=%d\n", i, display->stream_bitrate_kbps);
        fprintf(file, "display.%zu.stream_fps=%d\n", i, display->stream_fps);
        fprintf(file, "display.%zu.usb_serial=%s\n", i, display->usb_serial);
    }

    if (fclose(file) != 0) {
        log_error("failed to close %s: %s", path, strerror(errno));
        return false;
    }
    return true;
}

void loom_settings_print(const LoomSettings *settings)
{
    printf("display_count=%zu\n", settings->display_count);
    for (size_t i = 0; i < settings->display_count; i++) {
        const LoomDisplayProfile *display = &settings->displays[i];
        printf("display.%zu.id=%s\n", i, display->id);
        printf("display.%zu.name=%s\n", i, display->name);
        printf("display.%zu.enabled=%s\n", i, display->enabled ? "true" : "false");
        printf("display.%zu.paused=%s\n", i, display->paused ? "true" : "false");
        printf("display.%zu.auto_connect=%s\n", i, display->auto_connect ? "true" : "false");
        printf("display.%zu.mode_width=%d\n", i, display->mode_width);
        printf("display.%zu.mode_height=%d\n", i, display->mode_height);
        printf("display.%zu.mode_refresh=%d\n", i, display->mode_refresh);
        printf("display.%zu.stream_transport=%s\n", i, display->stream_transport);
        printf("display.%zu.stream_host=%s\n", i, display->stream_host);
        printf("display.%zu.stream_port=%d\n", i, display->stream_port);
        printf("display.%zu.stream_bitrate_kbps=%d\n", i, display->stream_bitrate_kbps);
        printf("display.%zu.stream_fps=%d\n", i, display->stream_fps);
        printf("display.%zu.usb_serial=%s\n", i, display->usb_serial);
    }
}

const char *loom_settings_default_user_path(char *buffer, size_t buffer_size)
{
    const char *home = getenv("HOME");
    if (!home || home[0] == '\0') {
        snprintf(buffer, buffer_size, "loomd.conf");
        return buffer;
    }
    snprintf(buffer, buffer_size, "%s/%s", home, LOOM_DEFAULT_CONFIG_RELATIVE_PATH);
    return buffer;
}
