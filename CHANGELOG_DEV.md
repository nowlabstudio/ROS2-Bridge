# Fejlesztési napló — W6100 EVB Pico micro-ROS Bridge

Folyamatos haladáskövetés. Minden munkamenet változásai időrendben.

---

## 2026-03-08 — v2.1: Firmware javítások, RC input, config-driven channels

### Kiindulási állapot

- Commit: `50d7f9b` (közelítőleg) — v2.0 firmware, 3 board flashelve, de egyik sem csatlakozik az agentre

### Elvégzett munka

**1. ERR-007 javítás — rclc_support_init dirty struct** (`8a30801`)

A reconnect loop hosszú ideje meglévő bugja: ha az első `rclc_support_init` sikertelen volt, a `support`/`node`/`executor` structs részlegesen inicializált állapotban maradtak. Minden következő retry ugyanezt a dirty struct-ot kapta, ami végtelen hibás inicializációhoz vezetett.

Javítás: `memset(&support, 0, ...)` + `memset(&node, 0, ...)` + `memset(&executor, 0, ...)` hozzáadva a `ros_session_init()` elejére.

**2. ERR-008 javítás — docker-run-agent-udp.sh** (`8a30801`)

A script bash-t indított a micro-ROS agent helyett, `$SCRIPT_DIR` nem volt definiálva.
Javítás: script teljes újraírása, helyes `udp4 -p "$PORT" -v6` paranccsal.

**3. cyclonedds.xml javítás** (`8a30801`)

- `NetworkInterfaceAddress`: `eth0` → `auto` (nem kellett hardcoded interface)
- Stale `<Peer address="192.168.68.201"/>` eltávolítva

**4. start-eth.sh javítás** (`8a30801`)

Robusztus `docker ps` várakozási ciklus hozzáadva (30× 1s), hogy a script csak akkor folytasson, ha az agent container ténylegesen fut. Frissített echo üzenetek a jelenlegi topicokkal.

**5. docker-run-ros2.sh frissítés** (`8a30801`)

Help szöveg és quick-reference parancsok frissítve a jelenlegi `/robot` namespace és topicok szerint.

**6. Memória optimalizáció** (`8a30801`)

- `config.c`: két külön `static char buf[2048]` helyett egy közös `static char cfg_io_buf[1536]` — ~500 byte BSS megtakarítás
- `config.h`: `CFG_MAX_CHANNELS` 16 → 12 — ~80 byte BSS megtakarítás

**7. ERR-009 javítás — DTR wait** (korábbi munkamenet)

500ms non-blocking poll váltotta fel a végtelen blokkoló DTR ciklust. A board USB serial nélkül is elindul.

**8. ERR-006 javítás — dupla namespace** (korábbi munkamenet)

`estop.c` topic_pub javítva: `"robot/estop"` → `"estop"`. A namespace-t a config adja, nem a topic stringbe kell beírni.

**9. ERR-010 javítás — topic collision, config-driven channels** (korábbi munkamenet)

- `user_channels.c`: `register_if_enabled()` wrapper — minden channel regisztrálás előtt ellenőrzi a config.json-t
- `config.h/.c`: `cfg_channel_entry_t` (name, enabled, topic override), `config_channel_enabled()`, `config_channel_topic()` API
- `channel_manager.c`: `config_channel_topic()` alapján felülírja a `topic_pub`/`topic_sub` értéket

**10. GPIO debounce — ERR-010 kiegészítés** (korábbi munkamenet)

- `drv_gpio.h`: `last_irq_ms` mező a `gpio_channel_cfg_t` structban
- `drv_gpio.c`: `gpio_isr_handler` 50ms DEBOUNCE_MS szűrővel (`k_uptime_get()`)

**11. RC PWM input driver** (korábbi munkamenet)

- `app/src/drivers/drv_pwm_in.h/.c`: pulse-width mérés `k_cycle_get_32()`-vel, GPIO IRQ rising/falling edge-en
- `app/src/user/rc.h/.c`: 6 csatorna (rc_ch1–rc_ch6), normalizáció `g_config.rc_trim` alapján
- `app/boards/w5500_evb_pico.overlay`: `rc_inputs` node, GP2–GP7 aliasok
- `app/CMakeLists.txt`: `drv_pwm_in.c` és `rc.c` hozzáadva

**12. RC trim konfiguráció** (korábbi munkamenet)

- `config.h`: `cfg_rc_trim_ch_t` (min/center/max) és `cfg_rc_trim_t` (6 channel + deadzone) structs
- `config.c`: `parse_rc_trim()`, `config_to_json()` frissítve, `config_reset_defaults()` alapértékekkel
- `tools/upload_config.py`: rc_trim szekció feltöltése dotted key-value párokkal

**13. Per-board config.json fájlok** (korábbi munkamenet)

- `devices/E_STOP/config.json`: csak `estop: true`, minden más false
- `devices/PEDAL/config.json`: saját konfig
- `devices/RC/config.json`: rc_ch1–6 enabled topic override-okkal, rc_trim section

### Érintett fájlok

| Fájl | Változás |
|------|---------|
| `app/src/main.c` | memset fix, DTR 500ms |
| `app/src/config/config.h` | channel entries, rc_trim structs, CFG_MAX_CHANNELS=12 |
| `app/src/config/config.c` | channel/rc_trim parse+save, shared cfg_io_buf[1536] |
| `app/src/bridge/channel_manager.c` | topic override config-ból |
| `app/src/user/user_channels.c` | register_if_enabled wrapper |
| `app/src/user/estop.c` | topic_pub "robot/estop" → "estop" |
| `app/src/user/rc.h/.c` | Új — RC csatornák, normalizáció |
| `app/src/drivers/drv_gpio.c/.h` | 50ms debounce |
| `app/src/drivers/drv_pwm_in.h/.c` | Új — PWM pulse-width driver |
| `app/boards/w5500_evb_pico.overlay` | rc_inputs node, GP2-7 aliasok |
| `app/CMakeLists.txt` | rc.c, drv_pwm_in.c hozzáadva |
| `tools/docker-run-agent-udp.sh` | Teljes újraírás |
| `tools/cyclonedds.xml` | auto interface, stale peer eltávolítva |
| `tools/start-eth.sh` | Robusztus agent wait loop |
| `tools/docker-run-ros2.sh` | Frissített help szöveg |
| `devices/E_STOP/config.json` | Per-board channel config |
| `devices/RC/config.json` | RC csatornák + rc_trim |

### Build és flash

- Build sikeres: `853504 bytes` UF2, RAM **97.49%**
- Commit: `8a30801` — 8 fájl, 67 sor hozzáadva, 86 törölve
- Flash: mindhárom board BOOTSEL módból egyszerre flashelve
- Teszt: mindhárom board csatlakozik az agentre ✅

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

### Build és flash

- `CONFIG_NET_HOSTNAME_MAX_LEN=64` → **build hiba**: Kconfig range [1, 63] — javítva 63-ra
- Build sikeres: `843264 bytes` UF2, RAM 97.09%
- Flash: mind 3 eszköz BOOTSEL módból flashelve

### Még nem tesztelt / nyitott

- [ ] Hardveres teszt: 1 board — MAC log, hostname a routeren, /diagnostics
- [ ] Hardveres teszt: 2+ board — eltérő MAC, eltérő IP, stabil kapcsolat
- [ ] ERR-001: heap stats logok kiértékelése boot után
