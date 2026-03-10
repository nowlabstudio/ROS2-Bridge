"""
RC Teleop Node — Arcade-style RC-to-cmd_vel translator.

Subscribes to two Float32 channels from the W6100 Pico (throttle + steering),
applies deadzone filtering and optional PWM normalization, then publishes
standard geometry_msgs/Twist on cmd_vel for the motor driver.

A 20 Hz safety timer ensures continuous publishing even if RC input stops
(publishes zero velocity in that case).  E-Stop gating forces zero output
when the hardware E-Stop is active.
"""

import rclpy
from rclpy.node import Node
from std_msgs.msg import Float32, Bool
from geometry_msgs.msg import Twist


class RCTeleopNode(Node):

    def __init__(self):
        super().__init__('rc_teleop')

        self.declare_parameter('max_linear_speed', 1.0)
        self.declare_parameter('max_angular_speed', 1.0)
        self.declare_parameter('deadzone', 0.05)
        self.declare_parameter('is_pwm_input', False)
        self.declare_parameter('throttle_topic', 'rc_ch2')
        self.declare_parameter('steering_topic', 'rc_ch1')
        self.declare_parameter('cmd_vel_topic', 'cmd_vel')
        self.declare_parameter('estop_topic', 'estop')
        self.declare_parameter('publish_rate', 20.0)

        self.max_lin = self.get_parameter('max_linear_speed').value
        self.max_ang = self.get_parameter('max_angular_speed').value
        self.deadzone = self.get_parameter('deadzone').value
        self.is_pwm = self.get_parameter('is_pwm_input').value
        publish_rate = self.get_parameter('publish_rate').value

        self.throttle = 0.0
        self.steering = 0.0
        self.estop_active = True  # safe default until first E-Stop message

        throttle_topic = self.get_parameter('throttle_topic').value
        steering_topic = self.get_parameter('steering_topic').value
        cmd_vel_topic = self.get_parameter('cmd_vel_topic').value
        estop_topic = self.get_parameter('estop_topic').value

        self.sub_throttle = self.create_subscription(
            Float32, throttle_topic, self._throttle_cb, 10)
        self.sub_steering = self.create_subscription(
            Float32, steering_topic, self._steering_cb, 10)
        self.sub_estop = self.create_subscription(
            Bool, estop_topic, self._estop_cb, 10)

        self.pub_cmd_vel = self.create_publisher(Twist, cmd_vel_topic, 10)

        self.timer = self.create_timer(1.0 / publish_rate, self._publish_twist)

        self.get_logger().info(
            "RC Teleop started: %s + %s -> %s (E-Stop: %s, rate: %.0f Hz)"
            % (throttle_topic, steering_topic, cmd_vel_topic,
               estop_topic, publish_rate))

    def _normalize(self, raw_value: float) -> float:
        if self.is_pwm:
            norm = (raw_value - 1500.0) / 500.0
        else:
            norm = raw_value

        norm = max(-1.0, min(1.0, norm))

        if abs(norm) < self.deadzone:
            return 0.0

        return norm

    def _throttle_cb(self, msg: Float32):
        self.throttle = self._normalize(msg.data)

    def _steering_cb(self, msg: Float32):
        self.steering = self._normalize(msg.data)

    def _estop_cb(self, msg: Bool):
        self.estop_active = msg.data

    def _publish_twist(self):
        twist = Twist()
        if not self.estop_active:
            twist.linear.x = self.throttle * self.max_lin
            twist.angular.z = self.steering * self.max_ang
        self.pub_cmd_vel.publish(twist)


def main(args=None):
    rclpy.init(args=args)
    node = RCTeleopNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.pub_cmd_vel.publish(Twist())
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == '__main__':
    main()
