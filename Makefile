ROOT_DIR := $(abspath .)
EVDI_DIR := $(ROOT_DIR)/third_party/evdi
SRC_DIR := $(ROOT_DIR)/src

.PHONY: all evdi loomd loomctl android clean run-loomd submodules

all: loomd loomctl

submodules:
	git submodule update --init --recursive

evdi:
	$(MAKE) -C $(EVDI_DIR) library

loomd: evdi
	$(MAKE) -C $(SRC_DIR) loomd EVDI_DIR=$(EVDI_DIR)

loomctl:
	$(MAKE) -C $(SRC_DIR) loomctl EVDI_DIR=$(EVDI_DIR)

android:
	cd android && ./gradlew :app:assembleDebug

run-loomd: loomd
	LD_LIBRARY_PATH=$(EVDI_DIR)/library sudo -E $(ROOT_DIR)/build/loomd

clean:
	$(MAKE) -C $(SRC_DIR) clean
	$(MAKE) -C $(EVDI_DIR)/library clean
