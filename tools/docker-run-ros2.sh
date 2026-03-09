#!/usr/bin/env bash
# =============================================================================
# docker-run-ros2.sh
# =============================================================================
# Opens an interactive ROS2 Jazzy shell for testing the W6100 bridge.
#
# Usage:
#   ./docker-run-ros2.sh
#
# Prerequisites:
#   - host_ws built in Docker: make host-build-docker
#   - Agent must be running first (docker-run-agent-udp.sh)
#   - Pico must be connected and LED on (agent connected)
#
# Inside the container — quick reference:
#
#   ros2 node list
#   ros2 topic list
#
#   # E-Stop board (/robot namespace)
#   ros2 topic echo /robot/estop std_msgs/msg/Bool
#
#   # RC board (/robot namespace)
#   ros2 topic echo /robot/motor_left  std_msgs/msg/Float32
#   ros2 topic echo /robot/motor_right std_msgs/msg/Float32
#   ros2 topic echo /robot/rc_mode     std_msgs/msg/Float32
#   ros2 topic echo /robot/winch       std_msgs/msg/Float32
#
#   # Test channels (if enabled in config)
#   ros2 topic echo /robot/estop/counter    std_msgs/msg/Int32
#   ros2 topic echo /robot/estop/heartbeat  std_msgs/msg/Bool
#
#   # Diagnostics
#   ros2 topic echo /diagnostics diagnostic_msgs/msg/DiagnosticArray
#
#   # Topic rate
#   ros2 topic hz /robot/motor_left
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
HOST_WS="$PROJECT_DIR/host_ws"
CONTAINER_NAME="w6100_bridge_ros2"
IMAGE="ros:jazzy"

# Skip if already running
if docker ps -q --filter "name=^${CONTAINER_NAME}$" | grep -q .; then
    echo "ROS2 shell already running in container '$CONTAINER_NAME'."
    echo "To restart: docker stop $CONTAINER_NAME && $0"
    exit 0
fi

# Clean up stopped container with same name
docker rm "$CONTAINER_NAME" 2>/dev/null || true

echo "============================================="
echo " ROS2 Jazzy - W6100 Bridge Test Shell"
echo "============================================="
echo ""
echo " Nodes:  /robot/estop   /robot/rc   /robot/pedal"
echo " Topics: /robot/estop"
echo "         /robot/motor_left  /robot/motor_right"
echo "         /robot/rc_mode     /robot/winch"
echo "         /diagnostics"
echo ""
echo " Press Ctrl+C or type 'exit' to quit."
echo "============================================="
echo ""

docker run -it --rm --init \
    --name "$CONTAINER_NAME" \
    --net=host \
    -v "$HOST_WS":/host_ws:rw \
    -v "$SCRIPT_DIR/cyclonedds.xml":/tmp/cyclonedds.xml:ro \
    -e CYCLONEDDS_URI=file:///tmp/cyclonedds.xml \
    "$IMAGE" \
    bash -c "apt-get update -qq && apt-get install -y -qq --no-install-recommends python3-serial >/dev/null 2>&1; export PYTHONPATH=/host_ws/src/basicmicro_ros2:/host_ws/src/basicmicro_python:\${PYTHONPATH:-}; source /opt/ros/jazzy/setup.bash && [ -f /host_ws/install/setup.bash ] && source /host_ws/install/setup.bash; exec bash"
