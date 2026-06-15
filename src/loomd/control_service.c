#include "control_service.h"

#include "logging.h"
#include "loom_protocol.h"
#include "usb_accessory.h"

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static int method_status(sd_bus_message *message, void *userdata, sd_bus_error *ret_error)
{
    (void)ret_error;
    LoomControlService *service = userdata;
    char status[1024];
    snprintf(status,
             sizeof(status),
             "loomd running; displays=%zu; active=%zu",
             service->display_manager ? service->display_manager->session_count : 0,
             service->display_manager ? display_manager_active_count(service->display_manager) : 0);

    return sd_bus_reply_method_return(message, "s", status);
}

static void sync_settings_from_manager(LoomControlService *service)
{
    service->settings->display_count = service->display_manager->session_count;
    for (size_t i = 0; i < service->display_manager->session_count; i++) {
        service->settings->displays[i] = service->display_manager->sessions[i].profile;
    }
}

static void save_settings(LoomControlService *service)
{
    if (service->config_path[0] == '\0') {
        return;
    }
    sync_settings_from_manager(service);
    if (!loom_settings_save(service->settings, service->config_path)) {
        log_warn("failed to save settings to %s", service->config_path);
    }
}

static int method_list_displays(sd_bus_message *message, void *userdata, sd_bus_error *ret_error)
{
    (void)ret_error;
    LoomControlService *service = userdata;
    char output[4096];
    size_t used = 0;

    used += (size_t)snprintf(output + used,
                             sizeof(output) - used,
                             "display_count=%zu\n",
                             service->display_manager->session_count);
    for (size_t i = 0; i < service->display_manager->session_count && used < sizeof(output); i++) {
        LoomDisplaySession *session = &service->display_manager->sessions[i];
        used += (size_t)snprintf(output + used,
                                 sizeof(output) - used,
                                 "%s name=\"%s\" state=%s enabled=%s paused=%s mode=%dx%d@%d transport=%s evdi_card=%d\n",
                                 session->profile.id,
                                 session->profile.name,
                                 display_session_state_name(session->state),
                                 session->profile.enabled ? "true" : "false",
                                 session->profile.paused ? "true" : "false",
                                 session->profile.mode_width,
                                 session->profile.mode_height,
                                 session->profile.mode_refresh,
                                 session->profile.stream_transport,
                                 session->evdi_open ? session->evdi.device_index : -1);
    }

    return sd_bus_reply_method_return(message, "s", output);
}

static int method_list_usb_devices(sd_bus_message *message, void *userdata, sd_bus_error *ret_error)
{
    (void)userdata;
    (void)ret_error;
    char output[4096];
    if (!usb_accessory_list_identities_text(output, sizeof(output))) {
        snprintf(output, sizeof(output), "usb_device_count=0\n");
    }
    return sd_bus_reply_method_return(message, "s", output);
}

static size_t appendf(char *buffer, size_t buffer_size, size_t used, const char *format, ...)
{
    if (used >= buffer_size) {
        return used;
    }

    va_list args;
    va_start(args, format);
    int written = vsnprintf(buffer + used, buffer_size - used, format, args);
    va_end(args);
    if (written < 0) {
        return used;
    }

    size_t next = used + (size_t)written;
    return next >= buffer_size ? buffer_size - 1 : next;
}

static ProcessMetricsSampler *process_sampler_for_pid(LoomControlService *service, pid_t pid)
{
    for (size_t i = 0; i < LOOM_PROCESS_METRICS_MAX; i++) {
        if (service->process_samplers[i].pid == pid) {
            return &service->process_samplers[i];
        }
    }

    for (size_t i = 0; i < LOOM_PROCESS_METRICS_MAX; i++) {
        if (service->process_samplers[i].pid == 0) {
            process_metrics_sampler_init(&service->process_samplers[i]);
            service->process_samplers[i].pid = pid;
            return &service->process_samplers[i];
        }
    }

    size_t slot = (size_t)pid % LOOM_PROCESS_METRICS_MAX;
    process_metrics_sampler_init(&service->process_samplers[slot]);
    service->process_samplers[slot].pid = pid;
    return &service->process_samplers[slot];
}

