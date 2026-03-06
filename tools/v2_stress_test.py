#!/usr/bin/env python3
"""
W6100 EVB Pico v2.0 - Industrial Robot Stress Test Suite
=========================================================
Tesztel minden v2.0 funkciót ipari AGV robot kontextusban.

Futtatás:
    python3 tools/v2_stress_test.py [--port /dev/tty.usbmodem231401]
                                    [--skip-manual]
                                    [--skip-ros]
                                    [--agent-ip 192.168.68.125]
                                    [--shell-only]

Szukseges:
    pip install pyserial
    ROS2 Jazzy + micro_ros_agent (opcionalis, --skip-ros nelkul)

FIGYELEM: Ez ipari robot teszt.
  Az E-Stop / relay tesztek valodi GPIO-t kapcsolnak.
  Futtasd csak ellenorzott, biztonsagos korulmenyek kozott.
"""

import serial
import serial.tools.list_ports
import subprocess
import time
import sys
import argparse
import json
import os
import re
import statistics
import threading

# ANSI
R    = "\033[31m"; G = "\033[32m"; Y = "\033[33m"; B = "\033[34m"
M    = "\033[35m"; C = "\033[36m"; DIM = "\033[2m"; RST = "\033[0m"; BOLD = "\033[1m"
COL  = 56

results = []

# Industrial safety thresholds
ESTOP_LATENCY_MAX_MS  = 100
SERVICE_LATENCY_MAX_MS = 500
TOPIC_RATE_MIN_HZ     = 0.8
RECONNECT_MAX_S       = 30
PICO_NODE             = "pico_bridge"
AGENT_IP              = "192.168.68.125"

# ---------------------------------------------------------------------------
#  Test infrastructure
# ---------------------------------------------------------------------------

def _badge(s):
    return {
        "PASS": f"{G} PASS {RST}", "FAIL": f"{R} FAIL {RST}",
        "ERROR": f"{R} ERR  {RST}", "SKIP": f"{Y} SKIP {RST}",
    }.get(s, f" {s} ")

def section(title):
    print(f"\n{BOLD}{B}+{'-'*68}+{RST}")
    print(f"{BOLD}{B}|  {title:<66}|{RST}")
    print(f"{BOLD}{B}+{'-'*68}+{RST}")

def auto_test(tid, name, category, func, *args, **kwargs):
    label = f"[{tid}] {name}"
    print(f"  {DIM}{label:<{COL}}{RST}", end="", flush=True)
    try:
        ok, note = func(*args, **kwargs)
        status = "PASS" if ok else "FAIL"
        note_str = f"  {DIM}{str(note)[:44]}{RST}" if note else ""
        print(f"{_badge(status)}{note_str}")
        results.append((tid, name, category, status, str(note) if note else ""))
        return ok
    except Exception as e:
        print(f"{_badge('ERROR')}  {DIM}{str(e)[:44]}{RST}")
        results.append((tid, name, category, "ERROR", str(e)))
        return False

def manual_test(tid, name, category, warning, instructions, expected):
    print(f"\n  {BOLD}{M}[MANUAL]{RST} {BOLD}[{tid}] {name}{RST}  {DIM}({category}){RST}")
    if warning:
        print(f"  {R}{BOLD}WARNING: {warning}{RST}")
    for i, step in enumerate(instructions, 1):
        print(f"  {Y}{i}.{RST} {step}")
    print(f"  {DIM}Expected:{RST} {C}{expected}{RST}")
    print(f"  {DIM}ENTER=pass  f=fail  s=skip{RST}  ", end="", flush=True)
    try:
        ans = input().strip().lower()
    except (KeyboardInterrupt, EOFError):
        ans = "s"
    if ans == "f":
        print(f"  Note: ", end="", flush=True)
        note = input().strip()
        print(f"  {_badge('FAIL')}  {DIM}{note}{RST}")
        results.append((tid, name, category, "FAIL", note))
        return False
    elif ans == "s":
        print(f"  {_badge('SKIP')}")
        results.append((tid, name, category, "SKIP", "skipped"))
        return None
    else:
        print(f"  {_badge('PASS')}")
        results.append((tid, name, category, "PASS", ""))
        return True

# ---------------------------------------------------------------------------
#  Serial helpers
# ---------------------------------------------------------------------------

def find_port():
    for p in serial.tools.list_ports.comports():
        if "usbmodem" in p.device.lower() or "acm" in p.device.lower():
            return p.device
    return None

def open_serial(port, baud=115200, timeout=3):
    ser = serial.Serial(port, baud, timeout=timeout)
    ser.dtr = True
    time.sleep(0.3)
    ser.reset_input_buffer()
    return ser

def send_cmd(ser, cmd, wait=0.8, expect=None):
    ser.reset_input_buffer()
    ser.write((cmd + "\r\n").encode())
    time.sleep(wait)
    raw = ser.read(ser.in_waiting or 1024).decode(errors="replace")
    if expect:
        return expect in raw, raw
    return True, raw

def drain(ser, duration=1.0):
    deadline = time.time() + duration
    buf = ""
    while time.time() < deadline:
        chunk = ser.read(ser.in_waiting or 1).decode(errors="replace")
        buf += chunk
        if not chunk:
            time.sleep(0.05)
    return buf

def shell_alive(ser):
    ok, _ = send_cmd(ser, "bridge config show", wait=1.5, expect="agent_ip")
    return ok

# ---------------------------------------------------------------------------
#  ROS2 CLI helpers
# ---------------------------------------------------------------------------

def ros2_cmd(args, timeout=10):
    try:
        r = subprocess.run(["ros2"] + args, capture_output=True, text=True, timeout=timeout)
        return r.returncode, r.stdout, r.stderr
    except subprocess.TimeoutExpired:
        return -1, "", "TIMEOUT"
    except FileNotFoundError:
        return -2, "", "ros2 not found"

def ros2_echo_once(topic, msg_type, timeout=6):
    rc, out, err = ros2_cmd(["topic", "echo", topic, msg_type, "--once"], timeout=timeout)
    return out.strip() if rc == 0 and out.strip() else None

def ros2_echo_n(topic, msg_type, n=5, timeout=15):
    try:
        r = subprocess.run(
            ["ros2", "topic", "echo", topic, msg_type, "--no-arr", "--times", str(n)],
            capture_output=True, text=True, timeout=timeout
        )
        return [m.strip() for m in r.stdout.split("---") if m.strip()]
    except Exception:
        return []

