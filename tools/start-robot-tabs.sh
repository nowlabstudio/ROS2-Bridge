#!/usr/bin/env bash
# =============================================================================
# start-robot-tabs.sh — Launch the full robot system in one terminal (3 tabs)
# =============================================================================
#
# Opens one GNOME Terminal window with three tabs:
#   [1] agent      — micro-ROS Agent (Docker, UDP)
#   [2] roboclaw   — RoboClaw TCP driver + safety bridge (ROS2 Docker)
#   [3] ros2-shell — Interactive ROS2 Jazzy shell (Docker)
#
# Usage:
#   ./tools/start-robot-tabs.sh
#   make robot-start
#
# Stop everything: make robot-stop  or  ./tools/stop-robot.sh
#
# Network parameters: host_ws/config/robot_network.yaml
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
HOST_WS="$PROJECT_DIR/host_ws"

# ── Read network config ─────────────────────────────────────────────────────
AGENT_PORT=$(grep -A2 'micro_ros_agent:' "$HOST_WS/config/robot_network.yaml" 2>/dev/null | grep 'port:' | awk '{print $2}')
AGENT_PORT="${AGENT_PORT:-8888}"

ROBOCLAW_HOST=$(grep -A5 'roboclaw:' "$HOST_WS/config/robot_network.yaml" 2>/dev/null | grep 'host:' | awk '{print $2}')
ROBOCLAW_HOST="${ROBOCLAW_HOST:-192.168.68.60}"

ROBOCLAW_PORT=$(grep -A5 'roboclaw:' "$HOST_WS/config/robot_network.yaml" 2>/dev/null | grep 'port:' | head -1 | awk '{print $2}')
ROBOCLAW_PORT="${ROBOCLAW_PORT:-8234}"

# Wait for agent container (for tabs 2 and 3)
WAIT_AGENT='for i in $(seq 1 30); do docker ps -q --filter name=w6100_bridge_agent_udp 2>/dev/null | grep -q . && break; sleep 1; done'

echo "============================================="
echo " Robot System — terminal tabs"
echo "============================================="
echo ""
echo " [1] micro-ROS Agent   UDP :${AGENT_PORT}"
echo " [2] RoboClaw driver   TCP ${ROBOCLAW_HOST}:${ROBOCLAW_PORT}"
echo " [3] ROS2 Jazzy shell"
echo ""
echo " Stop:  make robot-stop   or   ./tools/stop-robot.sh"
echo "============================================="

gnome-terminal --window \
    --tab --title="agent"      -- bash -c "bash '$SCRIPT_DIR/docker-run-agent-udp.sh' $AGENT_PORT; exec bash" \
    --tab --title="roboclaw"   -- bash -c "$WAIT_AGENT; bash '$SCRIPT_DIR/docker-run-roboclaw.sh' $ROBOCLAW_HOST $ROBOCLAW_PORT; exec bash" \
    --tab --title="ros2-shell" -- bash -c "$WAIT_AGENT; bash '$SCRIPT_DIR/docker-run-ros2.sh'; exec bash"