static void process_role_for_pid(LoomControlService *service,
                                 pid_t pid,
                                 char *role,
                                 size_t role_size,
                                 char *display,
                                 size_t display_size)
{
    snprintf(role, role_size, "%s", pid == getpid() ? "loomd" : "loom-child");
    snprintf(display, display_size, "-");

    if (!service->display_manager || pid == getpid()) {
        return;
    }

    for (size_t i = 0; i < service->display_manager->session_count; i++) {
        LoomDisplaySession *session = &service->display_manager->sessions[i];
        if (!session->evdi_open) {
            continue;
        }

        StreamMetrics metrics;
        memset(&metrics, 0, sizeof(metrics));
        stream_encoder_get_metrics(&session->evdi.stream_encoder, &metrics);
        if (metrics.encoder_pid > 0 && process_metrics_is_descendant(pid, metrics.encoder_pid)) {
            snprintf(role, role_size, "%s", pid == metrics.encoder_pid ? "encoder-launcher" : "encoder");
            snprintf(display, display_size, "%s", session->profile.id);
            return;
        }
    }
}

static int method_metrics(sd_bus_message *message, void *userdata, sd_bus_error *ret_error)
{
    (void)ret_error;
    LoomControlService *service = userdata;
    char output[16384];
    output[0] = '\0';

    pid_t pids[LOOM_PROCESS_METRICS_MAX];
    size_t process_count = 0;
    process_metrics_collect_tree(getpid(), pids, &process_count, LOOM_PROCESS_METRICS_MAX);
    size_t display_count = service->display_manager ? service->display_manager->session_count : 0;
    size_t used = appendf(output, sizeof(output), 0, "process_count=%zu\n", process_count);

    for (size_t i = 0; i < process_count; i++) {
        char role[32];
        char display[64];
        process_role_for_pid(service, pids[i], role, sizeof(role), display, sizeof(display));
        process_metrics_append_text(process_sampler_for_pid(service, pids[i]),
                                    pids[i],
                                    role,
                                    display,
                                    output,
                                    sizeof(output));
    }
    used = strlen(output);

    used = appendf(output, sizeof(output), used, "display_count=%zu\n", display_count);

    if (service->display_manager) {
        for (size_t i = 0; i < service->display_manager->session_count; i++) {
            LoomDisplaySession *session = &service->display_manager->sessions[i];
            StreamMetrics metrics;
            memset(&metrics, 0, sizeof(metrics));
            if (session->evdi_open) {
                stream_encoder_get_metrics(&session->evdi.stream_encoder, &metrics);
            }

            int mode_width = session->evdi_open && session->evdi.mode.width > 0
                                 ? session->evdi.mode.width
                                 : session->profile.mode_width;
            int mode_height = session->evdi_open && session->evdi.mode.height > 0
                                  ? session->evdi.mode.height
                                  : session->profile.mode_height;
            int mode_refresh = session->evdi_open && session->evdi.mode.refresh_rate > 0
                                   ? session->evdi.mode.refresh_rate
                                   : session->profile.mode_refresh;

            used = appendf(output,
                           sizeof(output),
                           used,
                           "display id=%s name=\"%s\" state=%s enabled=%s paused=%s present=%s evdi_card=%d "
                           "mode=%dx%d@%d stream_running=%s target_fps=%d raw_fps=%.1f "
                           "encoded_kbps=%.1f encoded_au_fps=%.1f usb_mbps=%.2f "
                           "avg_raw_write_ms=%.3f max_raw_write_ms=%.3f slow_raw_writes=%d "
                           "avg_usb_write_ms=%.3f max_usb_write_ms=%.3f usb_writes=%d "
                           "raw_frames_total=%llu encoded_bytes_total=%llu usb_bytes_total=%llu\n",
                           session->profile.id,
                           session->profile.name,
                           display_session_state_name(session->state),
                           session->profile.enabled ? "true" : "false",
                           session->profile.paused ? "true" : "false",
                           session->device_present ? "true" : "false",
                           session->evdi_open ? session->evdi.device_index : -1,
                           mode_width,
                           mode_height,
                           mode_refresh,
                           metrics.running ? "true" : "false",
                           metrics.target_fps,
                           metrics.raw_fps,
                           metrics.encoded_kbps,
                           metrics.encoded_au_fps,
                           metrics.usb_mbps,
                           metrics.avg_raw_write_ms,
                           metrics.max_raw_write_ms,
                           metrics.slow_raw_writes,
                           metrics.avg_usb_write_ms,
                           metrics.max_usb_write_ms,
                           metrics.usb_writes,
                           metrics.raw_frames_total,
                           metrics.encoded_bytes_total,
                           metrics.usb_bytes_total);
        }
    }

    used = appendf(output,
                   sizeof(output),
                   used,
                   "android_metrics available=false reason=client_telemetry_not_implemented\n");

    return sd_bus_reply_method_return(message, "s", output);
}

