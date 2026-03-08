#!/usr/bin/env bash
# =============================================================================
# docker-run-agent-udp.sh
# =============================================================================
# Starts the micro-ROS Jazzy agent in a Docker container (UDP transport).
#
# Usage:
#   ./tools/docker-run-agent-udp.sh          # default port 8888
#   ./tools/docker-run-agent-udp.sh 9999     # custom port
#
# Requires: Ubuntu/Linux (--net=host not supported on macOS Docker Desktop)
# =============================================================================

PORT="${1:-8888}"
CONTAINER_NAME="w6100_bridge_agent_udp"
IMAGE="microros/micro-ros-agent:jazzy"

# Skip if already running
if docker ps -q --filter "name=^${CONTAINER_NAME}$" | grep -q .; then
    echo "Agent already running in container '$CONTAINER_NAME'."
    echo "To restart: docker stop $CONTAINER_NAME && $0"
    exit 0
fi

# Clean up stopped container with same name
docker rm "$CONTAINER_NAME" 2>/dev/null || true

echo "============================================="
echo " micro-ROS Agent (Jazzy, UDP Mode)"
echo "============================================="
echo " Port:      $PORT (UDP)"
echo " Container: $CONTAINER_NAME"
echo " Image:     $IMAGE"
echo ""
echo " Agent IP:  $(hostname -I | awk '{print $1}')"
echo ""
echo " Press Ctrl+C to stop."
echo "============================================="
echo ""

docker run -it --rm \
    --name "$CONTAINER_NAME" \
    --net=host \
    "$IMAGE" \
    udp4 -p "$PORT" -v6
