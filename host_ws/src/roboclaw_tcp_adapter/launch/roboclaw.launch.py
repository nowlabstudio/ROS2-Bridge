"""
RoboClaw TCP launch file.

Starts three nodes:
  1. roboclaw_tcp_node   — monkey-patched basicmicro_ros2 driver over TCP (cmd_vel)
  2. safety_bridge_node  — /robot/estop -> zero cmd_vel + /emergency_stop
  3. rc_teleop_node      — RC throttle/steering (Float32) -> cmd_vel (Twist)

All parameters are read from roboclaw_params.yaml or can be overridden on the command line.
"""

import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    pkg_dir = get_package_share_directory("roboclaw_tcp_adapter")

    roboclaw_params_file = os.path.join(pkg_dir, "config", "roboclaw_params.yaml")

    return LaunchDescription(
        [
            DeclareLaunchArgument(
                "roboclaw_host",
                default_value="192.168.68.60",
                description="USR-K6 IP address",
            ),
            DeclareLaunchArgument(
                "roboclaw_port",
                default_value="8234",
                description="USR-K6 TCP port",
            ),
            DeclareLaunchArgument(
                "namespace",
                default_value="/robot",
                description="ROS2 namespace for all topics",
            ),
            DeclareLaunchArgument(
                "robot_type",
                default_value="diff_drive",
                description="basicmicro_ros2 robot type preset",
            ),
            # --- RoboClaw driver (TCP-patched) ---
            Node(
                package="roboclaw_tcp_adapter",
                executable="roboclaw_tcp_node",
                name="roboclaw_driver",
                namespace=LaunchConfiguration("namespace"),
                parameters=[
                    roboclaw_params_file,
                    {
                        "port": [
                            "tcp://",
                            LaunchConfiguration("roboclaw_host"),
                            ":",
                            LaunchConfiguration("roboclaw_port"),
                        ],
                        "robot_type": LaunchConfiguration("robot_type"),
                    },
                ],
                remappings=[
                    ("cmd_vel", "cmd_vel"),
                    ("odom", "odom"),
                ],
                output="screen",
            ),
            # --- Safety bridge ---
            Node(
                package="roboclaw_tcp_adapter",
                executable="safety_bridge_node",
                name="safety_bridge",
                namespace=LaunchConfiguration("namespace"),
                parameters=[
                    {
                        "estop_topic": "estop",
                        "cmd_vel_topic": "cmd_vel",
                        "emergency_stop_topic": "/emergency_stop",
                        "watchdog_timeout_sec": 2.0,
                        "estop_cmd_vel_rate_sec": 0.1,
                    }
                ],
                output="screen",
            ),
            # --- RC Teleop (arcade: throttle + steering -> cmd_vel) ---
            Node(
                package="roboclaw_tcp_adapter",
                executable="rc_teleop_node",
                name="rc_teleop",
                namespace=LaunchConfiguration("namespace"),
                parameters=[
                    roboclaw_params_file,
                ],
                output="screen",
            ),
        ]
    )
