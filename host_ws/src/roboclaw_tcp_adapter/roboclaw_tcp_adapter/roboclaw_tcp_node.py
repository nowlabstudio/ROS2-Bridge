"""
RoboClaw TCP Node — entry point that monkey-patches the upstream
basicmicro_ros2 driver to use TCP sockets instead of pyserial.

The patch replaces Basicmicro with RoboClawTCP in BOTH locations where
the class is importable:

  1. basicmicro.controller.Basicmicro  — the defining module
  2. basicmicro.Basicmicro             — the re-export in __init__.py

This is necessary because the upstream driver does:
    from basicmicro import Basicmicro
which binds the name from basicmicro.__init__, not from
basicmicro.controller.  Both must be patched before the driver module
is loaded into sys.modules.

Usage (via ROS2 launch or direct):
    ros2 run roboclaw_tcp_adapter roboclaw_tcp_node \
        --ros-args -p port:=tcp://192.168.68.60:8234
"""

import sys
import logging

logger = logging.getLogger(__name__)


def main():
    import basicmicro
    import basicmicro.controller
    from roboclaw_tcp_adapter.basicmicro_tcp import RoboClawTCP

    basicmicro.controller.Basicmicro = RoboClawTCP
    basicmicro.Basicmicro = RoboClawTCP

    logger.info(
        "Patched Basicmicro -> RoboClawTCP in both basicmicro.controller "
        "and basicmicro (TCP transport active)"
    )

    try:
        from basicmicro_driver.basicmicro_node import main as driver_main
        driver_main()
    except ImportError as exc:
        logger.error(
            "Could not import basicmicro_driver.basicmicro_node. "
            "Ensure basicmicro_ros2 is built and sourced: %s",
            exc,
        )
        sys.exit(1)


if __name__ == "__main__":
    main()
