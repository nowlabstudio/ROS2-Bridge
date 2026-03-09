#!/usr/bin/env bash
# =============================================================================
# stop-robot.sh — Stop all robot-related Docker containers and tmux session
# =============================================================================
#
# Stops:
#   - w6100_bridge_agent_udp  (micro-ROS Agent)
#   - w6100_bridge_roboclaw   (RoboClaw TCP adapter)
#   - w6100_bridge_ros2       (ROS2 shell)
#   - tmux session "robot"    (if was started with start-robot.sh)
#
# Usage:
#   ./tools/stop-robot.sh
#   make robot-stop
# =============================================================================

set -euo pipefail

CONTAINERS="w6100_bridge_agent_udp w6100_bridge_roboclaw w6100_bridge_ros2"
STOPPED=""

for c in $CONTAINERS; do
    if docker ps -q --filter "name=^${c}$" 2>/dev/null | grep -q .; then
        docker stop "$c" 2>/dev/null && STOPPED="$STOPPED $c"
    fi
done

tmux kill-session -t robot 2>/dev/null && STOPPED="${STOPPED} tmux:robot" || true

if [ -n "$STOPPED" ]; then
    echo "Stopped:$STOPPED"
else
    echo "No robot containers or tmux session were running."
fi
