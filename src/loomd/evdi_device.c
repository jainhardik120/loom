#include "evdi_device.h"

#include "logging.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const unsigned char k_loom_display_edid[] = {
    0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x4f, 0x52, 0x34, 0x12,
    0x01, 0x00, 0x00, 0x00, 0x01, 0x1e, 0x01, 0x04, 0xa5, 0x33, 0x1d, 0x78,
    0x0a, 0xcf, 0x74, 0xa3, 0x57, 0x4c, 0xb0, 0x23, 0x09, 0x48, 0x4c, 0x21,
    0x08, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x3a, 0x80, 0x18, 0x71, 0x38,
    0x2d, 0x40, 0x58, 0x2c, 0x45, 0x00, 0xfe, 0x1f, 0x11, 0x00, 0x00, 0x1e,
    0x00, 0x00, 0x00, 0xfc, 0x00, 0x4c, 0x6f, 0x6f, 0x6d, 0x20, 0x44, 0x69,
    0x73, 0x70, 0x6c, 0x61, 0x79, 0x0a, 0x00, 0x00, 0x00, 0xff, 0x00, 0x4c,
    0x4f, 0x4f, 0x4d, 0x44, 0x30, 0x30, 0x30, 0x31, 0x20, 0x20, 0x20, 0x0a,
    0x00, 0x00, 0x00, 0xfd, 0x00, 0x32, 0x4b, 0x1e, 0x53, 0x11, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1e
};

static void set_edid_descriptor_text(unsigned char *descriptor, unsigned char tag, const char *text)
{
    memset(descriptor, 0, 18);
    descriptor[3] = tag;
    descriptor[4] = 0x00;
    size_t length = strlen(text);
    if (length > 13) {
        length = 13;
    }
    memcpy(descriptor + 5, text, length);
    if (length < 13) {
        descriptor[5 + length] = '\n';
        for (size_t i = length + 1; i < 13; i++) {
            descriptor[5 + i] = ' ';
        }
    }
}

static void set_edid_range_descriptor(unsigned char *descriptor, int refresh)
{
    memset(descriptor, 0, 18);
    descriptor[3] = 0xfd;
    descriptor[5] = 24;
    descriptor[6] = refresh > 90 ? (unsigned char)refresh : 90;
    descriptor[7] = 30;
    descriptor[8] = 170;
    descriptor[9] = 17;
}

static void set_edid_detailed_timing(unsigned char *descriptor, int width, int height, int refresh)
{
    int h_blank = 160;
    int v_blank = 35;
    int h_sync_offset = 48;
    int h_sync_width = 32;
    int v_sync_offset = 3;
    int v_sync_width = 6;
    int h_size_mm = 520;
    int v_size_mm = (height * h_size_mm) / width;
    int h_total = width + h_blank;
    int v_total = height + v_blank;
    int pixel_clock_10khz = (int)((long long)h_total * (long long)v_total *
                                      (long long)refresh / 10000LL);

    memset(descriptor, 0, 18);
    descriptor[0] = (unsigned char)(pixel_clock_10khz & 0xff);
    descriptor[1] = (unsigned char)((pixel_clock_10khz >> 8) & 0xff);
    descriptor[2] = (unsigned char)(width & 0xff);
    descriptor[3] = (unsigned char)(h_blank & 0xff);
    descriptor[4] = (unsigned char)(((width >> 8) & 0x0f) << 4 | ((h_blank >> 8) & 0x0f));
    descriptor[5] = (unsigned char)(height & 0xff);
    descriptor[6] = (unsigned char)(v_blank & 0xff);
    descriptor[7] = (unsigned char)(((height >> 8) & 0x0f) << 4 | ((v_blank >> 8) & 0x0f));
    descriptor[8] = (unsigned char)(h_sync_offset & 0xff);
    descriptor[9] = (unsigned char)(h_sync_width & 0xff);
    descriptor[10] = (unsigned char)(((v_sync_offset & 0x0f) << 4) | (v_sync_width & 0x0f));
    descriptor[11] = (unsigned char)(((h_sync_offset >> 8) & 0x03) << 6 |
                                     (((h_sync_width >> 8) & 0x03) << 4) |
                                     (((v_sync_offset >> 4) & 0x03) << 2) |
                                     ((v_sync_width >> 4) & 0x03));
    descriptor[12] = (unsigned char)(h_size_mm & 0xff);
    descriptor[13] = (unsigned char)(v_size_mm & 0xff);
    descriptor[14] = (unsigned char)(((h_size_mm >> 8) & 0x0f) << 4 |
                                     ((v_size_mm >> 8) & 0x0f));
    descriptor[17] = 0x1a;
}