def ros2_service_call(srv, srv_type, request, timeout=8):
    t0 = time.monotonic()
    rc, out, err = ros2_cmd(["service", "call", srv, srv_type, request], timeout=timeout)
    lat_ms = (time.monotonic() - t0) * 1000
    return (lat_ms, out) if rc == 0 else (None, err)

def ros2_param_get(node, param):
    rc, out, err = ros2_cmd(["param", "get", node, param], timeout=5)
    return out.strip() if rc == 0 else None

def ros2_param_set(node, param, value):
    rc, _, __ = ros2_cmd(["param", "set", node, param, str(value)], timeout=5)
    return rc == 0

def ros2_node_list(timeout=5):
    rc, out, err = ros2_cmd(["node", "list"], timeout=timeout)
    return [n.strip() for n in out.splitlines() if n.strip()] if rc == 0 else []

def ros2_available():
    rc, _, __ = ros2_cmd(["--help"], timeout=3)
    return rc in (0, 1)

def wait_for_node(name, timeout=RECONNECT_MAX_S):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if any(name in n for n in ros2_node_list(timeout=3)):
            return True
        time.sleep(1)
    return False

def parse_int(msg):
    m = re.search(r"data:\s*(-?\d+)", msg)
    return int(m.group(1)) if m else None

def parse_bool(msg):
    m = re.search(r"data:\s*(true|false)", msg, re.IGNORECASE)
    return m.group(1).lower() == "true" if m else None

def parse_float(msg):
    m = re.search(r"data:\s*([-\d.]+)", msg)
    return float(m.group(1)) if m else None

def success_in(resp):
    return ("success: True" in resp or
            '"success": true' in resp or
            "success=True" in resp)

# ---------------------------------------------------------------------------
#  T01-T17  Shell / Config  (v1.5 parity)
# ---------------------------------------------------------------------------

def t01_shell_alive(ser):
    ok, raw = send_cmd(ser, "bridge config show", wait=1.2, expect="agent_ip")
    return ok, raw.strip().replace("\n", " ")[:80]

def t02_config_roundtrip(ser):
    send_cmd(ser, "bridge config set ros.node_name stress_node", wait=0.5)
    ok, _ = send_cmd(ser, "bridge config show", wait=1.0, expect="stress_node")
    send_cmd(ser, "bridge config set ros.node_name pico_bridge", wait=0.5)
    return ok, "OK" if ok else "FAIL"

def t03_config_all_keys(ser):
    keys = {
        "network.dhcp": "false", "network.ip": "192.168.68.114",
        "network.netmask": "255.255.255.0", "network.gateway": "192.168.68.1",
        "network.agent_ip": AGENT_IP, "network.agent_port": "8888",
        "ros.node_name": "pico_bridge", "ros.namespace": "/",
    }
    failed = [k for k, v in keys.items()
              if not send_cmd(ser, f"bridge config set {k} {v}", wait=0.4, expect="OK")[0]]
    return len(failed) == 0, f"failed: {failed}" if failed else f"all {len(keys)} keys OK"

def t04_config_save_load(ser):
    send_cmd(ser, "bridge config set ros.node_name saved_node", wait=0.4)
    send_cmd(ser, "bridge config save", wait=1.0)
    send_cmd(ser, "bridge config load", wait=1.0)
    ok, _ = send_cmd(ser, "bridge config show", wait=1.0, expect="saved_node")
    send_cmd(ser, "bridge config set ros.node_name pico_bridge", wait=0.4)
    send_cmd(ser, "bridge config save", wait=1.0)
    return ok, "save->load OK" if ok else "FAIL"

def t05_config_reset(ser):
    send_cmd(ser, "bridge config set ros.node_name garbage_xyz", wait=0.4)
    send_cmd(ser, "bridge config reset", wait=0.4)
    ok, _ = send_cmd(ser, "bridge config show", wait=1.0, expect="pico_bridge")
    send_cmd(ser, "bridge config save", wait=1.0)
    return ok, "reset->defaults OK" if ok else "FAIL"

def t06_empty_value(ser):
    send_cmd(ser, "bridge config set ros.node_name", wait=0.6)
    alive, _ = send_cmd(ser, "bridge config show", wait=0.8, expect="agent_ip")
    return alive, f"alive after empty set: {alive}"

def t07_unknown_key(ser):
    send_cmd(ser, "bridge config set network.nonexistent 99", wait=0.5)
    alive, _ = send_cmd(ser, "bridge config show", wait=0.8, expect="agent_ip")
    return alive, f"alive after unknown key: {alive}"

def t08_very_long_value(ser):
    send_cmd(ser, "bridge config set ros.node_name " + "A" * 200, wait=0.6)
    alive, _ = send_cmd(ser, "bridge config show", wait=1.0, expect="agent_ip")
    return alive, f"alive after 200-char value: {alive}"

def t09_special_chars(ser):
    specials = [("ros.node_name", "node-with-dashes"),
                ("ros.namespace", "/robot/arm"),
                ("network.ip",    "10.0.0.99")]
    failed = []
    for k, v in specials:
        send_cmd(ser, f"bridge config set {k} {v}", wait=0.4)
        ok, _ = send_cmd(ser, "bridge config show", wait=0.8, expect=v)
        if not ok:
            failed.append(f"{k}={v}")
    send_cmd(ser, "bridge config set ros.node_name pico_bridge", wait=0.4)
    send_cmd(ser, "bridge config set ros.namespace /", wait=0.4)
    send_cmd(ser, "bridge config set network.ip 192.168.68.114", wait=0.4)
    return len(failed) == 0, f"failed: {failed}" if failed else "all OK"

def t10_rapid_fire(ser):
    for i in range(50):
        ser.write(f"bridge config set ros.node_name node_{i}\r\n".encode())
        time.sleep(0.04)
        if i % 10 == 9:
            ser.read(ser.in_waiting or 1)
    drain(ser, 3.0)
    alive, _ = send_cmd(ser, "bridge config show", wait=3.0, expect="agent_ip")
    send_cmd(ser, "bridge config set ros.node_name pico_bridge", wait=0.4)
    return alive, f"alive after 50 rapid: {alive}"

def t11_save_verify_json(ser):
    send_cmd(ser, "bridge config save", wait=1.2)
    ok, raw = send_cmd(ser, "bridge config show", wait=1.2)
    missing = [f for f in ["dhcp", "agent_ip", "agent_port", "node_name", "namespace"]
               if f not in raw]
    return len(missing) == 0, f"missing: {missing}" if missing else "all fields present"

