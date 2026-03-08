#!/usr/bin/env bash
# =============================================================================
# start-foxglove.sh
# =============================================================================
# Starts the foxglove_bridge WebSocket server so Foxglove Studio can connect
# to the live ROS2 graph.
#
# Usage:
#   ./tools/start-foxglove.sh          # start bridge (agent must be running)
#   ./tools/start-foxglove.sh --stop   # stop bridge container
#
# First run builds the Docker image (~1 min).
#
# Foxglove Studio:
#   1. Install: https://foxglove.dev/download
#   2. Open Connection → Foxglove WebSocket → ws://localhost:8765
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
CONTAINER_NAME="w6100_foxglove_bridge"
IMAGE="w6100-foxglove:latest"
WS_PORT=8765

# ── Stop mode ─────────────────────────────────────────────────────────────────

if [ "$1" = "--stop" ]; then
    echo "[foxglove] Stopping bridge..."
    docker stop "$CONTAINER_NAME" >/dev/null 2>&1 && echo "[foxglove] Stopped." || echo "[foxglove] Not running."
    exit 0
fi

# ── Build image if needed ─────────────────────────────────────────────────────

if ! docker image inspect "$IMAGE" &>/dev/null; then
    echo "[foxglove] Building Docker image (first run, ~1 min)..."
    if ! docker build -t "$IMAGE" -f "$PROJECT_DIR/docker/Dockerfile.foxglove" "$PROJECT_DIR/docker/"; then
        echo "[foxglove] ERROR: Docker image build failed."
        exit 1
    fi
fi

# ── Skip if already running ───────────────────────────────────────────────────

if docker ps -q --filter "name=^${CONTAINER_NAME}$" | grep -q .; then
    echo "[foxglove] Bridge already running — nothing to do."
    echo "  URL: ws://localhost:${WS_PORT}"
    echo "  To restart: $0 --stop && $0"
    exit 0
fi

# ── Clean up stopped container with same name ────────────────────────────────

docker rm "$CONTAINER_NAME" 2>/dev/null || true

# ── Start bridge ──────────────────────────────────────────────────────────────

echo "[foxglove] Starting foxglove_bridge on ws://localhost:${WS_PORT} ..."

docker run -d --rm \
    --name "$CONTAINER_NAME" \
    --net=host \
    -v "$SCRIPT_DIR/cyclonedds.xml":/tmp/cyclonedds.xml:ro \
    -e CYCLONEDDS_URI=file:///tmp/cyclonedds.xml \
    -e ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-0}" \
    "$IMAGE"

echo ""
echo "[foxglove] Bridge running."
echo ""
echo "  Open Foxglove Studio → Open Connection → Foxglove WebSocket"
echo "  URL: ws://localhost:${WS_PORT}"
echo ""
echo "  To stop: ./tools/start-foxglove.sh --stop"
echo "           or: docker stop $CONTAINER_NAME"
