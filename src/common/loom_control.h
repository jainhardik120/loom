#ifndef LOOM_CONTROL_H
#define LOOM_CONTROL_H

#include <stddef.h>

int loom_control_status_text(char *buffer, size_t buffer_size);
int loom_control_get_setting_value(const char *key, char *buffer, size_t buffer_size);
int loom_control_set_setting_quiet(const char *key, const char *value);
int loom_control_list_displays_text(char *buffer, size_t buffer_size);
int loom_control_add_display(const char *id,
                             const char *name,
                             const char *transport,
                             int width,
                             int height,
                             int refresh);
int loom_control_remove_display(const char *id);
int loom_control_pause_display(const char *id);
int loom_control_resume_display(const char *id);
int loom_control_set_display_setting(const char *id, const char *key, const char *value);
int loom_control_list_usb_devices_text(char *buffer, size_t buffer_size);
int loom_control_metrics_text(char *buffer, size_t buffer_size);

int loom_control_status(void);
int loom_control_get_setting(const char *key);
int loom_control_set_setting(const char *key, const char *value);

#endif
