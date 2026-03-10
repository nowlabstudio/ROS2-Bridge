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

# Build C++ roboclaw_hardware package in Docker (needs ros2-control deps)
.PHONY: host-build-roboclaw-hw
host-build-roboclaw-hw:
	docker run --rm \
		-v $(PROJECT_DIR)/host_ws:/host_ws -w /host_ws \
		ros:jazzy \
		bash -c " \
		apt-get update -qq && \
		apt-get install -y -qq --no-install-recommends \
			ros-jazzy-ros2-control ros-jazzy-ros2-controllers \
			ros-jazzy-xacro ros-jazzy-robot-state-publisher >/dev/null 2>&1 && \
		source /opt/ros/jazzy/setup.bash && \
		colcon build --packages-select roboclaw_hardware --cmake-args -DCMAKE_BUILD_TYPE=Release"

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
	docker compose exec ros2-shell bash -c "source /opt/ros/jazzy/setup.bash && [ -f /host_ws/install/setup.bash ] && source /host_ws/install/setup.bash; exec bash"

# Echo /robot/diagnostics from inside roboclaw container (proves publish works if messages appear)
.PHONY: robot-diagnostics-echo
robot-diagnostics-echo:
	docker compose exec roboclaw bash -c "source /opt/ros/jazzy/setup.bash && source /host_ws/install/setup.bash && timeout 8 ros2 topic echo /robot/diagnostics diagnostic_msgs/msg/DiagnosticArray"

# Motor test: publish cmd_vel for 3s (E-Stop must be released!)
# Usage: make robot-motor-test [LINEAR=0.05] [DURATION=3]
LINEAR ?= 0.05
DURATION ?= 3
.PHONY: robot-motor-test
robot-motor-test:
	docker compose exec ros2-shell bash -c "source /opt/ros/jazzy/setup.bash && source /host_ws/install/setup.bash && echo 'Publishing cmd_vel linear.x=$(LINEAR) for $(DURATION)s...' && timeout $(DURATION) ros2 topic pub /robot/cmd_vel geometry_msgs/msg/Twist '{linear: {x: $(LINEAR), y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.0}}' --rate 10" || true

# M1 DutyM1 (PWM), 50%%, 3s — standalone mintájára (50% = 16384)
.PHONY: robot-motor-test-m1
robot-motor-test-m1:
	docker compose exec ros2-shell bash -c "source /opt/ros/jazzy/setup.bash && source /host_ws/install/setup.bash && echo 'M1 DutyM1 50%% 3s...' && ros2 topic pub /robot/motor_duty_m1 std_msgs/msg/Float32 '{data: 0.5}' --once && sleep 3 && ros2 topic pub /robot/motor_duty_m1 std_msgs/msg/Float32 '{data: 0.0}' --once" || true

# Standalone teszt (roboclaw STOPPED): közvetlen TCP, DutyM1 majd DutyM2
.PHONY: robot-motor-test-standalone
robot-motor-test-standalone:
	docker compose stop roboclaw && \
	docker compose run --rm --no-deps -v $$(pwd):/workspace:ro \
		-e ROBOCLAW_HOST=$${ROBOCLAW_HOST:-192.168.68.60} \
		-e ROBOCLAW_PORT=$${ROBOCLAW_PORT:-8234} \
		roboclaw bash -c "apt-get update -qq && apt-get install -y -qq python3-serial >/dev/null && PYTHONPATH=/workspace/host_ws/src/roboclaw_tcp_adapter:/workspace/host_ws/src/basicmicro_python python3 /workspace/tools/test_motor_duty.py" ; \
	docker compose start roboclaw

# M2 DutyM2 ROS2-ból: 5 Hz keepalive (watchdog?), 50%%, M2_DURATION s
.PHONY: robot-motor-test-m2
M2_DURATION ?= 10
robot-motor-test-m2:
	docker compose exec ros2-shell bash -c "source /opt/ros/jazzy/setup.bash && source /host_ws/install/setup.bash && echo 'M2 DutyM2 50%% $(M2_DURATION)s @5Hz...' && (timeout $(M2_DURATION) ros2 topic pub /robot/motor_duty_m2 std_msgs/msg/Float32 '{data: 0.5}' --rate 5 & sleep $$(($(M2_DURATION) + 1)) && ros2 topic pub /robot/motor_duty_m2 std_msgs/msg/Float32 '{data: 0.0}' --once)" || true

