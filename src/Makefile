ROOT_DIR := $(abspath ..)
EVDI_DIR ?= $(ROOT_DIR)/third_party/evdi
BUILD_DIR ?= $(ROOT_DIR)/build

CC ?= gcc
CFLAGS ?= -O2
COMMON_CPPFLAGS := -D_POSIX_C_SOURCE=200809L -Icommon
WARNINGS := -Wall -Wextra -Wpedantic

LOOMD_CPPFLAGS := $(COMMON_CPPFLAGS) -Iloomd -I$(EVDI_DIR)/library
LOOMD_LDFLAGS := -L$(EVDI_DIR)/library -Wl,-rpath,'$$ORIGIN/../third_party/evdi/library'
SYSTEMD_LDLIBS := $(shell pkg-config --libs libsystemd)
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

.PHONY: all loomd loomctl clean

all: loomd loomctl

loomd: $(BUILD_DIR)/loomd

loomctl: $(BUILD_DIR)/loomctl

$(BUILD_DIR)/loomd: $(LOOMD_SRCS) $(EVDI_DIR)/library/libevdi.so
	@mkdir -p $(BUILD_DIR)
	$(CC) $(LOOMD_CPPFLAGS) $(CFLAGS) $(WARNINGS) $(LOOMD_SRCS) $(LOOMD_LDFLAGS) $(LOOMD_LDLIBS) -o $@

$(BUILD_DIR)/loomctl: $(LOOMCTL_SRCS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(COMMON_CPPFLAGS) $(CFLAGS) $(WARNINGS) $(LOOMCTL_SRCS) $(SYSTEMD_LDLIBS) -o $@

$(EVDI_DIR)/library/libevdi.so:
	$(MAKE) -C $(EVDI_DIR) library

clean:
	rm -rf $(BUILD_DIR)
