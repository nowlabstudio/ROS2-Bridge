#!/usr/bin/env python3
"""
upload_config.py — Bridge konfig feltöltő
==========================================
Beolvassa a config.json-t és soros parancsokon keresztül
feltölti a Picóra, majd elmenti a flash-be.

Használat:
    python3 tools/upload_config.py
    python3 tools/upload_config.py --port /dev/tty.usbmodem231401
    python3 tools/upload_config.py --config app/config.json --port /dev/tty.usbmodem231401
"""

import argparse
import json
import sys
import time
import serial
import serial.tools.list_ports

# ------------------------------------------------------------------ #
#  Argumentumok                                                        #
# ------------------------------------------------------------------ #

parser = argparse.ArgumentParser(description="Bridge konfig feltöltő")
parser.add_argument("--port",   default=None,              help="Soros port (pl. /dev/tty.usbmodem231401)")
parser.add_argument("--baud",   default=115200, type=int,  help="Baudrate (alapértelmezett: 115200)")
parser.add_argument("--config", default="app/config.json", help="config.json elérési útja")
args = parser.parse_args()


# ------------------------------------------------------------------ #
#  Port automatikus keresés                                            #
# ------------------------------------------------------------------ #

def find_pico_port():
    ports = list(serial.tools.list_ports.comports())
    for p in ports:
        if "usbmodem" in p.device or "ACM" in p.device:
            return p.device
    return None


port = args.port
if not port:
    port = find_pico_port()
    if not port:
        print("HIBA: Nem találtam Pico soros portot.")
        print("Csatlakoztasd a Picót és add meg a --port argumentumot.")
        sys.exit(1)
    print(f"Port automatikusan megtalálva: {port}")


# ------------------------------------------------------------------ #
#  JSON konfig betöltés                                                #
# ------------------------------------------------------------------ #

try:
    with open(args.config, "r") as f:
        cfg = json.load(f)
except FileNotFoundError:
    print(f"HIBA: {args.config} nem található")
    sys.exit(1)
except json.JSONDecodeError as e:
    print(f"HIBA: JSON szintaxishiba: {e}")
    sys.exit(1)

# Kulcs-érték párok kinyerése (csak a bridge shell által ismert kulcsok)
dhcp_val = cfg.get("network", {}).get("dhcp")

KNOWN_KEYS = {
    "network.dhcp":        "true" if dhcp_val is True else ("false" if dhcp_val is False else None),
    "network.ip":          cfg.get("network", {}).get("ip"),
    "network.netmask":     cfg.get("network", {}).get("netmask"),
    "network.gateway":     cfg.get("network", {}).get("gateway"),
    "network.agent_ip":    cfg.get("network", {}).get("agent_ip"),
    "network.agent_port":  str(cfg.get("network", {}).get("agent_port", "")),
    "ros.node_name":       cfg.get("ros", {}).get("node_name"),
    "ros.namespace":       cfg.get("ros", {}).get("namespace"),
}

print("\nFeltöltendő konfig:")
for k, v in KNOWN_KEYS.items():
    if v:
        print(f"  {k} = {v}")


# ------------------------------------------------------------------ #
#  Soros kapcsolat és feltöltés                                        #
# ------------------------------------------------------------------ #

def send_cmd(ser, cmd, wait=0.3):
    """Küld egy parancsot és visszaadja a választ."""
    ser.write((cmd + "\n").encode())
    time.sleep(wait)
    response = ""
    while ser.in_waiting:
        response += ser.read(ser.in_waiting).decode(errors="replace")
        time.sleep(0.05)
    return response.strip()


print(f"\nKapcsoódás: {port} @ {args.baud}...")
try:
    ser = serial.Serial(port, args.baud, timeout=2)
    ser.dtr = True
    time.sleep(1.0)
except serial.SerialException as e:
    print(f"HIBA: Nem sikerült csatlakozni: {e}")
    sys.exit(1)

# Flush
ser.reset_input_buffer()
time.sleep(0.5)

print("Konfig beállítása...")
errors = 0

for key, value in KNOWN_KEYS.items():
    if not value:
        continue
    cmd = f"bridge config set {key} {value}"
    resp = send_cmd(ser, cmd)
    if "OK" in resp:
        print(f"  ✓ {key} = {value}")
    else:
        print(f"  ✗ {key} = {value}  (válasz: {resp!r})")
        errors += 1

# Mentés
print("\nMentés flash-be...")
resp = send_cmd(ser, "bridge config save", wait=1.0)
if "elmentve" in resp or "save" in resp.lower():
    print("  ✓ Elmentve")
else:
    print(f"  ? Válasz: {resp!r}")

# Megjelenítés
print("\nEllenőrzés:")
resp = send_cmd(ser, "bridge config show", wait=0.5)
print(resp)

ser.close()

if errors:
    print(f"\n{errors} hiba történt. Ellenőrizd a kimenetet.")
    sys.exit(1)
else:
    print("\n✓ Konfig sikeresen feltöltve!")
    print("Aktiváláshoz: bridge reboot  (vagy kézzel újraindítás)")
