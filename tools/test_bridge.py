#!/usr/bin/env python3
"""
test_bridge.py — Bridge shell funkció tesztelő
===============================================
Soros porton keresztül teszteli az összes 'bridge config' parancsot.

Használat:
    python3 tools/test_bridge.py
    python3 tools/test_bridge.py --port /dev/tty.usbmodem231401
"""

import argparse
import time
import sys
import serial
import serial.tools.list_ports

# ------------------------------------------------------------------ #
#  Argumentumok                                                        #
# ------------------------------------------------------------------ #

parser = argparse.ArgumentParser(description="Bridge funkció tesztelő")
parser.add_argument("--port", default=None, help="Soros port")
parser.add_argument("--baud", default=115200, type=int, help="Baudrate")
args = parser.parse_args()


# ------------------------------------------------------------------ #
#  Port keresés                                                        #
# ------------------------------------------------------------------ #

def find_pico_port():
    for p in serial.tools.list_ports.comports():
        if "usbmodem" in p.device or "ACM" in p.device:
            return p.device
    return None


port = args.port or find_pico_port()
if not port:
    print("HIBA: Nem találtam Pico soros portot.")
    sys.exit(1)
print(f"Port: {port}\n")


# ------------------------------------------------------------------ #
#  Soros kommunikáció                                                  #
# ------------------------------------------------------------------ #

try:
    ser = serial.Serial(port, args.baud, timeout=2)
    ser.dtr = True
    time.sleep(1.0)
    ser.reset_input_buffer()
except serial.SerialException as e:
    print(f"HIBA: {e}")
    sys.exit(1)


def send(cmd, wait=0.4):
    ser.write((cmd + "\n").encode())
    time.sleep(wait)
    out = ""
    while ser.in_waiting:
        out += ser.read(ser.in_waiting).decode(errors="replace")
        time.sleep(0.05)
    return out.strip()


# ------------------------------------------------------------------ #
#  Teszt keretrendszer                                                 #
# ------------------------------------------------------------------ #

passed = 0
failed = 0
total  = 0


def check(name, condition, detail=""):
    global passed, failed, total
    total += 1
    if condition:
        print(f"  ✓  {name}")
        passed += 1
    else:
        print(f"  ✗  {name}" + (f"  →  {detail}" if detail else ""))
        failed += 1


def section(title):
    print(f"\n{'='*50}")
    print(f"  {title}")
    print(f"{'='*50}")


# ------------------------------------------------------------------ #
#  Eredeti konfig mentése (visszaállításhoz)                          #
# ------------------------------------------------------------------ #

section("0. Eredeti konfig mentése")

orig = send("bridge config show")
print(orig)

def parse_field(output, key):
    for line in output.splitlines():
        if key + ":" in line:
            return line.split(":", 1)[1].strip()
    return None

orig_dhcp       = parse_field(orig, "dhcp")
orig_ip         = parse_field(orig, "ip")
orig_netmask    = parse_field(orig, "netmask")
orig_gateway    = parse_field(orig, "gateway")
orig_agent_ip   = parse_field(orig, "agent_ip")
orig_agent_port = parse_field(orig, "agent_port")
orig_node_name  = parse_field(orig, "node_name")
orig_namespace  = parse_field(orig, "namespace")

print(f"\nEredeti értékek elmentve.")


# ------------------------------------------------------------------ #
#  1. config show — minden mező jelen van                             #
# ------------------------------------------------------------------ #

section("1. config show — mezők ellenőrzése")

out = send("bridge config show")
check("dhcp mező megjelenik",      "dhcp:"       in out)
check("ip mező megjelenik",        "ip:"         in out)
check("netmask mező megjelenik",   "netmask:"    in out)
check("gateway mező megjelenik",   "gateway:"    in out)
check("agent_ip mező megjelenik",  "agent_ip:"   in out)
check("agent_port mező megjelenik","agent_port:" in out)
check("node_name mező megjelenik", "node_name:"  in out)
check("namespace mező megjelenik", "namespace:"  in out)


# ------------------------------------------------------------------ #
#  2. config set — minden kulcs beállítható                           #
# ------------------------------------------------------------------ #

section("2. config set — értékek beállítása")

TEST_VALUES = {
    "network.dhcp":        "false",
    "network.ip":          "10.0.0.99",
    "network.netmask":     "255.255.0.0",
    "network.gateway":     "10.0.0.1",
    "network.agent_ip":    "10.0.0.50",
    "network.agent_port":  "9999",
    "ros.node_name":       "test_node",
    "ros.namespace":       "/test",
}

for key, val in TEST_VALUES.items():
    resp = send(f"bridge config set {key} {val}")
    check(f"set {key} = {val}", "OK" in resp, resp)


# ------------------------------------------------------------------ #
#  3. config show — beállított értékek megjelennek                    #
# ------------------------------------------------------------------ #

