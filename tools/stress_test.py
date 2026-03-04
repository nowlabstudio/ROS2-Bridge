#!/usr/bin/env python3
"""
W6100 EVB Pico — Komplex Stresszteszt
======================================
Szoftveres + manuális tesztek.

Futtatás:
    python3 tools/stress_test.py [--port /dev/tty.usbmodem231401]

A szkript interaktív: a manuális lépéseknél megáll és vár a felhasználóra.
ENTER = lépés sikeres, 'f' + ENTER = lépés sikertelen, 's' + ENTER = skip.
"""

import serial
import serial.tools.list_ports
import time
import sys
import argparse
import json
import random
import string
import os

# ── ANSI colors ──────────────────────────────────────────────────────────────
R    = "\033[31m"
G    = "\033[32m"
Y    = "\033[33m"
B    = "\033[34m"
M    = "\033[35m"
C    = "\033[36m"
W    = "\033[37m"
DIM  = "\033[2m"
RST  = "\033[0m"
BOLD = "\033[1m"

COL = 60   # fixed width for test name column

# ── Result store ──────────────────────────────────────────────────────────────
results = []  # (id, name, category, status, note)

# ─────────────────────────────────────────────────────────────────────────────
#  Soros port segédek
# ─────────────────────────────────────────────────────────────────────────────

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

# ─────────────────────────────────────────────────────────────────────────────
#  Teszt infrastruktúra
# ─────────────────────────────────────────────────────────────────────────────

test_counter = [0]

def _badge(status):
    """Return a fixed-width colored status badge."""
    badges = {
        "PASS":  f"{G} PASS {RST}",
        "FAIL":  f"{R} FAIL {RST}",
        "ERROR": f"{R} ERR  {RST}",
        "SKIP":  f"{Y} SKIP {RST}",
    }
    return badges.get(status, f" {status} ")

def section(title):
    """Print a section header."""
    print(f"\n{BOLD}{B}┌{'─'*68}┐{RST}")
    print(f"{BOLD}{B}│  {title:<66}│{RST}")
    print(f"{BOLD}{B}└{'─'*68}┘{RST}")

def auto_test(tid, name, category, func, *args, **kwargs):
    """Run an automated test and print a single result line."""
    test_counter[0] += 1
    label = f"[{tid}] {name}"
    print(f"  {DIM}{label:<{COL}}{RST}", end="", flush=True)
    try:
        ok, note = func(*args, **kwargs)
        status = "PASS" if ok else "FAIL"
        note_str = f"  {DIM}{note[:40]}{RST}" if note else ""
        print(f"{_badge(status)}{note_str}")
        results.append((tid, name, category, status, note))
        return ok
    except Exception as e:
        print(f"{_badge('ERROR')}  {DIM}{str(e)[:40]}{RST}")
        results.append((tid, name, category, "ERROR", str(e)))
        return False

def manual_test(tid, name, category, instructions, expected):
    """Interactive manual test — prints steps, waits for user input."""
    test_counter[0] += 1
    print(f"\n  {BOLD}{M}[MANUAL]{RST} {BOLD}[{tid}] {name}{RST}  {DIM}({category}){RST}")
    print(f"  {'─'*66}")
    for i, step in enumerate(instructions, 1):
        print(f"  {Y}{i}.{RST} {step}")
    print(f"  {DIM}Expected:{RST} {C}{expected}{RST}")
    print(f"  {DIM}─────────────────────────────────────────────────────────{RST}")
    print(f"  {DIM}ENTER=pass  f=fail  s=skip{RST}  ", end="", flush=True)
    try:
        ans = input().strip().lower()
    except (KeyboardInterrupt, EOFError):
        ans = "s"
    if ans == "f":
        print(f"  Note (what went wrong?): ", end="", flush=True)
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

# ─────────────────────────────────────────────────────────────────────────────
#  T01–T05  Alap kommunikáció
# ─────────────────────────────────────────────────────────────────────────────

