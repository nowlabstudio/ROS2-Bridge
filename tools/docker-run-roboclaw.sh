#!/usr/bin/env bash
# =============================================================================
# docker-run-roboclaw.sh — Run RoboClaw TCP driver + safety bridge in ROS2 Docker
# =============================================================================
# Launches roboclaw_tcp_adapter (driver + safety_bridge) inside a ros:jazzy
# container with host_ws mounted. Use from start-robot.sh or standalone.
#
# Usage:
#   ./docker-run-roboclaw.sh [ROBOCLAW_HOST] [ROBOCLAW_PORT]
#   Defaults: 192.168.68.60 8234 (or set via env ROBOCLAW_HOST, ROBOCLAW_PORT)
#
# Prerequisites:
#   make host-build-docker   # build host_ws inside Docker
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
HOST_WS="$PROJECT_DIR/host_ws"
CONTAINER_NAME="w6100_bridge_roboclaw"
IMAGE="ros:jazzy"

ROBOCLAW_HOST="${1:-${ROBOCLAW_HOST:-192.168.68.60}}"
ROBOCLAW_PORT="${2:-${ROBOCLAW_PORT:-8234}}"

if docker ps -q --filter "name=^${CONTAINER_NAME}$" | grep -q .; then
    echo "RoboClaw container '$CONTAINER_NAME' already running."
    echo "To restart: docker stop $CONTAINER_NAME && $0 $ROBOCLAW_HOST $ROBOCLAW_PORT"
    exit 0
fi

docker rm "$CONTAINER_NAME" 2>/dev/null || true

if [ ! -f "$HOST_WS/install/setup.bash" ]; then
    echo "Error: host_ws not built. Run: make host-build-docker"
    exit 1
fi

echo "============================================="
echo " RoboClaw TCP adapter (ROS2 Docker)"
echo "============================================="
echo " Host: $ROBOCLAW_HOST  Port: $ROBOCLAW_PORT"
echo "============================================="

docker run -it --rm --init \
    --name "$CONTAINER_NAME" \
    --net=host \
    -v "$HOST_WS":/host_ws:rw \
    -v "$SCRIPT_DIR/cyclonedds.xml":/tmp/cyclonedds.xml:ro \
    -e CYCLONEDDS_URI=file:///tmp/cyclonedds.xml \
    -e ROBOCLAW_HOST="$ROBOCLAW_HOST" \
    -e ROBOCLAW_PORT="$ROBOCLAW_PORT" \
    "$IMAGE" \
    bash -c "apt-get update -qq && apt-get install -y -qq --no-install-recommends python3-serial >/dev/null 2>&1; \
             export PYTHONPATH=/host_ws/src/basicmicro_ros2:/host_ws/src/basicmicro_python:\${PYTHONPATH:-}; \
             source /opt/ros/jazzy/setup.bash && source /host_ws/install/setup.bash && \
             ros2 launch roboclaw_tcp_adapter roboclaw.launch.py \
               roboclaw_host:=\$ROBOCLAW_HOST roboclaw_port:=\$ROBOCLAW_PORT; \
             exec bash"
