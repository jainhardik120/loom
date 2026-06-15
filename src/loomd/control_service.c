#include "control_service.h"

#include "logging.h"
#include "loom_protocol.h"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static int method_status(sd_bus_message *message, void *userdata, sd_bus_error *ret_error)
{
    (void)ret_error;
    LoomControlService *service = userdata;
    char status[640];

    snprintf(status,
             sizeof(status),
             "loomd running; evdi_card=%d; connected=%s; capture=%s; dump_frame=%s; stream=%s; stream_transport=%s; stream_target=%s:%d; mode_limit=%dx%d@%d",
             service->device ? service->device->device_index : -1,
             service->device && service->device->connected ? "true" : "false",
             service->settings && service->settings->capture_enabled ? "true" : "false",
             service->settings && service->settings->dump_frame ? "true" : "false",
             service->settings && service->settings->stream_enabled ? "true" : "false",
             service->settings ? service->settings->stream_transport : "",
             service->settings ? service->settings->stream_host : "",
             service->settings ? service->settings->stream_port : 0,
             service->settings ? service->settings->mode_width : 0,
             service->settings ? service->settings->mode_height : 0,
             service->settings ? service->settings->mode_refresh : 0);

    return sd_bus_reply_method_return(message, "s", status);
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

    if (service->device) {
        service->device->capture_enabled = service->settings->capture_enabled;
        service->device->dump_frame = service->settings->dump_frame;
        service->device->dump_path = service->settings->dump_path;
        StreamConfig stream_config;
        stream_config.enabled = service->settings->stream_enabled;
        snprintf(stream_config.transport, sizeof(stream_config.transport), "%s", service->settings->stream_transport);
        snprintf(stream_config.host, sizeof(stream_config.host), "%s", service->settings->stream_host);
        stream_config.port = service->settings->stream_port;
        stream_config.bitrate_kbps = service->settings->stream_bitrate_kbps;
        stream_config.fps = service->settings->stream_fps;
        if (stream_config.enabled &&
            service->device->stream_encoder.running &&
            setting_requires_stream_restart(key)) {
            log_info("restarting stream encoder for setting change: %s", key);
            stream_encoder_stop(&service->device->stream_encoder);
        }
        stream_encoder_configure(&service->device->stream_encoder, &stream_config);
        if (!stream_config.enabled) {
            stream_encoder_stop(&service->device->stream_encoder);
        } else if (service->device->framebuffer.registered) {
            stream_encoder_start(&service->device->stream_encoder,
                                 service->device->framebuffer.evdi_buffer.width,
                                 service->device->framebuffer.evdi_buffer.height,
                                 service->device->framebuffer.evdi_buffer.stride);
        }
    }

    log_info("D-Bus setting changed: %s=%s", key, value);
    return sd_bus_reply_method_return(message, "b", 1);
}

static const sd_bus_vtable k_control_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Status", "", "s", method_status, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetSetting", "s", "s", method_get_setting, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetSetting", "ss", "b", method_set_setting, SD_BUS_VTABLE_UNPRIVILEGED),
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
                           EvdiDevice *device)
{
    memset(service, 0, sizeof(*service));
    service->settings = settings;
    service->device = device;

    int rc = sd_bus_open_user(&service->bus);
    if (rc < 0) {
        log_warn("sd_bus_open_user failed as current user: %s", strerror(-rc));
        rc = open_sudo_user_bus(&service->bus);

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
