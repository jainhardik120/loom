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

int loom_control_status(void)
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

    printf("%s\n", status);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);
    return 0;
}

int loom_control_get_setting(const char *key)
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

    printf("%s\n", value);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);
    return 0;
}

int loom_control_set_setting(const char *key, const char *value)
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

    printf("%s\n", changed ? "ok" : "unchanged");
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);
    return 0;
}
