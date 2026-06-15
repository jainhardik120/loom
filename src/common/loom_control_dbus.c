#include "loom_control.h"

#include "loom_protocol.h"

#include <stdio.h>
#include <string.h>
#include <systemd/sd-bus.h>

static int open_user_bus(sd_bus **bus)
{
    int rc = sd_bus_open_user(bus);
    if (rc < 0) {
        fprintf(stderr, "failed to connect to user D-Bus: %s\n", strerror(-rc));
        fprintf(stderr, "make sure loomd is running in the same user session\n");
    }
    return rc;
}

int loom_control_status_text(char *buffer, size_t buffer_size)
{
    sd_bus *bus = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    const char *status = NULL;

    int rc = open_user_bus(&bus);
    if (rc < 0) {
        return 1;
    }

    rc = sd_bus_call_method(bus,
                            LOOM_DBUS_SERVICE,
                            LOOM_DBUS_OBJECT_PATH,
                            LOOM_DBUS_INTERFACE,
                            "Status",
                            &error,
                            &reply,
                            "");
    if (rc < 0) {
        fprintf(stderr, "D-Bus Status failed: %s\n", error.message ? error.message : strerror(-rc));
        sd_bus_error_free(&error);
        sd_bus_unref(bus);
        return 1;
    }

    rc = sd_bus_message_read(reply, "s", &status);
    if (rc < 0) {
        fprintf(stderr, "failed to parse Status reply: %s\n", strerror(-rc));
        sd_bus_message_unref(reply);
        sd_bus_unref(bus);
        return 1;
    }

    snprintf(buffer, buffer_size, "%s", status);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);
    return 0;
}

int loom_control_get_setting_value(const char *key, char *buffer, size_t buffer_size)
{
    sd_bus *bus = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    const char *value = NULL;

    int rc = open_user_bus(&bus);
    if (rc < 0) {
        return 1;
    }

    rc = sd_bus_call_method(bus,
                            LOOM_DBUS_SERVICE,
                            LOOM_DBUS_OBJECT_PATH,
                            LOOM_DBUS_INTERFACE,
                            "GetSetting",
                            &error,
                            &reply,
                            "s",
                            key);
    if (rc < 0) {
        fprintf(stderr, "D-Bus GetSetting failed: %s\n", error.message ? error.message : strerror(-rc));
        sd_bus_error_free(&error);
        sd_bus_unref(bus);
        return 1;
    }

    rc = sd_bus_message_read(reply, "s", &value);
    if (rc < 0) {
        fprintf(stderr, "failed to parse GetSetting reply: %s\n", strerror(-rc));
        sd_bus_message_unref(reply);
        sd_bus_unref(bus);
        return 1;
    }

    snprintf(buffer, buffer_size, "%s", value);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);
    return 0;
}

int loom_control_set_setting_quiet(const char *key, const char *value)
{
    sd_bus *bus = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int changed = 0;

    int rc = open_user_bus(&bus);
    if (rc < 0) {
        return 1;
    }

    rc = sd_bus_call_method(bus,
                            LOOM_DBUS_SERVICE,
                            LOOM_DBUS_OBJECT_PATH,
                            LOOM_DBUS_INTERFACE,
                            "SetSetting",
                            &error,
                            &reply,
                            "ss",
                            key,
                            value);
    if (rc < 0) {
        fprintf(stderr, "D-Bus SetSetting failed: %s\n", error.message ? error.message : strerror(-rc));
        sd_bus_error_free(&error);
        sd_bus_unref(bus);
        return 1;
    }

    rc = sd_bus_message_read(reply, "b", &changed);
    if (rc < 0) {
        fprintf(stderr, "failed to parse SetSetting reply: %s\n", strerror(-rc));
        sd_bus_message_unref(reply);
        sd_bus_unref(bus);
        return 1;
    }

    sd_bus_message_unref(reply);
    sd_bus_unref(bus);
    return changed ? 0 : 0;
}

static int call_bool_method_s(const char *method, const char *id)
{
    sd_bus *bus = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int changed = 0;

    int rc = open_user_bus(&bus);
    if (rc < 0) {
        return 1;
    }
    rc = sd_bus_call_method(bus,
                            LOOM_DBUS_SERVICE,
                            LOOM_DBUS_OBJECT_PATH,
                            LOOM_DBUS_INTERFACE,
                            method,
                            &error,
                            &reply,
                            "s",
                            id);
    if (rc < 0) {
        fprintf(stderr, "D-Bus %s failed: %s\n", method, error.message ? error.message : strerror(-rc));
        sd_bus_error_free(&error);
        sd_bus_unref(bus);
        return 1;
    }
    rc = sd_bus_message_read(reply, "b", &changed);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);
    return rc < 0 ? 1 : 0;
}

int loom_control_list_displays_text(char *buffer, size_t buffer_size)
{
    sd_bus *bus = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    const char *value = NULL;

    int rc = open_user_bus(&bus);
    if (rc < 0) {
        return 1;
    }
    rc = sd_bus_call_method(bus,
                            LOOM_DBUS_SERVICE,
                            LOOM_DBUS_OBJECT_PATH,
                            LOOM_DBUS_INTERFACE,
                            "ListDisplays",
                            &error,
                            &reply,
                            "");
    if (rc < 0) {
        fprintf(stderr, "D-Bus ListDisplays failed: %s\n", error.message ? error.message : strerror(-rc));
        sd_bus_error_free(&error);
        sd_bus_unref(bus);
        return 1;
    }
    rc = sd_bus_message_read(reply, "s", &value);
    if (rc < 0) {
        fprintf(stderr, "failed to parse ListDisplays reply: %s\n", strerror(-rc));
        sd_bus_message_unref(reply);
        sd_bus_unref(bus);
        return 1;
    }
    snprintf(buffer, buffer_size, "%s", value);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);
    return 0;
}

