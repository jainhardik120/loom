#include "loom_control.h"

#include <gtk/gtk.h>
#include <libayatana-appindicator/app-indicator.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct LoomTrayApp {
    AppIndicator *indicator;
    GtkWidget *menu;
    GtkWidget *status_item;
    GtkWidget *capture_item;
    GtkWidget *dump_item;
    GtkWidget *stream_item;
    GtkWidget *transport_tcp_item;
    GtkWidget *transport_usb_item;
    GtkWidget *fps_30_item;
    GtkWidget *fps_60_item;
    GtkWidget *fps_90_item;
    GtkWidget *bitrate_4000_item;
    GtkWidget *bitrate_8000_item;
    GtkWidget *bitrate_12000_item;
    GtkWidget *bitrate_20000_item;
    bool refreshing;
    bool reachable;
} LoomTrayApp;

static gboolean refresh_state(gpointer user_data);

static bool get_setting_bool(const char *key, bool *value)
{
    char buffer[64];
    if (loom_control_get_setting_value(key, buffer, sizeof(buffer)) != 0) {
        return false;
    }
    *value = strcmp(buffer, "true") == 0 || strcmp(buffer, "1") == 0;
    return true;
}

static bool get_setting_int(const char *key, int *value)
{
    char buffer[64];
    if (loom_control_get_setting_value(key, buffer, sizeof(buffer)) != 0) {
        return false;
    }
    *value = atoi(buffer);
    return true;
}

static bool get_setting_text(const char *key, char *buffer, size_t buffer_size)
{
    return loom_control_get_setting_value(key, buffer, buffer_size) == 0;
}

static void set_item_active(GtkWidget *item, bool active)
{
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), active);
}

static void set_menu_sensitive(LoomTrayApp *app, bool sensitive)
{
    gtk_widget_set_sensitive(app->capture_item, sensitive);
    gtk_widget_set_sensitive(app->dump_item, sensitive);
    gtk_widget_set_sensitive(app->stream_item, sensitive);
    gtk_widget_set_sensitive(app->transport_tcp_item, sensitive);
    gtk_widget_set_sensitive(app->transport_usb_item, sensitive);
    gtk_widget_set_sensitive(app->fps_30_item, sensitive);
    gtk_widget_set_sensitive(app->fps_60_item, sensitive);
    gtk_widget_set_sensitive(app->fps_90_item, sensitive);
    gtk_widget_set_sensitive(app->bitrate_4000_item, sensitive);
    gtk_widget_set_sensitive(app->bitrate_8000_item, sensitive);
    gtk_widget_set_sensitive(app->bitrate_12000_item, sensitive);
    gtk_widget_set_sensitive(app->bitrate_20000_item, sensitive);
}

static void truncate_label(char *text, size_t size)
{
    const size_t limit = 110;
    if (strlen(text) <= limit || size <= limit + 4) {
        return;
    }
    text[limit] = '.';
    text[limit + 1] = '.';
    text[limit + 2] = '.';
    text[limit + 3] = '\0';
}

static void set_status_label(LoomTrayApp *app, const char *status)
{
    char label[160];
    snprintf(label, sizeof(label), "%.155s", status);
    truncate_label(label, sizeof(label));
    gtk_menu_item_set_label(GTK_MENU_ITEM(app->status_item), label);
}

static void set_indicator_state(LoomTrayApp *app, bool reachable, bool streaming)
{
    app_indicator_set_status(app->indicator, APP_INDICATOR_STATUS_ACTIVE);
    app_indicator_set_icon_full(app->indicator,
                                reachable && streaming ? "network-transmit-receive-symbolic" :
                                                          "video-display-symbolic",
                                reachable ? "Loom" : "Loom disconnected");
}

static void set_boolean_setting(GtkCheckMenuItem *item, gpointer user_data)
{
    LoomTrayApp *app = user_data;
    if (app->refreshing || !app->reachable) {
        return;
    }

    const char *key = NULL;
    if (GTK_WIDGET(item) == app->capture_item) {
        key = "capture_enabled";
    } else if (GTK_WIDGET(item) == app->dump_item) {
        key = "dump_frame";
    } else if (GTK_WIDGET(item) == app->stream_item) {
        key = "stream_enabled";
    }

    if (!key) {
        return;
    }

    const char *value = gtk_check_menu_item_get_active(item) ? "true" : "false";
    if (loom_control_set_setting_quiet(key, value) != 0) {
        refresh_state(app);
        return;
    }
    refresh_state(app);
}

