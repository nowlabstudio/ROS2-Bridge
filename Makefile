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

# Host-on (ha van natív ROS2): source /opt/ros/<distro>/setup.bash majd make host-build
.PHONY: host-build
host-build:
	cd $(HOST_WS) && colcon build --symlink-install

# ROS2 Dockerből: build a host_ws a ros:jazzy konténerben (nem kell natív ROS2)
# basicmicro_python nincs ament csomag → pip install; csak basicmicro_ros2 + roboclaw_tcp_adapter colcon.
.PHONY: host-build-docker
host-build-docker:
	docker run --rm \
		-v $(PROJECT_DIR)/host_ws:/host_ws -w /host_ws \
		ros:jazzy \
		bash -c "rm -rf /host_ws/build /host_ws/log && \
		source /opt/ros/jazzy/setup.bash && \
		colcon build --packages-select basicmicro_ros2 roboclaw_tcp_adapter --symlink-install"

.PHONY: host-shell
host-shell:
	bash -c "source $(HOST_WS)/install/setup.bash && exec bash"

.PHONY: robot-start
robot-start:
	docker compose up -d

.PHONY: robot-stop
robot-stop:
	docker compose down

.PHONY: robot-logs
robot-logs:
	docker compose logs -f

.PHONY: robot-logs-roboclaw
robot-logs-roboclaw:
	docker compose logs -f roboclaw

.PHONY: robot-shell
robot-shell:
	docker compose exec ros2-shell bash

.PHONY: robot-ps
robot-ps:
	docker compose ps

# Foxglove bridge image (compose has build; Portainer repo-deploy does not build → run once on host)
.PHONY: foxglove-build
foxglove-build:
	docker compose build foxglove

.PHONY: portainer-start
portainer-start:
	docker compose --profile management up -d portainer

.PHONY: portainer-stop
portainer-stop:
	docker compose --profile management stop portainer

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
	@echo "  make host-install-deps    - Install basicmicro_python + rosdep"
	@echo "  make host-build           - colcon build host_ws (needs native ROS2)"
	@echo "  make host-build-docker    - colcon build host_ws in ros:jazzy (no native ROS2)"
	@echo "  make host-shell           - Shell with host_ws sourced"
	@echo "  make robot-start         - Start all robot containers (compose up)"
	@echo "  make robot-stop          - Stop all robot containers (compose down)"
	@echo "  make robot-logs          - Follow all container logs"
	@echo "  make robot-logs-roboclaw - Follow roboclaw logs only"
	@echo "  make robot-shell         - Open ROS2 shell (exec into container)"
	@echo "  make robot-ps            - Show container status"
	@echo "  make foxglove-build      - Build Foxglove bridge image (once; then stack can start it)"
	@echo "  make portainer-start    - Start Portainer UI (https://localhost:9443)"
	@echo "  make portainer-stop      - Stop Portainer"
	@echo ""
