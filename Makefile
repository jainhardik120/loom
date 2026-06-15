ROOT_DIR := $(abspath .)
EVDI_DIR := $(ROOT_DIR)/third_party/evdi
SRC_DIR := $(ROOT_DIR)/src

.PHONY: all evdi evdi-module evdi-all loomd loomctl loom-tray android clean run-loomd submodules compile_commands

all: loomd loomctl loom-tray

submodules:
	git submodule update --init --recursive

evdi:
	$(MAKE) -C $(EVDI_DIR) library

evdi-module:
	$(MAKE) -C $(EVDI_DIR) module

evdi-all:
	$(MAKE) -C $(EVDI_DIR) module library

loomd: evdi
	$(MAKE) -C $(SRC_DIR) loomd EVDI_DIR=$(EVDI_DIR)

loomctl:
	$(MAKE) -C $(SRC_DIR) loomctl EVDI_DIR=$(EVDI_DIR)

loom-tray:
	$(MAKE) -C $(SRC_DIR) loom-tray EVDI_DIR=$(EVDI_DIR)

android:
	cd android && ./gradlew :app:assembleDebug

compile_commands:
	./scripts/generate-compile-commands.sh

run-loomd: loomd
	LD_LIBRARY_PATH=$(EVDI_DIR)/library sudo -E $(ROOT_DIR)/build/loomd

clean:
	$(MAKE) -C $(SRC_DIR) clean
	$(MAKE) -C $(EVDI_DIR)/library clean
