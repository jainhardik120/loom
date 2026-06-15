#ifndef LOOM_SETTINGS_H
#define LOOM_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LOOM_MAX_DISPLAY_PROFILES 8

typedef struct LoomDisplayProfile {
    char id[64];
    char name[128];
    bool enabled;
    bool paused;
    bool auto_connect;
    int mode_width;
    int mode_height;
    int mode_refresh;
    char stream_transport[32];
    char stream_host[128];
    int stream_port;
    int stream_bitrate_kbps;
    int stream_fps;
    char usb_serial[128];
} LoomDisplayProfile;

typedef struct LoomSettings {
    int device_index;
    bool capture_enabled;
    bool dump_frame;
    char dump_path[256];
    int mode_width;
    int mode_height;
    int mode_refresh;
    uint32_t pixel_area_limit;
    uint32_t pixel_per_second_limit;
    bool stream_enabled;
    char stream_transport[32];
    char stream_host[128];
    int stream_port;
    int stream_bitrate_kbps;
    int stream_fps;
    size_t display_count;
    LoomDisplayProfile displays[LOOM_MAX_DISPLAY_PROFILES];
} LoomSettings;

void loom_settings_defaults(LoomSettings *settings);
bool loom_settings_load(LoomSettings *settings, const char *path);
bool loom_settings_save(const LoomSettings *settings, const char *path);
bool loom_settings_set_value(LoomSettings *settings, const char *key, const char *value);
bool loom_display_profile_set_value(LoomDisplayProfile *profile, const char *key, const char *value);
bool loom_display_profile_get_value(const LoomDisplayProfile *profile,
                                    const char *key,
                                    char *buffer,
                                    size_t buffer_size);
bool loom_settings_get_value(const LoomSettings *settings,
                             const char *key,
                             char *buffer,
                             size_t buffer_size);
void loom_settings_print(const LoomSettings *settings);
const char *loom_settings_default_user_path(char *buffer, size_t buffer_size);

#endif
