#ifndef LOOMD_EVDI_DEVICE_H
#define LOOMD_EVDI_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "evdi_lib.h"
#include "framebuffer.h"
#include "stream.h"

typedef struct EvdiDevice {
    int device_index;
    evdi_handle handle;
    struct evdi_event_context event_context;
    struct evdi_mode mode;
    Framebuffer framebuffer;
    bool connected;
    bool capture_enabled;
    bool update_in_flight;
    bool dump_frame;
    bool frame_dumped;
    const char *dump_path;
    int mode_width;
    int mode_height;
    int mode_refresh;
    uint32_t pixel_area_limit;
    uint32_t pixel_per_second_limit;
    StreamEncoder stream_encoder;
} EvdiDevice;

bool evdi_device_open(EvdiDevice *device, int requested_device);
void evdi_device_connect(EvdiDevice *device);
void evdi_device_close(EvdiDevice *device);
void evdi_device_request_update(EvdiDevice *device);
int evdi_device_event_fd(const EvdiDevice *device);
void evdi_device_handle_events(EvdiDevice *device);

#endif
