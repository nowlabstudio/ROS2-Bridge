#!/usr/bin/env bash
# =============================================================================
# start-eth.sh
# =============================================================================
# Launches the full W6100 bridge test environment:
#   Window 1 — micro-ROS Jazzy agent (UDP, port 8888)
#   Window 2 — ROS2 Jazzy interactive test shell
#
# Usage:
#   ./start-eth.sh
#
# Requirements:
#   - Ubuntu with GNOME Terminal
#   - Docker installed and running
#   - Pico boards connected via Ethernet
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Window 1: micro-ROS Agent (UDP)
gnome-terminal --title="micro-ROS Agent (Jazzy UDP)" -- bash -c "\
    bash '$SCRIPT_DIR/docker-run-agent-udp.sh'; \
    exec bash"

# Wait until agent container is actually running (max 30s)
echo "Waiting for agent container to start..."
for i in $(seq 1 30); do
    if docker ps -q --filter "name=w6100_bridge_agent_udp" | grep -q .; then
        echo "Agent running."
        break
    fi
    sleep 1
done

# Window 2: ROS2 Jazzy Test Shell
gnome-terminal --title="ROS2 Jazzy - W6100 Bridge" -- bash -c "\
    bash '$SCRIPT_DIR/docker-run-ros2.sh'; \
    exec bash"

echo ""
echo "Started:"
echo "  [1] micro-ROS Agent  — UDP port 8888"
echo "  [2] ROS2 Jazzy shell — /robot namespace"
echo ""
echo "Pico LED on = agent connected."