static void set_transport(GtkCheckMenuItem *item, gpointer user_data)
{
    LoomTrayApp *app = user_data;
    if (app->refreshing || !app->reachable || !gtk_check_menu_item_get_active(item)) {
        return;
    }

    const char *transport = GTK_WIDGET(item) == app->transport_usb_item ? "usb_accessory" : "tcp";
    if (loom_control_set_setting_quiet("stream_transport", transport) != 0) {
        refresh_state(app);
        return;
    }
    refresh_state(app);
}

static void set_numeric_setting(const char *key, int value, LoomTrayApp *app)
{
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d", value);
    if (loom_control_set_setting_quiet(key, buffer) != 0) {
        refresh_state(app);
        return;
    }
    refresh_state(app);
}

static void set_fps(GtkCheckMenuItem *item, gpointer user_data)
{
    LoomTrayApp *app = user_data;
    if (app->refreshing || !app->reachable || !gtk_check_menu_item_get_active(item)) {
        return;
    }

    if (GTK_WIDGET(item) == app->fps_90_item) {
        set_numeric_setting("stream_fps", 90, app);
    } else if (GTK_WIDGET(item) == app->fps_60_item) {
        set_numeric_setting("stream_fps", 60, app);
    } else {
        set_numeric_setting("stream_fps", 30, app);
    }
}

static void set_bitrate(GtkCheckMenuItem *item, gpointer user_data)
{
    LoomTrayApp *app = user_data;
    if (app->refreshing || !app->reachable || !gtk_check_menu_item_get_active(item)) {
        return;
    }

    if (GTK_WIDGET(item) == app->bitrate_20000_item) {
        set_numeric_setting("stream_bitrate_kbps", 20000, app);
    } else if (GTK_WIDGET(item) == app->bitrate_12000_item) {
        set_numeric_setting("stream_bitrate_kbps", 12000, app);
    } else if (GTK_WIDGET(item) == app->bitrate_8000_item) {
        set_numeric_setting("stream_bitrate_kbps", 8000, app);
    } else {
        set_numeric_setting("stream_bitrate_kbps", 4000, app);
    }
}

static void refresh_clicked(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    refresh_state(user_data);
}

static void quit_clicked(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    (void)user_data;
    gtk_main_quit();
}

static gboolean refresh_state(gpointer user_data)
{
    LoomTrayApp *app = user_data;
    char status[1024];
    char transport[64];
    bool capture_enabled = false;
    bool dump_frame = false;
    bool stream_enabled = false;
    int fps = 0;
    int bitrate = 0;

    app->refreshing = true;
    if (loom_control_status_text(status, sizeof(status)) != 0) {
        app->reachable = false;
        set_status_label(app, "loomd is not reachable");
        set_menu_sensitive(app, false);
        set_indicator_state(app, false, false);
        app->refreshing = false;
        return G_SOURCE_CONTINUE;
    }

    app->reachable = true;
    set_status_label(app, status);
    set_menu_sensitive(app, true);

    if (get_setting_bool("capture_enabled", &capture_enabled)) {
        set_item_active(app->capture_item, capture_enabled);
    }
    if (get_setting_bool("dump_frame", &dump_frame)) {
        set_item_active(app->dump_item, dump_frame);
    }
    if (get_setting_bool("stream_enabled", &stream_enabled)) {
        set_item_active(app->stream_item, stream_enabled);
    }
    if (get_setting_text("stream_transport", transport, sizeof(transport))) {
        set_item_active(app->transport_usb_item, strcmp(transport, "usb_accessory") == 0);
        set_item_active(app->transport_tcp_item, strcmp(transport, "usb_accessory") != 0);
    }
    if (get_setting_int("stream_fps", &fps)) {
        set_item_active(app->fps_90_item, fps >= 90);
        set_item_active(app->fps_60_item, fps >= 60 && fps < 90);
        set_item_active(app->fps_30_item, fps < 60);
    }
    if (get_setting_int("stream_bitrate_kbps", &bitrate)) {
        set_item_active(app->bitrate_20000_item, bitrate >= 20000);
        set_item_active(app->bitrate_12000_item, bitrate >= 12000 && bitrate < 20000);
        set_item_active(app->bitrate_8000_item, bitrate >= 8000 && bitrate < 12000);
        set_item_active(app->bitrate_4000_item, bitrate < 8000);
    }

    set_indicator_state(app, true, stream_enabled);
    app->refreshing = false;
    return G_SOURCE_CONTINUE;
}

static GtkWidget *separator(void)
{
    return gtk_separator_menu_item_new();
}