static void build_loom_display_edid(EvdiDevice *device)
{
    memcpy(device->edid, k_loom_display_edid, sizeof(device->edid));

    memset(device->edid + 35, 0x01, 18);
    set_edid_detailed_timing(device->edid + 54,
                             device->mode_width,
                             device->mode_height,
                             device->mode_refresh);
    set_edid_descriptor_text(device->edid + 72, 0xfc, "Loom Display");
    set_edid_descriptor_text(device->edid + 90, 0xff, "LOOMD0001");
    set_edid_range_descriptor(device->edid + 108, device->mode_refresh);

    device->edid[126] = 0;
    unsigned int sum = 0;
    for (int i = 0; i < 127; i++) {
        sum += device->edid[i];
    }
    device->edid[127] = (unsigned char)((256 - (sum & 0xff)) & 0xff);
}

static const char *device_status_name(enum evdi_device_status status)
{
    switch (status) {
    case AVAILABLE:
        return "available";
    case UNRECOGNIZED:
        return "unrecognized";
    case NOT_PRESENT:
        return "not-present";
    }
    return "unknown";
}

static int find_available_device(void)
{
    for (int i = 0; i < 64; i++) {
        enum evdi_device_status status = evdi_check_device(i);
        log_debug("evdi_check_device(%d): %s", i, device_status_name(status));
        if (status == AVAILABLE) {
            return i;
        }
    }

    log_info("no available EVDI card found; asking kernel module to add one");
    if (!evdi_add_device()) {
        log_error("evdi_add_device failed; try: sudo modprobe evdi");
        return -1;
    }

    for (int i = 0; i < 64; i++) {
        enum evdi_device_status status = evdi_check_device(i);
        log_debug("evdi_check_device(%d): %s", i, device_status_name(status));
        if (status == AVAILABLE) {
            return i;
        }
    }

    log_error("evdi_add_device succeeded, but no available EVDI card was found");
    return -1;
}

static void handle_dpms(int dpms_mode, void *user_data)
{
    (void)user_data;
    log_info("event: dpms mode=%d", dpms_mode);
}

static void handle_crtc_state(int state, void *user_data)
{
    (void)user_data;
    log_info("event: crtc_state state=%d", state);
}

static void handle_cursor_set(struct evdi_cursor_set cursor_set, void *user_data)
{
    (void)user_data;
    log_info("event: cursor_set enabled=%u size=%ux%u hot=%d,%d stride=%u format=0x%x bytes=%u",
             cursor_set.enabled,
             cursor_set.width,
             cursor_set.height,
             cursor_set.hot_x,
             cursor_set.hot_y,
             cursor_set.stride,
             cursor_set.pixel_format,
             cursor_set.buffer_length);
    free(cursor_set.buffer);
}

static void handle_cursor_move(struct evdi_cursor_move cursor_move, void *user_data)
{
    (void)user_data;
    log_info("event: cursor_move x=%d y=%d", cursor_move.x, cursor_move.y);
}

static void handle_ddcci_data(struct evdi_ddcci_data ddcci_data, void *user_data)
{
    (void)user_data;
    log_info("event: ddcci_data address=0x%x flags=0x%x bytes=%u",
             ddcci_data.address,
             ddcci_data.flags,
             ddcci_data.buffer_length);
}

static void register_capture_buffer(EvdiDevice *device, struct evdi_mode mode)
{
    if (!device->capture_enabled) {
        return;
    }

    if (device->framebuffer.registered) {
        evdi_unregister_buffer(device->handle, device->framebuffer.evdi_buffer.id);
        device->framebuffer.registered = false;
    }
    stream_encoder_stop(&device->stream_encoder);
    framebuffer_destroy(&device->framebuffer);
    device->update_in_flight = false;

    if (!framebuffer_init(&device->framebuffer, 0, &mode)) {
        log_error("capture disabled because framebuffer allocation failed");
        device->capture_enabled = false;
        return;
    }

    evdi_register_buffer(device->handle, device->framebuffer.evdi_buffer);
    device->framebuffer.registered = true;
    log_info("registered EVDI framebuffer id=%d", device->framebuffer.evdi_buffer.id);
    stream_encoder_start(&device->stream_encoder,
                         device->framebuffer.evdi_buffer.width,
                         device->framebuffer.evdi_buffer.height,
                         device->framebuffer.evdi_buffer.stride);
    evdi_device_request_update(device);
}

static void handle_mode_changed(struct evdi_mode mode, void *user_data)
{
    EvdiDevice *device = user_data;
    device->mode = mode;
    log_info("event: mode_changed %dx%d@%d bpp=%d format=0x%x",
             mode.width,
             mode.height,
             mode.refresh_rate,
             mode.bits_per_pixel,
             mode.pixel_format);
    register_capture_buffer(device, mode);
}