# Csak M2 standalone (roboclaw stop) — ha ROS2 path nem működik
.PHONY: robot-motor-test-standalone-m2
M2_DURATION ?= 10
robot-motor-test-standalone-m2:
	docker compose stop roboclaw && \
	docker compose run --rm --no-deps -v $$(pwd):/workspace:ro \
		-e ROBOCLAW_HOST=$${ROBOCLAW_HOST:-192.168.68.60} \
		-e ROBOCLAW_PORT=$${ROBOCLAW_PORT:-8234} \
		-e M2_DURATION=$(M2_DURATION) \
		roboclaw bash -c "apt-get update -qq && apt-get install -y -qq python3-serial >/dev/null && PYTHONPATH=/workspace/host_ws/src/roboclaw_tcp_adapter:/workspace/host_ws/src/basicmicro_python python3 /workspace/tools/test_motor_duty.py m2" ; \
	docker compose start roboclaw

# C++ ros2_control driver (default — started by robot-start)
.PHONY: robot-hw-start
robot-hw-start:
	docker compose up -d roboclaw-hw

.PHONY: robot-hw-stop
robot-hw-stop:
	docker compose stop roboclaw-hw

.PHONY: robot-hw-logs
robot-hw-logs:
	docker compose logs -f roboclaw-hw

# Motor test via ros2_control diff_drive_controller cmd_vel (TwistStamped for Jazzy)
.PHONY: robot-hw-motor-test
robot-hw-motor-test:
	docker compose exec roboclaw-hw bash -c "source /opt/ros/jazzy/setup.bash && echo 'Publishing cmd_vel linear.x=$(LINEAR) for $(DURATION)s...' && timeout $(DURATION) ros2 topic pub /diff_drive_controller/cmd_vel geometry_msgs/msg/TwistStamped '{header: {stamp: {sec: 0, nanosec: 0}, frame_id: \"\"}, twist: {linear: {x: $(LINEAR), y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.0}}}' --rate 10" || true

# Legacy Python driver (use only if needed)
.PHONY: robot-legacy-start
robot-legacy-start:
	docker compose --profile legacy up -d roboclaw

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
	@echo "  make robot-diagnostics-echo - Echo /robot/diagnostics from roboclaw container (8s)"
	@echo "  make robot-motor-test    - Publish cmd_vel forward 3s (LINEAR=0.05, DURATION=3)"
	@echo "  make robot-motor-test-m1 - M1 DutyM1 100%% 3s"
	@echo "  make robot-motor-test-m2 - M2 DutyM2 50%% (M2_DURATION=10 pl. 10s)"
	@echo "  make robot-motor-test-standalone - Közvetlen TCP (M1+M2)"
	@echo "  make robot-motor-test-standalone-m2 - Csak M2, 50%%, 3s"
	@echo "  make robot-ps            - Show container status"
	@echo ""
	@echo "  ── C++ ros2_control Driver ──"
	@echo "  make host-build-roboclaw-hw - Build C++ roboclaw_hardware in Docker"
	@echo "  make robot-hw-start      - Start C++ driver (stops Python driver)"
	@echo "  make robot-hw-stop       - Stop C++ driver"
	@echo "  make robot-hw-logs       - Follow C++ driver logs"
	@echo "  make robot-hw-motor-test - cmd_vel test via diff_drive_controller"
	@echo ""
	@echo "  ── Management ──"
	@echo "  make foxglove-build      - Build Foxglove bridge image (once; then stack can start it)"
	@echo "  make portainer-start    - Start Portainer UI (https://localhost:9443)"
	@echo "  make portainer-stop      - Stop Portainer"
	@echo ""