def t01_shell_alive(ser):
    ok, raw = send_cmd(ser, "bridge config show", wait=1.2, expect="agent_ip")
    return ok, raw.strip().replace("\n", " ")[:80]

def t02_config_roundtrip(ser):
    """Írj egy értéket, olvasd vissza."""
    send_cmd(ser, "bridge config set ros.node_name stress_test_node", wait=0.5)
    ok, raw = send_cmd(ser, "bridge config show", wait=1.0, expect="stress_test_node")
    # visszaállítás
    send_cmd(ser, "bridge config set ros.node_name pico_bridge", wait=0.5)
    return ok, "node_name roundtrip" + (" OK" if ok else " FAIL")

def t03_config_all_keys(ser):
    """Az összes ismert kulcs set/show."""
    keys = {
        "network.dhcp":       "false",
        "network.ip":         "192.168.68.114",
        "network.netmask":    "255.255.255.0",
        "network.gateway":    "192.168.68.1",
        "network.agent_ip":   "192.168.68.125",
        "network.agent_port": "8888",
        "ros.node_name":      "pico_bridge",
        "ros.namespace":      "/",
    }
    failed = []
    for k, v in keys.items():
        ok, raw = send_cmd(ser, f"bridge config set {k} {v}", wait=0.4, expect="OK")
        if not ok:
            failed.append(k)
    if failed:
        return False, f"Sikertelen kulcsok: {failed}"
    return True, f"Mind a {len(keys)} kulcs OK"

def t04_config_save_load(ser):
    """Mentés → show → load → show: értékek megmaradnak."""
    send_cmd(ser, "bridge config set ros.node_name saved_node", wait=0.4)
    send_cmd(ser, "bridge config save", wait=1.0)
    send_cmd(ser, "bridge config load", wait=1.0)
    ok, raw = send_cmd(ser, "bridge config show", wait=1.0, expect="saved_node")
    send_cmd(ser, "bridge config set ros.node_name pico_bridge", wait=0.4)
    send_cmd(ser, "bridge config save", wait=1.0)
    return ok, "save→load roundtrip" + (" OK" if ok else " FAIL")

def t05_config_reset(ser):
    """Reset visszaállítja a gyári értékeket."""
    send_cmd(ser, "bridge config set ros.node_name garbage_xyz", wait=0.4)
    send_cmd(ser, "bridge config reset", wait=0.4)
    ok, raw = send_cmd(ser, "bridge config show", wait=1.0, expect="pico_bridge")
    # Visszamentés: alapértékek vannak, save-eld
    send_cmd(ser, "bridge config save", wait=1.0)
    return ok, "reset → alapértékek" + (" OK" if ok else " FAIL")

# ─────────────────────────────────────────────────────────────────────────────
#  T06–T10  Határesetek / Robusztusság
# ─────────────────────────────────────────────────────────────────────────────

def t06_empty_value(ser):
    """Üres értékű set nem crashel."""
    ok, raw = send_cmd(ser, "bridge config set ros.node_name", wait=0.6)
    alive, _ = send_cmd(ser, "bridge config show", wait=0.8, expect="agent_ip")
    return alive, "üres set után shell él: " + ("igen" if alive else "NEM")

def t07_unknown_key(ser):
    """Ismeretlen kulcs nem crashel."""
    ok, raw = send_cmd(ser, "bridge config set network.nonexistent 99", wait=0.5)
    alive, _ = send_cmd(ser, "bridge config show", wait=0.8, expect="agent_ip")
    return alive, "ismeretlen kulcs után shell él: " + ("igen" if alive else "NEM")

