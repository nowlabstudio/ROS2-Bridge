# memory.md — Projekt-emlékezet (élő dokumentum)

> Utolsó frissítés: 2026-04-19
> Hatóköre: ROS2-Bridge repo (W6100 EVB Pico + micro-ROS + RoboClaw stack).
> Karbantartás: minden munkamenet végén frissíteni kell; új technikai tény,
> mérés vagy döntés kerüljön ide. A `policy.md` rendelkezik erről.
> Testvér dokumentumok: `policy.md` (keretek), `docs/backlog.md` (TODO-k).

---

## 0. Folyamatban lévő munka (compacting-proof állapot)

> Ez a szekció a legutolsó munkamenet pontos állapotát rögzíti, hogy egy
> compacting utáni visszatéréskor minden szükséges info itt legyen. Mindig
> felülírjuk az új állapottal. `policy.md §6b` szabályozza.

### Munkamenet: 2026-04-19 — Pico firmware build helyreállítása Linuxon

> Progresszív diagnosztika: minden próba újabb réteg hibát tár fel. A
> kompatibilitási lánc most már **öt** komponensből áll, és mind ötöt
> pontosan meg kellett találni.

**Eddigi eredmények ebben a session-ben:**

1. Átnéztük a teljes dokumentációt és kódbázist (docs, Makefile, west.yml,
   prj.conf, overlay, main.c, devices, Docker, compose, ERRATA, CHANGELOG).
2. Létrehoztuk: `policy.md`, `memory.md`, `docs/backlog.md` (BL-001..BL-008).
   Commit `da70069`, push-olva origin/main-re.
3. `make workspace-init` **sikeres**: Zephyr main letöltve v4.4.99-ig,
   micro_ros_zephyr_module jazzy branch letöltve.
   - Zephyr SHA: `78903a98a3cc675c7fb3b3619123328c4bfbb675` (main, VERSION 4.4.99)
   - micro_ros_zephyr_module SHA: `87dbe3a9b9d0fa347772e971d58d123e2296281a` (jazzy HEAD)
4. `make build` **sikertelen** — konkrét hiba:
   ```
   CMake Error at /workdir/zephyr/cmake/modules/FindZephyr-sdk.cmake:160 (find_package):
     Could not find a configuration file for package "Zephyr-sdk" that is
     compatible with requested version "1.0".

     The following configuration files were considered but not accepted:
       /opt/toolchains/zephyr-sdk-0.17.4/cmake/Zephyr-sdkConfig.cmake, version: 0.17.4
   ```
   Hely: `app/CMakeLists.txt:6 (find_package)` → Zephyr `FindHostTools.cmake:51`.
5. Gyökérok feltárva — **SDK 0.17 → 1.0 drift a v4.3.0 → v4.4.0 Zephyr release határon:**

   | Zephyr tag | SDK_VERSION | `find_package(Zephyr-sdk X)` | Kompat. SDK 0.17.4-gyel |
   |------------|-------------|------------------------------|-------------------------|
   | v4.1.0 | 0.17.0 | 0.16 | ✅ |
   | v4.2.2 | 0.17.4 | 0.16 | ✅ |
   | **v4.3.0** | **0.17.4** | **0.16** | ✅ **utolsó** |
   | v4.4.0 | 1.0.1 | 1.0 | ❌ |
   | main (v4.4.99) | 1.0.1 | 1.0 | ❌ |

6. User közölt **kulcsfontosságú információt**: a W6100 chip WIZnet-spec
   szerint **W5500 kompatibilis módban** is üzemel IPv4-re. Mivel csak IPv4
   UDP-t használunk (`CONFIG_NET_IPV4=y`, IPv6 nincs), a W5500 driverrel is
   megy. Ezzel a board overlay-hack (`compatible = "wiznet,w6100"`)
   megszüntethető, a stock `w5500_evb_pico` board definíció használható.
7. User kérés: **maradjunk micro-ROS jazzy branch-en** (a host stack agent +
   roboclaw image ezt várja). `micro_ros_zephyr_module.revision: jazzy` marad.
8. User kérés: **test-first megközelítés** preferált (két lépés külön commit).

### Elfogadott stratégia és végrehajtás

**Iteratív, réteg-per-réteg megközelítés.** Minden build egy új hibát tárt fel
mélyebben a toolchain-ben. A teljes kompatibilitási lánc most:

### A kompatibilitási lánc (5 elem)

