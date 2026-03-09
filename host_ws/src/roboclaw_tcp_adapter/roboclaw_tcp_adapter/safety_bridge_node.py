"""
Safety Bridge Node — closes the E-Stop safety loop between the Pico bridge
and the RoboClaw motor controller.

Two independent triggers command an emergency stop:

1. Active E-Stop: /robot/estop publishes True  (physical button pressed)
2. Silence watchdog: /robot/estop topic has been silent for >2 seconds
   (Pico bridge crashed, network cable disconnected, agent died)

Motor stop mechanism:
  While E-Stop is active, this node publishes zero-velocity Twist messages
  on cmd_vel at 10 Hz, overriding any navigation commands.  This uses the
  driver's actual command interface rather than the undocumented
  /emergency_stop topic which the upstream basicmicro_ros2 driver does
  not subscribe to.

  Additionally, std_msgs/Empty is published on /emergency_stop for forward
  compatibility (when/if the upstream driver adds support for it).

Safety layers (defense in depth):
  1. This node: continuous zero cmd_vel while E-Stop active
  2. This node: watchdog triggers on topic silence
  3. RoboClaw hardware: serial timeout stops motors if no valid commands
"""

import rclpy
from rclpy.node import Node
from std_msgs.msg import Bool, Empty
from geometry_msgs.msg import Twist


WATCHDOG_TIMEOUT_SEC = 2.0
ESTOP_CMD_VEL_RATE_SEC = 0.1


class SafetyBridgeNode(Node):
    def __init__(self):
        super().__init__("safety_bridge")

        self.declare_parameter("estop_topic", "estop")
        self.declare_parameter("cmd_vel_topic", "cmd_vel")
        self.declare_parameter("emergency_stop_topic", "/emergency_stop")
        self.declare_parameter("watchdog_timeout_sec", WATCHDOG_TIMEOUT_SEC)
        self.declare_parameter("estop_cmd_vel_rate_sec", ESTOP_CMD_VEL_RATE_SEC)

        estop_topic = self.get_parameter("estop_topic").value
        cmd_vel_topic = self.get_parameter("cmd_vel_topic").value
        estop_out = self.get_parameter("emergency_stop_topic").value
        wd_timeout = self.get_parameter("watchdog_timeout_sec").value
        cmd_rate = self.get_parameter("estop_cmd_vel_rate_sec").value

        self._emergency_pub = self.create_publisher(Empty, estop_out, 10)
        self._cmd_vel_pub = self.create_publisher(Twist, cmd_vel_topic, 10)

        self._estop_sub = self.create_subscription(
            Bool, estop_topic, self._estop_callback, 10
        )

        self._watchdog_timer = self.create_timer(wd_timeout, self._watchdog_expired)
        self._zero_vel_timer = None
        self._zero_vel_rate = cmd_rate
        self._estop_active = False
        self._topic_alive = False
        self._zero_twist = Twist()

        self.get_logger().info(
            "Safety bridge: %s -> zero %s + %s (watchdog %.1fs)",
            estop_topic,
            cmd_vel_topic,
            estop_out,
            wd_timeout,
        )

    def _estop_callback(self, msg: Bool):
        self._topic_alive = True
        self._watchdog_timer.reset()

        if msg.data and not self._estop_active:
            self._activate_estop("E-Stop ACTIVE (physical button)")
        elif not msg.data and self._estop_active:
            self._deactivate_estop()

    def _watchdog_expired(self):
        if not self._topic_alive:
            self._activate_estop(
                "E-Stop topic SILENT for >%.1fs — assuming failure"
                % self.get_parameter("watchdog_timeout_sec").value
            )
        self._topic_alive = False

    def _activate_estop(self, reason: str):
        self._estop_active = True
        self.get_logger().warn("EMERGENCY STOP: %s", reason)
        self._emergency_pub.publish(Empty())
        self._cmd_vel_pub.publish(self._zero_twist)

        if self._zero_vel_timer is None:
            self._zero_vel_timer = self.create_timer(
                self._zero_vel_rate, self._publish_zero_vel
            )

    def _deactivate_estop(self):
        self._estop_active = False
        if self._zero_vel_timer is not None:
            self._zero_vel_timer.cancel()
            self.destroy_timer(self._zero_vel_timer)
            self._zero_vel_timer = None
        self.get_logger().info("E-Stop released — cmd_vel override stopped")

    def _publish_zero_vel(self):
        self._cmd_vel_pub.publish(self._zero_twist)


def main(args=None):
    rclpy.init(args=args)
    node = SafetyBridgeNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.try_shutdown()


if __name__ == "__main__":
    main()
