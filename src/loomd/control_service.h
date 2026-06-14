#ifndef LOOMD_CONTROL_SERVICE_H
#define LOOMD_CONTROL_SERVICE_H

#include "evdi_device.h"
#include "loom_settings.h"

#include <stdbool.h>
#include <systemd/sd-bus.h>

typedef struct LoomControlService {
    sd_bus *bus;
    sd_bus_slot *object_slot;
    sd_bus_slot *name_slot;
    LoomSettings *settings;
    EvdiDevice *device;
    bool available;
} LoomControlService;

bool control_service_start(LoomControlService *service,
                           LoomSettings *settings,
                           EvdiDevice *device);
void control_service_stop(LoomControlService *service);
int control_service_fd(const LoomControlService *service);
short control_service_events(const LoomControlService *service);
void control_service_dispatch(LoomControlService *service);

#endif

