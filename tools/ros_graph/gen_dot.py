#!/usr/bin/env python3
"""
gen_dot.py — Generate rosgraph.dot from the live ROS2 graph.

Runs inside the ROS2 Docker container (ros:jazzy with --net=host).
Output: rosgraph.dot in the same directory as this script.

Usage:
    python3 gen_dot.py [output_path]
"""

import sys
import subprocess
import re
import os

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "rosgraph.dot")


def run(cmd):
    result = subprocess.run(cmd, capture_output=True, text=True)
    return result.stdout.strip()


def get_nodes():
    out = run(["ros2", "node", "list"])
    return [n for n in out.splitlines() if n.strip()]


def get_topics():
    out = run(["ros2", "topic", "list"])
    return [t for t in out.splitlines() if t.strip()]


def get_topic_info(topic):
    """Return (publishers, subscribers) as lists of full node paths."""
    out = run(["ros2", "topic", "info", "-v", topic])
    publishers = []
    subscribers = []

    mode = None
    node_name = ""
    node_ns = ""

    for line in out.splitlines():
        line = line.strip()
        if line.startswith("Publisher count:") or "Publishers:" in line:
            mode = "pub"
        elif line.startswith("Subscription count:") or "Subscribers:" in line:
            mode = "sub"
        elif line.startswith("Node name:"):
            node_name = line.split(":", 1)[1].strip()
        elif line.startswith("Node namespace:"):
            node_ns = line.split(":", 1)[1].strip()
            if node_ns == "/":
                full = "/" + node_name
            else:
                full = node_ns + "/" + node_name
            if mode == "pub" and full not in publishers:
                publishers.append(full)
            elif mode == "sub" and full not in subscribers:
                subscribers.append(full)

    return publishers, subscribers


def sanitize(name):
    """Make a dot-safe identifier from a ROS name."""
    return '"' + name.replace('"', '\\"') + '"'


def wait_for_nodes(retries=10, delay=2):
    """Retry until at least one node appears (DDS discovery takes a few seconds)."""
    for attempt in range(1, retries + 1):
        nodes = get_nodes()
        if nodes:
            return nodes
        print(f"[gen_dot] Waiting for nodes... ({attempt}/{retries})")
        import time
        time.sleep(delay)
    return []


def main():
    print("[gen_dot] Querying ROS2 graph (waiting for DDS discovery)...")
    nodes = wait_for_nodes(retries=15, delay=2)

    if not nodes:
        print("[gen_dot] ERROR: No nodes found after waiting.")
        print("          Is the agent running? Are boards connected (green LED on)?")
        sys.exit(1)

    topics = get_topics()
    print(f"[gen_dot] Found {len(nodes)} node(s), {len(topics)} topic(s)")

    edges = []   # (publisher_node, topic, subscriber_node)
    topic_nodes = set()
    graph_nodes = set(nodes)

    for topic in topics:
        pubs, subs = get_topic_info(topic)
        for pub in pubs:
            for sub in subs:
                edges.append((pub, topic, sub))
                topic_nodes.add(topic)

    # Write dot file (rqt_graph compatible format)
    with open(OUT, "w") as f:
        f.write("digraph  {\n")

        # Node definitions
        for node in graph_nodes:
            label = node.split("/")[-1] if "/" in node else node
            f.write(f"  {sanitize(node)} [label={sanitize(node)}, shape=ellipse];\n")

        # Topic nodes
        for topic in topic_nodes:
            f.write(f"  {sanitize(topic)} [label={sanitize(topic)}, shape=box];\n")

        # Edges: publisher → topic → subscriber
        for pub, topic, sub in edges:
            f.write(f"  {sanitize(pub)} -> {sanitize(topic)};\n")
            f.write(f"  {sanitize(topic)} -> {sanitize(sub)};\n")

        f.write("}\n")

    print(f"[gen_dot] Written: {OUT}")
    print(f"[gen_dot] {len(graph_nodes)} nodes, {len(topic_nodes)} active topics, {len(edges)} connections")


if __name__ == "__main__":
    main()
