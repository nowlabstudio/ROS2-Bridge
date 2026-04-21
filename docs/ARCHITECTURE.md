# ROS2-Bridge — repo felépítés és orientáció

> Hatókör: a teljes repó. Cél: következő session-ben gyorsan visszataláljon
> bárki ahhoz, hogy **mi hol van** és **miért**. Élő dokumentum, frissítsd
> amikor a struktúra változik.

## 1. A rendszer áttekintése

Három (későbbiekben több) Pico-alapú bridge device (**E_STOP**, **RC**, **PEDAL**)
**per-device firmware binárisokból** (`apps/<device>/`) + közös infrastruktúra-
rétegből (`common/`) + device-onkénti `devices/<DEVICE>/config.json`-ból épül.
Mindegyik device micro-ROS sessiont tart a host PC-n futó `micro-ros-agent`-tel,
UDP-n át a W6100 Ethernet MAC-en keresztül.

- **Tier 1 (ez a repó):** Zephyr firmware a RP2040 + W6100 EVB Pico boardon.
  Csatornák (GPIO, ADC, PWM-in, stb.) → ROS2 topic-ok. Build-parancs:
  `make build DEVICE={estop|rc|pedal}`.
- **Tier 2:** host oldali ROS2 Jazzy csomagok (RoboClaw node, teleop, stb.)
  a `host_ws/`-ben.

## 2. Jelenlegi könyvtárszerkezet (2026-04-21, BL-014 Fázis 2 + BL-017 után)

```
ROS2-Bridge/
├── common/                     ← minden device-nak közös Zephyr forrás
│   ├── CMakeLists.txt          target_sources(app PRIVATE …) — NEM zephyr_library,
│   │                            mert a libmicroros/littlefs include-okat az app
│   │                            default target örökli az apps/<device>/-ből
│   ├── src/
│   │   ├── main.c              init, network, micro-ROS session, main loop,
│   │   │                       executor (param_server KIVÉVE — BL-017/ERR-032)
│   │   ├── bridge/             channel_t rendszer, channel_manager, diagnostics,
│   │   │                       service_manager, param_server.{c,h} (holt kód,
│   │   │                       user_channels.c nem hívja)
│   │   ├── drivers/            drv_gpio (IRQ + debounce + setup_output),
│   │   │                       drv_adc, drv_pwm_in
│   │   ├── config/             LittleFS config loader (config.json persist,
│   │   │                       CFG_MAX_CHANNELS=12)
│   │   ├── shell/              `bridge config / reboot / bootsel` CLI
│   │   └── user/
│   │       └── user_channels.h shared header (register_if_enabled, channel_t)
│   └── include/                közös headerek
│
├── apps/                       ← per-device Zephyr app (egyenként buildelhető)
│   ├── estop/
│   │   ├── CMakeLists.txt      beemeli common/-ot + saját src/
│   │   ├── prj.conf            Zephyr Kconfig (micro-ROS, net, shell, logs)
│   │   ├── west.yml            Zephyr manifest (v4.2.2 pin + micro-ROS)
│   │   ├── boards/w5500_evb_pico.overlay
│   │   │   ├─ GP25 user_led (LED, bridge-online indikátor)
│   │   │   ├─ GP27 estop_btn (Bool pub, IRQ edge-both)
│   │   │   ├─ GP2/GP3 mode_auto + mode_follow (Int32 pub, rotary 0/1/2)
│   │   │   ├─ GP4/GP5 okgo_btn_a + okgo_btn_b (Bool pub, 2-pin AND safety)
│   │   │   └─ GP22 okgo_led (Bool sub, ROS→firmware output)
│   │   └── src/
│   │       ├── user_channels.c (regisztrálja: estop, mode, okgo_btn, okgo_led)
│   │       ├── estop.{c,h}
│   │       ├── mode.{c,h}       (BL-014 Fázis 2)
│   │       ├── okgo_btn.{c,h}   (BL-014 Fázis 2)
│   │       └── okgo_led.{c,h}   (BL-014 Fázis 2, subscribe-only)
│   ├── rc/
│   │   ├── CMakeLists.txt
│   │   ├── prj.conf            (no ADC)
│   │   ├── west.yml
│   │   ├── boards/w5500_evb_pico.overlay (GP2..GP7 PWM input + GP25 LED)
│   │   └── src/
│   │       ├── user_channels.c (csak rc_ch1..6)
│   │       └── rc.{c,h}
│   └── pedal/
│       ├── CMakeLists.txt
│       ├── prj.conf
│       ├── west.yml
│       ├── boards/w5500_evb_pico.overlay (csak GP25 LED — pedál ADC pin-ek
│       │                                  majd BL-012 körül jönnek be)
│       └── src/
│           ├── user_channels.c (csak pedal_heartbeat)
│           └── pedal.{c,h}     (Bool 1 Hz /robot/heartbeat)
│
├── modules/
│   └── w6100_driver/           W6100 SPI MACRAW driver (BL-010, Zephyr v4.2.2)
│                                — out-of-tree Zephyr modul, mindhárom app
│                                  `ZEPHYR_EXTRA_MODULES`-on át hivatkozza
│
├── devices/                    ← runtime config per device (upload_config.py)
│   ├── E_STOP/config.json      MAC, IP (10.0.10.23 prod, dhcp=false),
│   │                           channels: estop, mode, okgo_btn, okgo_led
│   ├── RC/config.json          jelenleg DEV subneten (BL-013 restore TODO),
│   │                           channels: rc_ch1..6
│   └── PEDAL/config.json       prod (10.0.10.21),
│                               channels: pedal_heartbeat (BL-016-ig orphan
│                               test_* kulcsok még benne)
│
├── host_ws/                    ← ROS2 Jazzy host workspace
│   └── src/
│       ├── basicmicro_ros2/    RoboClaw ROS2 node
│       └── basicmicro_python/  RoboClaw Python lib
│
├── tools/                      ← diagnosztika / flash / mérés
│   ├── upload_config.py        config.json → board LittleFS (serial shell)
│   ├── docker-run-ros2.sh      ROS2 Jazzy konténer (teszteléshez)
│   ├── estop_measure.py        E-stop rate + gap stat (BL-014 Fázis 1)
│   ├── rc_measure.py           RC csatorna mérés / kiosztás
│   ├── flash.sh                cross-platform UF2 flash (macOS + Linux)
│   ├── patches/                upstream patch-ek (Patch 1, 2 — BL-009)
│   └── cyclonedds.xml          DDS konfig (localhost-only peer)
│
├── docker/                     ← build image-ek (Zephyr + micro-ROS)
│   └── Dockerfile.zephyr
│
├── docs/
│   ├── ARCHITECTURE.md         ← ez a fájl
│   ├── backlog.md              aktív TODO-k, BL-### számozással
│   └── upstream_prs.md         BL-009 előkészített upstream PR-anyagok
│
├── logs/                       ← mérési CSV kimenetek (.gitignore-olva)
│
├── workspace/                  ← west build workspace (generált, .gitignore)
│   └── build/zephyr/zephyr.uf2 flash artefakt
│
├── Makefile                    `make build DEVICE=…`, flash, host-build,
│                                robot-start, workspace-init
├── docker-compose.yml          agent (UDP 8888), roboclaw, foxglove, portainer
├── CHANGELOG_DEV.md            időrendi napló
├── ERRATA.md                   lezárt + aktív ERR-ek (ERR-032 = BL-017)
├── memory.md                   aktív projekt memória (compacting-proof)
├── policy.md                   fejlesztési szabályok (§1-6b)
└── README.md
```

