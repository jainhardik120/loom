#include "evdi_logging.h"

#include "evdi_lib.h"
#include "logging.h"

#include <stdarg.h>
#include <stdio.h>

static void evdi_log_callback(void *user_data, const char *fmt, ...)
{
    (void)user_data;

    va_list args;
    va_start(args, fmt);
    fprintf(stdout, "[evdi] ");
    vfprintf(stdout, fmt, args);
    fputc('\n', stdout);
    fflush(stdout);
    va_end(args);
}

void evdi_logging_install(void)
{
    struct evdi_logging ctx;
    ctx.function = evdi_log_callback;
    ctx.user_data = NULL;
    evdi_set_logging(ctx);
}

