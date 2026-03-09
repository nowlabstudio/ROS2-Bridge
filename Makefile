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
.PHONY: docker-build-visualizer
docker-build-visualizer:
	docker build -t w6100-visualizer:latest -f docker/Dockerfile.visualizer docker/

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
			west build -b $(BOARD) app --pristine=always"

# ──────────────────────────────────────────────
.PHONY: flash-uf2
flash-uf2:
	@ls -la $(WORKSPACE)/build/zephyr/zephyr.uf2 2>/dev/null || \
		echo "Run first: make build"

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

# ==============================================================
# Host Workspace (RoboClaw TCP adapter, safety bridge)
# ==============================================================
HOST_WS := $(PROJECT_DIR)/host_ws

.PHONY: host-install-deps
host-install-deps:
	cd $(HOST_WS)/src/basicmicro_python && pip3 install -e .
	@echo "If rosdep is available:"
	rosdep install --from-paths $(HOST_WS)/src --ignore-src -r -y || true

.PHONY: host-build
host-build:
	cd $(HOST_WS) && colcon build --symlink-install

.PHONY: host-shell
host-shell:
	bash -c "source $(HOST_WS)/install/setup.bash && exec bash"

.PHONY: robot-start
robot-start:
	bash tools/start-robot.sh

# ==============================================================
.PHONY: help
help:
	@echo ""
	@echo "  ── Zephyr Firmware (Tier 1) ──"
	@echo "  make docker-build    - Build Docker image (once)"
	@echo "  make workspace-init  - Download Zephyr workspace (~2GB, once)"
	@echo "  make build           - Build firmware"
	@echo "  make flash           - Flash via OpenOCD"
	@echo "  make flash-uf2       - Show UF2 file location"
	@echo "  make monitor         - Serial monitor 115200 baud"
	@echo "  make shell           - Open Docker shell"
	@echo "  make clean           - Remove build artifacts"
	@echo ""
	@echo "  ── Host Workspace (Tier 2) ──"
	@echo "  make host-install-deps - Install basicmicro_python + rosdep"
	@echo "  make host-build        - colcon build host_ws"
	@echo "  make host-shell        - Shell with host_ws sourced"
	@echo "  make robot-start       - Launch full robot (tmux)"
	@echo ""
