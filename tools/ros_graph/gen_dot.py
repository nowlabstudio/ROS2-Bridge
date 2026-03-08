#!/usr/bin/env python3
"""
gen_dot.py — Generate rosgraph.dot from the live ROS2 graph.
Uses 'ros2 node list' + 'ros2 node info' for reliable parsing.

Run inside the ROS2 Docker container (ros:jazzy --net=host).

Usage:
    python3 gen_dot.py [output_path]
"""

import sys
import subprocess
import time
import os

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "rosgraph.dot")

IGNORE_TOPICS = {"/rosout", "/parameter_events", "/tf", "/tf_static", "/clock"}
IGNORE_NODE_PREFIXES = ("/_ros2cli", "/rqt")


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.stdout.strip()


def get_nodes():
    out = run(["ros2", "node", "list"])
    nodes = [n for n in out.splitlines() if n.strip()]
    return [n for n in nodes if not any(n.startswith(p) for p in IGNORE_NODE_PREFIXES)]


def get_node_info(node):
    """Return (publishers, subscribers) — list of topic names."""
    out = run(["ros2", "node", "info", node])
    publishers = []
    subscribers = []
    mode = None
    for line in out.splitlines():
        stripped = line.strip()
        if stripped == "Publishers:":
            mode = "pub"
        elif stripped == "Subscribers:":
            mode = "sub"
        elif stripped in ("Service Servers:", "Service Clients:",
                          "Action Servers:", "Action Clients:"):
            mode = None
        elif mode and stripped and ":" in stripped:
            topic = stripped.split(":")[0].strip()
            if topic and topic not in IGNORE_TOPICS:
                if mode == "pub":
                    publishers.append(topic)
                elif mode == "sub":
                    subscribers.append(topic)
    return publishers, subscribers


def q(s):
    return '"' + s.replace('"', '\\"') + '"'


def topic_id(topic):
    """Unique dot ID for a topic node — avoids collision when topic name == ROS node name."""
    return '"__topic__' + topic.replace('"', '\\"') + '"'


def wait_for_nodes(retries=20, delay=2):
    for i in range(1, retries + 1):
        nodes = get_nodes()
        if nodes:
            return nodes
        print(f"[gen_dot] Waiting for nodes... {i}/{retries}", flush=True)
        time.sleep(delay)
    return []


def main():
    print("[gen_dot] Waiting for DDS discovery...", flush=True)
    nodes = wait_for_nodes()

    if not nodes:
        print("[gen_dot] ERROR: no nodes found after waiting.")
        print("          Check: agent running? boards connected (LED on)?")
        sys.exit(1)

    print(f"[gen_dot] Found {len(nodes)} node(s): {nodes}", flush=True)

    # Collect all edges: node → topic, topic → subscriber_node
    pub_edges = []   # (node, topic)
    sub_edges = []   # (topic, node)
    all_topics = set()

    for node in nodes:
        pubs, subs = get_node_info(node)
        for t in pubs:
            pub_edges.append((node, t))
            all_topics.add(t)
        for t in subs:
            sub_edges.append((t, node))
            all_topics.add(t)

    print(f"[gen_dot] {len(all_topics)} active topic(s)", flush=True)

    with open(OUT, "w") as f:
        f.write("digraph  {\n")

        # ROS nodes — ellipse
        for node in nodes:
            f.write(f"  {q(node)} [label={q(node)}, shape=ellipse];\n")

        # Topics — box, with unique dot ID to avoid collision with node names
        for topic in all_topics:
            f.write(f"  {topic_id(topic)} [label={q(topic)}, shape=box];\n")

        # Edges use topic_id() consistently
        for node, topic in pub_edges:
            f.write(f"  {q(node)} -> {topic_id(topic)};\n")

        for topic, node in sub_edges:
            f.write(f"  {topic_id(topic)} -> {q(node)};\n")

        f.write("}\n")

    print(f"[gen_dot] Written: {OUT}", flush=True)


if __name__ == "__main__":
    main()
