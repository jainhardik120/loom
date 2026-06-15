#ifndef LOOMD_DISPLAY_MANAGER_H
#define LOOMD_DISPLAY_MANAGER_H

#include "evdi_device.h"
#include "loom_settings.h"

#include <stdbool.h>
#include <stddef.h>

#define LOOM_MAX_DISPLAY_SESSIONS 8

typedef enum LoomDisplaySessionState {
    LOOM_DISPLAY_CONFIGURED = 0,
    LOOM_DISPLAY_WAITING_FOR_DEVICE,
    LOOM_DISPLAY_CONNECTING_EVDI,
    LOOM_DISPLAY_CONNECTED,
    LOOM_DISPLAY_STREAMING,
    LOOM_DISPLAY_PAUSED,
    LOOM_DISPLAY_DISCONNECTED,
    LOOM_DISPLAY_REMOVED
} LoomDisplaySessionState;

typedef struct LoomDisplaySession {
    LoomDisplayProfile profile;
    LoomDisplaySessionState state;
    bool device_present;
    bool evdi_open;
    EvdiDevice evdi;
} LoomDisplaySession;

typedef struct LoomDisplayManager {
    LoomDisplaySession sessions[LOOM_MAX_DISPLAY_SESSIONS];
    size_t session_count;
} LoomDisplayManager;

void display_manager_init(LoomDisplayManager *manager);
const char *display_session_state_name(LoomDisplaySessionState state);
bool display_manager_load_settings(LoomDisplayManager *manager, const LoomSettings *settings);
bool display_manager_start_session(LoomDisplaySession *session);
void display_manager_stop_session(LoomDisplaySession *session);
void display_manager_start_enabled(LoomDisplayManager *manager);
void display_manager_tick(LoomDisplayManager *manager);
void display_manager_stop_all(LoomDisplayManager *manager);
LoomDisplaySession *display_manager_find(LoomDisplayManager *manager, const char *id);
bool display_manager_add_profile(LoomDisplayManager *manager, const LoomDisplayProfile *profile);
bool display_manager_remove(LoomDisplayManager *manager, const char *id);
size_t display_manager_active_count(const LoomDisplayManager *manager);

#endif
