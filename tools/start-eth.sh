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
#   - Ubuntu 22.04 with GNOME Terminal
#   - Docker installed and running
#   - Pico connected via Ethernet, agent IP: 192.168.68.125
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Window 1: micro-ROS Agent (UDP)
gnome-terminal --title="micro-ROS Agent (Jazzy UDP)" -- bash -c "\
    bash $SCRIPT_DIR/docker-run-agent-udp.sh; \
    exec bash"

echo "Waiting for agent to start..."
sleep 2

# Window 2: ROS2 Jazzy Test Shell
gnome-terminal --title="ROS2 Jazzy - W6100 Bridge" -- bash -c "\
    bash $SCRIPT_DIR/docker-run-ros2.sh; \
    exec bash"

echo ""
echo "Started:"
echo "  [1] micro-ROS Agent  — UDP port 8888"
echo "  [2] ROS2 Jazzy shell — /pico_bridge"
echo ""
echo "Pico LED on = agent connected."
