#!/usr/bin/env bash
# =============================================================================
# start-visualizer.sh
# =============================================================================
# Launches the W6100 bridge agent + Dear RosNodeViewer.
#
# Flow:
#   1. Start micro-ROS agent (new terminal window)
#   2. Wait for agent + at least one ROS node to appear
#   3. Generate rosgraph.dot from the live ROS2 graph (via Docker)
#   4. Launch Dear RosNodeViewer with the generated dot file
#
# Usage:
#   ./tools/start-visualizer.sh            # auto: generate + open
#   ./tools/start-visualizer.sh --skip-agent  # skip agent start (already running)
#
# Requirements:
#   - Ubuntu with GNOME Terminal
#   - Docker installed and running
#   - dear-ros-node-viewer installed on host (auto-installed on first run)
#   - graphviz installed on host (auto-installed on first run)
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
GRAPH_DIR="$SCRIPT_DIR/ros_graph"
DOT_FILE="$GRAPH_DIR/rosgraph.dot"
CONTAINER="w6100_bridge_ros2_graphgen"
IMAGE="ros:jazzy"

# --refresh: skip agent start, just regenerate dot and reopen viewer
SKIP_AGENT=false
for arg in "$@"; do
    [[ "$arg" == "--skip-agent" || "$arg" == "--refresh" ]] && SKIP_AGENT=true
done

# ── Dependencies (host) ───────────────────────────────────────────────────────

if ! dpkg -s graphviz &>/dev/null 2>&1; then
    echo "[visualizer] Installing graphviz..."
    sudo apt-get install -y graphviz graphviz-dev
fi

if ! python3 -c "import dear_ros_node_viewer" &>/dev/null 2>&1; then
    echo "[visualizer] Installing dear-ros-node-viewer..."
    pip3 install dear-ros-node-viewer
fi

# ── Step 1: Start agent ───────────────────────────────────────────────────────

if [[ "$SKIP_AGENT" == "false" ]]; then
    gnome-terminal --title="micro-ROS Agent (Jazzy UDP)" -- bash -c \
        "bash '$SCRIPT_DIR/docker-run-agent-udp.sh'; exec bash"

    echo "[visualizer] Waiting for agent container..."
    for i in $(seq 1 30); do
        if docker ps -q --filter "name=w6100_bridge_agent_udp" | grep -q .; then
            echo "[visualizer] Agent running."
            break
        fi
        sleep 1
    done
fi

# ── Step 2: (node discovery handled inside gen_dot.py with retries) ──────────
# DDS discovery needs a few seconds inside the container — gen_dot.py retries.

# ── Step 3: Generate rosgraph.dot ─────────────────────────────────────────────

echo "[visualizer] Generating rosgraph.dot..."

# Stop any leftover graphgen container
docker rm -f "$CONTAINER" &>/dev/null || true

docker run --rm \
    --name "$CONTAINER" \
    --net=host \
    -e ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-0}" \
    -v "$GRAPH_DIR":/ros_graph \
    "$IMAGE" \
    bash -c "
        source /opt/ros/jazzy/setup.bash 2>/dev/null
        python3 /ros_graph/gen_dot.py /ros_graph/rosgraph.dot
    "

if [ ! -f "$DOT_FILE" ]; then
    echo "[visualizer] ERROR: rosgraph.dot was not generated."
    echo "             Is the agent running? Are boards connected (LED on)?"
    exit 1
fi

# ── Step 4: Launch Dear RosNodeViewer ─────────────────────────────────────────

echo ""
echo "============================================="
echo " Dear RosNodeViewer — W6100 Bridge"
echo "============================================="
echo " File:     $DOT_FILE"
echo " Settings: $GRAPH_DIR/setting.json"
echo ""
echo " Controls:"
echo "   Middle-button drag — move graph"
echo "   Mouse scroll       — zoom"
echo "   Click node title   — highlight connections"
echo ""
echo " Refresh graph after boards reconnect:"
echo "   ./tools/start-visualizer.sh --refresh"
echo "============================================="
echo ""

# Run from the graph dir so setting.json is found automatically
cd "$GRAPH_DIR"
dear_ros_node_viewer rosgraph.dot