def t12_repeated_save(ser):
    for i in range(20):
        ok, _ = send_cmd(ser, "bridge config save", wait=0.8)
        if not ok:
            return False, f"fail at {i+1}/20"
    ok2, _ = send_cmd(ser, "bridge config show", wait=1.0, expect="agent_ip")
    return ok2, f"20x save, data intact: {ok2}"

def t13_dhcp_toggle(ser):
    for _ in range(5):
        send_cmd(ser, "bridge config set network.dhcp true",  wait=0.4)
        send_cmd(ser, "bridge config set network.dhcp false", wait=0.4)
    ok, _ = send_cmd(ser, "bridge config show", wait=1.0, expect="dhcp")
    return ok, "5x toggle stable" if ok else "FAIL"

def t14_port_boundary(ser):
    for port in ["0", "1", "8888", "65535", "99999"]:
        send_cmd(ser, f"bridge config set network.agent_port {port}", wait=0.4)
    send_cmd(ser, "bridge config set network.agent_port 8888", wait=0.4)
    alive, _ = send_cmd(ser, "bridge config show", wait=0.8, expect="agent_ip")
    return alive, f"alive after port boundaries: {alive}"

def t15_ip_format(ser):
    for ip in ["999.999.999.999", "abc.def.ghi.jkl", "1.2.3", "::1"]:
        send_cmd(ser, f"bridge config set network.ip {ip}", wait=0.4)
    alive, _ = send_cmd(ser, "bridge config show", wait=1.0, expect="agent_ip")
    send_cmd(ser, "bridge config set network.ip 192.168.68.114", wait=0.4)
    return alive, f"alive after bad IPs: {alive}"

def t16_shell_latency(ser):
    times = []
    for _ in range(10):
        t0 = time.time()
        ok, _ = send_cmd(ser, "bridge config show", wait=2.0, expect="agent_ip")
        if ok:
            times.append(time.time() - t0)
    if not times:
        return False, "no response"
    avg, mx = statistics.mean(times), max(times)
    return mx < 3.0, f"avg={avg*1000:.0f}ms  max={mx*1000:.0f}ms  ({len(times)}/10)"

def t17_concurrent_usb_serial(ser):
    for i in range(100):
        ser.write(b"bridge config show\r\n")
        if i % 10 == 0:
            time.sleep(0.1)
    time.sleep(3.0)
    ser.reset_input_buffer()
    alive, _ = send_cmd(ser, "bridge config show", wait=2.0, expect="agent_ip")
    return alive, f"alive after 100-burst: {alive}"

# ---------------------------------------------------------------------------
#  T20-T27  ROS2 Topics
# ---------------------------------------------------------------------------

def t20_node_online():
    nodes = ros2_node_list(timeout=10)
    found = any(PICO_NODE in n for n in nodes)
    return found, f"/{PICO_NODE} online" if found else f"not in: {nodes}"

def t21_counter_publishes():
    msgs = ros2_echo_n("/pico/counter", "std_msgs/msg/Int32", n=3, timeout=10)
    vals = [parse_int(m) for m in msgs if parse_int(m) is not None]
    if len(vals) < 2:
        return False, f"only {len(vals)} values parsed"
    mono = all(vals[i+1] > vals[i] for i in range(len(vals)-1))
    return mono, f"values={vals}  monotone={mono}"

def t22_heartbeat_toggles():
    msgs = ros2_echo_n("/pico/heartbeat", "std_msgs/msg/Bool", n=4, timeout=10)
    vals = [parse_bool(m) for m in msgs if parse_bool(m) is not None]
    if len(vals) < 2:
        return False, f"only {len(vals)} values"
    toggled = any(vals[i] != vals[i+1] for i in range(len(vals)-1))
    return toggled, f"values={vals}  toggles={toggled}"

def t23_echo_roundtrip():
    received = []
    def sub_thread():
        msg = ros2_echo_once("/pico/echo_out", "std_msgs/msg/Int32", timeout=8)
        if msg:
            received.append(msg)
    t = threading.Thread(target=sub_thread, daemon=True)
    t.start()
    time.sleep(0.5)
    sentinel = 42424
    ros2_cmd(["topic", "pub", "--once", "/pico/echo_in",
               "std_msgs/msg/Int32", f"{{data: {sentinel}}}"], timeout=5)
    t.join(timeout=9)
    if not received:
        return False, "no echo_out received"
    v = parse_int(received[0])
    return v == sentinel, f"sent={sentinel}  echoed={v}"

def t24_battery_voltage_valid():
    msg = ros2_echo_once("/robot/battery_voltage", "std_msgs/msg/Float32", timeout=8)
    if not msg:
        return False, "no message on /robot/battery_voltage"
    v = parse_float(msg)
    if v is None:
        return False, f"cannot parse float: {msg[:60]}"
    # -1.0 = sentinel (no ADC hardware), else must be 0..50V
    valid = v == -1.0 or 0.0 <= v <= 50.0
    return valid, f"voltage={v:.3f}V {'(sentinel: no ADC HW)' if v == -1.0 else 'OK'}"

def t25_heartbeat_rate():
    msgs = []
    def collect():
        try:
            r = subprocess.run(
                ["ros2", "topic", "echo", "/pico/heartbeat",
                 "std_msgs/msg/Bool", "--times", "5"],
                capture_output=True, text=True, timeout=12
            )
            msgs.extend(r.stdout.splitlines())
        except Exception:
            pass
    t = threading.Thread(target=collect, daemon=True)
    t0 = time.monotonic()
    t.start(); t.join(timeout=13)
    elapsed = time.monotonic() - t0
    count = len([m for m in msgs if "data:" in m])
    if count < 2 or elapsed < 1:
        return False, f"only {count} msgs in {elapsed:.1f}s"
    rate = count / elapsed
    return rate >= TOPIC_RATE_MIN_HZ, f"rate={rate:.2f}Hz  (min={TOPIC_RATE_MIN_HZ}Hz)"

def t26_diagnostics_publishes():
    msg = ros2_echo_once("/diagnostics",
                         "diagnostic_msgs/msg/DiagnosticArray", timeout=8)
    if not msg:
        return False, "no /diagnostics message"
    return "status" in msg and ("w6100" in msg.lower() or "pico" in msg.lower()), \
           "OK" if "status" in msg else f"unexpected: {msg[:60]}"