static int method_add_display(sd_bus_message *message, void *userdata, sd_bus_error *ret_error)
{
    LoomControlService *service = userdata;
    const char *id = NULL;
    const char *name = NULL;
    int width = 0;
    int height = 0;
    int refresh = 0;
    const char *transport = NULL;

    int rc = sd_bus_message_read(message, "sssiii", &id, &name, &transport, &width, &height, &refresh);
    if (rc < 0) {
        return rc;
    }
    if (!id || id[0] == '\0' || display_manager_find(service->display_manager, id)) {
        return sd_bus_error_setf(ret_error, SD_BUS_ERROR_INVALID_ARGS, "invalid or duplicate display id");
    }

    LoomDisplayProfile profile;
    memset(&profile, 0, sizeof(profile));
    snprintf(profile.id, sizeof(profile.id), "%s", id);
    snprintf(profile.name, sizeof(profile.name), "%s", name && name[0] ? name : id);
    profile.enabled = true;
    profile.paused = false;
    profile.auto_connect = true;
    profile.mode_width = width > 0 ? width : 1920;
    profile.mode_height = height > 0 ? height : 1200;
    profile.mode_refresh = refresh > 0 ? refresh : 30;
    snprintf(profile.stream_transport, sizeof(profile.stream_transport), "%s",
             transport && transport[0] ? transport : "usb_accessory");
    snprintf(profile.stream_host, sizeof(profile.stream_host), "127.0.0.1");
    profile.stream_port = 27183;
    profile.stream_bitrate_kbps = 12000;
    profile.stream_fps = profile.mode_refresh;
    if (!display_manager_add_profile(service->display_manager, &profile)) {
        return sd_bus_error_setf(ret_error, SD_BUS_ERROR_FAILED, "failed to add display");
    }
    display_manager_start_session(display_manager_find(service->display_manager, id));
    save_settings(service);
    return sd_bus_reply_method_return(message, "b", 1);
}

static int method_remove_display(sd_bus_message *message, void *userdata, sd_bus_error *ret_error)
{
    LoomControlService *service = userdata;
    const char *id = NULL;
    int rc = sd_bus_message_read(message, "s", &id);
    if (rc < 0) {
        return rc;
    }
    if (!display_manager_remove(service->display_manager, id)) {
        return sd_bus_error_setf(ret_error, SD_BUS_ERROR_INVALID_ARGS, "unknown display: %s", id);
    }
    save_settings(service);
    return sd_bus_reply_method_return(message, "b", 1);
}

static int method_pause_display(sd_bus_message *message, void *userdata, sd_bus_error *ret_error)
{
    LoomControlService *service = userdata;
    const char *id = NULL;
    int rc = sd_bus_message_read(message, "s", &id);
    if (rc < 0) {
        return rc;
    }
    LoomDisplaySession *session = display_manager_find(service->display_manager, id);
    if (!session) {
        return sd_bus_error_setf(ret_error, SD_BUS_ERROR_INVALID_ARGS, "unknown display: %s", id);
    }
    session->profile.paused = true;
    display_manager_stop_session(session);
    save_settings(service);
    return sd_bus_reply_method_return(message, "b", 1);
}

static int method_resume_display(sd_bus_message *message, void *userdata, sd_bus_error *ret_error)
{
    LoomControlService *service = userdata;
    const char *id = NULL;
    int rc = sd_bus_message_read(message, "s", &id);
    if (rc < 0) {
        return rc;
    }
    LoomDisplaySession *session = display_manager_find(service->display_manager, id);
    if (!session) {
        return sd_bus_error_setf(ret_error, SD_BUS_ERROR_INVALID_ARGS, "unknown display: %s", id);
    }
    session->profile.paused = false;
    session->profile.enabled = true;
    display_manager_start_session(session);
    save_settings(service);
    return sd_bus_reply_method_return(message, "b", 1);
}

static int method_set_display_setting(sd_bus_message *message, void *userdata, sd_bus_error *ret_error)
{
    LoomControlService *service = userdata;
    const char *id = NULL;
    const char *key = NULL;
    const char *value = NULL;
    int rc = sd_bus_message_read(message, "sss", &id, &key, &value);
    if (rc < 0) {
        return rc;
    }
    LoomDisplaySession *session = display_manager_find(service->display_manager, id);
    if (!session) {
        return sd_bus_error_setf(ret_error, SD_BUS_ERROR_INVALID_ARGS, "unknown display: %s", id);
    }
    if (!loom_display_profile_set_value(&session->profile, key, value)) {
        return sd_bus_error_setf(ret_error, SD_BUS_ERROR_INVALID_ARGS, "invalid setting: %s", key);
    }
    if (session->evdi_open) {
        display_manager_stop_session(session);
        if (session->profile.enabled && !session->profile.paused) {
            display_manager_start_session(session);
        }
    }
    save_settings(service);
    return sd_bus_reply_method_return(message, "b", 1);
}

