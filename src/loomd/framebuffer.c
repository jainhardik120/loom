#include "framebuffer.h"

#include "logging.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int align_stride(int width, int bytes_per_pixel)
{
    const int pitch_mask = 63;
    int pixels = width + pitch_mask;
    pixels &= ~pitch_mask;
    return pixels * bytes_per_pixel;
}

bool framebuffer_init(Framebuffer *fb, int id, const struct evdi_mode *mode)
{
    memset(fb, 0, sizeof(*fb));

    fb->bytes_per_pixel = mode->bits_per_pixel > 0 ? mode->bits_per_pixel / 8 : 4;
    if (fb->bytes_per_pixel <= 0) {
        fb->bytes_per_pixel = 4;
    }

    fb->evdi_buffer.id = id;
    fb->evdi_buffer.width = mode->width;
    fb->evdi_buffer.height = mode->height;
    fb->evdi_buffer.stride = align_stride(mode->width, fb->bytes_per_pixel);
    fb->evdi_buffer.rect_count = FRAMEBUFFER_RECT_CAPACITY;
    fb->size_bytes = (size_t)fb->evdi_buffer.stride * (size_t)fb->evdi_buffer.height;

    fb->evdi_buffer.buffer = calloc(1, fb->size_bytes);
    fb->evdi_buffer.rects = calloc(FRAMEBUFFER_RECT_CAPACITY, sizeof(struct evdi_rect));
    if (!fb->evdi_buffer.buffer || !fb->evdi_buffer.rects) {
        log_error("failed to allocate framebuffer: %s", strerror(errno));
        framebuffer_destroy(fb);
        return false;
    }

    log_info("allocated framebuffer id=%d size=%dx%d stride=%d bytes=%zu bpp=%d format=0x%x",
             fb->evdi_buffer.id,
             fb->evdi_buffer.width,
             fb->evdi_buffer.height,
             fb->evdi_buffer.stride,
             fb->size_bytes,
             mode->bits_per_pixel,
             mode->pixel_format);

    return true;
}

void framebuffer_destroy(Framebuffer *fb)
{
    free(fb->evdi_buffer.buffer);
    free(fb->evdi_buffer.rects);
    memset(fb, 0, sizeof(*fb));
}

bool framebuffer_dump_raw(const Framebuffer *fb, const char *path)
{
    FILE *file = fopen(path, "wb");
    if (!file) {
        log_error("failed to open %s for frame dump: %s", path, strerror(errno));
        return false;
    }

    const size_t written = fwrite(fb->evdi_buffer.buffer, 1, fb->size_bytes, file);
    if (fclose(file) != 0 || written != fb->size_bytes) {
        log_error("failed to write complete frame dump to %s", path);
        return false;
    }

    log_info("dumped raw framebuffer to %s (%zu bytes)", path, fb->size_bytes);
    return true;
}

void framebuffer_log_rects(const struct evdi_rect *rects, int rect_count)
{
    log_info("dirty rectangles: count=%d", rect_count);
    for (int i = 0; i < rect_count; i++) {
        log_info("  rect[%d]=(%d,%d)-(%d,%d)",
                 i,
                 rects[i].x1,
                 rects[i].y1,
                 rects[i].x2,
                 rects[i].y2);
    }
}