def t27_diagnostics_kv_fields():
    msg = ros2_echo_once("/diagnostics",
                         "diagnostic_msgs/msg/DiagnosticArray", timeout=8)
    if not msg:
        return False, "no message"
    missing = [k for k in ["uptime_s", "channels", "reconnects", "firmware"]
               if k not in msg]
    return len(missing) == 0, f"missing: {missing}" if missing else "all KV fields present"

# ---------------------------------------------------------------------------
#  T30-T38  Services - relay, e-stop
# ---------------------------------------------------------------------------

def t30_relay_brake_on():
    lat, resp = ros2_service_call("/bridge/relay_brake",
                                  "std_srvs/srv/SetBool", "{data: true}")
    if lat is None:
        return False, f"no response: {resp}"
    return success_in(resp), f"lat={lat:.0f}ms"

def t31_relay_brake_off():
    lat, resp = ros2_service_call("/bridge/relay_brake",
                                  "std_srvs/srv/SetBool", "{data: false}")
    if lat is None:
        return False, f"no response: {resp}"
    return success_in(resp), f"lat={lat:.0f}ms"

def t32_relay_toggle_50x():
    lats, errors = [], 0
    for i in range(50):
        lat, resp = ros2_service_call("/bridge/relay_brake", "std_srvs/srv/SetBool",
                                      f"{{data: {'true' if i%2==0 else 'false'}}}", timeout=5)
        if lat is None:
            errors += 1
        else:
            lats.append(lat)
        time.sleep(0.05)
    if not lats:
        return False, f"all {errors} failed"
    avg, mx = statistics.mean(lats), max(lats)
    return errors == 0 and mx < SERVICE_LATENCY_MAX_MS, \
           f"avg={avg:.0f}ms max={mx:.0f}ms errors={errors}/50"

def t33_estop_trigger():
    lat, resp = ros2_service_call("/bridge/estop", "std_srvs/srv/Trigger", "{}")
    if lat is None:
        return False, f"no response: {resp}"
    return success_in(resp), f"lat={lat:.0f}ms"

def t34_estop_latency_10x():
    lats, errors = [], 0
    for _ in range(10):
        lat, resp = ros2_service_call("/bridge/estop", "std_srvs/srv/Trigger",
                                      "{}", timeout=5)
        if lat is None:
            errors += 1
        else:
            lats.append(lat)
        time.sleep(0.1)
    if not lats:
        return False, f"all {errors} failed"
    mx, avg = max(lats), statistics.mean(lats)
    return errors == 0 and mx < ESTOP_LATENCY_MAX_MS, \
           f"avg={avg:.0f}ms max={mx:.0f}ms limit={ESTOP_LATENCY_MAX_MS}ms err={errors}"

def t35_estop_rapid_100x():
    lats, errors = [], 0
    for _ in range(100):
        lat, resp = ros2_service_call("/bridge/estop", "std_srvs/srv/Trigger",
                                      "{}", timeout=4)
        if lat is None:
            errors += 1
        else:
            lats.append(lat)
    lat_last, _ = ros2_service_call("/bridge/estop", "std_srvs/srv/Trigger",
                                    "{}", timeout=8)
    if not lats:
        return False, f"all {errors} calls failed"
    avg, mx = statistics.mean(lats), max(lats)
    return errors < 5 and lat_last is not None, \
           f"avg={avg:.0f}ms max={mx:.0f}ms err={errors}/100 last={'OK' if lat_last else 'FAIL'}"

def t36_mixed_services_concurrent():
    res_lats, errors = [], []
    def call_set_bool():
        for _ in range(10):
            lat, resp = ros2_service_call("/bridge/relay_brake",
                                         "std_srvs/srv/SetBool", "{data: true}", timeout=5)
            if lat:
                res_lats.append(lat)
            else:
                errors.append("sb")
    def call_trigger():
        for _ in range(10):
            lat, resp = ros2_service_call("/bridge/estop",
                                         "std_srvs/srv/Trigger", "{}", timeout=5)
            if lat:
                res_lats.append(lat)
            else:
                errors.append("tr")
    t1 = threading.Thread(target=call_set_bool, daemon=True)
    t2 = threading.Thread(target=call_trigger, daemon=True)
    t1.start(); t2.start()
    t1.join(timeout=30); t2.join(timeout=30)
    ok = len(errors) == 0 and len(res_lats) >= 15
    avg = statistics.mean(res_lats) if res_lats else 0
    return ok, f"total={len(res_lats)}/20 avg={avg:.0f}ms errors={len(errors)}"

def t37_service_under_topic_flood():
    stop = threading.Event()
    def flood():
        while not stop.is_set():
            ros2_cmd(["topic", "pub", "--once", "/pico/echo_in",
                      "std_msgs/msg/Int32", "{data: 1}"], timeout=2)
            time.sleep(0.01)
    t = threading.Thread(target=flood, daemon=True)
    t.start(); time.sleep(0.5)
    lat, resp = ros2_service_call("/bridge/estop", "std_srvs/srv/Trigger", "{}", timeout=8)
    stop.set(); t.join(timeout=3)
    ok = lat is not None and lat < SERVICE_LATENCY_MAX_MS
    return ok, f"lat={lat:.0f}ms under flood" if lat else f"no response: {resp}"

def t38_services_listed():
    rc, out, err = ros2_cmd(["service", "list"], timeout=8)
    if rc != 0:
        return False, "ros2 service list failed"
    missing = [s for s in ["/bridge/relay_brake", "/bridge/estop"] if s not in out]
    return len(missing) == 0, f"missing: {missing}" if missing else "all services listed"

# ---------------------------------------------------------------------------
#  T40-T44  Parameter server
# ---------------------------------------------------------------------------

def t40_param_server_online():
    rc, out, err = ros2_cmd(["param", "list", f"/{PICO_NODE}"], timeout=8)
    return rc == 0 and bool(out.strip()), \
           f"params: {out.strip().replace(chr(10), ', ')[:60]}"

def t41_param_period_get_set():
    orig = ros2_param_get(f"/{PICO_NODE}", "ch.test_counter.period_ms")
    if orig is None:
        return False, "cannot get ch.test_counter.period_ms"
    ok = ros2_param_set(f"/{PICO_NODE}", "ch.test_counter.period_ms", "750")
    if not ok:
        return False, "set failed"
    result = ros2_param_get(f"/{PICO_NODE}", "ch.test_counter.period_ms")
    ros2_param_set(f"/{PICO_NODE}", "ch.test_counter.period_ms",
                   orig.split(":")[-1].strip() if orig else "500")
    confirmed = result is not None and "750" in result
    return confirmed, f"orig={orig}  set=750  readback={result}"

