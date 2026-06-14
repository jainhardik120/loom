#ifndef TABLET_DISPLAYD_FRAMEBUFFER_H
#define TABLET_DISPLAYD_FRAMEBUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "evdi_lib.h"

#define FRAMEBUFFER_RECT_CAPACITY 16

typedef struct Framebuffer {
    struct evdi_buffer evdi_buffer;
    size_t size_bytes;
    int bytes_per_pixel;
    bool registered;
} Framebuffer;

bool framebuffer_init(Framebuffer *fb, int id, const struct evdi_mode *mode);
void framebuffer_destroy(Framebuffer *fb);
bool framebuffer_dump_raw(const Framebuffer *fb, const char *path);
void framebuffer_log_rects(const struct evdi_rect *rects, int rect_count);

#endif