| # | Komponens | Állapot | Amit meg kell oldani |
|---|-----------|---------|----------------------|
| 1 | **Zephyr SDK ↔ Zephyr** | SDK 0.17.4 (Docker-ben) kompat. v4.2.x-ig; v4.4.0+ SDK 1.0-t követel. | Pin `zephyr.revision: v4.2.2` (utolsó SDK 0.17.4 tag). |
| 2 | **micro-ROS jazzy ↔ Zephyr POSIX layout** | jazzy HEAD `rcutils` `<zephyr/posix/time.h>`-t várja. v4.2.2-ben megvan, v4.3.0-ban eltávolították. | v4.2.2 az utolsó tag, ahol a header is megvan ÉS SDK 0.17.4 is jó. |
| 3 | **W6100 Ethernet driver** | `CONFIG_ETH_W6100` csak újabb Zephyrekben (v4.3+) létezik. | W6100→W5500 kompat. mód: `CONFIG_ETH_W5500=y`, overlay override törlés. |
| 4 | **micro-ROS UDP transport header** | `microros_transports.h` régi layout-ot vár (`<posix/sys/socket.h>`), jazzy HEAD elfelejtette migrálni a serial transportokhoz hasonló conditional-lal. | Patch: `ZEPHYR_VERSION_CODE >= 3.1.0` feltételes include. |
| 5 | **std_srvs enablement** | jazzy HEAD `libmicroros.mk:103` `touch std_srvs/COLCON_IGNORE` (kizárva). A `service_manager.c` `SetBool`/`Trigger`-t használja. | Patch: `touch` → `rm -f`, hogy minden buildnél biztosan a helyes állapot jöjjön létre. |

### Pin-mátrix (2026-04-19 állapot)

| Komponens | Pin |
|-----------|-----|
| Docker base | `zephyrprojectrtos/ci:v0.28.8` (Zephyr SDK 0.17.4 bundled) |
| Zephyr | tag `v4.2.2`, SHA `dbb536326e611dc8e97cc6a322d379ba9cac1ab7` |
| micro_ros_zephyr_module | branch `jazzy`, HEAD `87dbe3a9b9d0fa347772e971d58d123e2296281a` (de két lokális patch kell) |
| Ethernet driver | `CONFIG_ETH_W5500` (W6100 W5500-kompat módban) |

### Lokális patch-ek (repro kötelező!)

1. `workspace/modules/lib/micro_ros_zephyr_module/modules/libmicroros/microros_transports/udp/microros_transports.h`
   — `<posix/sys/socket.h>` + `<posix/poll.h>` → ZEPHYR_VERSION_CODE conditional (zephyr/posix új layout).
2. `workspace/modules/lib/micro_ros_zephyr_module/modules/libmicroros/libmicroros.mk`
   — `touch std_srvs/COLCON_IGNORE` → `rm -f std_srvs/COLCON_IGNORE` (idempotens engedélyezés).

Ezeket a patcheket a `workspace/` bármikori újragenerálásakor újra kell
alkalmazni — **a `tools/patches/` alá fogjuk menteni és a Makefile-ba
bedrótozni**, hogy reprodukálható legyen (BL-009, backlog).

### App szintű warning-ok (nem hiba, de tartsuk szemmel)

- `main.c:254` `net_if_ipv4_set_gw` cast mismatch: `const struct net_in_addr *`
  vs `const struct in_addr *`. Zephyr API változás valahol v4.x közben.
- `main.c:296, 318, 355` + `channel_manager.c` + `diagnostics.c` több helyen
  `warn_unused_result` figyelmen kívül hagyva — kozmetikai.

### Következő pontos lépés

A clean rebuild fut (`/tmp/ros2bridge_build_v4.2.2_stdsrvs_clean.log`).
Várakozás értesítésre. Ha zöld:
1. UF2/ELF artifacts ellenőrzés (flash+RAM használat).
2. `tools/patches/` létrehozás a két patch-nek.
3. Makefile: `patch` target, futtatás `build` előtt.
4. `west.yml` micro_ros pin SHA-ra (`87dbe3a9`).
5. `ERRATA.md`: ERR-025 (SDK drift), ERR-026 (W5500 kompat mód),
   ERR-027 (UDP transport POSIX include bug), ERR-028 (std_srvs COLCON_IGNORE).
6. Commit + push + ONBOARDING táblázat frissítés.

