#include "display_manager.h"

#include "logging.h"
#include "usb_accessory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void display_manager_init(LoomDisplayManager *manager)
{
    memset(manager, 0, sizeof(*manager));
}

const char *display_session_state_name(LoomDisplaySessionState state)
{
    switch (state) {
    case LOOM_DISPLAY_CONFIGURED:
        return "configured";
    case LOOM_DISPLAY_WAITING_FOR_DEVICE:
        return "waiting_for_device";
    case LOOM_DISPLAY_CONNECTING_EVDI:
        return "connecting_evdi";
    case LOOM_DISPLAY_CONNECTED:
        return "connected";
    case LOOM_DISPLAY_STREAMING:
        return "streaming";
    case LOOM_DISPLAY_PAUSED:
        return "paused";
    case LOOM_DISPLAY_DISCONNECTED:
        return "disconnected";
    case LOOM_DISPLAY_REMOVED:
        return "removed";
    }
    return "unknown";
}

static void apply_profile_to_evdi(LoomDisplaySession *session)
{
    EvdiDevice *device = &session->evdi;
    const LoomDisplayProfile *profile = &session->profile;

    device->capture_enabled = true;
    device->dump_frame = false;
    device->dump_path = "frame.raw";
    device->mode_width = profile->mode_width;
    device->mode_height = profile->mode_height;
    device->mode_refresh = profile->mode_refresh;
    device->pixel_area_limit = (uint32_t)(profile->mode_width * profile->mode_height);
    device->pixel_per_second_limit = device->pixel_area_limit * (uint32_t)profile->mode_refresh;

    StreamConfig stream_config;
    stream_config.enabled = true;
    snprintf(stream_config.transport, sizeof(stream_config.transport), "%s", profile->stream_transport);
    snprintf(stream_config.host, sizeof(stream_config.host), "%s", profile->stream_host);
    stream_config.port = profile->stream_port;
    stream_config.bitrate_kbps = profile->stream_bitrate_kbps;
    stream_config.fps = profile->stream_fps;
    stream_encoder_configure(&device->stream_encoder, &stream_config);
}

static bool session_requires_usb_accessory(const LoomDisplaySession *session)
{
    return strcmp(session->profile.stream_transport, "usb_accessory") == 0;
}

static bool session_required_device_present(const LoomDisplaySession *session)
{
    if (session_requires_usb_accessory(session)) {
        if (usb_accessory_device_present()) {
            return true;
        }
        const char *allow_switch = getenv("LOOM_USB_ACCESSORY_AUTO_SWITCH");
        if (allow_switch && strcmp(allow_switch, "1") == 0) {
            return usb_accessory_switch_to_accessory();
        }
        return false;
    }
    return true;
}

bool display_manager_load_settings(LoomDisplayManager *manager, const LoomSettings *settings)
{
    display_manager_init(manager);
    for (size_t i = 0; i < settings->display_count && i < LOOM_MAX_DISPLAY_SESSIONS; i++) {
        if (!display_manager_add_profile(manager, &settings->displays[i])) {
            return false;
        }
    }
    return true;
}

bool display_manager_start_session(LoomDisplaySession *session)
{
    if (!session->profile.enabled || session->profile.paused) {
        session->state = LOOM_DISPLAY_PAUSED;
        return true;
    }
    if (session->evdi_open) {
        return true;
    }
    if (!session_required_device_present(session)) {
        session->state = LOOM_DISPLAY_WAITING_FOR_DEVICE;
        log_info("display session id=%s waiting for required device", session->profile.id);
        return true;
    }

    session->state = LOOM_DISPLAY_CONNECTING_EVDI;
    log_info("starting display session id=%s name=%s mode=%dx%d@%d transport=%s",
             session->profile.id,
             session->profile.name,
             session->profile.mode_width,
             session->profile.mode_height,
             session->profile.mode_refresh,
             session->profile.stream_transport);
    if (!evdi_device_open(&session->evdi, -1)) {
        session->state = LOOM_DISPLAY_DISCONNECTED;
        return false;
    }
    session->evdi_open = true;
    apply_profile_to_evdi(session);
    evdi_device_connect(&session->evdi);
    session->state = LOOM_DISPLAY_CONNECTED;
    return true;
}

