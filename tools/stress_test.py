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

# ── ANSI színek ──────────────────────────────────────────────────────────────
R  = "\033[31m"   # piros
G  = "\033[32m"   # zöld
Y  = "\033[33m"   # sárga
B  = "\033[34m"   # kék
M  = "\033[35m"   # magenta
C  = "\033[36m"   # cián
W  = "\033[37m"   # fehér
DIM = "\033[2m"
RST = "\033[0m"
BOLD = "\033[1m"

# ── Globális eredmény tárolás ─────────────────────────────────────────────────
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

def section(title):
    print(f"\n{BOLD}{B}{'─'*70}{RST}")
    print(f"{BOLD}{B}  {title}{RST}")
    print(f"{BOLD}{B}{'─'*70}{RST}")

def auto_test(tid, name, category, func, *args, **kwargs):
    test_counter[0] += 1
    print(f"\n{DIM}[{tid}]{RST} {BOLD}{name}{RST}  {DIM}({category}){RST}")
    try:
        ok, note = func(*args, **kwargs)
        status = "PASS" if ok else "FAIL"
        color = G if ok else R
        print(f"  {color}{'✓ PASS' if ok else '✗ FAIL'}{RST}  {DIM}{note}{RST}")
        results.append((tid, name, category, status, note))
        return ok
    except Exception as e:
        print(f"  {R}✗ ERROR{RST}  {e}")
        results.append((tid, name, category, "ERROR", str(e)))
        return False

def manual_test(tid, name, category, instructions, expected):
    """Interaktív manuális teszt — megáll és vár."""
    test_counter[0] += 1
    print(f"\n{DIM}[{tid}]{RST} {BOLD}{M}[MANUÁLIS]{RST} {BOLD}{name}{RST}  {DIM}({category}){RST}")
    print(f"\n  {Y}Lépések:{RST}")
    for i, step in enumerate(instructions, 1):
        print(f"    {i}. {step}")
    print(f"\n  {C}Elvárt eredmény:{RST} {expected}")
    print(f"\n  {DIM}ENTER = sikeres | 'f' = sikertelen | 's' = skip{RST}")
    try:
        ans = input("  > ").strip().lower()
    except (KeyboardInterrupt, EOFError):
        ans = "s"
    if ans == "f":
        note = input("  Rövid megjegyzés (mi ment rosszul?): ").strip()
        print(f"  {R}✗ FAIL{RST}  {note}")
        results.append((tid, name, category, "FAIL", note))
        return False
    elif ans == "s":
        print(f"  {Y}⊘ SKIP{RST}")
        results.append((tid, name, category, "SKIP", "kihagyva"))
        return None
    else:
        print(f"  {G}✓ PASS{RST}")
        results.append((tid, name, category, "PASS", ""))
        return True

