#!/usr/bin/env bash
# =============================================================================
# start-visualizer.sh
# =============================================================================
# Launches the W6100 bridge agent + Dear RosNodeViewer (runs in Docker).
#
# Usage:
#   ./tools/start-visualizer.sh            # start agent + generate graph + open viewer
#   ./tools/start-visualizer.sh --refresh  # skip agent start, regenerate + reopen
#
# First run builds the visualizer Docker image (~1 min).
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
GRAPH_DIR="$SCRIPT_DIR/ros_graph"
DOT_FILE="$GRAPH_DIR/rosgraph.dot"
VIZ_IMAGE="w6100-visualizer:latest"

# ── Parse args ────────────────────────────────────────────────────────────────

REFRESH=false
if [ "$1" = "--refresh" ]; then
    REFRESH=true
fi

# ── Build visualizer image if needed ─────────────────────────────────────────

if ! docker image inspect "$VIZ_IMAGE" &>/dev/null; then
    echo "[visualizer] Building Docker image (first run, ~1 min)..."
    if ! docker build -t "$VIZ_IMAGE" -f "$PROJECT_DIR/docker/Dockerfile.visualizer" "$PROJECT_DIR/docker/"; then
        echo "[visualizer] ERROR: Docker image build failed."
        exit 1
    fi
fi

# ── Step 1: Start agent (skip if --refresh) ───────────────────────────────────

if [ "$REFRESH" = "false" ]; then
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
else
    echo "[visualizer] --refresh: skipping agent start."
fi

# ── Step 2 + 3: Generate dot + open viewer in one container ──────────────────

echo "[visualizer] Generating graph and opening viewer..."

xhost +local:docker &>/dev/null || true

docker run --rm \
    --net=host \
    -e DISPLAY="$DISPLAY" \
    -v /tmp/.X11-unix:/tmp/.X11-unix \
    -v "$GRAPH_DIR":/ros_graph \
    -v "$SCRIPT_DIR/cyclonedds.xml":/tmp/cyclonedds.xml:ro \
    -e CYCLONEDDS_URI=file:///tmp/cyclonedds.xml \
    -e ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-0}" \
    -e LIBGL_ALWAYS_SOFTWARE=1 \
    -e MESA_GL_VERSION_OVERRIDE=3.3 \
    "$VIZ_IMAGE" \
    bash -c "
        source /opt/ros/jazzy/setup.bash
        echo '[visualizer] Generating rosgraph.dot...'
        python3 /ros_graph/gen_dot.py /ros_graph/rosgraph.dot || exit 1
        echo '[visualizer] Opening Dear RosNodeViewer...'
        cd /ros_graph
        dear_ros_node_viewer --display-unconnected-nodes rosgraph.dot
    "