void display_manager_stop_session(LoomDisplaySession *session)
{
    if (session->evdi_open) {
        log_info("stopping display session id=%s", session->profile.id);
        evdi_device_close(&session->evdi);
        session->evdi_open = false;
    }
    session->state = session->profile.paused ? LOOM_DISPLAY_PAUSED : LOOM_DISPLAY_DISCONNECTED;
}

void display_manager_start_enabled(LoomDisplayManager *manager)
{
    for (size_t i = 0; i < manager->session_count; i++) {
        LoomDisplaySession *session = &manager->sessions[i];
        if (session->profile.enabled && !session->profile.paused && session->profile.auto_connect) {
            display_manager_start_session(session);
        } else {
            session->state = session->profile.paused ? LOOM_DISPLAY_PAUSED : LOOM_DISPLAY_CONFIGURED;
        }
    }
}

void display_manager_tick(LoomDisplayManager *manager)
{
    for (size_t i = 0; i < manager->session_count; i++) {
        LoomDisplaySession *session = &manager->sessions[i];
        if (!session->profile.enabled || session->profile.paused) {
            if (session->evdi_open) {
                display_manager_stop_session(session);
            }
            session->state = session->profile.paused ? LOOM_DISPLAY_PAUSED : LOOM_DISPLAY_CONFIGURED;
            continue;
        }

        bool present = session_required_device_present(session);
        session->device_present = present;
        if (!present) {
            if (session->evdi_open) {
                log_info("required device disappeared for display id=%s; disconnecting virtual display",
                         session->profile.id);
                display_manager_stop_session(session);
            }
            session->state = LOOM_DISPLAY_WAITING_FOR_DEVICE;
            continue;
        }

        if (!session->evdi_open && session->profile.auto_connect) {
            display_manager_start_session(session);
        }
    }
}

void display_manager_stop_all(LoomDisplayManager *manager)
{
    for (size_t i = 0; i < manager->session_count; i++) {
        display_manager_stop_session(&manager->sessions[i]);
    }
}

LoomDisplaySession *display_manager_find(LoomDisplayManager *manager, const char *id)
{
    for (size_t i = 0; i < manager->session_count; i++) {
        if (strcmp(manager->sessions[i].profile.id, id) == 0) {
            return &manager->sessions[i];
        }
    }
    return NULL;
}

bool display_manager_add_profile(LoomDisplayManager *manager, const LoomDisplayProfile *profile)
{
    if (manager->session_count >= LOOM_MAX_DISPLAY_SESSIONS ||
        display_manager_find(manager, profile->id)) {
        return false;
    }
    LoomDisplaySession *session = &manager->sessions[manager->session_count++];
    memset(session, 0, sizeof(*session));
    session->profile = *profile;
    session->state = LOOM_DISPLAY_CONFIGURED;
    return true;
}

bool display_manager_remove(LoomDisplayManager *manager, const char *id)
{
    for (size_t i = 0; i < manager->session_count; i++) {
        if (strcmp(manager->sessions[i].profile.id, id) != 0) {
            continue;
        }
        display_manager_stop_session(&manager->sessions[i]);
        for (size_t j = i + 1; j < manager->session_count; j++) {
            manager->sessions[j - 1] = manager->sessions[j];
        }
        manager->session_count--;
        return true;
    }
    return false;
}

size_t display_manager_active_count(const LoomDisplayManager *manager)
{
    size_t count = 0;
    for (size_t i = 0; i < manager->session_count; i++) {
        if (manager->sessions[i].evdi_open) {
            count++;
        }
    }
    return count;
}