static void grab_pixels(EvdiDevice *device)
{
    if (!device->capture_enabled || !device->framebuffer.registered) {
        return;
    }

    int rect_count = FRAMEBUFFER_RECT_CAPACITY;
    device->framebuffer.evdi_buffer.rect_count = rect_count;
    evdi_grab_pixels(device->handle, device->framebuffer.evdi_buffer.rects, &rect_count);
    device->framebuffer.evdi_buffer.rect_count = rect_count;
    device->update_in_flight = false;

    framebuffer_log_rects(device->framebuffer.evdi_buffer.rects, rect_count);
    if (device->dump_frame && !device->frame_dumped && rect_count > 0) {
        device->frame_dumped = framebuffer_dump_raw(&device->framebuffer, device->dump_path);
    }
    if (rect_count > 0) {
        stream_encoder_write_frame(&device->stream_encoder,
                                   device->framebuffer.evdi_buffer.buffer,
                                   device->framebuffer.size_bytes);
    }

    evdi_device_request_update(device);
}

static void handle_update_ready(int buffer_to_be_updated, void *user_data)
{
    EvdiDevice *device = user_data;
    log_info("event: update_ready buffer=%d", buffer_to_be_updated);

    if (!device->framebuffer.registered ||
        buffer_to_be_updated != device->framebuffer.evdi_buffer.id) {
        log_warn("ignoring update for unexpected buffer id=%d", buffer_to_be_updated);
        device->update_in_flight = false;
        return;
    }

    grab_pixels(device);
}

bool evdi_device_open(EvdiDevice *device, int requested_device)
{
    memset(device, 0, sizeof(*device));
    device->device_index = -1;
    device->capture_enabled = true;
    device->dump_frame = true;
    device->dump_path = "frame.raw";
    device->mode_width = 1920;
    device->mode_height = 1080;
    device->mode_refresh = 60;
    device->pixel_area_limit = 1920U * 1080U;
    device->pixel_per_second_limit = device->pixel_area_limit * 60U;
    stream_encoder_init(&device->stream_encoder);

    int device_index = requested_device >= 0 ? requested_device : find_available_device();
    if (device_index < 0) {
        return false;
    }

    if (requested_device >= 0) {
        enum evdi_device_status status = evdi_check_device(device_index);
        log_info("requested card%d status: %s", device_index, device_status_name(status));
        if (status != AVAILABLE) {
            log_error("/dev/dri/card%d is not an available EVDI device", device_index);
            return false;
        }
    }

    device->handle = evdi_open(device_index);
    if (device->handle == EVDI_INVALID_HANDLE) {
        log_error("failed to open /dev/dri/card%d as EVDI", device_index);
        return false;
    }

    device->device_index = device_index;

    memset(&device->event_context, 0, sizeof(device->event_context));
    device->event_context.dpms_handler = handle_dpms;
    device->event_context.mode_changed_handler = handle_mode_changed;
    device->event_context.update_ready_handler = handle_update_ready;
    device->event_context.crtc_state_handler = handle_crtc_state;
    device->event_context.cursor_set_handler = handle_cursor_set;
    device->event_context.cursor_move_handler = handle_cursor_move;
    device->event_context.ddcci_data_handler = handle_ddcci_data;
    device->event_context.user_data = device;

    evdi_enable_cursor_events(device->handle, false);
    log_info("opened EVDI device /dev/dri/card%d", device->device_index);
    return true;
}

void evdi_device_connect(EvdiDevice *device)
{
    build_loom_display_edid(device);
    log_info("connecting fake monitor EDID %dx%d@%d limit area=%u pps=%u",
             device->mode_width,
             device->mode_height,
             device->mode_refresh,
             device->pixel_area_limit,
             device->pixel_per_second_limit);
    evdi_connect2(device->handle,
                  device->edid,
                  sizeof(device->edid),
                  device->pixel_area_limit,
                  device->pixel_per_second_limit);
    device->connected = true;
}

void evdi_device_close(EvdiDevice *device)
{
    if (!device->handle) {
        return;
    }

    if (device->framebuffer.registered) {
        log_info("unregistering framebuffer id=%d", device->framebuffer.evdi_buffer.id);
        evdi_unregister_buffer(device->handle, device->framebuffer.evdi_buffer.id);
        device->framebuffer.registered = false;
    }
    framebuffer_destroy(&device->framebuffer);

    if (device->connected) {
        log_info("disconnecting EVDI monitor");
        evdi_disconnect(device->handle);
        device->connected = false;
    }

    log_info("closing EVDI device /dev/dri/card%d", device->device_index);
    evdi_close(device->handle);
    device->handle = NULL;
}

void evdi_device_request_update(EvdiDevice *device)
{
    if (!device->capture_enabled || !device->framebuffer.registered || device->update_in_flight) {
        return;
    }

    device->update_in_flight = true;
    bool ready_now = evdi_request_update(device->handle, device->framebuffer.evdi_buffer.id);
    log_debug("requested update buffer=%d ready_now=%s",
              device->framebuffer.evdi_buffer.id,
              ready_now ? "true" : "false");
    if (ready_now) {
        grab_pixels(device);
    }
}

int evdi_device_event_fd(const EvdiDevice *device)
{
    return evdi_get_event_ready(device->handle);
}

void evdi_device_handle_events(EvdiDevice *device)
{
    evdi_handle_events(device->handle, &device->event_context);
}
