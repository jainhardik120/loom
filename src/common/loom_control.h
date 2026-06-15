#ifndef LOOM_CONTROL_H
#define LOOM_CONTROL_H

#include <stddef.h>

int loom_control_status_text(char *buffer, size_t buffer_size);
int loom_control_get_setting_value(const char *key, char *buffer, size_t buffer_size);
int loom_control_set_setting_quiet(const char *key, const char *value);

int loom_control_status(void);
int loom_control_get_setting(const char *key);
int loom_control_set_setting(const char *key, const char *value);

#endif
