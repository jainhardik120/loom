ROOT_DIR := $(abspath .)
EVDI_DIR := $(ROOT_DIR)/third_party/evdi
LOOMD_DIR := $(ROOT_DIR)/loomd

.PHONY: all evdi loomd loomctl gnome-shell-extension android clean run-loomd submodules

all: loomd

submodules:
	git submodule update --init --recursive

evdi:
	$(MAKE) -C $(EVDI_DIR) library

loomd: evdi
	$(MAKE) -C $(LOOMD_DIR) EVDI_DIR=$(EVDI_DIR)

loomctl:
	@echo "loomctl is not implemented yet"

gnome-shell-extension:
	@echo "GNOME Shell extension is not implemented yet"

android:
	@echo "Android client is not implemented yet"

run-loomd: loomd
	LD_LIBRARY_PATH=$(EVDI_DIR)/library sudo -E $(LOOMD_DIR)/build/loomd

clean:
	$(MAKE) -C $(LOOMD_DIR) clean
	$(MAKE) -C $(EVDI_DIR)/library clean
