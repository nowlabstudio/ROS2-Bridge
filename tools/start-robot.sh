#!/usr/bin/env bash
# =============================================================================
# start-robot.sh — Launch the full robot system in a tmux session
# =============================================================================
#
# Creates a tmux session "robot" with three windows:
#   [0] agent         — micro-ROS Agent (Docker, UDP)
#   [1] roboclaw      — RoboClaw TCP driver + safety bridge (ROS2 launch)
#   [2] ros2-shell    — Interactive ROS2 Jazzy shell
#
# Usage:
#   ./tools/start-robot.sh          # start everything
#   tmux attach -t robot            # re-attach later
#   tmux kill-session -t robot      # stop everything
#
# Unlike start-eth.sh (gnome-terminal), this works headless over SSH.
#
# Network parameters are read from host_ws/config/robot_network.yaml.
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
HOST_WS="$PROJECT_DIR/host_ws"
SESSION="robot"

# ── Read network config (simple grep, no Python dependency) ──────────────────
AGENT_PORT=$(grep -A2 'micro_ros_agent:' "$HOST_WS/config/robot_network.yaml" \
    | grep 'port:' | awk '{print $2}')
AGENT_PORT="${AGENT_PORT:-8888}"

ROBOCLAW_HOST=$(grep -A5 'roboclaw:' "$HOST_WS/config/robot_network.yaml" \
    | grep 'host:' | awk '{print $2}')
ROBOCLAW_HOST="${ROBOCLAW_HOST:-192.168.68.60}"

ROBOCLAW_PORT=$(grep -A5 'roboclaw:' "$HOST_WS/config/robot_network.yaml" \
    | grep 'port:' | head -1 | awk '{print $2}')
ROBOCLAW_PORT="${ROBOCLAW_PORT:-8234}"

# ── Kill existing session if running ─────────────────────────────────────────
tmux kill-session -t "$SESSION" 2>/dev/null || true

echo "============================================="
echo " Robot System — tmux session: $SESSION"
echo "============================================="
echo ""
echo " [0] micro-ROS Agent   UDP :${AGENT_PORT}"
echo " [1] RoboClaw driver   TCP ${ROBOCLAW_HOST}:${ROBOCLAW_PORT}"
echo " [2] ROS2 Jazzy shell"
echo ""
echo " Attach:  tmux attach -t $SESSION"
echo " Stop:    tmux kill-session -t $SESSION"
echo "============================================="

# ── Window 0: micro-ROS Agent ────────────────────────────────────────────────
tmux new-session -d -s "$SESSION" -n "agent" \
    "bash '$SCRIPT_DIR/docker-run-agent-udp.sh' $AGENT_PORT; exec bash"

# Wait for agent container
for i in $(seq 1 30); do
    if docker ps -q --filter "name=w6100_bridge_agent_udp" 2>/dev/null | grep -q .; then
        echo "Agent container running."
        break
    fi
    sleep 1
done

# ── Window 1: RoboClaw driver + safety bridge ────────────────────────────────
tmux new-window -t "$SESSION" -n "roboclaw" \
    "source '$HOST_WS/install/setup.bash' 2>/dev/null; \
     ros2 launch roboclaw_tcp_adapter roboclaw.launch.py \
       roboclaw_host:=$ROBOCLAW_HOST \
       roboclaw_port:=$ROBOCLAW_PORT; \
     exec bash"

# ── Window 2: ROS2 interactive shell ─────────────────────────────────────────
tmux new-window -t "$SESSION" -n "ros2-shell" \
    "bash '$SCRIPT_DIR/docker-run-ros2.sh'; exec bash"

# Attach to session
tmux select-window -t "$SESSION":0
tmux attach-session -t "$SESSION"
