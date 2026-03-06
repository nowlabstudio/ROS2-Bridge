#!/usr/bin/env python3
"""
E-Stop monitor — feliratkozik a robot/estop topicra és logol minden állapotváltozást.

Használat:
    python3 examples/estop_monitor/estop_monitor.py

Előfeltétel: ROS 2 (Jazzy) source-olva, micro-ROS agent fut.
"""

import rclpy
from rclpy.node import Node
from std_msgs.msg import Bool


ESTOP_TOPIC = "robot/estop"


class EStopMonitor(Node):
    def __init__(self):
        super().__init__("estop_monitor")
        self._last_state = None

        self.create_subscription(Bool, ESTOP_TOPIC, self._cb, 10)
        self.get_logger().info(f"Subscribed to {ESTOP_TOPIC} — waiting for messages...")

    def _cb(self, msg: Bool):
        state = msg.data

        # Csak állapotváltáskor logol (ne árasszuk el a terminált)
        if state != self._last_state:
            if state:
                self.get_logger().error("*** E-STOP ACTIVE ***  (robot/estop = true)")
            else:
                self.get_logger().info("E-Stop cleared — system OK  (robot/estop = false)")
            self._last_state = state


def main():
    rclpy.init()
    node = EStopMonitor()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