static GtkWidget *check_item(const char *label, GCallback callback, LoomTrayApp *app)
{
    GtkWidget *item = gtk_check_menu_item_new_with_label(label);
    g_signal_connect(item, "toggled", callback, app);
    return item;
}

static GtkWidget *radio_item(GSList **group, const char *label, GCallback callback, LoomTrayApp *app)
{
    GtkWidget *item = gtk_radio_menu_item_new_with_label(*group, label);
    *group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
    g_signal_connect(item, "toggled", callback, app);
    return item;
}

static void build_menu(LoomTrayApp *app)
{
    app->menu = gtk_menu_new();
    app->status_item = gtk_menu_item_new_with_label("loomd is not reachable");
    gtk_widget_set_sensitive(app->status_item, false);
    gtk_menu_shell_append(GTK_MENU_SHELL(app->menu), app->status_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(app->menu), separator());

    app->capture_item = check_item("Capture framebuffer", G_CALLBACK(set_boolean_setting), app);
    app->dump_item = check_item("Dump next raw frame", G_CALLBACK(set_boolean_setting), app);
    app->stream_item = check_item("Stream enabled", G_CALLBACK(set_boolean_setting), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(app->menu), app->capture_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(app->menu), app->dump_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(app->menu), app->stream_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(app->menu), separator());

    GtkWidget *transport_root = gtk_menu_item_new_with_label("Transport");
    GtkWidget *transport_menu = gtk_menu_new();
    GSList *transport_group = NULL;
    app->transport_tcp_item = radio_item(&transport_group, "TCP", G_CALLBACK(set_transport), app);
    app->transport_usb_item = radio_item(&transport_group, "USB accessory", G_CALLBACK(set_transport), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(transport_menu), app->transport_tcp_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(transport_menu), app->transport_usb_item);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(transport_root), transport_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(app->menu), transport_root);

    GtkWidget *fps_root = gtk_menu_item_new_with_label("FPS");
    GtkWidget *fps_menu = gtk_menu_new();
    GSList *fps_group = NULL;
    app->fps_30_item = radio_item(&fps_group, "30", G_CALLBACK(set_fps), app);
    app->fps_60_item = radio_item(&fps_group, "60", G_CALLBACK(set_fps), app);
    app->fps_90_item = radio_item(&fps_group, "90", G_CALLBACK(set_fps), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(fps_menu), app->fps_30_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(fps_menu), app->fps_60_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(fps_menu), app->fps_90_item);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(fps_root), fps_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(app->menu), fps_root);

    GtkWidget *bitrate_root = gtk_menu_item_new_with_label("Bitrate");
    GtkWidget *bitrate_menu = gtk_menu_new();
    GSList *bitrate_group = NULL;
    app->bitrate_4000_item = radio_item(&bitrate_group, "4 Mbps", G_CALLBACK(set_bitrate), app);
    app->bitrate_8000_item = radio_item(&bitrate_group, "8 Mbps", G_CALLBACK(set_bitrate), app);
    app->bitrate_12000_item = radio_item(&bitrate_group, "12 Mbps", G_CALLBACK(set_bitrate), app);
    app->bitrate_20000_item = radio_item(&bitrate_group, "20 Mbps", G_CALLBACK(set_bitrate), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(bitrate_menu), app->bitrate_4000_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(bitrate_menu), app->bitrate_8000_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(bitrate_menu), app->bitrate_12000_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(bitrate_menu), app->bitrate_20000_item);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(bitrate_root), bitrate_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(app->menu), bitrate_root);
    gtk_menu_shell_append(GTK_MENU_SHELL(app->menu), separator());

    GtkWidget *refresh_item = gtk_menu_item_new_with_label("Refresh");
    GtkWidget *quit_item = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(refresh_item, "activate", G_CALLBACK(refresh_clicked), app);
    g_signal_connect(quit_item, "activate", G_CALLBACK(quit_clicked), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(app->menu), refresh_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(app->menu), quit_item);

    gtk_widget_show_all(app->menu);
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);

    LoomTrayApp app;
    memset(&app, 0, sizeof(app));
    app.indicator = app_indicator_new("loom-tray",
                                      "video-display-symbolic",
                                      APP_INDICATOR_CATEGORY_HARDWARE);
    app_indicator_set_title(app.indicator, "Loom");
    app_indicator_set_status(app.indicator, APP_INDICATOR_STATUS_ACTIVE);

    build_menu(&app);
    app_indicator_set_menu(app.indicator, GTK_MENU(app.menu));

    refresh_state(&app);
    g_timeout_add_seconds(2, refresh_state, &app);

    gtk_main();
    return 0;
}