def t08_very_long_value(ser):
    """Nagyon hosszú érték (buffer overflow teszt)."""
    long_val = "A" * 200  # CFG_STR_LEN=48, ez messze túlmutat
    send_cmd(ser, f"bridge config set ros.node_name {long_val}", wait=0.6)
    alive, raw = send_cmd(ser, "bridge config show", wait=1.0, expect="agent_ip")
    # Ellenőrzés: a node_name legfeljebb 47 karakter
    truncated = "A" * 47 not in raw  # 48 char null-term → 47 A max
    return alive, f"shell él: {alive}, értéktörés: {not truncated}"

def t09_special_chars(ser):
    """Speciális karakterek a névben."""
    specials = [
        ("ros.node_name", "node-with-dashes"),
        ("ros.namespace", "/robot/arm"),
        ("network.ip",    "10.0.0.99"),
    ]
    failed = []
    for k, v in specials:
        ok, raw = send_cmd(ser, f"bridge config set {k} {v}", wait=0.4)
        r2, raw2 = send_cmd(ser, "bridge config show", wait=0.8, expect=v)
        if not r2:
            failed.append(f"{k}={v}")
    send_cmd(ser, "bridge config set ros.node_name pico_bridge", wait=0.4)
    send_cmd(ser, "bridge config set ros.namespace /", wait=0.4)
    send_cmd(ser, "bridge config set network.ip 192.168.68.114", wait=0.4)
    return len(failed) == 0, f"sikertelen: {failed}" if failed else "mind OK"

def t10_rapid_fire(ser):
    """50 rapid commands — shell must survive."""
    for i in range(50):
        ser.write(f"bridge config set ros.node_name node_{i}\r\n".encode())
        time.sleep(0.04)
        # Drain output every 10 commands to prevent OS-level buffer stalls
        if i % 10 == 9:
            ser.read(ser.in_waiting or 1)
    drain(ser, 3.0)  # wait for shell to finish all pending output
    alive, _ = send_cmd(ser, "bridge config show", wait=3.0, expect="agent_ip")
    send_cmd(ser, "bridge config set ros.node_name pico_bridge", wait=0.4)
    return alive, f"shell alive after 50 rapid commands: {alive}"

# ─────────────────────────────────────────────────────────────────────────────
#  T11–T15  Config JSON integritás
# ─────────────────────────────────────────────────────────────────────────────

def t11_save_verify_json(ser):
    """Mentett JSON érvényes-e? (show kimenet ellenőrzés)"""
    send_cmd(ser, "bridge config save", wait=1.2)
    ok, raw = send_cmd(ser, "bridge config show", wait=1.2)
    # Alapmezők megvannak?
    fields = ["dhcp", "agent_ip", "agent_port", "node_name", "namespace"]
    missing = [f for f in fields if f not in raw]
    return len(missing) == 0, f"hiányzó mezők: {missing}" if missing else "összes mező jelen"

def t12_repeated_save(ser):
    """20x mentés egymás után — flash wear + LittleFS stabilitás."""
    for i in range(20):
        ok, _ = send_cmd(ser, "bridge config save", wait=0.8)
        if not ok:
            return False, f"mentés hiba a {i+1}. mentésnél"
    ok2, raw = send_cmd(ser, "bridge config show", wait=1.0, expect="agent_ip")
    return ok2, f"20x save után adatok intaktak: {ok2}"

def t13_dhcp_toggle(ser):
    """DHCP on/off váltás többször."""
    for i in range(5):
        send_cmd(ser, "bridge config set network.dhcp true",  wait=0.4)
        send_cmd(ser, "bridge config set network.dhcp false", wait=0.4)
    ok, raw = send_cmd(ser, "bridge config show", wait=1.0, expect="dhcp")
    return ok, "dhcp toggle stabilitás: " + ("OK" if ok else "FAIL")