static int method_get_setting(sd_bus_message *message, void *userdata, sd_bus_error *ret_error)
{
    LoomControlService *service = userdata;
    const char *key = NULL;
    char value[256];

    int rc = sd_bus_message_read(message, "s", &key);
    if (rc < 0) {
        return rc;
    }

    if (!loom_settings_get_value(service->settings, key, value, sizeof(value))) {
        return sd_bus_error_setf(ret_error,
                                 SD_BUS_ERROR_INVALID_ARGS,
                                 "unknown setting: %s",
                                 key);
    }

    return sd_bus_reply_method_return(message, "s", value);
}

static bool setting_requires_stream_restart(const char *key)
{
    return strcmp(key, "stream_transport") == 0 ||
           strcmp(key, "stream_host") == 0 ||
           strcmp(key, "stream_port") == 0 ||
           strcmp(key, "stream_bitrate_kbps") == 0 ||
           strcmp(key, "stream_fps") == 0;
}

static int method_set_setting(sd_bus_message *message, void *userdata, sd_bus_error *ret_error)
{
    LoomControlService *service = userdata;
    const char *key = NULL;
    const char *value = NULL;

    int rc = sd_bus_message_read(message, "ss", &key, &value);
    if (rc < 0) {
        return rc;
    }

    if (!loom_settings_set_value(service->settings, key, value)) {
        return sd_bus_error_setf(ret_error,
                                 SD_BUS_ERROR_INVALID_ARGS,
                                 "invalid setting or value: %s=%s",
                                 key,
                                 value);
    }

    (void)setting_requires_stream_restart;
    log_info("D-Bus setting changed: %s=%s", key, value);
    return sd_bus_reply_method_return(message, "b", 1);
}

static const sd_bus_vtable k_control_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Status", "", "s", method_status, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetSetting", "s", "s", method_get_setting, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetSetting", "ss", "b", method_set_setting, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ListDisplays", "", "s", method_list_displays, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ListUsbDevices", "", "s", method_list_usb_devices, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Metrics", "", "s", method_metrics, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("AddDisplay", "sssiii", "b", method_add_display, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RemoveDisplay", "s", "b", method_remove_display, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("PauseDisplay", "s", "b", method_pause_display, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ResumeDisplay", "s", "b", method_resume_display, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetDisplaySetting", "sss", "b", method_set_display_setting, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

static bool parse_id(const char *text, uid_t *out)
{
    if (!text || text[0] == '\0') {
        return false;
    }

    char *end = NULL;
    errno = 0;
    unsigned long parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }

    *out = (uid_t)parsed;
    return true;
}

static int open_sudo_user_bus(sd_bus **bus)
{
    uid_t sudo_uid = 0;
    uid_t sudo_gid = 0;

    if (geteuid() != 0 ||
        !parse_id(getenv("SUDO_UID"), &sudo_uid) ||
        !parse_id(getenv("SUDO_GID"), &sudo_gid)) {
        return -ENXIO;
    }

    char runtime_dir[128];
    char bus_address[160];
    snprintf(runtime_dir, sizeof(runtime_dir), "/run/user/%lu", (unsigned long)sudo_uid);
    snprintf(bus_address, sizeof(bus_address), "unix:path=%s/bus", runtime_dir);

    log_info("trying invoking user's D-Bus address as uid=%lu gid=%lu: %s",
             (unsigned long)sudo_uid,
             (unsigned long)sudo_gid,
             bus_address);

    uid_t original_euid = geteuid();
    gid_t original_egid = getegid();
    const char *old_runtime_dir = getenv("XDG_RUNTIME_DIR");
    const char *old_bus_address = getenv("DBUS_SESSION_BUS_ADDRESS");

    char old_runtime_copy[256] = "";
    char old_bus_copy[256] = "";
    if (old_runtime_dir) {
        snprintf(old_runtime_copy, sizeof(old_runtime_copy), "%s", old_runtime_dir);
    }
    if (old_bus_address) {
        snprintf(old_bus_copy, sizeof(old_bus_copy), "%s", old_bus_address);
    }

    int rc = 0;
    if (setegid((gid_t)sudo_gid) != 0) {
        rc = -errno;
    }
    if (rc >= 0 && seteuid(sudo_uid) != 0) {
        rc = -errno;
    }
    if (rc >= 0 && setenv("XDG_RUNTIME_DIR", runtime_dir, 1) != 0) {
        rc = -errno;
    }
    if (rc >= 0 && setenv("DBUS_SESSION_BUS_ADDRESS", bus_address, 1) != 0) {
        rc = -errno;
    }
    if (rc >= 0) {
        rc = sd_bus_open_user(bus);
    }

    int restore_rc = 0;
    if (seteuid(original_euid) != 0) {
        restore_rc = -errno;
    }
    if (setegid(original_egid) != 0 && restore_rc >= 0) {
        restore_rc = -errno;
    }

    if (old_runtime_dir) {
        setenv("XDG_RUNTIME_DIR", old_runtime_copy, 1);
    } else {
        unsetenv("XDG_RUNTIME_DIR");
    }
    if (old_bus_address) {
        setenv("DBUS_SESSION_BUS_ADDRESS", old_bus_copy, 1);
    } else {
        unsetenv("DBUS_SESSION_BUS_ADDRESS");
    }

    if (restore_rc < 0) {
        log_error("failed to restore root privileges after D-Bus connection: %s", strerror(-restore_rc));
        return restore_rc;
    }

    return rc;
}

