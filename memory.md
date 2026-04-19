# memory.md — Projekt-emlékezet (élő dokumentum)

> Utolsó frissítés: 2026-04-19
> Hatóköre: ROS2-Bridge repo (W6100 EVB Pico + micro-ROS + RoboClaw stack).
> Karbantartás: minden munkamenet végén frissíteni kell; új technikai tény,
> mérés vagy döntés kerüljön ide. A `policy.md` rendelkezik erről.
> Testvér dokumentumok: `policy.md` (keretek), `docs/backlog.md` (TODO-k).

---

## 1. Projekt állapot röviden

| Terület | Állapot | Megjegyzés |
|--------|---------|-----------|
| Host stack (ROS2 Jazzy, RoboClaw C++) | **Production-ready** | `roboclaw_hardware` submodule élő, 100 Hz-en stabil Ethernet-en |
| Pico firmware (Zephyr + micro-ROS) | **Prototípus, működő kód, törött build** | `workspace/` hiányzik, west pull az aktuális környezetben nem validált |
| Docker image `w6100-zephyr-microros:latest` | **Jelen van** a gépen (27.9 GB) | `zephyrprojectrtos/ci:v0.28.8` alapú, korábban működött |
| RP2040 bridge IP-k | `10.0.10.21..23` (DHCP + statikus fallback) | `devices/*/config.json`, agent IP `10.0.10.1` |
| Robot subnet | `10.0.10.0/24` | Commit `e5fe072` (2026-04-19) migrálta, korábban `192.168.68.0/24` |
| Primary user | Eduard (kartoon.hu) / nowlab | Magyarul kér kontextust és dokumentációt |

---

## 2. Architektúra — emléktömb

### Két szint (Tier 1 / Tier 2)

- **Tier 1 — MCU (ez a törött):** W6100 EVB Pico (RP2040 + WIZnet W6100 Ethernet),
  Zephyr RTOS + micro-ROS, UDP :8888 felé. Per-board identitás egyedi
  `config.json`-nal (MAC, hostname, node_name, enabled csatornák).
- **Tier 2 — Host:** Docker stack: `agent` (micro-ROS Jazzy), `roboclaw`
  (C++ ros2_control driver + rc_teleop), `foxglove`, `ros2-shell`, opcionális
  `portainer`. `network_mode: host`, CycloneDDS `tools/cyclonedds.xml`.

### Csatorna modell (Tier 1)

- `channel_t` descriptor (flash, const) + `channel_state_t` (RAM).
- `user_register_channels()` + `register_if_enabled()` a `config.json`-hoz köt.
- IRQ-képes csatornák (E-Stop) atomikus flaget állítanak → fő loop ciklusban
  publish (<1 ms overhead az ISR-ben).
- Max 12 csatorna (`CFG_MAX_CHANNELS`).

### RC → motor hookup (Tier 2)

Pico küld `rc_ch1..6` (Float32, [-1..+1], 50 Hz stick / 20 Hz switch), plusz
`estop` (Bool, 10 Hz). `rc_teleop_node` tank → arcade konverziót végez a
`/diff_drive_controller/cmd_vel` topicra (TwistStamped, 20 Hz). CH5 ROS/RC
mode switch — azonnali átváltás, safety funkció.

---

## 3. Build-hiba gyökérok-hipotézisek (Tier 1)

Az elsődleges feladatunk a Pico firmware **reprodukálható fordítása Linuxon**.
A fejlesztő beszámolója szerint macOS-en Docker alatt egyszer működött, majd
egy újrafordításnál elveszett a környezet.

### Ami biztosan állapot (2026-04-19-kor):

- `workspace/` mappa **nincs meg** a gépen → `make workspace-init` kell.
- Docker image `w6100-zephyr-microros:latest` **létezik** (27.9 GB),
  `zephyrprojectrtos/ci:v0.28.8` alapú (`docker/Dockerfile`).
- `docker-compose.yml` Tier 2 stackje és a `host_ws/install/` **működő állapotú**
  a commit log alapján (e5fe072, f7a0fbe).
- A fő repo `main` branch-en van; `git status` tiszta.

### Legvalószínűbb gyökérok: **unpinned west manifest**

`app/west.yml`:
```yaml
projects:
  - name: zephyr
    remote: zephyrproject-rtos
    revision: main            # <— NEM pinelt, minden west update-kor új commit
    import: true
  - name: micro_ros_zephyr_module
    remote: micro-ros
    revision: jazzy           # branch, nem tag/sha
    path: modules/lib/micro_ros_zephyr_module
```

