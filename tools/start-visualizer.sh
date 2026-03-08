#!/usr/bin/env bash
# =============================================================================
# start-visualizer.sh
# =============================================================================
# Launches Dear RosNodeViewer — visual ROS2 node graph inspector.
#   https://github.com/iwatake2222/dear_ros_node_viewer
#
# Usage:
#   ./tools/start-visualizer.sh               # live graph (agent + nodes must be running)
#   ./tools/start-visualizer.sh graph.dot     # from rqt_graph dot export
#   ./tools/start-visualizer.sh arch.yaml     # from CARET architecture.yaml
#
# Requirements:
#   - Ubuntu with ROS2 Jazzy on the host (/opt/ros/jazzy/setup.bash)
#   - micro-ROS agent running  (./tools/start-eth.sh)
#   - Pico boards connected    (LED on)
#
# First run installs: graphviz, dear-ros-node-viewer (pip3, no sudo needed)
#
# To export the graph manually as a dot file (inside docker-run-ros2.sh):
#   ros2 run rqt_graph rqt_graph
#   # File → Export → rosgraph.dot
#   # Then: ./tools/start-visualizer.sh rosgraph.dot
# =============================================================================

set -e

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

# ── ROS2 ─────────────────────────────────────────────────────────────────────

if [ -f "$ROS_SETUP" ]; then
    # shellcheck disable=SC1090
    source "$ROS_SETUP"
else
    echo "[visualizer] WARNING: $ROS_SETUP not found."
    echo "             Live graph mode requires ROS2 Jazzy installed on the host."
    echo "             Alternatively, pass a .dot or .yaml file as argument."
fi

# ── Launch ───────────────────────────────────────────────────────────────────

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
    dear_ros_node_viewer "$1"
else
    echo " Mode:   live ROS graph"
    echo ""
    echo " Prerequisites:"
    echo "   [1] micro-ROS agent running   ./tools/start-eth.sh"
    echo "   [2] Pico boards connected     (green LED on)"
    echo ""
    echo " Controls:"
    echo "   Middle-button drag — move graph"
    echo "   Mouse scroll       — zoom"
    echo "============================================="
    echo ""
    dear_ros_node_viewer
fi
