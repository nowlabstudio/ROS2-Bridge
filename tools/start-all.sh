#!/usr/bin/env bash
# =============================================================================
# start-all.sh
# =============================================================================
# Launches the full W6100 bridge environment with Foxglove visualization:
#
#   1. micro-ROS Agent  (UDP, port 8888)   — gnome-terminal window
#   2. Foxglove Bridge  (WS,  port 8765)   — background container
#   3. ROS2 Jazzy shell                    — gnome-terminal window
#   4. Foxglove Studio  (native app)       — if installed
#
# Each step waits for the previous one to be ready before proceeding.
# Idempotent: re-running skips services that are already up.
#
# Usage:
#   ./tools/start-all.sh            # start everything
#   ./tools/start-all.sh --stop     # stop all containers
#
# Requirements:
#   - Ubuntu with GNOME Terminal
#   - Docker installed and running
#   - Foxglove Studio (optional): sudo snap install foxglove-studio
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
AGENT_CONTAINER="w6100_bridge_agent_udp"
FOXGLOVE_CONTAINER="w6100_foxglove_bridge"
ROS2_CONTAINER="w6100_bridge_ros2"
WAIT_TIMEOUT=30

log() { echo "[start-all] $*"; }

is_running() { docker ps -q --filter "name=^${1}$" 2>/dev/null | grep -q .; }

wait_for_container() {
    local name="$1"
    local label="$2"
    for _ in $(seq 1 "$WAIT_TIMEOUT"); do
        if is_running "$name"; then
            log "$label ready."
            return 0
        fi
        sleep 1
    done
    log "ERROR: $label did not start within ${WAIT_TIMEOUT}s."
    return 1
}

wait_for_port() {
    local port="$1"
    local label="$2"
    for _ in $(seq 1 "$WAIT_TIMEOUT"); do
        if ss -tlnp 2>/dev/null | grep -q ":${port} "; then
            log "$label ready (port $port)."
            return 0
        fi
        sleep 1
    done
    log "ERROR: $label port $port not listening within ${WAIT_TIMEOUT}s."
    return 1
}

# ── Stop mode ────────────────────────────────────────────────────────────────

if [ "${1:-}" = "--stop" ]; then
    log "Stopping all containers..."
    docker stop "$FOXGLOVE_CONTAINER" "$AGENT_CONTAINER" "$ROS2_CONTAINER" 2>/dev/null || true
    log "Done."
    exit 0
fi

# ── 1. micro-ROS Agent ───────────────────────────────────────────────────────

if is_running "$AGENT_CONTAINER"; then
    log "[1/4] Agent already running — skipping."
else
    log "[1/4] Starting micro-ROS Agent (UDP)..."
    gnome-terminal --title="micro-ROS Agent (Jazzy UDP)" -- bash -c \
        "bash '$SCRIPT_DIR/docker-run-agent-udp.sh'; exec bash"
    wait_for_container "$AGENT_CONTAINER" "Agent"
fi

# ── 2. Foxglove Bridge ───────────────────────────────────────────────────────

if is_running "$FOXGLOVE_CONTAINER"; then
    log "[2/4] Foxglove Bridge already running — skipping."
else
    log "[2/4] Starting Foxglove Bridge..."
    bash "$SCRIPT_DIR/start-foxglove.sh"
    wait_for_port 8765 "Foxglove Bridge"
fi

# ── 3. ROS2 Jazzy Shell ──────────────────────────────────────────────────────

if is_running "$ROS2_CONTAINER"; then
    log "[3/4] ROS2 shell already running — skipping."
else
    log "[3/4] Opening ROS2 shell..."
    gnome-terminal --title="ROS2 Jazzy - W6100 Bridge" -- bash -c \
        "bash '$SCRIPT_DIR/docker-run-ros2.sh'; exec bash"
    log "ROS2 shell opened."
fi

# ── 4. Foxglove Studio ───────────────────────────────────────────────────────

if pgrep -f "foxglove-studio" >/dev/null 2>&1; then
    log "[4/4] Foxglove Studio already running — skipping."
elif command -v foxglove-studio &>/dev/null; then
    log "[4/4] Launching Foxglove Studio..."
    nohup foxglove-studio </dev/null >/dev/null 2>&1 &
    STUDIO_PID=$!
    sleep 3
    if kill -0 "$STUDIO_PID" 2>/dev/null; then
        log "Foxglove Studio running (PID $STUDIO_PID)."
    else
        log "WARNING: Foxglove Studio exited. Try launching manually:"
        log "         foxglove-studio"
    fi
else
    log "[4/4] Foxglove Studio not installed."
    log "      Install:  sudo snap install foxglove-studio"
    log "      Or open:  https://studio.foxglove.dev"
    log "      Connect:  ws://localhost:8765 (Foxglove WebSocket)"
fi

# ── Summary ──────────────────────────────────────────────────────────────────

echo ""
echo "══════════════════════════════════════════════"
echo "  W6100 Bridge — All services running"
echo ""
echo "  Agent:    UDP :8888"
echo "  Bridge:   ws://localhost:8765"
echo "  ROS2:     interactive shell"
echo "  Studio:   Foxglove WebSocket → ws://localhost:8765"
echo ""
echo "  Stop all: ./tools/start-all.sh --stop"
echo "══════════════════════════════════════════════"
