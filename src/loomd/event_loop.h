#ifndef LOOMD_EVENT_LOOP_H
#define LOOMD_EVENT_LOOP_H

#include "control_service.h"
#include "display_manager.h"

int event_loop_run(LoomDisplayManager *display_manager, LoomControlService *control_service);

#endif