## 3. Build flow (BL-015 utáni)

```bash
# Device-specifikus build (pl. E_STOP)
make build DEVICE=estop
 → docker run w6100-zephyr-microros
   → west build -b w5500_evb_pico apps/estop/
     (ZEPHYR_EXTRA_MODULES → modules/w6100_driver + common/ target_sources)
     → zephyr.uf2 (≈ 863 KB, FLASH 2.57% / RAM 97.42% on estop)

# Flash (UF2 mass storage, BOOTSEL gomb vagy `bridge bootsel` shell cmd)
make flash
 → tools/flash.sh (cross-platform: macOS /Volumes/RPI-RP2, Linux /media/$USER/RPI-RP2)

# Runtime konfig feltöltés (reboot közben)
python3 tools/upload_config.py --config devices/<DEVICE>/config.json --port /dev/ttyACM0
 → bridge config set + save + reboot
```

A per-device binárisok izolálják a pin-foglalást és a kódméretet — ugyanaz a
`zephyr.uf2` NEM flash-elhető másik device-ra (GP-allokáció overlay-szinten
különbözik). A `config.json` `channels.*` mezői csak a beregisztrált csatornák
**enable/disable** vezérlésére + topic-remap-re szolgálnak, nem cross-device
swap-re.

## 4. Architektúrális invariansok

- **`devices/<DEVICE>/config.json`** — runtime, per-board MAC+IP+channels.
  A single source of truth a csatorna-paraméterekre (period_ms, enabled,
  invert_logic). **Az interaktív `ros2 param` NEM elérhető** (BL-017 /
  ERR-032): a `rclc_parameter_server` okozta executor-dispatch-corruption
  miatt a `param_server_init` hívás ki van véve a main.c-ből. Konfig-frissítés
  flow: `config.json` szerkesztés → `tools/upload_config.py` → bridge
  automatikus reboot (vagy `bridge config reload`).
- **`modules/w6100_driver/`** — out-of-tree Zephyr modul, minden app használja.
  MACRAW mode, ERR-031 után a `net_if_set_link_addr` is megy.
- **`tools/*.py`** — diagnosztika, hardver-független.
- **`host_ws/`** — teljesen külön tier, érintetlen a Pico firmware work-től.
- **`common/CMakeLists.txt`** — `target_sources(app PRIVATE …)` stílus (NEM
  `zephyr_library_named`) — ez lényeges: a libmicroros/littlefs include
  path-ok csak a default `app` target-re vannak rákötve az apps/ build-ben.

## 5. Baseline tag-ek

- **`bl-014-phase1-done`** (2026-04-20): E_STOP 20 Hz, egyetlen közös `app/`
  tree-vel. Visszaugrás: `git checkout bl-014-phase1-done`.
- **`bl-014-phase2-done`** (2026-04-21): E_STOP 4 csatorna (estop + mode +
  okgo_btn + okgo_led) mind zöld hw-teszten, BL-017 / ERR-032 lezárva,
  prod subnet (10.0.10.23) restore-olva. Következő Pico firmware feature-nek
  ez a baseline.
- Minden BL-### lezárása előtti utolsó stabil commit-ot érdemes taggelni.

## 6. Tovább olvasandó

- `policy.md` — session indítás, TODO szabály, memory.md karbantartás.
- `memory.md` — aktuális munkamenet compacting-proof state-je.
- `docs/backlog.md` — aktív BL-###-ok (BL-013 RC subnet restore, BL-016
  config cleanup, stb.).
- `ERRATA.md` — aktív és lezárt ERR-ek (ERR-030 W6100 SPI, ERR-031 MACRAW
  MAC, ERR-032 rclc_parameter_server partial handle-regisztráció).
- `CHANGELOG_DEV.md` — időrendi napló, újabb-először sorrend.
