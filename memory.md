# memory.md — Projekt-emlékezet (élő dokumentum)

> Utolsó frissítés: 2026-04-21 (BL-014 Fázis 2 hw-teszt 100% zöld; BL-017 LEZÁRVA — okgo_led dispatch gyógyítva a param_server kivételével; subnet restore & commit hátra)
> Hatóköre: ROS2-Bridge repo (W6100 EVB Pico + micro-ROS + RoboClaw stack).
> Karbantartás: minden munkamenet végén frissíteni kell; új technikai tény,
> mérés vagy döntés kerüljön ide. A `policy.md` rendelkezik erről.
> Testvér dokumentumok: `policy.md` (keretek), `docs/backlog.md` (TODO-k).

---

## 0. Folyamatban lévő munka (compacting-proof állapot)

> Ez a szekció a legutolsó munkamenet pontos állapotát rögzíti, hogy egy
> compacting utáni visszatéréskor minden szükséges info itt legyen. Mindig
> felülírjuk az új állapottal. `policy.md §6b` szabályozza.

### Munkamenet: 2026-04-21 — BL-014 Fázis 2 + BL-017 LEZÁRVA (okgo_led dispatch gyógyítva)

**Állapot:** BL-014 Fázis 2 firmware ÉS hw-teszt 100% zöld. BL-017 (okgo_led sub
callback nem fut) lezárva a `param_server_init` eltávolításával. Hátra: subnet
restore `10.0.10.23`-ra + commit(ek) + `bl-014-phase2-done` tag.

**BL-017 gyökérok (fontos ERRATA anyag):** a `rclc_parameter_server_init_with_option`
belül bukott `RCL_RET_INVALID_ARGUMENT`-tel (`error: 11`). Bár a heap változatlan
maradt (`free=98208 alloc=0` előtt/után), a `rclc_executor_add_parameter_server_with_context`
a belső hiba ELŐTT már regisztrált 6 handle-t az executor handles[] tömbbe
`initialized=true`-ként. A spin_some loop (executor.c:1824:
`for (i=0; i<max_handles && handles[i].initialized; i++)`) így végigment a 6
törött param-service handle-en ÉS a valós okgo_led sub handle-en, de a
törött handle-okon a dispatch silent-fail volt, ami megtörte a valós sub
DATA feldolgozását is (vagy az XRCE READ_DATA request, vagy az rcl_take
szinten — pontos mechanizmus nincs bizonyítva).

**Fix:** `common/src/main.c`-ben a `param_server_init(&node, &executor)` hívás
eltávolítva, a `handle_count = sub_count + PARAM_SERVER_HANDLES + service_count()`
→ `sub_count + service_count()` (PARAM_SERVER_HANDLES 6 konstans kivéve).
User explicit döntése: a `config.json` a single source of truth a csatorna-
paraméterekhez, az interaktív `ros2 param` felület NEM szükséges. A
`common/src/bridge/param_server.{c,h}` fájlok maradnak a fában holt kódként
(nem hívjuk; BL-018 visszahozhatja ha valaha).

**Verifikáció (2026-04-21 dev subnet 192.168.68.x):** boot után az executor
`1 subs + 0 svcs = 1 handles`, nincs `param_server_init error: 11`. Publish
`ros2 topic pub /robot/okgo_led std_msgs/msg/Bool "{data: true}"` →
`<inf> okgo_led: write CB: val=1` a Pico logban → LED világít. 5 publish-ből
4 callback entry logolva (1 a capture-start előtt veszett el); LED vizuális
teszt — user megerősítés `megerősítem, a led világít`.

**Új csatornák (`apps/estop/src/`):**

**Új csatornák (`apps/estop/src/`):**

