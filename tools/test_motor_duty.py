#!/usr/bin/env python3
"""
Standalone motor test — NO ROS2. Connects directly to RoboClaw via TCP,
sends DutyM1 then DutyM2. Use when roboclaw container is STOPPED.

  make robot-motor-test-standalone
"""
import os
import sys
import time

host = os.environ.get("ROBOCLAW_HOST", "192.168.68.60")
port = int(os.environ.get("ROBOCLAW_PORT", "8234"))
addr = 128

base = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(base, "host_ws/src/basicmicro_python"))
sys.path.insert(0, os.path.join(base, "host_ws/src/roboclaw_tcp_adapter"))

from roboclaw_tcp_adapter.basicmicro_tcp import RoboClawTCP

def main():
    print("Connecting to %s:%d ..." % (host, port))
    ctrl = RoboClawTCP("tcp://%s:%d" % (host, port), 38400, timeout=0.5)
    if not ctrl.Open():
        print("FAILED to open connection")
        sys.exit(1)
    print("Connected")

    for motor, name, duty_fn in [
        (1, "M1", ctrl.DutyM1),
        (2, "M2", ctrl.DutyM2),
    ]:
        print("\n--- %s: 50%% forward 2s ---" % name)
        ok = duty_fn(addr, 16384)
        print("  DutyM%d(16384): %s" % (motor, "OK" if ok else "FAILED"))
        time.sleep(2)
        ok = duty_fn(addr, 0)
        print("  DutyM%d(0) stop: %s" % (motor, "OK" if ok else "FAILED"))
        time.sleep(0.5)

    ctrl.close()
    print("\nDone. Did either motor move?")

def main_m2_only(duration_sec=10):
    """Only M2, 50%, duration_sec — ha csak M2 van bekötve."""
    print("Connecting to %s:%d ..." % (host, port))
    ctrl = RoboClawTCP("tcp://%s:%d" % (host, port), 38400, timeout=0.5)
    if not ctrl.Open():
        print("FAILED to open connection")
        sys.exit(1)
    print("Connected")
    print("\n--- M2 only: 50%% forward %ds ---" % duration_sec)
    ok = ctrl.DutyM2(addr, 16384)
    print("  DutyM2(16384): %s" % ("OK" if ok else "FAILED"))
    time.sleep(duration_sec)
    ok = ctrl.DutyM2(addr, 0)
    print("  DutyM2(0) stop: %s" % ("OK" if ok else "FAILED"))
    ctrl.close()
    print("\nDone. Did M2 move?")

if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "m2":
        dur = int(os.environ.get("M2_DURATION", "10"))
        main_m2_only(dur)
    else:
        main()
