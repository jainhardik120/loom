#ifndef TABLET_DISPLAYD_LOGGING_H
#define TABLET_DISPLAYD_LOGGING_H

#include "evdi_lib.h"

void log_info(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_error(const char *fmt, ...);
void log_debug(const char *fmt, ...);

void logging_install_evdi_logger(void);
struct evdi_logging logging_evdi_context(void);

#endif