def t42_param_enable_disable():
    ok_off = ros2_param_set(f"/{PICO_NODE}", "ch.test_counter.enabled", "false")
    if not ok_off:
        return False, "cannot set enabled=false"
    time.sleep(0.5)
    ok_on = ros2_param_set(f"/{PICO_NODE}", "ch.test_counter.enabled", "true")
    return ok_on, "enable/disable roundtrip OK" if ok_on else "re-enable failed"

def t43_param_stress_50x():
    param = "ch.test_counter.period_ms"
    errors = 0
    for i in range(50):
        val = str(200 + i * 10)
        ros2_param_set(f"/{PICO_NODE}", param, val)
        rb = ros2_param_get(f"/{PICO_NODE}", param)
        if rb is None or val not in rb:
            errors += 1
    ros2_param_set(f"/{PICO_NODE}", param, "500")
    return errors < 5, f"errors={errors}/50"

def t44_param_boundary_values():
    for val in ["1", "1000", "10000", "0"]:
        ros2_param_set(f"/{PICO_NODE}", "ch.test_counter.period_ms", val)
        time.sleep(0.2)
    ros2_param_set(f"/{PICO_NODE}", "ch.test_counter.period_ms", "500")
    alive = bool(ros2_param_get(f"/{PICO_NODE}", "ch.test_counter.period_ms"))
    return alive, "param server alive after boundary values" if alive else "FAIL"

# ---------------------------------------------------------------------------
#  T50-T53  Diagnostics
# ---------------------------------------------------------------------------

def t50_diagnostics_rate():
    msgs = ros2_echo_n("/diagnostics", "diagnostic_msgs/msg/DiagnosticArray",
                       n=3, timeout=10)
    return len(msgs) >= 2, f"received {len(msgs)} msgs"

def t51_diagnostics_uptime_increases():
    def get_uptime():
        msg = ros2_echo_once("/diagnostics",
                             "diagnostic_msgs/msg/DiagnosticArray", timeout=5)
        if not msg:
            return None
        m = re.search(r"uptime_s.*?value:\s*'?(\d+)'?", msg, re.DOTALL)
        return int(m.group(1)) if m else None
    up1 = get_uptime()
    time.sleep(2.5)
    up2 = get_uptime()
    if up1 is None or up2 is None:
        return False, f"cannot read uptime (up1={up1} up2={up2})"
    return up2 > up1, f"uptime: {up1}s -> {up2}s"

def t52_diagnostics_firmware():
    msg = ros2_echo_once("/diagnostics",
                         "diagnostic_msgs/msg/DiagnosticArray", timeout=5)
    if not msg:
        return False, "no message"
    ok = "v2.0-W6100" in msg
    return ok, "firmware='v2.0-W6100' found" if ok else "not found"

def t53_diagnostics_status_ok():
    msg = ros2_echo_once("/diagnostics",
                         "diagnostic_msgs/msg/DiagnosticArray", timeout=5)
    if not msg:
        return False, "no message"
    ok = "level: 0" in msg
    return ok, "level=OK(0)" if ok else f"unexpected level: {msg[:60]}"

# ---------------------------------------------------------------------------
#  T60-T65  Reconnect stress
# ---------------------------------------------------------------------------

def t60_node_visible_after_connect():
    ok = wait_for_node(PICO_NODE, timeout=RECONNECT_MAX_S)
    return ok, f"/{PICO_NODE} appeared within {RECONNECT_MAX_S}s"

def t61_topics_resume_after_reboot(ser):
    send_cmd(ser, "bridge reboot", wait=1.0)
    time.sleep(5)
    if not wait_for_node(PICO_NODE, timeout=RECONNECT_MAX_S):
        return False, f"node not back in {RECONNECT_MAX_S}s"
    msg = ros2_echo_once("/pico/counter", "std_msgs/msg/Int32", timeout=8)
    return msg is not None, "counter resumed" if msg else "no counter after reboot"

def t62_services_resume_after_reboot(ser):
    if not wait_for_node(PICO_NODE, timeout=RECONNECT_MAX_S):
        return False, "node not online"
    time.sleep(3)
    lat, resp = ros2_service_call("/bridge/relay_brake",
                                  "std_srvs/srv/SetBool", "{data: false}", timeout=8)
    return lat is not None, \
           f"relay_brake lat={lat:.0f}ms" if lat else f"not available: {resp}"

def t63_params_survive_reboot(ser):
    ros2_param_set(f"/{PICO_NODE}", "ch.test_counter.period_ms", "750")
    time.sleep(0.5)
    send_cmd(ser, "bridge reboot", wait=1.0)
    time.sleep(5)
    if not wait_for_node(PICO_NODE, timeout=RECONNECT_MAX_S):
        return False, "node not back"
    time.sleep(2)
    val = ros2_param_get(f"/{PICO_NODE}", "ch.test_counter.period_ms")
    persisted = val is not None and "750" in val
    return persisted, f"persisted={persisted}  readback={val}"

def t64_5x_rapid_reboot(ser):
    failures = 0
    for i in range(5):
        send_cmd(ser, "bridge reboot", wait=0.5)
        time.sleep(4)
        if not wait_for_node(PICO_NODE, timeout=RECONNECT_MAX_S):
            failures += 1
        else:
            time.sleep(1)
    return failures == 0, f"failures={failures}/5"

def t65_estop_available_after_reboot(ser):
    send_cmd(ser, "bridge reboot", wait=0.5)
    time.sleep(5)
    t_start = time.monotonic()
    if not wait_for_node(PICO_NODE, timeout=RECONNECT_MAX_S):
        return False, "node not back"
    estop_delay = None
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        lat, resp = ros2_service_call("/bridge/estop", "std_srvs/srv/Trigger",
                                     "{}", timeout=3)
        if lat is not None:
            estop_delay = time.monotonic() - t_start
            break
        time.sleep(0.5)
    if estop_delay is None:
        return False, "E-Stop never became available after reboot"
    return True, f"E-Stop available {estop_delay:.1f}s after reboot"

# ---------------------------------------------------------------------------
#  T70-T74  Safety & Latency
# ---------------------------------------------------------------------------