def t14_port_boundary(ser):
    """Port értékek határon (0, 1, 65535, 99999)."""
    tests = [("0", False), ("1", True), ("8888", True), ("65535", True), ("99999", False)]
    results_local = []
    for port, should_accept in tests:
        send_cmd(ser, f"bridge config set network.agent_port {port}", wait=0.4)
        ok, raw = send_cmd(ser, "bridge config show", wait=0.6, expect="agent_port")
        results_local.append(f"port={port}")
    # Visszaállítás
    send_cmd(ser, "bridge config set network.agent_port 8888", wait=0.4)
    alive, _ = send_cmd(ser, "bridge config show", wait=0.8, expect="agent_ip")
    return alive, f"port boundary tesztek után él: {alive}"

def t15_ip_format(ser):
    """Érvénytelen IP formátumok."""
    bad_ips = ["999.999.999.999", "abc.def.ghi.jkl", "1.2.3", "::1"]
    for ip in bad_ips:
        send_cmd(ser, f"bridge config set network.ip {ip}", wait=0.4)
    alive, _ = send_cmd(ser, "bridge config show", wait=1.0, expect="agent_ip")
    send_cmd(ser, "bridge config set network.ip 192.168.68.114", wait=0.4)
    return alive, f"rossz IP-k után él: {alive}"

# ─────────────────────────────────────────────────────────────────────────────
#  T16–T20  Watchdog / Timing
# ─────────────────────────────────────────────────────────────────────────────

def t16_shell_latency(ser):
    """Shell válaszidő mérés (10 minta)."""
    times = []
    for _ in range(10):
        t0 = time.time()
        ok, _ = send_cmd(ser, "bridge config show", wait=2.0, expect="agent_ip")
        if ok:
            times.append(time.time() - t0)
    if not times:
        return False, "nem kapott választ"
    avg = sum(times) / len(times)
    max_t = max(times)
    return max_t < 3.0, f"avg={avg*1000:.0f}ms  max={max_t*1000:.0f}ms  ({len(times)}/10 OK)"

def t17_concurrent_usb_serial(ser):
    """USB konzol stabilitás: 100 parancs burst."""
    burst = 100
    for i in range(burst):
        ser.write(f"bridge config show\r\n".encode())
        if i % 10 == 0:
            time.sleep(0.1)
    time.sleep(3.0)
    ser.reset_input_buffer()
    alive, _ = send_cmd(ser, "bridge config show", wait=2.0, expect="agent_ip")
    return alive, f"{burst} burst után él: {alive}"

# ─────────────────────────────────────────────────────────────────────────────
#  Eredmény összefoglaló
# ─────────────────────────────────────────────────────────────────────────────

def print_summary():
    total_pass = sum(1 for *_, s, _ in results if s == "PASS")
    total_fail = sum(1 for *_, s, _ in results if s == "FAIL")
    total_skip = sum(1 for *_, s, _ in results if s == "SKIP")
    total_err  = sum(1 for *_, s, _ in results if s == "ERROR")
    total      = len(results)
    ran        = total - total_skip
    rate       = (total_pass / ran * 100) if ran > 0 else 0
    rate_color = G if rate >= 90 else (Y if rate >= 70 else R)

    print(f"\n{BOLD}{'═'*70}{RST}")
    print(f"{BOLD}  STRESS TEST RESULTS{RST}")
    print(f"{'═'*70}")

    # Group by category
    categories = {}
    for tid, name, cat, status, note in results:
        categories.setdefault(cat, []).append((tid, name, status, note))

    for cat, items in sorted(categories.items()):
        print(f"\n  {BOLD}{C}{cat}{RST}")
        print(f"  {'─'*66}")
        for tid, name, status, note in items:
            label = f"[{tid}] {name}"
            note_str = f"  {DIM}{note[:36]}{RST}" if note else ""
            print(f"  {label:<{COL}}{_badge(status)}{note_str}")

    print(f"\n{'─'*70}")
    print(
        f"  Total: {total}   "
        f"{G}PASS: {total_pass}{RST}   "
        f"{R}FAIL: {total_fail}{RST}   "
        f"{Y}SKIP: {total_skip}{RST}   "
        f"{R}ERROR: {total_err}{RST}"
    )
    print(f"  {rate_color}Success rate: {rate:.1f}%{RST}  ({total_pass}/{ran} tests run)")
    print(f"{'═'*70}\n")

    # JSON mentés
    report = {
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "summary": {
            "total": total, "pass": total_pass, "fail": total_fail,
            "skip": total_skip, "error": total_err, "rate_pct": round(rate, 1)
        },
        "tests": [
            {"id": tid, "name": name, "category": cat, "status": status, "note": note}
            for tid, name, cat, status, note in results
        ]
    }
    report_path = os.path.join(os.path.dirname(__file__), "stress_report.json")
    with open(report_path, "w") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    print(f"  {DIM}Riport mentve: {report_path}{RST}\n")

