#ifndef LOOMD_EVENT_LOOP_H
#define LOOMD_EVENT_LOOP_H

#include "control_service.h"
#include "evdi_device.h"

int event_loop_run(EvdiDevice *device, LoomControlService *control_service);

#endif
