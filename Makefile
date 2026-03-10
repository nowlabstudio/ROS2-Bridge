BOARD       := w5500_evb_pico
DOCKER_IMG  := w6100-zephyr-microros:latest
FLASH_PORT  := /dev/tty.usbmodem231401

PROJECT_DIR := $(shell pwd)
WORKSPACE   := $(PROJECT_DIR)/workspace
APP_DIR     := $(PROJECT_DIR)/app
HOST_WS     := $(PROJECT_DIR)/host_ws

# ==============================================================
# Zephyr Firmware (Tier 1 — Pico MCU)
# ==============================================================

.PHONY: docker-build
docker-build:
	docker build -t $(DOCKER_IMG) docker/

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

.PHONY: build
build:
	docker run --rm \
		-v $(WORKSPACE):/workdir \
		-v $(APP_DIR):/workdir/app \
		$(DOCKER_IMG) bash -c "\
			cd /workdir && \
			west build -b $(BOARD) app --pristine=always"

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

.PHONY: monitor
monitor:
	screen $(FLASH_PORT) 115200

.PHONY: shell
shell:
	docker run --rm -it \
		-v $(WORKSPACE):/workdir \
		-v $(APP_DIR):/workdir/app \
		$(DOCKER_IMG) bash

.PHONY: clean
clean:
	rm -rf $(WORKSPACE)/build

# ==============================================================
# Host Workspace Build (Tier 2 — ROS2 packages)
# ==============================================================

# Build C++ roboclaw_hardware + Python rc_teleop (roboclaw_tcp_adapter) in Docker
.PHONY: host-build
host-build:
	docker run --rm \
		-v $(PROJECT_DIR)/host_ws:/host_ws -w /host_ws \
		ros:jazzy \
		bash -c " \
		apt-get update -qq && \
		apt-get install -y -qq --no-install-recommends \
			ros-jazzy-ros2-control ros-jazzy-ros2-controllers \
			ros-jazzy-xacro ros-jazzy-robot-state-publisher >/dev/null 2>&1 && \
		source /opt/ros/jazzy/setup.bash && \
		colcon build --packages-select roboclaw_hardware roboclaw_tcp_adapter \
			--cmake-args -DCMAKE_BUILD_TYPE=Release"

.PHONY: host-shell
host-shell:
	bash -c "source $(HOST_WS)/install/setup.bash && exec bash"

# ==============================================================
# Robot Stack (docker compose)
# ==============================================================

.PHONY: robot-start
robot-start:
	docker compose up -d

.PHONY: robot-stop
robot-stop:
	docker compose down

.PHONY: robot-restart
robot-restart:
	docker compose down && docker compose up -d

.PHONY: robot-ps
robot-ps:
	docker compose ps

.PHONY: robot-logs
robot-logs:
	docker compose logs -f

.PHONY: robot-logs-roboclaw
robot-logs-roboclaw:
	docker compose logs -f roboclaw

.PHONY: robot-shell
robot-shell:
	docker compose exec ros2-shell bash -c "source /opt/ros/jazzy/setup.bash && [ -f /host_ws/install/setup.bash ] && source /host_ws/install/setup.bash; exec bash"

# ==============================================================
# Diagnostics & Testing
# ==============================================================

# Echo GPIO diagnostics (battery, temp, error, current, encoder health)
.PHONY: robot-diagnostics
robot-diagnostics:
	docker compose exec roboclaw bash -c "source /opt/ros/jazzy/setup.bash && source /host_ws/install/setup.bash && timeout 8 ros2 topic echo /dynamic_joint_states --once"

# Motor test: publish cmd_vel for DURATION seconds (TwistStamped for diff_drive_controller)
# Usage: make robot-motor-test [LINEAR=0.05] [DURATION=3]
LINEAR ?= 0.05
DURATION ?= 3
.PHONY: robot-motor-test
robot-motor-test:
	docker compose exec roboclaw bash -c "source /opt/ros/jazzy/setup.bash && source /host_ws/install/setup.bash && echo 'Publishing cmd_vel linear.x=$(LINEAR) for $(DURATION)s...' && timeout $(DURATION) ros2 topic pub /diff_drive_controller/cmd_vel geometry_msgs/msg/TwistStamped '{header: {stamp: {sec: 0, nanosec: 0}, frame_id: \"\"}, twist: {linear: {x: $(LINEAR), y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.0}}}' --rate 10" || true

# List all ROS2 topics
.PHONY: robot-topics
robot-topics:
	docker compose exec roboclaw bash -c "source /opt/ros/jazzy/setup.bash && source /host_ws/install/setup.bash && ros2 topic list"

# List controllers and their states
.PHONY: robot-controllers
robot-controllers:
	docker compose exec roboclaw bash -c "source /opt/ros/jazzy/setup.bash && source /host_ws/install/setup.bash && ros2 control list_controllers"

# ==============================================================
# Foxglove & Portainer
# ==============================================================

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
# Help
# ==============================================================

.PHONY: help
help:
	@echo ""
	@echo "  ── Zephyr Firmware (Tier 1 — Pico MCU) ──"
	@echo "  make docker-build    - Build Docker image (once)"
	@echo "  make workspace-init  - Download Zephyr workspace (~2GB, once)"
	@echo "  make build           - Build firmware"
	@echo "  make flash           - Flash via OpenOCD"
	@echo "  make flash-uf2       - Show UF2 file location"
	@echo "  make monitor         - Serial monitor 115200 baud"
	@echo "  make shell           - Open Docker shell"
	@echo "  make clean           - Remove build artifacts"
	@echo ""
	@echo "  ── Host Workspace (Tier 2 — ROS2) ──"
	@echo "  make host-build          - Build roboclaw_hardware + rc_teleop in Docker"
	@echo "  make host-shell          - Shell with host_ws sourced"
	@echo ""
	@echo "  ── Robot Stack ──"
	@echo "  make robot-start         - Start all containers (compose up)"
	@echo "  make robot-stop          - Stop all containers (compose down)"
	@echo "  make robot-restart       - Restart all containers"
	@echo "  make robot-ps            - Show container status"
	@echo "  make robot-logs          - Follow all container logs"
	@echo "  make robot-logs-roboclaw - Follow RoboClaw driver logs"
	@echo "  make robot-shell         - Open ROS2 shell in container"
	@echo ""
	@echo "  ── Diagnostics & Testing ──"
	@echo "  make robot-diagnostics   - Echo GPIO diagnostics (battery, temp, etc.)"
	@echo "  make robot-motor-test    - cmd_vel test (LINEAR=0.05, DURATION=3)"
	@echo "  make robot-topics        - List all ROS2 topics"
	@echo "  make robot-controllers   - List ros2_control controllers"
	@echo ""
	@echo "  ── Management ──"
	@echo "  make foxglove-build      - Build Foxglove bridge image (once)"
	@echo "  make portainer-start     - Start Portainer UI (https://localhost:9443)"
	@echo "  make portainer-stop      - Stop Portainer"
	@echo ""