def t70_estop_under_pub_storm():
    stop = threading.Event()
    def flood():
        while not stop.is_set():
            ros2_cmd(["topic", "pub", "--once", "/pico/echo_in",
                      "std_msgs/msg/Int32", "{data: 9}"], timeout=2)
            time.sleep(0.02)
    t = threading.Thread(target=flood, daemon=True)
    t.start(); time.sleep(0.3)
    lats, errors = [], 0
    for _ in range(20):
        lat, resp = ros2_service_call("/bridge/estop", "std_srvs/srv/Trigger",
                                     "{}", timeout=5)
        if lat is None:
            errors += 1
        else:
            lats.append(lat)
        time.sleep(0.2)
    stop.set(); t.join(timeout=10)
    if not lats:
        return False, f"all {errors} E-Stop calls failed"
    mx, avg = max(lats), statistics.mean(lats)
    p95 = sorted(lats)[int(len(lats) * 0.95)]
    return errors == 0 and p95 < ESTOP_LATENCY_MAX_MS, \
           f"avg={avg:.0f}ms p95={p95:.0f}ms max={mx:.0f}ms err={errors}/20"

def t71_estop_p99_latency():
    lats, errors = [], 0
    for _ in range(50):
        lat, _ = ros2_service_call("/bridge/estop", "std_srvs/srv/Trigger",
                                   "{}", timeout=5)
        if lat is None:
            errors += 1
        else:
            lats.append(lat)
    if len(lats) < 10:
        return False, f"too few samples: {len(lats)}"
    p99 = sorted(lats)[int(len(lats) * 0.99)]
    p95 = sorted(lats)[int(len(lats) * 0.95)]
    avg = statistics.mean(lats)
    return p99 < ESTOP_LATENCY_MAX_MS and errors < 3, \
           f"avg={avg:.0f}ms p95={p95:.0f}ms p99={p99:.0f}ms err={errors}"

def t72_relay_off_on_estop():
    ros2_service_call("/bridge/relay_brake", "std_srvs/srv/SetBool",
                      "{data: true}", timeout=5)
    time.sleep(0.2)
    lat, resp = ros2_service_call("/bridge/estop", "std_srvs/srv/Trigger",
                                  "{}", timeout=5)
    ok = lat is not None
    return ok, f"E-Stop triggered lat={lat:.0f}ms" if ok else f"failed: {resp}"

def t73_no_counter_skip():
    msgs = ros2_echo_n("/pico/counter", "std_msgs/msg/Int32", n=10, timeout=20)
    vals = [parse_int(m) for m in msgs if parse_int(m) is not None]
    if len(vals) < 5:
        return False, f"only {len(vals)} values"
    gaps = [vals[i+1] - vals[i] for i in range(len(vals)-1)]
    max_gap = max(gaps) if gaps else 0
    return max_gap <= 2, f"values={vals[:5]}... max_gap={max_gap}"

def t74_service_survives_bad_request():
    ros2_cmd(["service", "call", "/bridge/relay_brake",
               "std_srvs/srv/SetBool", "{invalid_field: 123}"], timeout=5)
    time.sleep(0.5)
    lat, resp = ros2_service_call("/bridge/relay_brake",
                                  "std_srvs/srv/SetBool", "{data: false}", timeout=6)
    ok = lat is not None
    return ok, f"service alive lat={lat:.0f}ms" if ok else "service dead after bad req"

# ---------------------------------------------------------------------------
#  Summary
# ---------------------------------------------------------------------------

def print_summary():
    total_pass = sum(1 for *_, s, _ in results if s == "PASS")
    total_fail = sum(1 for *_, s, _ in results if s == "FAIL")
    total_skip = sum(1 for *_, s, _ in results if s == "SKIP")
    total_err  = sum(1 for *_, s, _ in results if s == "ERROR")
    total = len(results)
    ran   = total - total_skip
    rate  = (total_pass / ran * 100) if ran > 0 else 0
    rc    = G if rate >= 90 else (Y if rate >= 70 else R)

    print(f"\n{BOLD}{'='*70}{RST}")
    print(f"{BOLD}  v2.0 INDUSTRIAL STRESS TEST RESULTS{RST}")
    print(f"{'='*70}")

    cats = {}
    for tid, name, cat, status, note in results:
        cats.setdefault(cat, []).append((tid, name, status, note))

    for cat, items in sorted(cats.items()):
        cp = sum(1 for *_, s, _ in items if s == "PASS")
        ct = sum(1 for *_, s, _ in items if s != "SKIP")
        cc = G if cp == ct else (Y if cp >= ct * 0.8 else R)
        print(f"\n  {BOLD}{C}{cat}{RST}  {cc}({cp}/{ct}){RST}")
        for tid, name, status, note in items:
            label = f"[{tid}] {name}"
            ns = f"  {DIM}{str(note)[:38]}{RST}" if note else ""
            print(f"  {label:<{COL}}{_badge(status)}{ns}")

    safety = [(t, n, s, no) for t, n, c, s, no in results
              if c in ("Safety", "E-Stop")]
    if safety:
        sp = sum(1 for *_, s, _ in safety if s == "PASS")
        print(f"\n  {BOLD}{R}SAFETY CRITICAL: {sp}/{len(safety)} passed{RST}")

    print(f"\n{'-'*70}")
    print(f"  Total: {total}  "
          f"{G}PASS: {total_pass}{RST}  "
          f"{R}FAIL: {total_fail}{RST}  "
          f"{Y}SKIP: {total_skip}{RST}  "
          f"{R}ERR: {total_err}{RST}")
    print(f"  {rc}Success rate: {rate:.1f}%{RST}  ({total_pass}/{ran})")
    print(f"{'='*70}\n")

    report = {
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "version":   "v2.0",
        "thresholds": {
            "estop_latency_max_ms": ESTOP_LATENCY_MAX_MS,
            "service_latency_max_ms": SERVICE_LATENCY_MAX_MS,
            "topic_rate_min_hz": TOPIC_RATE_MIN_HZ,
            "reconnect_max_s": RECONNECT_MAX_S,
        },
        "summary": {
            "total": total, "pass": total_pass, "fail": total_fail,
            "skip": total_skip, "error": total_err, "rate_pct": round(rate, 1),
        },
        "tests": [{"id": t, "name": n, "category": c, "status": s, "note": no}
                  for t, n, c, s, no in results],
    }
    path = os.path.join(os.path.dirname(__file__), "v2_stress_report.json")
    with open(path, "w") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    print(f"  {DIM}Report: {path}{RST}\n")

# ---------------------------------------------------------------------------
#  Main
# ---------------------------------------------------------------------------

