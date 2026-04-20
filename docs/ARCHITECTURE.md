# ROS2-Bridge — repo felépítés és orientáció

> Hatókör: a teljes repó. Cél: következő session-ben gyorsan visszataláljon
> bárki ahhoz, hogy **mi hol van** és **miért**. Élő dokumentum, frissítsd
> amikor a struktúra változik (különösen BL-015 restructure után).

## 1. A rendszer áttekintése

Három (későbbiekben több) Pico-alapú bridge device (**E_STOP**, **RC**, **PEDAL**)
egy közös firmware-ből + device-onkénti `config.json`-ból. Mindegyik micro-ROS
sessiont tart a host PC-n futó `micro-ros-agent`-tel, UDP-n át a W6100
Ethernet MAC-en keresztül.

- **Tier 1 (ez a repó):** Zephyr firmware a RP2040 + W6100 EVB Pico boardon.
  Csatornák (GPIO, ADC, PWM-in, stb.) → ROS2 topic-ok.
- **Tier 2:** host oldali ROS2 Jazzy csomagok (RoboClaw node, teleop, stb.)
  a `host_ws/`-ben.

## 2. Jelenlegi könyvtárszerkezet (2026-04-20, BL-014 Fázis 1 után)

```
ROS2-Bridge/
├── app/                        ← Zephyr application (egy közös build mindhárom device-hoz)
│   ├── CMakeLists.txt          source list + w6100_driver modul beemelés
│   ├── prj.conf                Zephyr Kconfig (micro-ROS, net, shell, logs)
│   ├── west.yml                Zephyr manifest (v4.2.2 pin + micro-ROS)
│   ├── boards/
│   │   └── w5500_evb_pico.overlay
│   │        — jelenleg KÖZÖS overlay minden device-nak:
│   │          GP27 estop_btn, GP2..GP7 rc_ch1..6, GP14 relay_brake,
│   │          GP25 user_led. Pin-konfliktus forrása (ld. BL-014 Fázis 2
│   │          és BL-015).
│   ├── modules/
│   │   └── w6100_driver/       W6100 SPI MACRAW driver (BL-010, Zephyr v4.2.2)
│   └── src/
│       ├── main.c              init, network, micro-ROS session, main loop
│       ├── bridge/             channel_t rendszer, diagnostics, param_server,
│       │                       service_manager — micro-ROS pub/sub absztrakció
│       ├── drivers/            drv_gpio (IRQ + debounce), drv_adc, drv_pwm_in
│       ├── config/             LittleFS config loader (config.json persist)
│       ├── shell/              `bridge config / reboot / bootsel` CLI
│       └── user/               per-device csatornák (estop, rc, test_*)
│           └── user_channels.c register_if_enabled() — config dönti, mi él
│
├── devices/                    ← runtime config per device (upload_config.py)
│   ├── E_STOP/config.json      MAC, IP, DHCP, agent, channels.estop=true
│   ├── RC/config.json          channels.rc_ch1..6=true
│   └── PEDAL/config.json       channels.test_heartbeat=true (placeholder)
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
│   └── cyclonedds.xml          DDS konfig (localhost-only peer)
│
├── docker/                     ← build image-ek (Zephyr + micro-ROS)
│   └── Dockerfile.zephyr
│
├── docs/
│   ├── ARCHITECTURE.md         ← ez a fájl
│   ├── backlog.md              aktív TODO-k, BL-### számozással
│   ├── CHANGELOG_DEV.md        fejlesztői napló (nem user-facing)
│   ├── ERRATA.md               lezárt ERR-ek
│   └── policy.md?              ld. gyökérben
│
├── logs/                       ← mérési CSV kimenetek (.gitignore-olva)
│
├── workspace/                  ← west build workspace (generált, .gitignore)
│   └── build/zephyr/zephyr.uf2 flash artefakt
│
├── Makefile                    `make build | flash | host-build | robot-start` stb.
├── docker-compose.yml          agent (UDP 8888), roboclaw, foxglove, portainer
├── memory.md                   aktív projekt memória (compacting-proof)
├── policy.md                   fejlesztési szabályok (§1-6b)
└── README.md
```

## 3. Build flow (jelenlegi)

```
make build
 → docker run w6100-zephyr-microros
   → west build -b w5500_evb_pico app/
     → zephyr.uf2 (kb. 845 KB, FLASH 2.6% / RAM 97.5%)
make flash
 → python3 -m flash_uf2 zephyr.uf2    (RPI-RP2 mass storage)
 → fizikai BOOTSEL gomb kell (bridge bootsel shell cmd nem vált módot
   — BL-014 záráskor ERR-be veendő)
python3 tools/upload_config.py --config devices/<DEVICE>/config.json
 → bridge config set + save + reboot
```

