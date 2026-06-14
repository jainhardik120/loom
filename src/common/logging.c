#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "logging.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static void log_with_level(FILE *stream, const char *level, const char *fmt, va_list args)
{
    struct timespec ts;
    struct tm tm;
    char timebuf[32];

    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm);
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm);

    fprintf(stream, "%s.%03ld [%s] ", timebuf, ts.tv_nsec / 1000000L, level);
    vfprintf(stream, fmt, args);
    fputc('\n', stream);
    fflush(stream);
}

void log_info(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_with_level(stdout, "info", fmt, args);
    va_end(args);
}

void log_warn(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_with_level(stderr, "warn", fmt, args);
    va_end(args);
}

void log_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_with_level(stderr, "error", fmt, args);
    va_end(args);
}

void log_debug(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_with_level(stdout, "debug", fmt, args);
    va_end(args);
}