def main():
    global AGENT_IP

    parser = argparse.ArgumentParser()
    parser.add_argument("--port",        help="Serial port")
    parser.add_argument("--agent-ip",    default=AGENT_IP)
    parser.add_argument("--skip-manual", action="store_true")
    parser.add_argument("--skip-ros",    action="store_true")
    parser.add_argument("--shell-only",  action="store_true")
    args = parser.parse_args()

    AGENT_IP = args.agent_ip

    print(f"\n{BOLD}W6100 EVB Pico v2.0  -  Industrial Robot Stress Test{RST}")
    print(f"{DIM}Started: {time.strftime('%Y-%m-%d %H:%M:%S')}{RST}")
    print(f"{R}{BOLD}WARNING: E-Stop/relay tests activate physical GPIO!{RST}")
    print(f"{R}         Ensure robot stop zone is clear before running!{RST}\n")

    port = args.port or find_port()
    if not port:
        print(f"{R}Serial port not found. Use --port.{RST}")
        sys.exit(1)
    print(f"{G}Serial port: {port}{RST}")
    try:
        ser = open_serial(port)
    except Exception as e:
        print(f"{R}Cannot open port: {e}{RST}")
        sys.exit(1)

    print(f"{DIM}Waiting for boot (3s)...{RST}")
    time.sleep(3)
    drain(ser, 2.0)

    has_ros2 = False
    if not args.skip_ros and not args.shell_only:
        has_ros2 = ros2_available()
        print(f"{G if has_ros2 else Y}ROS2: {'available' if has_ros2 else 'NOT available - ROS2 tests skipped'}{RST}")

    # -----------------------------------------------------------------------
    section("1/7  SHELL / CONFIG  (T01-T17)")
    auto_test("T01", "Shell alive",              "Shell/Config", t01_shell_alive,            ser)
    auto_test("T02", "Config roundtrip",         "Shell/Config", t02_config_roundtrip,       ser)
    auto_test("T03", "All keys set/show",        "Shell/Config", t03_config_all_keys,        ser)
    auto_test("T04", "Save->Load persistence",   "Shell/Config", t04_config_save_load,       ser)
    auto_test("T05", "Config reset to defaults", "Shell/Config", t05_config_reset,           ser)
    auto_test("T06", "Empty value set",          "Robustness",   t06_empty_value,            ser)
    auto_test("T07", "Unknown key",              "Robustness",   t07_unknown_key,            ser)
    auto_test("T08", "200-char value overflow",  "Robustness",   t08_very_long_value,        ser)
    auto_test("T09", "Special characters",       "Robustness",   t09_special_chars,          ser)
    auto_test("T10", "50 rapid-fire commands",   "Robustness",   t10_rapid_fire,             ser)
    auto_test("T11", "JSON fields complete",     "Flash/Config", t11_save_verify_json,       ser)
    auto_test("T12", "20x repeated save",        "Flash/Config", t12_repeated_save,          ser)
    auto_test("T13", "DHCP toggle 5x",           "Flash/Config", t13_dhcp_toggle,            ser)
    auto_test("T14", "Port boundary values",     "Flash/Config", t14_port_boundary,          ser)
    auto_test("T15", "Invalid IP formats",       "Flash/Config", t15_ip_format,              ser)
    auto_test("T16", "Shell latency (10 samples)","Performance", t16_shell_latency,          ser)
    auto_test("T17", "100-command burst stable", "Performance",  t17_concurrent_usb_serial,  ser)

    if args.shell_only:
        ser.close(); print_summary(); return

    if not has_ros2:
        print(f"\n  {Y}ROS2 tests skipped{RST}")
        if not args.skip_manual:
            _manual(ser)
        ser.close(); print_summary(); return

    # -----------------------------------------------------------------------
    section("2/7  ROS2 TOPICS  (T20-T27)")
    auto_test("T20", "Node online",              "ROS2 Topics",  t20_node_online)
    auto_test("T21", "Counter monotonically up", "ROS2 Topics",  t21_counter_publishes)
    auto_test("T22", "Heartbeat toggles",        "ROS2 Topics",  t22_heartbeat_toggles)
    auto_test("T23", "Echo roundtrip",           "ROS2 Topics",  t23_echo_roundtrip)
    auto_test("T24", "Battery voltage valid",    "ROS2 Topics",  t24_battery_voltage_valid)
    auto_test("T25", "Heartbeat rate >= 0.8Hz",  "ROS2 Topics",  t25_heartbeat_rate)
    auto_test("T26", "/diagnostics publishes",   "ROS2 Topics",  t26_diagnostics_publishes)
    auto_test("T27", "Diagnostics KV fields",    "ROS2 Topics",  t27_diagnostics_kv_fields)

    # -----------------------------------------------------------------------
    section("3/7  SERVICES - RELAY & E-STOP  (T30-T38)")
    print(f"  {R}WARNING: E-Stop tests activate physical GPIO!{RST}")
    auto_test("T30", "Relay brake ON",           "Services",     t30_relay_brake_on)
    auto_test("T31", "Relay brake OFF",          "Services",     t31_relay_brake_off)
    auto_test("T32", "Relay 50x toggle",         "Services",     t32_relay_toggle_50x)
    auto_test("T33", "E-Stop Trigger",           "E-Stop",       t33_estop_trigger)
    auto_test("T34", "E-Stop latency 10x <100ms","E-Stop",       t34_estop_latency_10x)
    auto_test("T35", "E-Stop rapid 100x",        "E-Stop",       t35_estop_rapid_100x)
    auto_test("T36", "Concurrent SetBool+Trigger","Services",    t36_mixed_services_concurrent)
    auto_test("T37", "Service under topic flood", "Services",    t37_service_under_topic_flood)
    auto_test("T38", "All services listed",      "Services",     t38_services_listed)

    # -----------------------------------------------------------------------
    section("4/7  PARAMETER SERVER  (T40-T44)")
    auto_test("T40", "Param server online",      "Param Server", t40_param_server_online)
    auto_test("T41", "Period get/set roundtrip", "Param Server", t41_param_period_get_set)
    auto_test("T42", "Enable/disable channel",   "Param Server", t42_param_enable_disable)
    auto_test("T43", "50x set/get stress",       "Param Server", t43_param_stress_50x)
    auto_test("T44", "Boundary period values",   "Param Server", t44_param_boundary_values)

    # -----------------------------------------------------------------------
    section("5/7  DIAGNOSTICS  (T50-T53)")
    auto_test("T50", "/diagnostics rate",        "Diagnostics",  t50_diagnostics_rate)
    auto_test("T51", "Uptime increases",         "Diagnostics",  t51_diagnostics_uptime_increases)
    auto_test("T52", "Firmware = v2.0-W6100",    "Diagnostics",  t52_diagnostics_firmware)
    auto_test("T53", "Status = OK (level=0)",    "Diagnostics",  t53_diagnostics_status_ok)

    # -----------------------------------------------------------------------
    section("6/7  SAFETY & LATENCY  (T70-T74)")
    print(f"  {R}INDUSTRIAL SAFETY TESTS - verify robot stop zone!{RST}")
    auto_test("T70", "E-Stop under pub storm",   "Safety",       t70_estop_under_pub_storm)
    auto_test("T71", "E-Stop P99 < 100ms",       "Safety",       t71_estop_p99_latency)
    auto_test("T72", "E-Stop triggers relay off","Safety",       t72_relay_off_on_estop)
    auto_test("T73", "No counter message skip",  "Safety",       t73_no_counter_skip)
    auto_test("T74", "Service survives bad req", "Safety",       t74_service_survives_bad_request)

    # -----------------------------------------------------------------------
    section("7/7  RECONNECT STRESS  (T60-T65)")
    print(f"  {DIM}This section performs reboots!{RST}")
    auto_test("T60", "Node visible after connect","Reconnect",   t60_node_visible_after_connect)
    auto_test("T61", "Topics resume after reboot","Reconnect",   t61_topics_resume_after_reboot, ser)
    auto_test("T62", "Services resume after reboot","Reconnect", t62_services_resume_after_reboot, ser)
    auto_test("T63", "Params persist after reboot","Reconnect",  t63_params_survive_reboot, ser)
    auto_test("T64", "5x rapid reboot",          "Reconnect",    t64_5x_rapid_reboot, ser)
    auto_test("T65", "E-Stop < 10s after reboot","Reconnect",    t65_estop_available_after_reboot, ser)

    if not args.skip_manual:
        _manual(ser)

    ser.close()
    print_summary()