# ─────────────────────────────────────────────────────────────────────────────
#  FŐPROGRAM
# ─────────────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="W6100 EVB Pico Stresszteszt")
    parser.add_argument("--port", help="Soros port (pl. /dev/tty.usbmodem231401)")
    parser.add_argument("--skip-manual", action="store_true",
                        help="Manuális tesztek kihagyása (CI/CD mód)")
    args = parser.parse_args()

    print(f"\n{BOLD}W6100 EVB Pico  —  Stress Test Suite{RST}")
    print(f"{DIM}Started: {time.strftime('%Y-%m-%d %H:%M:%S')}{RST}")

    # ── Port keresés ──────────────────────────────────────────────────────────
    port = args.port or find_port()
    if not port:
        print(f"{R}✗ Soros port nem található. Csatlakoztasd a Picót és add meg --port-tal.{RST}")
        sys.exit(1)
    print(f"{G}✓ Port: {port}{RST}")

    try:
        ser = open_serial(port)
    except Exception as e:
        print(f"{R}✗ Port megnyitás sikertelen: {e}{RST}")
        sys.exit(1)

    # DTR + boot wait
    print(f"{DIM}Waiting for boot (3s)...{RST}")
    time.sleep(3)
    drain(ser, 2.0)

    skip = args.skip_manual

    # ══════════════════════════════════════════════════════════════════════════
    section("1 / 4   BASIC COMMUNICATION  (automated)")
    # ══════════════════════════════════════════════════════════════════════════
    auto_test("T01", "Shell alive",                 "Communication", t01_shell_alive,      ser)
    auto_test("T02", "Config roundtrip",            "Communication", t02_config_roundtrip, ser)
    auto_test("T03", "All keys set/show",           "Communication", t03_config_all_keys,  ser)
    auto_test("T04", "Save → Load persistence",     "Communication", t04_config_save_load, ser)
    auto_test("T05", "Config reset to defaults",    "Communication", t05_config_reset,     ser)

    # ══════════════════════════════════════════════════════════════════════════
    section("2 / 4   EDGE CASES & ROBUSTNESS  (automated)")
    # ══════════════════════════════════════════════════════════════════════════
    auto_test("T06", "Empty value set",             "Robustness",    t06_empty_value,      ser)
    auto_test("T07", "Unknown key",                 "Robustness",    t07_unknown_key,      ser)
    auto_test("T08", "200-char value (overflow)",   "Robustness",    t08_very_long_value,  ser)
    auto_test("T09", "Special characters",          "Robustness",    t09_special_chars,    ser)
    auto_test("T10", "50 rapid-fire commands",      "Robustness",    t10_rapid_fire,       ser)

    # ══════════════════════════════════════════════════════════════════════════
    section("3 / 4   FLASH INTEGRITY & PERFORMANCE  (automated)")
    # ══════════════════════════════════════════════════════════════════════════
    auto_test("T11", "Saved JSON fields complete",  "Flash/Config",  t11_save_verify_json,      ser)
    auto_test("T12", "20× repeated save",           "Flash/Config",  t12_repeated_save,         ser)
    auto_test("T13", "DHCP on/off toggle (5×)",     "Flash/Config",  t13_dhcp_toggle,           ser)
    auto_test("T14", "Port boundary values",        "Flash/Config",  t14_port_boundary,         ser)
    auto_test("T15", "Invalid IP formats",          "Flash/Config",  t15_ip_format,             ser)
    auto_test("T16", "Shell latency (10 samples)",  "Performance",   t16_shell_latency,         ser)
    auto_test("T17", "100-command burst stability", "Performance",   t17_concurrent_usb_serial, ser)

    if skip:
        print(f"\n  {Y}Manual tests skipped (--skip-manual){RST}")
        ser.close()
        print_summary()
        return

    # ══════════════════════════════════════════════════════════════════════════
    section("4 / 4   MANUAL TESTS  (you perform — script waits)")
    # ══════════════════════════════════════════════════════════════════════════
    print(f"  {DIM}ENTER=pass  f=fail  s=skip{RST}\n")

    # ── Network ───────────────────────────────────────────────────────────────
    manual_test("M01", "Ethernet cable pull — agent loss", "Network", [
        "Confirm agent is running  (ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888)",
        "LED should be ON (session active)",
        "Pull the Ethernet cable",
        "Wait: LED turns OFF, log shows 'Agent connection lost'",
        "Re-plug the cable",
        "Wait: LED turns ON again  (max ~20s: link UP + IP + agent ping)",
    ], "LED: off → on.  Log: disconnect → reconnect.  No WDT reboot.")

    manual_test("M02", "Agent stop and restart", "Network", [
        "Session active (LED ON)",
        "On ROS2 host: stop the agent  (Ctrl+C)",
        "Watch: LED turns off, log shows 'Searching for agent'",
        "Restart the agent",
        "Watch: LED turns on, session re-established",
    ], "Automatic reconnect without crash or WDT trigger.")

    manual_test("M03", "Rapid agent restart  (5× in a row)", "Network", [
        "Repeat 5 times: stop agent → wait 2s → start agent",
        "Watch LED and log on every cycle",
    ], "Reconnect on every cycle.  No memory leak, no crash.")

    manual_test("M04", "Hot-swap Ethernet cable", "Network", [
        "Session active",
        "Swap Ethernet cable with a different (working) cable",
        "Watch: brief disconnect → reconnect",
    ], "Reconnect within 30s.  LED turns on again.")

    manual_test("M05", "Network switch power off", "Network", [
        "Session active (LED ON)",
        "Power off the switch / router",
        "Wait for disconnect  (log: 'Searching for agent')",
        "Power the switch back on",
        "Wait for automatic reconnect",
    ], "Reconnect within 45s (including switch boot time).")

    # ── Power ─────────────────────────────────────────────────────────────────
    manual_test("M06", "Cold power cycle", "Power", [
        "Unplug the power USB  (NOT the console USB)",
        "Wait 3 seconds",
        "Re-plug",
        "Watch boot: watchdog init, config load, network, agent search",
    ], "Clean boot within 15s.  Config preserved.  LED turns on.")

    manual_test("M07", "Power cut during config save", "Power", [
        "On serial console: type  bridge config set ros.node_name crash_test",
        "Immediately unplug power during  bridge config save",
        "Re-plug, boot",
        "Check:  bridge config show — what is node_name?",
    ], "Either new or previous value — NOT corrupted JSON.  Boot succeeds.")

    manual_test("M08", "10× rapid power cycle", "Power", [
        "Repeat 10 times: power off → 1s → power on → wait for LED → repeat",
        "After last boot: bridge config show",
    ], "Config intact.  Every boot succeeds.  No LittleFS corruption.")

    manual_test("M09", "RESET button during operation", "Power", [
        "Session active (LED ON, ROS2 topic streaming)",
        "Press the RESET button",
        "Watch boot and reconnect",
    ], "Boot within 15s.  Automatic reconnect.  Config preserved.")

    manual_test("M10", "Short BOOTSEL button press", "Power", [
        "Session active",
        "Press BOOTSEL briefly  (do NOT hold)",
        "Confirm: firmware does NOT enter BOOTSEL mode on a short press",
    ], "No effect from short press.  Firmware continues running.")

    # ── USB Console ───────────────────────────────────────────────────────────
    manual_test("M11", "USB console unplug during operation", "USB Console", [
        "Session active (LED ON)",
        "Unplug the console USB cable",
        "Wait 5 seconds  (DTR timeout expires)",
        "Watch: does LED stay ON?  (autonomous mode)",
        "Re-plug console",
        "Run: bridge config show — does shell respond?",
    ], "LED stays ON after USB unplug.  Shell responds after re-plug.")

    manual_test("M12", "Boot without USB console  (autonomous mode)", "USB Console", [
        "Unplug the console USB",
        "Power cycle the board",
        "Wait 5 seconds  (DTR timeout)",
        "Watch: LED should turn ON  (agent connect in autonomous mode)",
        "Re-plug console",
    ], "Boot succeeds without DTR.  LED behavior normal.  Shell alive after re-plug.")

    manual_test("M13", "Console unplug during config save", "USB Console", [
        "Send:  bridge config save  on serial console",
        "Immediately unplug the console USB",
        "Re-plug, check:  bridge config show",
    ], "No flash corruption.  Config remains valid.")

    # ── Config Switching ──────────────────────────────────────────────────────
    manual_test("M14", "Static IP → DHCP switch + reboot", "Config Switch", [
        "bridge config set network.dhcp true",
        "bridge config save",
        "bridge reboot",
        "Watch: DHCP lease in log",
        "Verify: ping Pico at DHCP-assigned IP",
    ], "DHCP IP assigned.  Ping OK.  ROS2 agent connects.")

    manual_test("M15", "DHCP → Static IP switch + reboot", "Config Switch", [
        "bridge config set network.dhcp false",
        "bridge config set network.ip 192.168.68.114",
        "bridge config save",
        "bridge reboot",
        "Verify: ping 192.168.68.114",
    ], "Static IP active.  Ping OK.  LED turns on.")

    manual_test("M16", "Change agent IP + reboot", "Config Switch", [
        "bridge config set network.agent_ip 192.168.68.125",
        "bridge config save",
        "bridge reboot",
        "Confirm: agent reachable at new IP?",
    ], "After reboot: connects to new agent IP.")

    # ── Long-run & Environmental ──────────────────────────────────────────────
    manual_test("M17", "15-minute continuous stability", "Long-run", [
        "Start ROS2 agent",
        "Let it run for 15 minutes  (LED ON the whole time)",
        "On ROS2 host:  ros2 topic echo <topic>  — continuous data?",
        "After 15 min:  bridge config show  — shell responds?",
    ], "LED stays ON.  Topic continuous.  Shell responds.  No WDT reboot.")

    manual_test("M18", "Ethernet cable wiggling / intermittent contact", "Long-run", [
        "Session active",
        "Shake / bend the Ethernet cable for 30 seconds",
        "Simulate intermittent contact  (partially pull and re-insert)",
        "Watch log and LED",
    ], "Transient disconnect → auto reconnect.  No crash.  No WDT trigger.")

    manual_test("M19", "Temperature stress simulation", "Long-run", [
        "Session active",
        "Cover board with a small box  (heat accumulation, ~5 minutes)",
        "Confirm: continuous operation",
        "Remove cover  (sudden cooling)",
    ], "Continuous operation through temperature change.  No crash.")

    manual_test("M20", "Vibration test", "Long-run", [
        "Session active",
        "Place board on a vibrating surface  (speaker, motor) for 1 minute",
        "Watch: LED, log, topic data",
    ], "Continuous operation.  No disconnect due to vibration.")

    # ── Done ──────────────────────────────────────────────────────────────────
    ser.close()
    print_summary()

if __name__ == "__main__":
    main()
