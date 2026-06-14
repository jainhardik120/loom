#ifndef LOOM_CONTROL_H
#define LOOM_CONTROL_H

int loom_control_status(void);
int loom_control_get_setting(const char *key);
int loom_control_set_setting(const char *key, const char *value);

#endif
