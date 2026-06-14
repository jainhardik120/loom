#ifndef TABLET_DISPLAYD_EVDI_DEVICE_H
#define TABLET_DISPLAYD_EVDI_DEVICE_H

#include <stdbool.h>

#include "evdi_lib.h"
#include "framebuffer.h"

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
} EvdiDevice;

bool evdi_device_open(EvdiDevice *device, int requested_device);
void evdi_device_connect(EvdiDevice *device);
void evdi_device_close(EvdiDevice *device);
void evdi_device_request_update(EvdiDevice *device);
int evdi_device_event_fd(const EvdiDevice *device);
void evdi_device_handle_events(EvdiDevice *device);

#endif