Amikor a dev először buildelte (Zephyr `v4.3.99`, micro-ROS jazzy jan–márc
’26), minden OK volt. Újrabuildkor a `west update` pull-ol egy újabb
Zephyr `main`-t (esetleg breaking change a W6100 driverben, net stackben,
watchdog API-ban, DT-ben), és a micro-ROS jazzy is drift-elhet. Ezzel az
eredeti `app/src/**` kód inkompatibilis lehet.

**Másodlagos gyanú:** `CONFIG_ETH_W6100` a Zephyr-ben viszonylag új (W5500
utódja). Ha az upstream átnevezte / kettészedte a Kconfigot vagy a DT
binding-ot (`compatible = "wiznet,w6100"`), a board overlay eltörhet.

### Ellenőrzési minimum, mielőtt kódot írnánk

Mindig a **kezünkben levő logokra** támaszkodjunk — ha a fordítás most nem fut
le, először fussunk egy `make workspace-init && make build`-ot és **a
konkrét hibaüzenetet** rögzítsük ide, ne találgassunk.

### Védelem jövőre (pinelés)

Az új `west.yml`-ben `zephyr.revision` legyen egy konkrét **tag vagy SHA**
(például `v4.0.0` vagy a bizonyítottan működő commit). A
`micro_ros_zephyr_module` szintén tag-re (`jazzy` helyett egy konkrét
`v3.x.y` tag) vagy SHA-ra mutasson. Részletek: `docs/backlog.md`.

---

## 4. Docker image és környezet

- Build parancs: `make docker-build` → `docker build -t w6100-zephyr-microros:latest docker/`.
- Base: `zephyrprojectrtos/ci:v0.28.8` (hivatalos CI image, Zephyr SDK 0.17.4,
  ARM Cortex-M0+ toolchain, west, colcon).
- A `pip3` telepíti: `rosdep`, `catkin_pkg`, `lark`, `empy`,
  `colcon-common-extensions`.
- Működés linuxon: `--device=$(FLASH_PORT)` mount kellhet flasheléshez; a
  Makefile jelenleg a macOS-es `/dev/tty.usbmodem231401` portot feltételezi —
  Linuxon tipikusan `/dev/ttyACM0..N`.
- `--platform=linux/amd64` pin nincs → Apple silicon rossz arch-ot húzhat; a
  Linux/amd64 hoston oké.

### Tárhely / filesystem gotcha

A README szerint a `workspace/` **nem lehet** Dropbox/iCloud alatt (virtiofs +
sync deadlock macOS-en). Linuxon általában oké, de **ne** tegyük NFS-re.

---

## 5. Tier 2 host build — ami működik

- `make host-build` a `ros:jazzy` imaget használja egy `docker run` snapshotban;
  telepíti a `ros2-control`, `ros2-controllers`, `xacro`,
  `robot-state-publisher` csomagokat, majd `colcon build`-olja a
  `roboclaw_hardware` és `roboclaw_tcp_adapter` csomagokat Release módban.
- Az install fájlok a `host_ws/install/` alatt vannak; az `rc_teleop_node` és
  a C++ hardware plugin innen jön.

---

## 6. Kulcsmérések és döntések (megőrzendők)

| Terület | Érték | Forrás |
|---------|-------|--------|
| RP2040 RAM használat | ~263 KB / 264 KB (97.49%) | README.md status |
| Flash használat | ~426 KB / 16 MB (2.54%) | README.md status |
| Heap (`CONFIG_HEAP_MEM_POOL_SIZE`) | 96 KB — csökkentve 128 KB-ról v2.0-ban (ERR lenne) | `prj.conf` + TECH OVERVIEW §10 |
| Watchdog timeout | 8000 ms (RP2040 max ~8388 ms) | `main.c#WDT_TIMEOUT_MS` |
| RC stick rate / switch rate | 50 Hz / 20 Hz | ONBOARDING §7b |
| cmd_vel timeout | 0.5 s | `diff_drive_controllers.yaml` |
| duty_accel_rate / decel_rate | 15000 / 30000 (0-32767 skála) | URDF xacro (session 23j) |
| duty_max_rad_s | 22.5 rad/s → 100% PWM | URDF xacro (session 23g) |
| WiFi ping RTT vs Ethernet | 4.2 ms vs 1.4 ms | session 23d, ERR-023 |
| 100 Hz ros2_control loop budget | 10 ms | `roboclaw_hardware.cpp` |
| Max 12 csatorna | `CFG_MAX_CHANNELS = 12` | README §channel |
| micro-ROS entity limits | pub 20, sub 16, srv 8 | `prj.conf` / colcon.meta |