| csatorna | típus | irány | pin(ek) | period | logika |
|---|---|---|---|---|---|
| `mode` | Int32 | pub | GP2 (auto) + GP3 (follow), ACTIVE_LOW+PULL_UP | 100 ms + IRQ | GP2 aktív → 2, GP3 aktív → 1, egyik sem → 0 |
| `okgo_btn` | Bool | pub | GP4 + GP5, ACTIVE_LOW+PULL_UP | 100 ms + IRQ | safety 2-pin AND |
| `okgo_led` | Bool | sub | GP22 ACTIVE_HIGH OUTPUT | — | drv_gpio_write(val->b) |

**Új közös driver API:** `drv_gpio_setup_output(cfg)` a
`common/src/drivers/drv_gpio.{c,h}`-ban — output pin configure
(`GPIO_OUTPUT_INACTIVE`) + ready check, IRQ nélkül.

**Overlay bővítés (`apps/estop/boards/w5500_evb_pico.overlay`):** 4 új
`gpio-keys` node (`mode_auto`, `mode_follow`, `okgo_btn_a`, `okgo_btn_b`) +
új `gpio-leds` node (`okgo_led`) + DT alias-ok (`mode-auto`, `mode-follow`,
`okgo-btn-a`, `okgo-btn-b`, `okgo-led`).

**Multi-pin → egy csatorna:** `mode.c` és `okgo_btn.c` két különálló
`gpio_channel_cfg_t`-t regisztrál ugyanarra a `channel_idx`-re. A
`drv_gpio.c` ISR-je `CONTAINER_OF`-fal pin-szintű callback struct-ot lát,
mindkét pin külön `gpio_init_callback(BIT(cfg->spec.pin))`-en jön be, de
mindkettő ugyanazt az atomic flag-et állítja → main loop egy publish. Ez
a pattern feltétel nélkül működik; nem kell `drv_gpio` változás.

**Config cleanup (BL-016 előrehozott E_STOP rész):** a
`devices/E_STOP/config.json` `channels:` blokkja 13 kulcsról 4 kulcsra
csökkent (`estop`, `mode`, `okgo_btn`, `okgo_led`). Ok: `CFG_MAX_CHANNELS=12`
parser limit — 13 bejegyzésből silent-dropping lett volna. A többi device
(RC, PEDAL) config marad BL-016-nak a cleanup-ra.

**Build (2026-04-21):**

| metrika | érték | vs. BL-015 step 2 (97.45%) |
|---|---|---|
| FLASH | 430956 B (2.57%) | +~1.3 KB (3 új obj) |
| RAM | 263360 B (97.42%) | -80 B (lényegében azonos) |
| UF2 | 862720 B | +~0.5 KB |

A RAM margó nem szűkült — a 3 új statikus `gpio_channel_cfg_t` (kb. 96 B
× 5 pin = ~0.5 KB) belül tartózkodik, valamint az `apps/estop/` kódja
eleve szűkebb, mint a legacy `app/` (nincs rc.c / test_channels.c).

**Fázis 3 hw-teszt (2026-04-21, dev subnet):**

Flash megvolt, session aktív (`4 channels, 1 subscribers`), manuális input-teszt
`ros2 topic echo` alatt:

| csatorna | eredmény |
|---|---|
| `/robot/estop` (GP27 NC) | ✅ False↔True, felengedve vissza |
| `/robot/mode` (GP2/GP3 Int32) | ✅ 0/1/2 mind a 3 állás |
| `/robot/okgo_btn` (GP4+GP5 AND Bool) | ✅ AND helyes, IRQ edge-both |
| `/robot/okgo_led` (GP22 output Bool sub) | ✅ true→magas / false→alacsony, LED világít (BL-017 fix után) |

**Build BL-017 fix után:** RAM 96.62% (-0.8% a param_server 6 handle allokációjának megtakarítása miatt; FLASH marginal).

**Nyitott:**

1. `devices/E_STOP/config.json` prod subnet restore (`ip=10.0.10.23`,
   `gateway=10.0.10.1`, `agent_ip=10.0.10.1`, `dhcp=false`).