def info(msg):
    print(f"  {DIM}ℹ  {msg}{RST}")

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
    """50 parancs gyors egymás után — shell nem fagy."""
    for i in range(50):
        ser.write(f"bridge config set ros.node_name node_{i}\r\n".encode())
        time.sleep(0.04)
    time.sleep(1.5)
    ser.reset_input_buffer()
    alive, _ = send_cmd(ser, "bridge config show", wait=1.5, expect="agent_ip")
    send_cmd(ser, "bridge config set ros.node_name pico_bridge", wait=0.4)
    return alive, f"50 gyors parancs után shell él: {alive}"

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
    print(f"\n\n{BOLD}{W}{'═'*70}{RST}")
    print(f"{BOLD}{W}  STRESSZTESZT ÖSSZEFOGLALÓ{RST}")
    print(f"{BOLD}{W}{'═'*70}{RST}\n")

    categories = {}
    for tid, name, cat, status, note in results:
        categories.setdefault(cat, []).append((tid, name, status, note))

    total_pass = sum(1 for *_, s, _ in results if s == "PASS")
    total_fail = sum(1 for *_, s, _ in results if s == "FAIL")
    total_skip = sum(1 for *_, s, _ in results if s == "SKIP")
    total_err  = sum(1 for *_, s, _ in results if s == "ERROR")
    total      = len(results)

    for cat, items in sorted(categories.items()):
        print(f"  {BOLD}{C}{cat}{RST}")
        for tid, name, status, note in items:
            if status == "PASS":
                icon, color = "✓", G
            elif status == "FAIL":
                icon, color = "✗", R
            elif status == "ERROR":
                icon, color = "!", R
            else:
                icon, color = "⊘", Y
            note_str = f"  {DIM}{note[:60]}{RST}" if note else ""
            print(f"    {color}{icon}{RST} [{tid}] {name}{note_str}")
        print()

    print(f"{'─'*70}")
    print(f"  Összesen: {total}  |  "
          f"{G}PASS: {total_pass}{RST}  "
          f"{R}FAIL: {total_fail}{RST}  "
          f"{Y}SKIP: {total_skip}{RST}  "
          f"{R}ERROR: {total_err}{RST}")
    rate = (total_pass / (total - total_skip) * 100) if (total - total_skip) > 0 else 0
    color = G if rate >= 90 else (Y if rate >= 70 else R)
    print(f"  {color}Sikerességi arány: {rate:.1f}%{RST}")
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

    print(f"\n{BOLD}{W}W6100 EVB Pico — Komplex Stresszteszt{RST}")
    print(f"{DIM}Indítás: {time.strftime('%Y-%m-%d %H:%M:%S')}{RST}\n")

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
    print(f"{DIM}Indulási idő megvárása (3s)...{RST}")
    time.sleep(3)
    drain(ser, 2.0)

    skip = args.skip_manual

    # ══════════════════════════════════════════════════════════════════════════
    section("1. ALAP KOMMUNIKÁCIÓ")
    # ══════════════════════════════════════════════════════════════════════════
    auto_test("T01", "Shell életjel",              "Kommunikáció",  t01_shell_alive,     ser)
    auto_test("T02", "Config roundtrip",            "Kommunikáció",  t02_config_roundtrip, ser)
    auto_test("T03", "Összes kulcs set/show",       "Kommunikáció",  t03_config_all_keys,  ser)
    auto_test("T04", "Save → Load perzisztencia",   "Kommunikáció",  t04_config_save_load, ser)
    auto_test("T05", "Config reset alapértékre",    "Kommunikáció",  t05_config_reset,     ser)

    # ══════════════════════════════════════════════════════════════════════════
    section("2. HATÁRESETEK ÉS ROBUSZTUSSÁG")
    # ══════════════════════════════════════════════════════════════════════════
    auto_test("T06", "Üres érték set",              "Robusztusság",  t06_empty_value,     ser)
    auto_test("T07", "Ismeretlen kulcs",            "Robusztusság",  t07_unknown_key,     ser)
    auto_test("T08", "200 karakter hosszú érték",   "Robusztusság",  t08_very_long_value, ser)
    auto_test("T09", "Speciális karakterek",        "Robusztusság",  t09_special_chars,   ser)
    auto_test("T10", "50 gyors parancs (rapid fire)","Robusztusság", t10_rapid_fire,      ser)

    # ══════════════════════════════════════════════════════════════════════════
    section("3. CONFIG / FLASH INTEGRITÁS")
    # ══════════════════════════════════════════════════════════════════════════
    auto_test("T11", "Mentett JSON mezők teljesek",  "Flash/Config", t11_save_verify_json, ser)
    auto_test("T12", "20× ismételt mentés",          "Flash/Config", t12_repeated_save,    ser)
    auto_test("T13", "DHCP on/off toggle (5×)",      "Flash/Config", t13_dhcp_toggle,      ser)
    auto_test("T14", "Port értékek határon",         "Flash/Config", t14_port_boundary,    ser)
    auto_test("T15", "Érvénytelen IP formátumok",    "Flash/Config", t15_ip_format,        ser)

    # ══════════════════════════════════════════════════════════════════════════
    section("4. TELJESÍTMÉNY / TIMING")
    # ══════════════════════════════════════════════════════════════════════════
    auto_test("T16", "Shell válaszidő (10 minta)",   "Teljesítmény", t16_shell_latency,        ser)
    auto_test("T17", "100 burst parancs stabilitás", "Teljesítmény", t17_concurrent_usb_serial, ser)

    # ══════════════════════════════════════════════════════════════════════════
    section("5. HÁLÓZATI STRESSZ — MANUÁLIS")
    # ══════════════════════════════════════════════════════════════════════════

    if not skip:
        manual_test("M01", "Ethernet kábel kihúzás — agent elvesztés",
            "Hálózat/Manuális",
            [
                "Győződj meg, hogy az agent fut (docker: ros2 run micro_ros_agent ...)",
                "Nézd a soros monitort: LED égjen, 'session aktív' legyen a logban",
                "Húzd ki az Ethernet kábelt",
                "Várd meg: LED aludjon ki, log: 'Agent kapcsolat megszakadt'",
                "Dugd vissza a kábelt",
                "Várd meg: LED visszagyúljon (max ~20s: link UP + DHCP/statikus + agent ping)",
            ],
            "LED: ki → be. Log: disconnect → reconnect. Nincs WDT reboot."
        )

        manual_test("M02", "Agent leállítás, majd újraindítás",
            "Hálózat/Manuális",
            [
                "Session legyen aktív (LED ég)",
                "A ROS2 gépen állítsd le az agentet (Ctrl+C)",
                "Figyeld: LED alszik, log: 'Agent keresése...'",
                "Indítsd újra az agentet",
                "Figyeld: LED visszagyúl, session újraindul",
            ],
            "Automatikus újracsatlakozás agent nélkül is. Nincs crash, nincs WDT."
        )

        manual_test("M03", "Gyors agent restart (5× egymás után)",
            "Hálózat/Manuális",
            [
                "5× egymás után: stop agent → 2s várakozás → start agent",
                "Minden ciklusban figyeld a LED-et és a logot",
            ],
            "Minden ciklusban reconnect. Nincs memória-szivárgás, nincs crash."
        )

        manual_test("M04", "Ethernet kábel csere futás közben",
            "Hálózat/Manuális",
            [
                "Session aktív",
                "Cseréld ki az Ethernet kábelt egy másik (de működő) kábelre",
                "Figyeld: rövid disconnect → reconnect",
            ],
            "Reconnect ≤30s. LED visszagyúl."
        )

        manual_test("M05", "Hálózati switch kikapcsolás",
            "Hálózat/Manuális",
            [
                "Session aktív (LED ég)",
                "Kapcsold ki a switcht / routert",
                "Várd meg a disconnect detektálást (log: 'Agent keresése')",
                "Kapcsold vissza a switcht",
                "Várd meg az automatikus reconnectet",
            ],
            "Reconnect ≤45s a switch boot idejével együtt."
        )

    # ══════════════════════════════════════════════════════════════════════════
    section("6. TÁPELLÁTÁS STRESSZ — MANUÁLIS")
    # ══════════════════════════════════════════════════════════════════════════

    if not skip:
        manual_test("M06", "Hideg újraindítás (power cycle)",
            "Tápellátás/Manuális",
            [
                "Húzd ki a táp USB-t (NEM a konzol USB-t)",
                "Várd meg: 3 másodperc",
                "Dugd vissza",
                "Figyeld a bootot: watchdog init, config load, network, agent keresés",
            ],
            "Normális boot ≤15s. Config megmarad flash-ben. LED végül visszagyúl."
        )

        manual_test("M07", "Power cut config save közben",
            "Tápellátás/Manuális",
            [
                "Soros konzolon: 'bridge config set ros.node_name crash_test'",
                "Azonnal húzd ki a táp USB-t (lehetőleg a 'bridge config save' alatt)",
                "Dugd vissza, boot",
                "Ellenőrizd: 'bridge config show' — mi az értéke a node_name-nek?",
            ],
            "Vagy az új érték, vagy az előző — de NEM korrupt JSON. Boot sikeres."
        )

        manual_test("M08", "10× gyors power cycle",
            "Tápellátás/Manuális",
            [
                "10× gyorsan: táp ki → 1s → táp be → boot bevárása (LED ég) → repeat",
                "Utolsó boot után: bridge config show",
            ],
            "Config intakt, boot mindig sikeres, nincs LittleFS korrupció."
        )

        manual_test("M09", "Reset gomb megnyomása futás közben",
            "Tápellátás/Manuális",
            [
                "Session aktív (LED ég, ROS2 topic aktív)",
                "Nyomd meg a RESET gombot",
                "Figyeld a bootot és a reconnectet",
            ],
            "Boot ≤15s. Automatikus reconnect. Config megmarad."
        )

        manual_test("M10", "BOOTSEL gomb véletlenszerű megnyomása",
            "Tápellátás/Manuális",
            [
                "Session aktív",
                "Nyomd meg a BOOTSEL gombot (NE tartsd nyomva!)",
                "Figyeld: a firmware NE lépjen BOOTSEL módba rövid megnyomásra",
            ],
            "Nincs hatása rövid megnyomásra. Firmware fut tovább."
        )

    # ══════════════════════════════════════════════════════════════════════════
    section("7. USB KONZOL STRESSZ — MANUÁLIS")
    # ══════════════════════════════════════════════════════════════════════════

    if not skip:
        manual_test("M11", "USB konzol lecsatlakoztatás futás közben",
            "USB/Manuális",
            [
                "Session aktív (LED ég)",
                "Húzd ki a konzol USB kábelt",
                "Várd meg: 5 másodperc (DTR timeout lejár)",
                "Figyeld: LED marad ég? (autonóm mód)",
                "Dugd vissza a konzolt",
                "bridge config show — válaszol?",
            ],
            "LED nem alszik ki USB lecsatkor. Konzol visszadugás után shell él."
        )

        manual_test("M12", "USB konzol nélküli boot (autonóm mód)",
            "USB/Manuális",
            [
                "Húzd ki a konzol USB-t",
                "Végezz power cycle-t",
                "Várd meg: 5 másodperc (DTR timeout)",
                "Figyeld: LED kell gyúljon (agent csatlakozás nélkül autonóm mode)",
                "Csatlakoztasd vissza a konzolt",
            ],
            "Boot DTR nélkül sikeres. LED viselkedés normális. Shell él visszadugás után."
        )

        manual_test("M13", "Monitor lecsatlakoztatás config mentés közben",
            "USB/Manuális",
            [
                "Indíts: 'bridge config save' (soros konzolon)",
                "Azonnal húzd ki a konzol USB-t",
                "Dugd vissza, ellenőrizd: bridge config show",
            ],
            "Nincs flash korrupció. Config valid marad."
        )

    # ══════════════════════════════════════════════════════════════════════════
    section("8. HÁLÓZATI KONFIG VÁLTÁS — MANUÁLIS")
    # ══════════════════════════════════════════════════════════════════════════

    if not skip:
        manual_test("M14", "Statikus IP → DHCP váltás + reboot",
            "Konfig/Manuális",
            [
                "bridge config set network.dhcp true",
                "bridge config save",
                "bridge reboot",
                "Figyeld: DHCP kiosztás logban",
                "Ellenőrizd: ping a Picóra (DHCP által kiosztott IP)",
            ],
            "DHCP IP kiosztva. Ping sikeres. ROS2 agent csatlakozik."
        )

        manual_test("M15", "DHCP → Statikus IP váltás + reboot",
            "Konfig/Manuális",
            [
                "bridge config set network.dhcp false",
                "bridge config set network.ip 192.168.68.114",
                "bridge config save",
                "bridge reboot",
                "Ellenőrizd: ping 192.168.68.114",
            ],
            "Statikus IP aktív. Ping sikeres. LED visszagyúl."
        )

        manual_test("M16", "Agent IP megváltoztatása futás közben",
            "Konfig/Manuális",
            [
                "bridge config set network.agent_ip 192.168.68.125",
                "bridge config save",
                "bridge reboot",
                "Figyeld: az agent az új IP-n elérhető-e?",
            ],
            "Reboot után az új agent IP-vel csatlakozik."
        )

    # ══════════════════════════════════════════════════════════════════════════
    section("9. HOSSZÚ FUTÁS — MANUÁLIS")
    # ══════════════════════════════════════════════════════════════════════════

    if not skip:
        manual_test("M17", "15 perces folyamatos futás stabilitás",
            "Hosszú futás/Manuális",
            [
                "Indítsd el a ROS2 agentet",
                "Hagyd futni 15 percig (LED égjen végig)",
                "ROS2 gépen: ros2 topic echo <topic> — folyamatos adat?",
                "Utána: bridge config show — shell válaszol?",
            ],
            "LED végig ég. Topic folyamatos. Shell válaszol. Nincs WDT reboot."
        )

        manual_test("M18", "Ethernet kábel rázás / gyenge kontaktus",
            "Hosszú futás/Manuális",
            [
                "Session aktív",
                "Rázkódtasd/hajlítsd az Ethernet kábelt 30 másodpercig",
                "Adj meg intermittáló kontaktust (részlegesen húzd ki és tedd vissza)",
                "Figyeld a logot és a LED-et",
            ],
            "Átmeneti disconnect → auto reconnect. Nincs crash. Nincs WDT trigger."
        )

        manual_test("M19", "Hőmérséklet stressz szimulálás",
            "Hosszú futás/Manuális",
            [
                "Session aktív",
                "Fedd be a boardot egy kis dobozzal (hő felhalmozódás, ~5 perc)",
                "Figyeld: folyamatos működés",
                "Vedd le a borítást (hirtelen hűlés)",
            ],
            "Folyamatos működés hőmérséklet-váltás alatt. Nincs crash."
        )

        manual_test("M20", "Vibráció teszt",
            "Hosszú futás/Manuális",
            [
                "Session aktív",
                "Tedd a boardot egy rezgő felületre (pl. hangszóró, motor) 1 percig",
                "Figyeld: LED, log, topic adat",
            ],
            "Folyamatos működés. Nincs disconnect a vibráció miatt."
        )

    # ══════════════════════════════════════════════════════════════════════════
    #  ÖSSZEFOGLALÓ
    # ══════════════════════════════════════════════════════════════════════════
    ser.close()
    print_summary()

if __name__ == "__main__":
    main()