---

## 7. Ismert, nyitott ERR-k

| ID | Leírás | Állapot |
|----|--------|---------|
| **ERR-001** | `param_server_init error: 11` — XRCE-DDS allocation vagy session timeout gyanúja | Nyitott; nem fatális, a board fut nélküle is |
| **ERR-BOOTSEL** | `bridge bootsel` nem működik, ha a firmware node_init hibaciklusban van (érvénytelen node_name: pl. `E-STOP` kötőjel miatt) | Nyitott workaround-dal; fix: shell szintű node_name validáció |

Minden más `ERR-002..024` **javítva** — lásd `ERRATA.md`.

---

## 8. Devices (per-board konfig snapshot)

| Board | `node_name` | IP | MAC | Aktív csatornák |
|-------|-------------|----|----|-----------------|
| E_STOP | `estop` | 10.0.10.23 | `0C:2F:94:30:58:33` | `estop` |
| RC | `rc` | 10.0.10.22 | `0C:2F:94:30:58:22` | `rc_ch1..6` (CH1→motor_right, CH2→motor_left, CH5→rc_mode, CH6→winch) + rc_trim |
| PEDAL | `pedal` | 10.0.10.21 | `0C:2F:94:30:58:11` | üres (csak placeholder) |

Figyelem: `E_STOP` board `node_name`-e `estop` (alsó-kis, aláhúzás nélkül), nem
`E-STOP` — a ROS2 név szabály `[a-zA-Z][a-zA-Z0-9_]*` és az `E-STOP` kötőjele
végzetes (`ERRATA_BOOTSEL.md`).

---

## 9. Parancsok, amikre gyakran szükség lesz

```bash
# Tier 1 (firmware)
make docker-build      # egyszer, vagy Dockerfile módosításkor
make workspace-init    # első alkalommal (~2 GB letöltés)
make build             # teljes, pristine build
tools/flash.sh         # flash, ha a firmware él és a 'bridge bootsel' működik
make monitor           # screen /dev/tty.usbmodem... 115200 (macOS)
python3 tools/upload_config.py --config devices/RC/config.json

# Tier 2 (host stack)
make host-build        # roboclaw_hardware + rc_teleop
make robot-start       # docker compose up -d
make robot-logs        # követés
make robot-shell       # belépés a ros2-shell konténerbe
```

Linuxon a `FLASH_PORT` jellemzően `/dev/ttyACM0` — a Makefile alapértelmezését
a `FLASH_PORT=/dev/ttyACM0 make flash` hívással lehet felülírni.

---

## 10. Nyitott kérdések / döntésre váró pontok

A részletek `docs/backlog.md`-ben. Rövid:

1. **`west.yml` pinelés** — Zephyr + micro-ROS `revision` legyen konkrét tag/SHA.
2. **Docker image pinelése a Dockerfile-ben** — `zephyrprojectrtos/ci:v0.28.8`
   már pin, ezt digestre lehet szigorítani (`@sha256:...`).
3. **Workspace cache mentés** — Docker build stage-be emelt „golden workspace”
   snapshot a jövőbeli reprodukálhatóságért (opció: `docker image save` a west
   pull után a fennmaradt állapotról).
4. **ERR-001 (param server)** — érdemi diagnosztikához `CONFIG_SYS_HEAP_RUNTIME_STATS`
   már megvan, hiányzik a runtime heap log az init körül.
5. **`bridge config set` node_name validáció** — `[-/ ]` elutasítása shell
   szinten.
6. **Pico board target vs chip név** — dokumentált, hogy a Zephyr board
   `w5500_evb_pico`, de `compatible = "wiznet,w6100"` overlay-ben. Upstream
   változások követése a pinelt commit mellé.

---

## 11. Változásnapló (memory-é, nem kódé)

- **2026-04-19** — Létrehozva a `memory.md`. Első átnézés: a `workspace/`
  hiányzik Linuxon, Docker image megvan, git tiszta, dokumentáció bő és
  naprakész. Elsődleges gyanú a build-hibára: `west.yml` `revision: main`
  + `jazzy` branch drift. Ellenőrzés akkor esedékes, amikor először futtatunk
  `make workspace-init && make build`-ot Linuxon (a tényleges hibaüzenet fog
  döntést diktálni).
