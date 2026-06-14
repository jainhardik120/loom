EVDI_DIR ?= ../third_party/evdi
BUILD_DIR ?= build
TARGET := $(BUILD_DIR)/loomd

CC ?= gcc
CFLAGS ?= -O2
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -I$(EVDI_DIR)/library
WARNINGS := -Wall -Wextra -Wpedantic
LDFLAGS += -L$(EVDI_DIR)/library
LDFLAGS += -Wl,-rpath,'$$ORIGIN/../../third_party/evdi/library'
LDLIBS += -levdi

SRCS := \
	src/main.c \
	src/evdi_device.c \
	src/framebuffer.c \
	src/event_loop.c \
	src/logging.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRCS) $(EVDI_DIR)/library/libevdi.so
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(SRCS) $(LDFLAGS) $(LDLIBS) -o $@

$(EVDI_DIR)/library/libevdi.so:
	$(MAKE) -C $(EVDI_DIR) library

run: $(TARGET)
	LD_LIBRARY_PATH=$(EVDI_DIR)/library sudo -E ./$(TARGET)

clean:
	rm -rf $(BUILD_DIR) frame.raw
