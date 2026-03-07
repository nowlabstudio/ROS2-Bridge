#!/usr/bin/env bash
# =============================================================================
# docker-run-agent-udp.sh
# =============================================================================
# Starts the micro-ROS Jazzy agent in a Docker container (UDP transport).
#
# Usage:
#   ./docker-run-agent-udp.sh          # default port 8888
#   ./docker-run-agent-udp.sh 9999     # custom port
#
# Requires: Docker with --net=host (Linux only)
# =============================================================================

PORT="${1:-8888}"
CONTAINER_NAME="w6100_bridge_agent_udp"
IMAGE="microros/micro-ros-agent:jazzy"

# Stop previous instance if still running
if docker ps -q --filter "name=$CONTAINER_NAME" | grep -q .; then
    echo "Stopping previous agent container..."
    docker stop "$CONTAINER_NAME" >/dev/null 2>&1 || true
fi

echo "============================================="
echo " micro-ROS Agent (Jazzy, UDP Mode)"
echo "============================================="
echo " Port:      $PORT (UDP)"
echo " Container: $CONTAINER_NAME"
echo " Image:     $IMAGE"
echo ""
echo " Pico IP:   192.168.68.114"
echo " Agent IP:  192.168.68.125 (this host)"
echo ""
echo " Press Ctrl+C to stop."
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
docker run -it --rm \
    --name "$CONTAINER_NAME" \
    --net=host \
    "$IMAGE" \
    udp4 -p "$PORT" -v6