Ugyanaz a `zephyr.uf2` flash-elhető mindhárom device-ra — a viselkedést a
`config.json` `channels.*` mezői + a ROS node-név/namespace döntik el
runtime-ban (`user_channels.c` register_if_enabled).

## 4. Tervezett könyvtárszerkezet BL-015 (Repo restructure) után

Cél: per-device **build profile** — minden eszköz csak a saját kódját
tartalmazza, közös infra külön rétegben.

```
ROS2-Bridge/
├── common/                     ← minden device-nak közös
│   ├── CMakeLists.txt          lib target (w6100_bridge_common)
│   ├── src/
│   │   ├── main.c              shared entry point (hook-okkal testre szabható)
│   │   ├── bridge/             (változatlan: channel_manager stb.)
│   │   ├── drivers/            drv_gpio (mindenkinek kell)
│   │   ├── config/             LittleFS loader
│   │   └── shell/              shell_cmd
│   └── include/                shared headerek
│
├── apps/                       ← per-device Zephyr app
│   ├── estop/
│   │   ├── CMakeLists.txt      közösben lévő forrás + saját src/
│   │   ├── prj.conf            csak amire kell (no ADC, no PWM-in)
│   │   ├── boards/w5500_evb_pico.overlay
│   │   │   ├─ GP27 estop_btn
│   │   │   ├─ GP2/GP3 mode (rotary)
│   │   │   ├─ GP4/GP5 okgo (AND safety button)
│   │   │   └─ GP22 okgo_led (output)
│   │   └── src/
│   │       ├── user_channels.c (csak: estop, mode, okgo, okgo_led)
│   │       ├── estop.c/h
│   │       ├── mode.c/h        (Fázis 2 új)
│   │       ├── okgo.c/h        (Fázis 2 új)
│   │       └── okgo_led.c/h    (Fázis 2 új)
│   ├── rc/
│   │   ├── CMakeLists.txt
│   │   ├── prj.conf            (no ADC)
│   │   ├── boards/w5500_evb_pico.overlay  (GP2..GP7 PWM)
│   │   └── src/
│   │       ├── user_channels.c (csak rc_ch1..6)
│   │       └── rc.c/h
│   └── pedal/
│       ├── CMakeLists.txt
│       ├── prj.conf
│       ├── boards/w5500_evb_pico.overlay
│       └── src/
│           ├── user_channels.c
│           └── pedal.c/h        (PEDAL jelenleg test_heartbeat placeholder-t
│                                 használ — ekkor lesz saját kód)
│
├── devices/                    ← változatlan (runtime config)
├── modules/w6100_driver/       ← változatlan (közös out-of-tree modul)
├── host_ws/                    ← változatlan
├── tools/                      ← változatlan
├── docs/                       ← változatlan
└── Makefile                    DEVICE=estop|rc|pedal switch, APP_DIR=apps/$(DEVICE)
```

**Várható előnyök BL-015 után:**
- E_STOP binárisban nincs `rc.c` / `drv_pwm_in` / ADC — kb. 5-15 KB RAM
  margó (prj.conf subsystem disable miatt).
- Pin-konfliktus megszűnik (GP2-GP5 device-onként foglalható).
- Közös réteg bugfix egy helyen (BL-013-as típusú javítás automatikusan
  mindhárom device-on).

**Megtartott elemek (ne nyúlj hozzájuk BL-015 során):**
- `devices/<DEVICE>/config.json` — runtime, per-board MAC+IP+channels.
- `modules/w6100_driver/` — out-of-tree Zephyr modul, minden app használja.
- `tools/*.py` — diagnosztika, hardver-független.
- `host_ws/` — teljesen külön tier, érintetlen.

## 5. Baseline állapotok

- **`bl-014-phase1-done` git tag** (2026-04-20): az első működő, E_STOP 20 Hz-es
  állapot, egyetlen közös `app/` tree-vel. Visszaugrás: `git checkout bl-014-phase1-done`.
- Minden BL-### lezárása előtti utolsó stabil commit is taggelhető fontosság
  szerint (nem kötelező, de hasznos).

## 6. Tovább olvasandó

- `policy.md` — session indítás, TODO szabály, memory.md karbantartás.
- `memory.md` — aktuális munkamenet compacting-proof state-je.
- `docs/backlog.md` — aktív BL-###-ok (BL-014 Fázis 2 csatornák, BL-015 restructure,
  BL-013 RC subnet restore).
- `docs/ERRATA.md` — lezárt ERR-ek (ERR-030 W6100 SPI, ERR-031 MACRAW MAC).
- `docs/CHANGELOG_DEV.md` — időrendi napló.