Ha piros: a log tail alapján következő réteg.

### Kulcs referenciák (ha compacting utána kell visszanézni)

- Build log: `/tmp/ros2bridge_build.log`
- Workspace-init log: `/tmp/ros2bridge_workspace_init.log`
- Docker image: `w6100-zephyr-microros:latest` (27.9 GB, jelen van a gépen)
- Host: `/home/eduard/Dev/ROS2-Bridge` (`main` branch, git tiszta commit
  `da70069` óta)

### Fel nem tett kérdések (döntésre vár)

- Szint 2 lockdown (SHA pin, Dockerfile digest): T1 után tárgyalandó.
- Saját board definíció (`boards/nowlab/w6100_evb_pico/`): T2 után, opcionális;
  W5500-kompat módban nem szükséges, csak IPv6/W6100-exkluzív feature-ökhöz.
- BL-003 (node_name validáció), BL-004 (ERR-001 diag), BL-007 (flash port
  Linuxon), BL-008 (`app/config.json` template IP migráció): külön track.

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

- **2026-04-19 (v1)** — Létrehozva a `memory.md`. Első átnézés: a `workspace/`
  hiányzik Linuxon, Docker image megvan, git tiszta, dokumentáció bő és
  naprakész. Elsődleges gyanú a build-hibára: `west.yml` `revision: main`
  + `jazzy` branch drift. Ellenőrzés akkor esedékes, amikor először futtatunk
  `make workspace-init && make build`-ot Linuxon (a tényleges hibaüzenet fog
  döntést diktálni).

- **2026-04-19 (v2)** — Megtörtént a `make workspace-init && make build`.
  Workspace sikeresen letöltve (Zephyr main v4.4.99, jazzy HEAD), de a build
  **SDK inkompatibilitáson elbukott**: Zephyr main `find_package(Zephyr-sdk 1.0)`,
  a Docker image SDK 0.17.4-et szállít. A drift-pont Zephyr v4.3.0 → v4.4.0
  release között lépett fel (SDK_VERSION 0.17.4 → 1.0.1 bump). A v4.3.0 az
  utolsó stabil tag, ami kompatibilis a jelenlegi Dockerrel.

- **2026-04-19 (v3)** — User közölte: a W6100 chip W5500 kompatibilis módban
  is megy IPv4-re, így a W5500 driverrel is működik, és elhagyható az
  overlay-hack. Elfogadott stratégia: T1 (csak Zephyr pin v4.3.0-ra, W6100
  driver marad) → T2 (W5500 driverre váltás). micro-ROS jazzy branch marad.
  `policy.md`-be bekerült a compacting-policy (§6b + §6) a folytonos
  memóriamentésről.

- **2026-04-19 (v4)** — T1 alkalmazva (v4.3.0 pin), SDK-dfrift megoldva, de
  új blocker: `CONFIG_ETH_W6100` Kconfig symbol nem létezik v4.3.0-ban.
  T2 alkalmazva (CONFIG_ETH_W5500 + overlay törlés), új blocker:
  `zephyr/posix/time.h` hiány v4.3.0-ban. Átpineltük Zephyr-t **v4.2.2**-re
  (mindkét feltételt kielégíti). Új blocker: `microros_transports.h` bare
  `posix/sys/socket.h` (jazzy HEAD elfelejtett feltételes include). Patch
  alkalmazva. Új blocker: `std_srvs/srv/set_bool.h` hiány — `libmicroros.mk`
  `touch std_srvs/COLCON_IGNORE`. Patch alkalmazva (`touch` → `rm -f`).
  **Build ZÖLD:** FLASH 431 KB (2.57%), RAM 263432 B (97.45%), UF2 843 KB.
  Méretben megegyezik a v2.2 README dokumentált értékével.

- **2026-04-19 (v5)** — Stabilizálás: `tools/patches/apply.sh` script
  létrehozva (idempotens), `Makefile` `apply-patches` target a `build`
  előfeltétele, `workspace-init` is futtatja. `west.yml` mindkét
  projektet pin-eli SHA-ra (Zephyr v4.2.2 tag, micro_ros `87dbe3a9`).
  ERRATA ERR-025..028 rögzítve, backlog BL-001/BL-002 lezárva, BL-009
  új (upstream PR). Pristine rebuild (`rm -rf workspace` → `make
  workspace-init` → `make build`) fut, a reprodukálhatóság bizonyítására.
