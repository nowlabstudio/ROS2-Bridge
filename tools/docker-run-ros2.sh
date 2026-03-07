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
#   - Agent must be running first (docker-run-agent-udp.sh)
#   - Pico must be connected and LED on (agent connected)
#
# Inside the container — quick reference:
#
#   ros2 node list
#   ros2 topic list
#
#   # Test channels
#   ros2 topic echo /pico/counter std_msgs/msg/Int32
#   ros2 topic echo /pico/heartbeat std_msgs/msg/Bool
#   ros2 topic pub  /pico/echo_in std_msgs/msg/Int32 "{data: 42}"
#   ros2 topic echo /pico/echo_out std_msgs/msg/Int32
#
#   # Services
#   ros2 service call /bridge/relay_brake std_srvs/srv/SetBool "{data: true}"
#   ros2 service call /bridge/relay_brake std_srvs/srv/SetBool "{data: false}"
#   ros2 service call /bridge/estop       std_srvs/srv/Trigger  "{}"
#
#   # Parameter server
#   ros2 param list   /pico_bridge
#   ros2 param get    /pico_bridge ch.test_counter.period_ms
#   ros2 param set    /pico_bridge ch.test_counter.period_ms 200
#   ros2 param set    /pico_bridge ch.test_heartbeat.enabled false
#   ros2 param dump   /pico_bridge
#
#   # Diagnostics
#   ros2 topic echo /diagnostics diagnostic_msgs/msg/DiagnosticArray
#
#   # Topic rate check
#   ros2 topic hz /pico/counter
#   ros2 topic hz /pico/heartbeat
# =============================================================================

set -euo pipefail

CONTAINER_NAME="w6100_bridge_ros2"
IMAGE="ros:jazzy"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Stop previous instance if still running
if docker ps -q --filter "name=$CONTAINER_NAME" | grep -q .; then
    echo "Stopping previous ros2 container..."
    docker stop "$CONTAINER_NAME" >/dev/null 2>&1 || true
fi

echo "============================================="
echo " ROS2 Jazzy - W6100 Bridge Test Shell"
echo "============================================="
echo ""
echo " Node:   /pico_bridge"
echo " Topics: /pico/counter  /pico/heartbeat"
echo "         /pico/echo_in  /pico/echo_out"
echo "         /diagnostics"
echo ""
echo " Services: /bridge/relay_brake  (SetBool)"
echo "           /bridge/estop        (Trigger)"
echo ""
echo " Type 'help' after sourcing for ROS2 commands."
echo "============================================="
echo ""

docker run -it --rm --init \
    --name "$CONTAINER_NAME" \
    --net=host \
    -v "$SCRIPT_DIR":/tools:ro \
    -v "$SCRIPT_DIR/cyclonedds.xml":/tmp/cyclonedds.xml:ro \
    -e CYCLONEDDS_URI=file:///tmp/cyclonedds.xml \
    "$IMAGE" \
    bash -c "source /opt/ros/jazzy/setup.bash && exec bash"

: <<'KOMMENT'
docker run -it --rm --init \
    --name "$CONTAINER_NAME" \
    --net=host \
    -v "$SCRIPT_DIR":/tools:ro \
    "$IMAGE" \
    bash -c "source /opt/ros/jazzy/setup.bash && exec bash"
