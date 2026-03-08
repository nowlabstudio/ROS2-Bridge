#!/usr/bin/env bash
# =============================================================================
# start-visualizer.sh
# =============================================================================
# Launches the full W6100 bridge environment + Dear RosNodeViewer:
#   Window 1 — micro-ROS Jazzy agent (UDP, port 8888)
#   Window 2 — Dear RosNodeViewer (live ROS2 node graph)
#
# Usage:
#   ./tools/start-visualizer.sh               # agent + live graph
#   ./tools/start-visualizer.sh graph.dot     # agent + static dot file
#   ./tools/start-visualizer.sh arch.yaml     # agent + static yaml file
#
# Requirements:
#   - Ubuntu with GNOME Terminal
#   - Docker installed and running
#   - ROS2 Jazzy on the host (/opt/ros/jazzy/setup.bash)
#     for live graph mode; not needed for static file mode
#
# To export a static dot file (inside docker-run-ros2.sh container):
#   ros2 run rqt_graph rqt_graph   → File → Export → rosgraph.dot
#   ./tools/start-visualizer.sh rosgraph.dot
#
# First run installs: graphviz, dear-ros-node-viewer (pip3)
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROS_SETUP="/opt/ros/jazzy/setup.bash"

# ── Dependencies ─────────────────────────────────────────────────────────────

if ! dpkg -s graphviz &>/dev/null 2>&1; then
    echo "[visualizer] Installing graphviz..."
    sudo apt-get install -y graphviz
fi

if ! python3 -c "import dear_ros_node_viewer" &>/dev/null 2>&1; then
    echo "[visualizer] Installing dear-ros-node-viewer..."
    pip3 install dear-ros-node-viewer
fi

# ── Window 1: micro-ROS Agent ─────────────────────────────────────────────────

gnome-terminal --title="micro-ROS Agent (Jazzy UDP)" -- bash -c "\
    bash '$SCRIPT_DIR/docker-run-agent-udp.sh'; \
    exec bash"

echo "[visualizer] Waiting for agent container to start..."
for i in $(seq 1 30); do
    if docker ps -q --filter "name=w6100_bridge_agent_udp" | grep -q .; then
        echo "[visualizer] Agent running."
        break
    fi
    sleep 1
done

# ── Window 2: Dear RosNodeViewer ─────────────────────────────────────────────

echo "============================================="
echo " Dear RosNodeViewer — W6100 Bridge"
echo "============================================="

if [ -n "$1" ]; then
    if [ ! -f "$1" ]; then
        echo "ERROR: File not found: $1"
        exit 1
    fi
    echo " Mode:   static file — $1"
    echo "============================================="
    echo ""
    if [ -f "$ROS_SETUP" ]; then source "$ROS_SETUP"; fi
    dear_ros_node_viewer "$1"
else
    echo " Mode:   live ROS graph"
    echo ""
    echo " Prerequisites:"
    echo "   [1] Pico boards powered and connected via Ethernet"
    echo "   [2] Green LED on = agent connected"
    echo ""
    echo " Controls:"
    echo "   Middle-button drag — move graph"
    echo "   Mouse scroll       — zoom"
    echo "============================================="
    echo ""
    if [ ! -f "$ROS_SETUP" ]; then
        echo "WARNING: $ROS_SETUP not found — live mode requires ROS2 Jazzy on the host."
        echo "Pass a .dot or .yaml file as argument for static mode."
        exit 1
    fi
    source "$ROS_SETUP"
    dear_ros_node_viewer
fi