bool control_service_start(LoomControlService *service,
                           LoomSettings *settings,
                           LoomDisplayManager *display_manager,
                           const char *config_path)
{
    memset(service, 0, sizeof(*service));
    service->settings = settings;
    service->display_manager = display_manager;
    host_metrics_sampler_init(&service->metrics_sampler);
    for (size_t i = 0; i < LOOM_PROCESS_METRICS_MAX; i++) {
        process_metrics_sampler_init(&service->process_samplers[i]);
    }
    if (config_path) {
        snprintf(service->config_path, sizeof(service->config_path), "%s", config_path);
    }

    int rc = -ENXIO;
    if (geteuid() == 0 && getenv("SUDO_UID")) {
        rc = open_sudo_user_bus(&service->bus);
        if (rc < 0) {
            log_warn("failed to open invoking user's D-Bus as root: %s", strerror(-rc));
        }
    }

    if (rc < 0) {
        rc = sd_bus_open_user(&service->bus);
    }
    if (rc < 0) {
        log_warn("sd_bus_open_user failed as current user: %s", strerror(-rc));
        if (geteuid() != 0 || !getenv("SUDO_UID")) {
            rc = open_sudo_user_bus(&service->bus);
        }

        if (rc < 0) {
            log_warn("D-Bus disabled: failed to connect to user bus: %s", strerror(-rc));
            log_warn("run loomd inside the user session, or run with sudo so SUDO_UID points at the desktop user");
            control_service_stop(service);
            return false;
        }
    }

    rc = sd_bus_add_object_vtable(service->bus,
                                  &service->object_slot,
                                  LOOM_DBUS_OBJECT_PATH,
                                  LOOM_DBUS_INTERFACE,
                                  k_control_vtable,
                                  service);
    if (rc < 0) {
        log_warn("D-Bus disabled: failed to add object vtable: %s", strerror(-rc));
        control_service_stop(service);
        return false;
    }

    rc = sd_bus_request_name(service->bus, LOOM_DBUS_SERVICE, 0);
    if (rc < 0) {
        log_warn("D-Bus disabled: failed to own %s: %s", LOOM_DBUS_SERVICE, strerror(-rc));
        control_service_stop(service);
        return false;
    }

    service->available = true;
    log_info("D-Bus service ready: %s %s %s",
             LOOM_DBUS_SERVICE,
             LOOM_DBUS_OBJECT_PATH,
             LOOM_DBUS_INTERFACE);
    return true;
}

void control_service_stop(LoomControlService *service)
{
    if (!service) {
        return;
    }

    service->name_slot = sd_bus_slot_unref(service->name_slot);
    service->object_slot = sd_bus_slot_unref(service->object_slot);
    service->bus = sd_bus_unref(service->bus);
    service->available = false;
}

int control_service_fd(const LoomControlService *service)
{
    if (!service || !service->available) {
        return -1;
    }
    return sd_bus_get_fd(service->bus);
}

short control_service_events(const LoomControlService *service)
{
    if (!service || !service->available) {
        return 0;
    }

    int events = sd_bus_get_events(service->bus);
    if (events < 0) {
        return 0;
    }
    return (short)events;
}

void control_service_dispatch(LoomControlService *service)
{
    if (!service || !service->available) {
        return;
    }

    for (;;) {
        int rc = sd_bus_process(service->bus, NULL);
        if (rc < 0) {
            log_warn("D-Bus processing failed: %s", strerror(-rc));
            return;
        }
        if (rc == 0) {
            return;
        }
    }
}