2. Commit(ek): (a) `param_server_init` call removal + executor handle_count
   csökkentés; (b) okgo_led `LOG_DBG` entry dokumentáló kommentárral; (c)
   `devices/E_STOP/config.json` prod restore.
3. `git tag bl-014-phase2-done` annotated tag a zöld állapotra.
4. ERRATA.md új entry: ERR-032 — partial param_server handle registration
   poisons executor dispatch.
5. BL-018 nyitása: ha interaktív paramserver visszakell, a
   `rclc_parameter_server_init_service`-ben érdemes debuggolni (első
   gyanúsított: `rcl_node_get_name()` + service_name concat 32-byte stack buffer).

---

### Munkamenet: 2026-04-20 — BL-014 Fázis 1 LEZÁRVA (E-stop rate tuning)

**Állapot:** Fázis 1 commit+push után. Következik Fázis 2 (új input/output
csatornák + per-device overlay).

**Eredmény (mért, `logs/estop_20260420_*_summary.csv`):**

| metrika | before (`period_ms=500`) | after (`period_ms=50`) |
|---|---|---|
| effektív rate | 2.46 Hz | **20.47 Hz** (+8.3×) |
| gap median | 476 ms | **50 ms** |
| gap max | 614 ms | **53 ms** |
| gap p99 | 608 ms | **52 ms** |
| gap std | 196 ms | **7.4 ms** |
| gap min (IRQ) | 0.77 ms | 0.77 ms |

30 s-os gombnyomásos teszt: 42 edge (21 PRESSED + 21 RELEASED) — IRQ
fast-path edge→publish 6.3…52 ms. `period_ms=50` 20 Hz = konzisztens RC
switch rate-tel (BL-011). `DEBOUNCE_MS=50` változatlan.

**Repo-állapot (commit után):**

- `app/src/user/estop.c` — `period_ms = 50` + magyar komment BL-014 refsszel.
- `tools/estop_measure.py` — új mérőeszköz (`rc_measure.py` mintájára),
  `stream` + `compare` subcommand; CSV-k `logs/` alatt (`.gitignore`-olva).
- `docs/backlog.md` — BL-014 3 fázisra bontva, Fázis 1 lezárható.
- `devices/E_STOP/config.json` — **nem lett commitolva**: dev subnetre van
  állítva (`192.168.68.203`, DHCP, agent `192.168.68.125`) csak a mérés
  idejére. Fázis 3 végén visszaáll prod-ra (`10.0.10.23`, dhcp=false),
  akkor jön commit (BL-013 mintájára).

**Nyitott ERR kandidátus (BL-014 záráskor ERR-be veendő):**

1. `bridge bootsel` shell parancs kiírja "Entering USB bootloader..." majd
   `reset_usb_boot(0,0)` hívódik, de a Pico **nem megy át BOOTSEL-be** (USB
   re-enum nem történik, ugyanaz a VID `2fe3:0100` marad). Fizikai BOOTSEL
   gombnyomás kell. Zephyr pico_bootrom integráció gyanús. Érintett:
   `app/src/shell/shell_cmd.c:157` `cmd_bootsel`.
2. PEDAL teszt-boardon `test_heartbeat: true` esetén `rclc_executor_init
   error` retry loop — 2 s-os ciklus, puffer overflow. Handle count gyanús
   (`sub_count + PARAM_SERVER_HANDLES + service_count()`). Érintett:
   `app/src/user/test_channels.c`, `app/src/main.c`.

---

### Munkamenet: 2026-04-19 — BL-010 + ERR-031 lezárva, micro-ROS end-to-end zöld

**BL-010 LEZÁRVA end-to-end** — a PEDAL Ethernet-en kommunikál, micro-ROS session felépül, `/robot/heartbeat` 1 Hz-en publikál ROS2 Jazzy alá.

