# Fejlesztési napló — W6100 EVB Pico micro-ROS Bridge

Folyamatos haladáskövetés. Minden munkamenet változásai időrendben.

---

## 2026-03-07 — Errata javítások (ERR-001 → ERR-005)

### Kiindulási állapot

- Commit: `202bbb1` — működő firmware, ismert hibákkal
- Egyetlen board működik a hálózaton, több board nem

### Elvégzett munka

**1. Errata konszolidáció** (`948d109`)
- `ERRATA_PARAM_SERVER.md` → egységes `ERRATA.md` (ERR-001 – ERR-005)
- 50 eszközös skálázási elemzés dokumentálva
- Root cause azonosítva: hardcoded MAC `00:00:00:01:02:03` a DTS-ben

**2. ERR-005 javítás — egyedi MAC cím** (`c816b9e`)
- `CONFIG_HWINFO=y` — RP2040 flash unique ID elérhetővé tétele
- `apply_mac_address()` függvény a `main.c`-ben:
  - Ha `config.json`-ban van `"mac"` mező → azt használja (parse + validáció)
  - Ha nincs → `hwinfo_get_device_id()` → `02:xx:xx:xx:xx:xx` (LAA)
  - Fallback: DTS MAC marad (de log warning)
- `cfg_network_t` bővítve `mac[20]` mezővel
- `config.c`: load, save, set, print, to_json, defaults mind frissítve
- Boot sorrend: MAC beállítás → hostname → link wait → DHCP/static IP

**3. ERR-003 javítás — egyedi hostname** (`c816b9e`)
- `CONFIG_NET_HOSTNAME_ENABLE=y`, `CONFIG_NET_HOSTNAME="ROS_Bridge"`
- `net_hostname_set_postfix()` a `node_name`-ből
- Eredmény: pl. `ROS_Bridge_E_STOP` a routeren

**4. ERR-002 javítás — diagnostics aktuális IP** (`c816b9e`)
- `diagnostics_publish()` a Zephyr net stack-ből olvassa az IP-t
- Új 6. KeyValue: `mac` — az aktuális MAC cím
- `DIAG_KV_COUNT` 5 → 6

**5. ERR-001 kutatás — heap stats** (`c816b9e`)
- `CONFIG_SYS_HEAP_RUNTIME_STATS=y`
- Heap free/alloc/max logolás a `rclc_parameter_server_init_with_option` előtt és után
- Eredmény: következő boot-nál a logban látható lesz a heap állapot

### Érintett fájlok

| Fájl | Változás |
|------|---------|
| `app/prj.conf` | +3 Kconfig szekció (hwinfo, hostname, heap stats) |
| `app/src/config/config.h` | `mac[20]` mező |
| `app/src/config/config.c` | MAC kezelés a teljes config pipeline-ban |
| `app/src/main.c` | `apply_mac_address()` + `net_hostname_set_postfix()` |
| `app/src/bridge/diagnostics.c` | Aktuális IP + MAC a /diagnostics-ban |
| `app/src/bridge/param_server.c` | Heap stats logolás |
| `ERRATA.md` | Állapot táblázat frissítve |

### Még nem tesztelt / nyitott

- [ ] Build teszt (cross-compile RP2040-re)
- [ ] Hardveres teszt: 1 board — MAC log, hostname a routeren, /diagnostics
- [ ] Hardveres teszt: 2 board — eltérő MAC, eltérő IP, stabil kapcsolat
- [ ] ERR-001: heap stats logok kiértékelése boot után