int loom_control_add_display(const char *id,
                             const char *name,
                             const char *transport,
                             int width,
                             int height,
                             int refresh)
{
    sd_bus *bus = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int changed = 0;

    int rc = open_user_bus(&bus);
    if (rc < 0) {
        return 1;
    }
    rc = sd_bus_call_method(bus,
                            LOOM_DBUS_SERVICE,
                            LOOM_DBUS_OBJECT_PATH,
                            LOOM_DBUS_INTERFACE,
                            "AddDisplay",
                            &error,
                            &reply,
                            "sssiii",
                            id,
                            name,
                            transport,
                            width,
                            height,
                            refresh);
    if (rc < 0) {
        fprintf(stderr, "D-Bus AddDisplay failed: %s\n", error.message ? error.message : strerror(-rc));
        sd_bus_error_free(&error);
        sd_bus_unref(bus);
        return 1;
    }
    rc = sd_bus_message_read(reply, "b", &changed);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);
    return rc < 0 ? 1 : 0;
}

int loom_control_remove_display(const char *id)
{
    return call_bool_method_s("RemoveDisplay", id);
}

int loom_control_pause_display(const char *id)
{
    return call_bool_method_s("PauseDisplay", id);
}

int loom_control_resume_display(const char *id)
{
    return call_bool_method_s("ResumeDisplay", id);
}

int loom_control_set_display_setting(const char *id, const char *key, const char *value)
{
    sd_bus *bus = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int changed = 0;

    int rc = open_user_bus(&bus);
    if (rc < 0) {
        return 1;
    }
    rc = sd_bus_call_method(bus,
                            LOOM_DBUS_SERVICE,
                            LOOM_DBUS_OBJECT_PATH,
                            LOOM_DBUS_INTERFACE,
                            "SetDisplaySetting",
                            &error,
                            &reply,
                            "sss",
                            id,
                            key,
                            value);
    if (rc < 0) {
        fprintf(stderr, "D-Bus SetDisplaySetting failed: %s\n", error.message ? error.message : strerror(-rc));
        sd_bus_error_free(&error);
        sd_bus_unref(bus);
        return 1;
    }
    rc = sd_bus_message_read(reply, "b", &changed);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);
    return rc < 0 ? 1 : 0;
}

int loom_control_list_usb_devices_text(char *buffer, size_t buffer_size)
{
    sd_bus *bus = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    const char *value = NULL;

    int rc = open_user_bus(&bus);
    if (rc < 0) {
        return 1;
    }
    rc = sd_bus_call_method(bus,
                            LOOM_DBUS_SERVICE,
                            LOOM_DBUS_OBJECT_PATH,
                            LOOM_DBUS_INTERFACE,
                            "ListUsbDevices",
                            &error,
                            &reply,
                            "");
    if (rc < 0) {
        fprintf(stderr, "D-Bus ListUsbDevices failed: %s\n", error.message ? error.message : strerror(-rc));
        sd_bus_error_free(&error);
        sd_bus_unref(bus);
        return 1;
    }
    rc = sd_bus_message_read(reply, "s", &value);
    if (rc < 0) {
        fprintf(stderr, "failed to parse ListUsbDevices reply: %s\n", strerror(-rc));
        sd_bus_message_unref(reply);
        sd_bus_unref(bus);
        return 1;
    }
    snprintf(buffer, buffer_size, "%s", value);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);
    return 0;
}

int loom_control_metrics_text(char *buffer, size_t buffer_size)
{
    sd_bus *bus = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    const char *value = NULL;

    int rc = open_user_bus(&bus);
    if (rc < 0) {
        return 1;
    }
    rc = sd_bus_call_method(bus,
                            LOOM_DBUS_SERVICE,
                            LOOM_DBUS_OBJECT_PATH,
                            LOOM_DBUS_INTERFACE,
                            "Metrics",
                            &error,
                            &reply,
                            "");
    if (rc < 0) {
        fprintf(stderr, "D-Bus Metrics failed: %s\n", error.message ? error.message : strerror(-rc));
        sd_bus_error_free(&error);
        sd_bus_unref(bus);
        return 1;
    }
    rc = sd_bus_message_read(reply, "s", &value);
    if (rc < 0) {
        fprintf(stderr, "failed to parse Metrics reply: %s\n", strerror(-rc));
        sd_bus_message_unref(reply);
        sd_bus_unref(bus);
        return 1;
    }
    snprintf(buffer, buffer_size, "%s", value);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);
    return 0;
}

int loom_control_status(void)
{
    char status[1024];
    int rc = loom_control_status_text(status, sizeof(status));
    if (rc != 0) {
        return rc;
    }
    printf("%s\n", status);
    return 0;
}

int loom_control_get_setting(const char *key)
{
    char value[256];
    int rc = loom_control_get_setting_value(key, value, sizeof(value));
    if (rc != 0) {
        return rc;
    }
    printf("%s\n", value);
    return 0;
}

int loom_control_set_setting(const char *key, const char *value)
{
    int rc = loom_control_set_setting_quiet(key, value);
    if (rc != 0) {
        return rc;
    }
    printf("ok\n");
    return 0;
}