Root cause lánc (időrendi):
1. **ERR-030** — W6100 chip nem válaszolt SPI-n (hiányzó reset pulzus + CHPLCKR/NETLCKR unlock). Javítás: Zephyr upstream W6100 driver (PR #101753) backport v4.2.2 alá, out-of-tree modulként: `app/modules/w6100_driver/`. A régi W5500 patch-ek (Patch 3/4/5) törölve `apply.sh`-ból.
2. **ERR-031** — MACRAW módban a Zephyr szoftveresen építi a kimenő keretet `iface->link_addr`-ból, de `w6100_set_config(MAC_ADDRESS)` csak a chip SHAR-t frissítette. Javítás: a SHAR write után `net_if_set_link_addr(ctx->iface, ctx->mac_addr, 6, NET_LINK_ETHERNET)` hívás (carrier_off állapotban hívódik, így `NET_IF_RUNNING` még nincs, nincs `-EPERM`).

Verifikáció (PEDAL, 2026-04-19):
- `ip neigh show 192.168.68.200` → `lladdr 0c:2f:94:30:58:11 REACHABLE`
- `ping -c 3 192.168.68.200` → 0% loss, ~4.3 ms RTT
- Agent log: `client_key: 0x5496464D` valid session, CREATE/STATUS cserék után DDS writer él
- `ros2 node list` → `/robot/pedal` (publisher: `/robot/heartbeat`, `/diagnostics`)
- `ros2 topic hz /robot/heartbeat` → 1.00 Hz stabil (`std_msgs/msg/Bool`)

**Előző milestonok (2026-04-19):**
- Phase B LEZÁRVA (commit `b988627`) — Linux build ZÖLD.
- BL-007 LEZÁRVA — `tools/flash.sh` + `Makefile` cross-platform.
- BL-008 LEZÁRVA — `app/config.json` migrálva `10.0.10.x` subnetre.

---

### Bizonyított build állapot

Tiszta pristine flow (`rm -rf workspace` → `make workspace-init` → `make build`) **ZÖLD**:

```
RAM:      263440 B / 264 KB (97.45%)
Wrote 864768 bytes to zephyr.uf2   # W6100 driver beépítve (+1.5 KB)
```

Artifacts helye: `workspace/build/zephyr/zephyr.uf2` és `workspace/build/zephyr/zephyr.elf`

---

### Pin-mátrix (2026-04-19 végleges)

| Komponens | Pin |
|-----------|-----|
| Docker base | `zephyrprojectrtos/ci:v0.28.8` (Zephyr SDK 0.17.4) |
| Zephyr | tag `v4.2.2` (SHA `dbb536326e611dc8e97cc6a322d379ba9cac1ab7`) |
| micro_ros_zephyr_module | SHA `87dbe3a9b9d0fa347772e971d58d123e2296281a` (jazzy branch pinelt HEAD) |
| Ethernet driver | `CONFIG_ETH_W6100` — **out-of-tree backport** (Zephyr main PR #101753 → v4.2.2); forrás `app/modules/w6100_driver/` |

Miért v4.2.2 és nem v4.3.0: `zephyr/posix/time.h` eltűnt v4.3.0-ban (micro-ROS jazzy igényli).
Miért out-of-tree W6100: a stock v4.2.2 nem tartalmaz W6100 drivert; a W5500 driver W6100-at hack-kel sem tudott felhozni (hiányzó reset pulzus + CHPLCKR/NETLCKR unlock). ERR-030 részletes root cause.

---

### Lokális patch-ek (automatizálva)

Script: `tools/patches/apply.sh` — idempotens, Makefile `apply-patches` target hívja.
**Minden `make build` előtt automatikusan lefut** (dependency a Makefile-ban).
**Minden `make workspace-init` után is lefut.**

| # | Fájl a workspace-ben | Mit javít |
|---|----------------------|-----------|
| 1 | `.../udp/microros_transports.h` | `<posix/sys/socket.h>` → `ZEPHYR_VERSION_CODE` conditional (ERR-027) |
| 2 | `.../libmicroros/libmicroros.mk` | `touch std_srvs/COLCON_IGNORE` → `rm -f ...` (ERR-028) |

---

### Következő session feladatai (PEDAL flash + end-to-end test)

**Sorrendben:**

1. ~~**flash.sh Linux adaptáció** (BL-007)~~ — **KÉSZ**

2. **PEDAL board flashelése** az új firmware-rel:
   - Ha `workspace/build/zephyr/zephyr.uf2` nem létezik: `make build` (vagy `make workspace-init` + `make build`)
   - PEDAL-t BOOTSEL módba: tápcsatlakoztatáskor BOOTSEL nyomva
   - `cp workspace/build/zephyr/zephyr.uf2 /media/$USER/RPI-RP2/`

3. **PEDAL config feltöltése** — legalább egy csatorna engedélyezése:
   - `devices/PEDAL/config.json` jelenleg **minden csatorna `false`** (blank test device)
   - Minimális teszt: keress engedélyezhető csatornát `app/src/channels/` alapján
   - `python3 tools/upload_config.py --config devices/PEDAL/config.json`
   - PEDAL IP: `10.0.10.21`

4. **Docker micro-ROS agent indítása** (Tier 2 stack):
   - `make robot-start` → agent + ros2-shell + roboclaw
   - `make robot-ps` → ellenőrzés
   - `make robot-logs` → PEDAL UDP csatlakozás `10.0.10.1:8888` porton

5. **End-to-end ROS2 topic echo**:
   - `make robot-shell` → ros2-shell konténerbe
   - `ros2 topic list` → pedal node topicjai
   - `ros2 topic echo /robot/pedal/heartbeat` (vagy ami engedélyezve van)

---

### Ismert kockázatok

- Ha `workspace/` el lett törölve: `make workspace-init` kell (~2 GB, ~15 perc)
- PEDAL BOOTSEL flasheléshez fizikai hozzáférés kell
- Topic névtér: ellenőrizd `app/src/bridge/bridge.c` namespace logikáját
- PEDAL (`10.0.10.21`) és agent (`10.0.10.1`) ugyanazon subneten kell legyenek

---

### Hibasorozat összefoglalva (Phase B)

| ERR | Gyökérok | Megoldás |
|-----|----------|---------|
| ERR-025 | `west.yml revision: main` → Zephyr v4.4.99 → SDK 1.0 required | `zephyr.revision: v4.2.2` pin |
| ERR-026 | `CONFIG_ETH_W6100` nincs v4.2.2 Kconfig-ban | Eredetileg W5500 driverrel workaround; ERR-030 után out-of-tree W6100 backport (lásd alább) |
| ERR-027 | `microros_transports.h` bare `<posix/sys/socket.h>` | Patch 1: ZEPHYR_VERSION_CODE conditional |
| ERR-028 | `libmicroros.mk:103` `touch std_srvs/COLCON_IGNORE` | Patch 2: `touch` → `rm -f` |
| ERR-030 | W6100 chip nem válaszolt SPI-n (hiányzó reset pulzus + CHPLCKR/NETLCKR unlock) | Out-of-tree W6100 driver backport: `app/modules/w6100_driver/` (PR #101753 v4.2.2-re adaptálva) |
| ERR-031 | W6100 `set_config(MAC)` csak SHAR-t frissíti, iface link_addr beragad → MACRAW src MAC rossz, host→Pico return path blokkolt | `net_if_set_link_addr` hívás hozzáadva a `w6100_set_config` MAC ágához |
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

- **2026-04-19 (v6)** — Handover snapshot. Phase B lezárva, commit `b988627`
  push-olva. §0 frissítve a következő session teendőivel: PEDAL flash
  (tools/flash.sh Linux adaptáció), PEDAL config feltöltés, Tier 2 stack
  indítás (`make robot-start`), end-to-end ROS2 topic echo.
