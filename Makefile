BOARD       := w5500_evb_pico
DOCKER_IMG  := w6100-zephyr-microros:latest
FLASH_PORT  := /dev/tty.usbmodem231401

PROJECT_DIR := $(shell pwd)
WORKSPACE   := $(PROJECT_DIR)/workspace
APP_DIR     := $(PROJECT_DIR)/app

# ──────────────────────────────────────────────
.PHONY: docker-build
docker-build:
	docker build -t $(DOCKER_IMG) docker/

# ──────────────────────────────────────────────
.PHONY: workspace-init
workspace-init:
	docker run --rm \
		-v $(WORKSPACE):/workdir \
		-v $(APP_DIR):/workdir/app \
		$(DOCKER_IMG) bash -c "\
			cd /workdir && \
			west init -l app && \
			west update && \
			west zephyr-export"

# ──────────────────────────────────────────────
.PHONY: build
build:
	docker run --rm \
		-v $(WORKSPACE):/workdir \
		-v $(APP_DIR):/workdir/app \
		$(DOCKER_IMG) bash -c "\
			cd /workdir && \
			west build -b $(BOARD) app --pristine=auto"

# ──────────────────────────────────────────────
.PHONY: flash-uf2
flash-uf2:
	@ls -la $(WORKSPACE)/build/zephyr/zephyr.uf2 2>/dev/null || \
		echo "Előbb futtasd: make build"

.PHONY: flash
flash:
	docker run --rm \
		-v $(WORKSPACE):/workdir \
		--device=$(FLASH_PORT) \
		$(DOCKER_IMG) bash -c "cd /workdir && west flash"

# ──────────────────────────────────────────────
.PHONY: monitor
monitor:
	screen $(FLASH_PORT) 115200

# ──────────────────────────────────────────────
.PHONY: shell
shell:
	docker run --rm -it \
		-v $(WORKSPACE):/workdir \
		-v $(APP_DIR):/workdir/app \
		$(DOCKER_IMG) bash

# ──────────────────────────────────────────────
.PHONY: clean
clean:
	rm -rf $(WORKSPACE)/build

.PHONY: help
help:
	@echo "  make docker-build    - Docker image build (egyszer)"
	@echo "  make workspace-init  - Zephyr letöltés (~2GB, egyszer)"
	@echo "  make build           - Firmware build"
	@echo "  make flash           - Flash (openocd)"
	@echo "  make flash-uf2       - UF2 fájl helye"
	@echo "  make monitor         - Serial monitor 115200"
	@echo "  make shell           - Docker shell"
	@echo "  make clean           - Build törlése"