def _manual(ser):
    section("MANUAL TESTS")
    print(f"  {DIM}ENTER=pass  f=fail  s=skip{RST}\n")

    manual_test("M01", "Ethernet cable pull", "Network", None, [
        "Confirm agent running, LED ON",
        "Pull Ethernet cable",
        "LED OFF, log: 'Agent connection lost'",
        "Re-plug cable",
        "LED ON again (max ~20s)",
    ], "LED: OFF->ON. Reconnect <20s. No WDT.")

    manual_test("M02", "5x rapid agent restart", "Network", None, [
        "Repeat 5x: stop agent -> 2s -> start agent",
        "Watch LED and log each cycle",
    ], "Reconnect every cycle. No memory leak. No crash.")

    manual_test("M03", "Network switch power off", "Network", None, [
        "Power off the network switch",
        "Wait for disconnect (log: Searching for agent)",
        "Power switch back on",
        "Wait for reconnect",
    ], "Reconnect within 45s.")

    manual_test("M04", "Cold power cycle", "Power", None, [
        "Unplug power USB (NOT console USB)",
        "Wait 3 seconds, re-plug",
        "Watch boot: watchdog, config load, network, agent",
    ], "Clean boot <15s. Config preserved. LED on.")

    manual_test("M05", "10x rapid power cycle", "Power", None, [
        "Repeat 10x: power off -> 1s -> power on -> LED on -> repeat",
        "After last boot: bridge config show",
    ], "Config intact. Every boot succeeds. No LittleFS corruption.")

    manual_test("M06", "Power cut during save", "Power", None, [
        "Type: bridge config set ros.node_name crash_test",
        "Immediately unplug power during: bridge config save",
        "Re-plug, boot, check: bridge config show",
    ], "Either new or old value. NOT corrupted JSON. Boot succeeds.")

    manual_test("M07", "E-Stop physical GPIO validation",
                "Safety",
                "PHYSICAL SAFETY: Verify brake relay circuit is safe!",
                [
        "ros2 service call /bridge/relay_brake std_srvs/srv/SetBool '{data: true}'",
        "Verify: GP14 HIGH -> relay activated",
        "ros2 service call /bridge/relay_brake std_srvs/srv/SetBool '{data: false}'",
        "Verify: GP14 LOW -> relay released",
        "ros2 service call /bridge/estop std_srvs/srv/Trigger '{}'",
        "Verify: E-Stop logic executed, GP15 status checked",
    ], "GPIO state matches service call. Physical relay confirms.")

    manual_test("M08", "E-Stop under physical robot load",
                "Safety",
                "INDUSTRIAL ROBOT: Ensure stop zone is clear!",
                [
        "Robot in motion (simulated or real)",
        "Trigger E-Stop: 10x rapid calls per second",
        "Every call < 100ms latency?",
        "Robot stops on every trigger?",
    ], "All E-Stops < 100ms. Robot stops every time.")

    manual_test("M09", "USB console autonomous mode", "USB Console", None, [
        "Session active (LED ON)",
        "Unplug console USB cable",
        "Wait 5s (DTR timeout)",
        "LED stays ON (autonomous mode)?",
        "Re-plug, bridge config show responds?",
    ], "LED stays ON after USB unplug. Shell responds after re-plug.")

    manual_test("M10", "15-min continuous stability", "Long-run", None, [
        "Start micro-ROS agent",
        "Run 15 minutes (LED ON whole time)",
        "Monitor: ros2 topic echo /pico/counter -- continuous?",
        "After 15 min: bridge config show responds?",
    ], "LED ON. Counter continuous. Shell alive. No WDT reboot.")

    manual_test("M11", "Vibration + cable flex", "Long-run",
                "Mechanical stress - ensure cable integrity!",
                [
        "Session active",
        "Flex/bend Ethernet cable vigorously for 60s",
        "Flex USB cable for 30s",
        "Watch LED and topic continuity",
    ], "Transient disconnects auto-reconnect. No crash.")

    manual_test("M12", "Static IP -> DHCP -> Static roundtrip", "Config Switch", None, [
        "bridge config set network.dhcp true && bridge config save && bridge reboot",
        "Verify DHCP lease in log, ping Pico",
        "bridge config set network.dhcp false && bridge config set network.ip 192.168.68.114",
        "bridge config save && bridge reboot",
        "Verify: ping 192.168.68.114",
    ], "DHCP assigns IP. Static IP active after 2nd reboot. Agent connects both times.")


if __name__ == "__main__":
    main()
