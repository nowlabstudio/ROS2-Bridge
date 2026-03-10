"""
RC Teleop Node — Tank/Arcade RC-to-cmd_vel translator with ROS/RC mode switch.

Supports two mixing modes:
  - tank:   CH2 = left motor, CH1 = right motor  (tank→arcade conversion)
  - arcade: CH2 = throttle,   CH1 = steering      (direct mapping)

CH5 provides an instant ROS/RC mode switch:
  - CH5 > mode_switch_threshold  → RC mode  (rc_teleop publishes cmd_vel)
  - CH5 ≤ mode_switch_threshold  → ROS mode (rc_teleop stops, Nav2 takes over)

Switching to RC mode is instantaneous and serves as a safety override.

A configurable-rate safety timer ensures continuous publishing in RC mode
even if RC input stops (publishes zero velocity). E-Stop gating forces
zero output regardless of mode.

Output: geometry_msgs/TwistStamped (required by diff_drive_controller in Jazzy).
"""

import rclpy
from rclpy.node import Node
from std_msgs.msg import Float32, Bool
from geometry_msgs.msg import TwistStamped


class RCTeleopNode(Node):

    def __init__(self):
        super().__init__('rc_teleop')

        self.declare_parameter('mixing_mode', 'tank')
        self.declare_parameter('max_linear_speed', 1.0)
        self.declare_parameter('max_angular_speed', 1.0)
        self.declare_parameter('deadzone', 0.05)
        self.declare_parameter('is_pwm_input', False)
        self.declare_parameter('left_topic', '/robot/rc_ch2')
        self.declare_parameter('right_topic', '/robot/rc_ch1')
        self.declare_parameter('throttle_topic', '/robot/rc_ch2')
        self.declare_parameter('steering_topic', '/robot/rc_ch1')
        self.declare_parameter('mode_switch_topic', '/robot/rc_ch5')
        self.declare_parameter('mode_switch_threshold', 0.5)
        self.declare_parameter('cmd_vel_topic', '/diff_drive_controller/cmd_vel')
        self.declare_parameter('estop_topic', '/robot/estop')
        self.declare_parameter('publish_rate', 20.0)

        self.mixing_mode = self.get_parameter('mixing_mode').value
        self.max_lin = self.get_parameter('max_linear_speed').value
        self.max_ang = self.get_parameter('max_angular_speed').value
        self.deadzone = self.get_parameter('deadzone').value
        self.is_pwm = self.get_parameter('is_pwm_input').value
        self.mode_switch_threshold = self.get_parameter('mode_switch_threshold').value
        publish_rate = self.get_parameter('publish_rate').value

        self.ch_a = 0.0
        self.ch_b = 0.0
        self.rc_mode = False
        self.estop_active = True

        cmd_vel_topic = self.get_parameter('cmd_vel_topic').value
        estop_topic = self.get_parameter('estop_topic').value
        mode_switch_topic = self.get_parameter('mode_switch_topic').value

        if self.mixing_mode == 'tank':
            left_topic = self.get_parameter('left_topic').value
            right_topic = self.get_parameter('right_topic').value
            self.sub_a = self.create_subscription(
                Float32, left_topic, self._ch_a_cb, 10)
            self.sub_b = self.create_subscription(
                Float32, right_topic, self._ch_b_cb, 10)
            ch_a_label, ch_b_label = left_topic, right_topic
        else:
            throttle_topic = self.get_parameter('throttle_topic').value
            steering_topic = self.get_parameter('steering_topic').value
            self.sub_a = self.create_subscription(
                Float32, throttle_topic, self._ch_a_cb, 10)
            self.sub_b = self.create_subscription(
                Float32, steering_topic, self._ch_b_cb, 10)
            ch_a_label, ch_b_label = throttle_topic, steering_topic

        self.sub_mode = self.create_subscription(
            Float32, mode_switch_topic, self._mode_switch_cb, 10)
        self.sub_estop = self.create_subscription(
            Bool, estop_topic, self._estop_cb, 10)

        self.pub_cmd_vel = self.create_publisher(TwistStamped, cmd_vel_topic, 10)
        self.timer = self.create_timer(1.0 / publish_rate, self._publish_twist)

        self.get_logger().info(
            "RC Teleop [%s] started: %s + %s -> %s | mode_switch: %s | rate: %.0f Hz"
            % (self.mixing_mode, ch_a_label, ch_b_label, cmd_vel_topic,
               mode_switch_topic, publish_rate))

    def _normalize(self, raw_value: float) -> float:
        if self.is_pwm:
            norm = (raw_value - 1500.0) / 500.0
        else:
            norm = raw_value

        norm = max(-1.0, min(1.0, norm))

        if abs(norm) < self.deadzone:
            return 0.0
        return norm

    def _ch_a_cb(self, msg: Float32):
        self.ch_a = self._normalize(msg.data)

    def _ch_b_cb(self, msg: Float32):
        self.ch_b = self._normalize(msg.data)

    def _estop_cb(self, msg: Bool):
        self.estop_active = msg.data

    def _mode_switch_cb(self, msg: Float32):
        new_rc_mode = msg.data > self.mode_switch_threshold
        if new_rc_mode != self.rc_mode:
            self.rc_mode = new_rc_mode
            self._publish_zero()
            self.get_logger().info(
                "Mode switch → %s" % ("RC" if self.rc_mode else "ROS"))

    def _publish_zero(self):
        msg = TwistStamped()
        msg.header.stamp = self.get_clock().now().to_msg()
        self.pub_cmd_vel.publish(msg)

    def _publish_twist(self):
        if not self.rc_mode:
            return

        msg = TwistStamped()
        msg.header.stamp = self.get_clock().now().to_msg()

        if not self.estop_active:
            if self.mixing_mode == 'tank':
                left = self.ch_a
                right = self.ch_b
                msg.twist.linear.x = (left + right) / 2.0 * self.max_lin
                msg.twist.angular.z = (left - right) / 2.0 * self.max_ang
            else:
                msg.twist.linear.x = self.ch_a * self.max_lin
                msg.twist.angular.z = self.ch_b * self.max_ang

        self.pub_cmd_vel.publish(msg)


def main(args=None):
    rclpy.init(args=args)
    node = RCTeleopNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node._publish_zero()
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == '__main__':
    main()
