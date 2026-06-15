ROOT_DIR := $(abspath ..)
EVDI_DIR ?= $(ROOT_DIR)/third_party/evdi
BUILD_DIR ?= $(ROOT_DIR)/build

CC ?= gcc
CFLAGS ?= -O2
COMMON_CPPFLAGS := -D_POSIX_C_SOURCE=200809L -Icommon
WARNINGS := -Wall -Wextra -Wpedantic
GTK_WARNINGS := -Wall -Wextra -Wno-deprecated-declarations

LOOMD_CPPFLAGS := $(COMMON_CPPFLAGS) -Iloomd -I$(EVDI_DIR)/library
LOOMD_LDFLAGS := -L$(EVDI_DIR)/library -Wl,-rpath,'$$ORIGIN/../third_party/evdi/library'
SYSTEMD_LDLIBS := $(shell pkg-config --libs libsystemd)
TRAY_CPPFLAGS := $(shell pkg-config --cflags gtk+-3.0 ayatana-appindicator3-0.1)
TRAY_LDLIBS := $(shell pkg-config --libs gtk+-3.0 ayatana-appindicator3-0.1)
HAVE_LIBUSB := $(shell pkg-config --exists libusb-1.0 && echo 1 || echo 0)
LIBUSB_CPPFLAGS := $(shell pkg-config --cflags libusb-1.0 2>/dev/null)
LIBUSB_LDLIBS := $(shell pkg-config --libs libusb-1.0 2>/dev/null)

ifeq ($(HAVE_LIBUSB),1)
LOOMD_CPPFLAGS += -DHAVE_LIBUSB $(LIBUSB_CPPFLAGS)
endif

LOOMD_LDLIBS := -levdi $(SYSTEMD_LDLIBS) $(LIBUSB_LDLIBS) -pthread

COMMON_SRCS := \
	common/logging.c \
	common/loom_settings.c

LOOMD_SRCS := \
	loomd/main.c \
	loomd/control_service.c \
	loomd/display_manager.c \
	loomd/evdi_device.c \
	loomd/evdi_logging.c \
	loomd/framebuffer.c \
	loomd/event_loop.c \
	loomd/stream.c \
	loomd/usb_accessory.c \
	$(COMMON_SRCS)

LOOMCTL_SRCS := \
	loomctl/main.c \
	common/loom_settings.c \
	common/loom_control_dbus.c \
	common/logging.c

LOOM_TRAY_SRCS := \
	loom-tray/main.c \
	common/loom_control_dbus.c

.PHONY: all loomd loomctl loom-tray clean

all: loomd loomctl loom-tray

loomd: $(BUILD_DIR)/loomd

loomctl: $(BUILD_DIR)/loomctl

loom-tray: $(BUILD_DIR)/loom-tray

$(BUILD_DIR)/loomd: $(LOOMD_SRCS) $(EVDI_DIR)/library/libevdi.so
	@mkdir -p $(BUILD_DIR)
	$(CC) $(LOOMD_CPPFLAGS) $(CFLAGS) $(WARNINGS) $(LOOMD_SRCS) $(LOOMD_LDFLAGS) $(LOOMD_LDLIBS) -o $@

$(BUILD_DIR)/loomctl: $(LOOMCTL_SRCS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(COMMON_CPPFLAGS) $(CFLAGS) $(WARNINGS) $(LOOMCTL_SRCS) $(SYSTEMD_LDLIBS) -o $@

$(BUILD_DIR)/loom-tray: $(LOOM_TRAY_SRCS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(COMMON_CPPFLAGS) $(TRAY_CPPFLAGS) $(CFLAGS) $(GTK_WARNINGS) $(LOOM_TRAY_SRCS) $(SYSTEMD_LDLIBS) $(TRAY_LDLIBS) -o $@

$(EVDI_DIR)/library/libevdi.so:
	$(MAKE) -C $(EVDI_DIR) library

clean:
	rm -rf $(BUILD_DIR)