section("3. config show — beállított értékek ellenőrzése")

out = send("bridge config show")

check("dhcp = false",       parse_field(out, "dhcp")       == "false")
check("ip = 10.0.0.99",     parse_field(out, "ip")         == "10.0.0.99")
check("netmask = 255.255.0.0", parse_field(out, "netmask") == "255.255.0.0")
check("gateway = 10.0.0.1", parse_field(out, "gateway")    == "10.0.0.1")
check("agent_ip = 10.0.0.50", parse_field(out, "agent_ip") == "10.0.0.50")
check("agent_port = 9999",  parse_field(out, "agent_port") == "9999")
check("node_name = test_node", parse_field(out, "node_name") == "test_node")
check("namespace = /test",  parse_field(out, "namespace")  == "/test")


# ------------------------------------------------------------------ #
#  4. network.dhcp = true                                             #
# ------------------------------------------------------------------ #

section("4. config set network.dhcp = true")

resp = send("bridge config set network.dhcp true")
check("set dhcp = true", "OK" in resp, resp)

out = send("bridge config show")
check("dhcp = true megjelenik", parse_field(out, "dhcp") == "true")


# ------------------------------------------------------------------ #
#  5. Ismeretlen kulcs → hibaüzenet                                   #
# ------------------------------------------------------------------ #

section("5. Ismeretlen kulcs → hibaüzenet")

resp = send("bridge config set network.xyz 123")
check("ismeretlen kulcs hibát ad", "Ismeretlen" in resp or "error" in resp.lower() or "Unknown" in resp)

resp = send("bridge config set ros.fake value")
check("ismeretlen ros kulcs hibát ad", "Ismeretlen" in resp or "error" in resp.lower() or "Unknown" in resp)


# ------------------------------------------------------------------ #
#  6. config save — mentés                                            #
# ------------------------------------------------------------------ #

section("6. config save — flash-be mentés")

resp = send("bridge config save", wait=1.0)
check("save sikeres", "elmentve" in resp.lower() or "saved" in resp.lower(), resp)


# ------------------------------------------------------------------ #
#  7. config load — visszatöltés                                      #
# ------------------------------------------------------------------ #

section("7. config load — flash-ből visszatöltés")

# Módosítsuk az agent_port-ot, majd load-dal állítsuk vissza
send("bridge config set network.agent_port 1111")
out_before = send("bridge config show")
check("módosítás előtt agent_port = 1111", parse_field(out_before, "agent_port") == "1111")

resp = send("bridge config load", wait=0.5)
check("load sikeres", "tve" in resp or "load" in resp.lower(), resp)

out_after = send("bridge config show")
check("load után agent_port visszaállt 9999-re", parse_field(out_after, "agent_port") == "9999")


# ------------------------------------------------------------------ #
#  8. config reset — gyári alapértékek                                #
# ------------------------------------------------------------------ #

section("8. config reset — gyári alapértékek")

resp = send("bridge config reset")
check("reset válasz megérkezett", len(resp) > 0, resp)

out = send("bridge config show")
check("reset után dhcp = false",               parse_field(out, "dhcp")       == "false")
check("reset után ip = 192.168.68.114",        parse_field(out, "ip")         == "192.168.68.114")
check("reset után agent_ip = 192.168.68.125",  parse_field(out, "agent_ip")   == "192.168.68.125")
check("reset után agent_port = 8888",          parse_field(out, "agent_port") == "8888")
check("reset után node_name = pico_bridge",    parse_field(out, "node_name")  == "pico_bridge")


# ------------------------------------------------------------------ #
#  9. Eredeti konfig visszaállítása                                   #
# ------------------------------------------------------------------ #

section("9. Eredeti konfig visszaállítása")

restore = {
    "network.dhcp":       orig_dhcp,
    "network.ip":         orig_ip,
    "network.netmask":    orig_netmask,
    "network.gateway":    orig_gateway,
    "network.agent_ip":   orig_agent_ip,
    "network.agent_port": orig_agent_port,
    "ros.node_name":      orig_node_name,
    "ros.namespace":      orig_namespace,
}

for key, val in restore.items():
    if val:
        send(f"bridge config set {key} {val}")

resp = send("bridge config save", wait=1.0)
check("eredeti konfig elmentve", "elmentve" in resp.lower() or "saved" in resp.lower())

out = send("bridge config show")
print("\nVisszaállított konfig:")
print(out)


# ------------------------------------------------------------------ #
#  Összesítés                                                          #
# ------------------------------------------------------------------ #

ser.close()

print(f"\n{'='*50}")
print(f"  EREDMÉNY: {passed}/{total} teszt sikeres", end="")
if failed:
    print(f"  ({failed} HIBA)")
else:
    print("  ✓ MIND OK")
print(f"{'='*50}\n")

sys.exit(0 if failed == 0 else 1)
