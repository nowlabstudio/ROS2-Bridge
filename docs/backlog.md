# docs/backlog.md — Nyitott feladatok és TODO-k

> Utolsó frissítés: 2026-04-20 (BL-011 RC EMA tuning lezárva; BL-012 GPIO placeholders, BL-013 RC subnet restore nyitva)
> Hatókör: a teljes ROS2-Bridge repo.
> Szabály (`policy.md#2`): minden TODO ide kerül; minden bejegyzés tartalmazza
> a **kontextust**, az **okot** és az **érintett fájlokat**.

Rendezés: súlyosság szerint csökkenő. Lezárt tételek a fájl alján, dátummal.

---

_(BL-001, BL-002 lezárva — lásd a „Lezárt tételek" szekciót.)_

---

_(BL-003, BL-004 lezárva — lásd a „Lezárt tételek" szekciót.)_

---

## BL-005 — Docker image reprodukálhatóság szigorítása

- **Kontextus:** `docker/Dockerfile` `FROM zephyrprojectrtos/ci:v0.28.8` —
  ez már egy jó pin, de tag mozoghat. A `pip3 install` verzió nélkül a
  dátum alapján másmás csomag-matrix-ot hoz.
- **Ok:** Ugyanazt kell buildelnünk, amikor vissza kell állni. A base tagot
  `@sha256:...` digestre szigoríthatjuk, és a Python csomagoknak minimum
  verziót adhatunk (vagy `requirements.txt`-t).
- **Érintett fájlok:** `docker/Dockerfile`.

---

## BL-006 — Board target név dokumentáció rendbetétel

- **Kontextus:** A Zephyr board target `w5500_evb_pico` (overlay-ben
  `compatible = "wiznet,w6100"`-val). Több doksiban emlegetjük „W6100 EVB
  Pico” néven a board-ot — könnyű összekeverni.
- **Ok:** Upstream Zephyr egyszer létrehozhat egy `w6100_evb_pico` targetet,
  és akkor migrálni kell. Érdemes ezt a BL-001 pinelés mellett nyomon
  követni.
- **Érintett fájlok:** `README.md` §Hardware, `TECHNICAL_OVERVIEW.md`,
  `app/boards/w5500_evb_pico.overlay`.

---

_(BL-007, BL-008 lezárva — lásd a „Lezárt tételek" szekciót.)_

---

## BL-009 — Upstream PR-ok (ELŐKÉSZÍTVE, ellenőrzésre vár)

- **Kontextus:** Három lokálisan hordozott fix, amelyek upstream bug-okat
  javítanak — mindegyikhez készen van a PR-anyag (diff + commit message +
  PR title/body) a `docs/upstream_prs.md`-ben:
  - **PR-A** (`micro_ros_zephyr_module`, ERR-027) — UDP transport header POSIX conditional
  - **PR-B** (`micro_ros_zephyr_module`, ERR-028) — libmicroros.mk ne zárja ki a std_srvs-t
  - **PR-C** (`zephyrproject-rtos/zephyr`, ERR-031) — eth_w6100 set_config propagálja az iface link_addr-t
- **Állapot:** a benyújtás **manuális lépés** a felhasználó részéről (fork,
  branch, DCO, stb.), a kódon nem kell több munka. Ha bármelyik merged,
  a megfelelő helyen `west.yml` SHA pint frissítjük és a patch-et töröljük
  (teljes flow: `docs/upstream_prs.md` — „Benyújtási ellenőrzőlista").
- **Érintett fájlok (ha merged):**
  - `west.yml` — új SHA-ra lépés,
  - `tools/patches/apply.sh` — Patch 1 és/vagy Patch 2 törlése,
  - ha mindkét micro-ROS patch merged: `apply.sh` teljesen törölhető,
  - `app/modules/w6100_driver/` — upstream driver használata (ha a fix
    v4.2.2-re back-portolt verzióra is rámegy).

---

_(BL-010, BL-011 lezárva — lásd a „Lezárt tételek" szekciót.)_

---

## BL-012 — RC bridge jövőbeni GPIO csatornák (GP07–GP11 + A0)

- **Kontextus:** A W6100 EVB Pico RC bridge-en a GP07, GP08, GP09, GP10, GP11
  digitális és az A0 (ADC0) analóg lábak ki vannak vezetve, de jelenleg nincs
  használatban. A user jelezte, hogy később kerülnek bekötésre — addig is
  legyenek a configban placeholder-ként, és a firmware publikáljon rájuk
  topic-okat a `/robot` namespace alatt (konzisztens a meglévő csatornákkal).
- **Ok:** Ha előre rögzítjük a topic-neveket és a config schema-t, a későbbi
  bekötéskor nem kell újra firmware-t flashelni — `bridge config set`-tel
  engedélyezhető lesz. Jelenleg a munka a szűrésre lett fókuszálva (BL-011),
  ezért a GPIO bővítés deferred.
- **Scope:**
  - `devices/RC/config.json` — új `gpio` blokk: `gp07..gp11` digitális
    (enabled bool + topic string), `a0` analóg (enabled + topic).
  - `app/src/config/config.{c,h}` — `gpio_ch_t` struct + parser (a
    `channels` mintájára), defaultolva `enabled=false`.
  - `app/src/user/` — új modul (pl. `gpio_publish.c`): GP07–GP11 input-pull-up
    publikáció `std_msgs/Bool`-ként; A0 ADC1 channel read → `std_msgs/Float32`
    normalizálva (0.0–1.0 vagy -1.0..+1.0, tisztázandó).
  - `app/boards/w5500_evb_pico.overlay` — GP07–GP11 GPIO aliases + ADC node.
  - Publikációs ráta: ~20–50 Hz (mint az rc_mode/winch), CPU budget vizsgálandó.
- **Javasolt topic-nevek:** `/robot/gp07`..`/robot/gp11` (Bool), `/robot/a0` (Float32).
  Final nevek a config-remap-elhetők (mint a meglévő CH-k).
- **Érintett fájlok:** `devices/RC/config.json`, `app/src/config/config.{c,h}`,
  `app/src/user/gpio_publish.{c,h}` (új), `app/boards/w5500_evb_pico.overlay`,
  `app/CMakeLists.txt`, `app/prj.conf` (ADC / GPIO konfig), `README.md` §RC config.

---

## BL-013 — RC config: prod subnet visszaállítása teszt után

- **Kontextus:** `devices/RC/config.json` jelenleg dev subneten van
  (`ip=192.168.68.202`, `dhcp=true`, `agent_ip=192.168.68.125`), hogy a
  laptop-hoz közvetlenül elérhető legyen — a 2026-04-19/20 zaj-mérés ezt
  követelte meg. A többi bridge (PEDAL, E_STOP) már prod subneten van
  (`10.0.10.0/24`, `dhcp=false`).
- **Ok:** Éles üzemben az RC-nek a robot 10.0.10.0/24 subnetén kell lennie
  (agent a roboton belül), különben nem párosul a többi bridge-dzsel és a
  ROS2 Jazzy agenttel.
- **Visszaállítandó értékek** (lásd memory: `project_network_subnets.md`):
  - `network.ip` = `10.0.10.22`
  - `network.gateway` = `10.0.10.1`
  - `network.agent_ip` = `10.0.10.1`
  - `network.dhcp` = `false`
- **Művelet:** JSON szerkesztés + `python3 tools/upload_config.py --config devices/RC/config.json --port /dev/ttyACM0`
  (a board reboot-ol, új subneten jön fel).
- **Érintett fájlok:** `devices/RC/config.json`.

---

## Lezárt tételek

### BL-001 — `west.yml` pinelése — LEZÁRVA 2026-04-19

A `zephyr.revision: main` → `v4.2.2`, `micro_ros_zephyr_module.revision: jazzy`
→ SHA `87dbe3a9` pinek alkalmazva. A Zephyr v4.2.2 a sweet spot (SDK 0.17.4
kompat + `zephyr/posix/time.h` még megvan). A micro_ros SHA-pin biztosítja,
hogy a west update ne driftelje az ismert-jó HEAD-et. Részletek: ERR-025.

### BL-002 — Reprodukálható Pico firmware build Linuxon — LEZÁRVA 2026-04-19

Egy tiszta `rm -rf workspace && make workspace-init && make build` flow
zöld, mert a `workspace-init` automatikusan hívja a `apply-patches`
targetet, a `build` szintén függ tőle. Lásd memory.md §0 és ERR-025..028.

### BL-007 — Flash port Linux-on — LEZÁRVA 2026-04-19

`tools/flash.sh` cross-platform adaptálva: `uname -s` alapján macOS `/Volumes/RPI-RP2` + `/dev/tty.usbmodem*`, Linux `/media/$USER/RPI-RP2` (+ `/run/media/$USER/RPI-RP2` fallback) + `/dev/ttyACM*`.
`Makefile` `FLASH_PORT` default: `ifeq ($(UNAME_S),Darwin)` branch → macOS/Linux automatikus. `?=` operátor, felülírható `FLASH_PORT=/dev/ttyACM1 make flash`-sal.

### BL-008 — Single central config.json template vs devices/ eltérés — LEZÁRVA 2026-04-19

`app/config.json` migrálva `10.0.10.x` subnetre: ip `10.0.10.20`, gateway+agent_ip `10.0.10.1`.
Logika: ez a firmware-be égetett fallback default; az `upload_config.py` futtatásával a board felülírja a `devices/*/config.json` értékével.
`10.0.10.20` szándékos placeholder (kívül a 21–23 device range-en) — DHCP-s boardoknak sem okoz ütközést.

### BL-003 — `bridge config set` node_name validáció — LEZÁRVA 2026-04-19

A `config_set()` mostantól ROS2 identifier szabály szerint validálja a `ros.node_name` (`[a-zA-Z][a-zA-Z0-9_]*`) és a `ros.namespace` (`/` vagy `/seg[/seg...]` szegmensenként ugyanez a szabály) mezőket. Érvénytelen érték `-EINVAL`-t ad, a shell oldal konkrét hibaüzenettel utasítja el (`OK/BAD` példákkal). Mivel az `upload_config.py` is a `bridge config set`-en át dolgozik, a validáció ezen az úton is érvényesül — ezzel megszűnik a BOOTSEL-lock reprodukciója érvénytelen `node_name`-mel (ERRATA_BOOTSEL.md). Érintett fájlok: `app/src/config/config.c` (is_valid_ros2_name + is_valid_ros2_namespace), `app/src/shell/shell_cmd.c` (hibaüzenet `-EINVAL`-ra), `ERRATA_BOOTSEL.md`.

### BL-004 — ERR-001 diagnosztika mélyebbre — LEZÁRVA 2026-04-19

`CONFIG_SYS_HEAP_RUNTIME_STATS=y` + `param_server.c:117–137` logolja a heap állapotot az `rclc_parameter_server_init_with_option` **előtt és után** (`Heap before/after param_server: free=... alloc=... max=...`). Ezzel a következő `error: 11` reprodukciónál egy pillantásból eldönthető, hogy `RCL_RET_BAD_ALLOC` (heap szűkösség) vagy `RCL_RET_INVALID_ARGUMENT` a gyökérok — lásd `ERRATA.md` §ERR-001. A diagnosztikai infrastruktúra kész, a feladat az első reprodukciónál vált élesbe.

### BL-011 — RC bridge zajmérés + EMA filter tuning — LEZÁRVA 2026-04-20

A user zajos kimenetet jelzett az RC bridge csatornáin. A diagnosztikához létrehozott `tools/rc_measure.py` (3 subcommand: `measure` fázisos, `stream` folyamatos, `compare` CSV delta) rclpy-alapú mérőeszközzel 12 s folyamatos felvétel készült három alpha értékre, a stickek folyamatos full-sweep mozgatása mellett. A konstans csatornák (CH3 = `rc_ch3`, CH5 = `rc_mode`) szolgáltatják a tiszta zajfloor-t, a mozgatott CH1/CH2 (`motor_right`/`motor_left`) a dinamikai ellenőrzést.

**Eredmények (std a konstans CH3/CH5-ön):**

| α     | CH3 std | CH5 std | zaj vs. floor | lag (≈1/α · 24 ms) |
| ----- | ------- | ------- | ------------- | ------------------ |
| 1.0   | 0.0436  | 0.0468  | **100%**      | ~24 ms (nincs)     |
| 0.30  | 0.0181  | 0.0217  | **~44%**      | ~80 ms             |
| 0.25  | 0.0207  | 0.0203  | **~45%**      | ~95 ms             |

A mozgatott CH1/CH2 mindhárom értéknél megtartja a ±1 full range-et (p2p ≈ 1.95–2.00), a 42 Hz publikációs ráta változatlan. Alpha 0.25 és 0.30 közti zajcsökkenés különbség a mérési varianciát nem lépi át (~5%), de a 0.30 **~15 ms-mal gyorsabb válaszidőt ad** ugyanazért a zajcsökkenésért.

**Választott érték:** `rc_trim.ema_alpha = 0.30` (gyorsabb response, azonos zajjavítás).

**Firmware-oldali megerősítés** (korábbi commit): az EMA kód (`app/src/user/rc.c`) `alpha >= 1.0f` esetén passthrough (filter off), `alpha < 1.0f` esetén `ema_state = α · norm + (1-α) · ema_state` első híváskor önmagát inicializálja. A `bridge config set rc_trim.ema_alpha <val>` reboot nélkül hat (live read a `g_config.rc_trim.ema_alpha`-ból minden normalizálási körben), így a tuning interaktív volt.

**Érintett fájlok:**
- `tools/rc_measure.py` (új) — mérőeszköz rclpy subscription + per-channel stat + summary/raw CSV export, compare tool pairwise std/p2p delta+ratio.
- `tools/docker-run-ros2.sh` — új mount pontok (`$SCRIPT_DIR:/tools:ro`, `logs:/logs:rw`) és `-w /logs` workdir, hogy a konténerből közvetlen legyen futtatható.
- `.gitignore` — `logs/` kizárva (CSV mérési output nem kerül verziókezelés alá).
- `devices/RC/config.json` — `rc_trim.ema_alpha: 0.30` commit + `upload_config.py` → flash save.

### BL-010 — W6100 chip Ethernet nem működik — LEZÁRVA 2026-04-19

**Eredeti hipotézis (téves):** a W6100 SPI protokoll nem kompatibilis a W5500-zal, ezért a common block olvasások mind 0x00-t adnak.

**Tényleges gyökérok (ERR-030 részletesen):** az SPI keretformátum (3-byte header, BSB<<3, R/W bit) teljesen megegyezik — a probléma az init szekvenciában volt:

1. **Hiányzó reset pulzus.** A W5500 driver csak elengedi a reset vonalat; a W6100-nak aktív pulzus kell (T_RST ≥ 2 µs + T_STA ≤ 100 ms).
2. **Hiányzó CHPLCKR/NETLCKR unlock.** A W6100 common és network blokkjai reset után zártak; írás/olvasás mindenhol 0x00-át eredményez, amíg `CHPLCKR=0xCE` és `NETLCKR=0x3A` unlock nem történik.

**Javítás:** Zephyr upstream W6100 driver (PR #101753) backportolva v4.2.2 alá **out-of-tree Zephyr modulként** az alkalmazás fa alatt. Nem patcheljük többé a W5500 drivert (Patch 3/4/5 eltávolítva `apply.sh`-ból — ezek az eredeti téves hipotézis kompenzációi voltak).

**Szükséges v4.2.2 API adaptációk** az upstream (Zephyr main) driverhez képest:
- `net_eth_mac_load()` + `NET_ETH_MAC_DT_INST_CONFIG_INIT()` → kivéve (v4.3.0 API); helyette W5500-mintás `.mac_addr = DT_INST_PROP(0, local_mac_address)` `COND_CODE_1(DT_NODE_HAS_PROP(..., local_mac_address), ..., ())`.
- `NET_AF_UNSPEC` → `AF_UNSPEC`.
- `SPI_DT_SPEC_INST_GET(inst, SPI_WORD_SET(8))` → `SPI_DT_SPEC_INST_GET(inst, SPI_WORD_SET(8), 0)` (3-argumentumos v4.2.2 API).

**Érintett fájlok:**
- Új: `app/modules/w6100_driver/` (7 fájl — `zephyr/module.yml`, `zephyr/CMakeLists.txt`, `zephyr/Kconfig`, `zephyr/dts/bindings/ethernet/wiznet,w6100.yaml`, `drivers/ethernet/eth_w6100.{c,h}`, `drivers/ethernet/Kconfig.w6100`).
- `app/CMakeLists.txt` — `ZEPHYR_EXTRA_MODULES` kiegészítve a modul útjával.
- `app/boards/w5500_evb_pico.overlay` — `&ethernet { compatible = "wiznet,w6100"; };` override.
- `app/prj.conf` — `CONFIG_ETH_W5500=n` + `CONFIG_ETH_W6100=y`.
- `tools/patches/apply.sh` — Patch 3/4/5 törölve (marad Patch 1 + 2).

**Verifikáció (EVB-Pico, 2026-04-19):**

```
<inf> eth_w6100: W6100 Initialized
<inf> eth_w6100: w5500@0 MAC set to 0c:2f:94:30:58:11
<inf> eth_w6100: w5500@0: Link up
<inf> eth_w6100: w5500@0: Link speed 10 Mb, half duplex
<inf> main: Ethernet link UP
<inf> main: Network: static IP 192.168.68.200
<inf> main: Searching for agent: 192.168.68.125:8888 ...
```

Build méret hatás: `zephyr.uf2` 863232 B → 864768 B (+1.5 KB), RAM 97.45% (változatlan).

**Follow-up bug (ERR-031) — end-to-end kiegészítés, 2026-04-19:**

Az első board-on-board teszt során kiderült, hogy a session handshake nem zárul le: agent látja a PEDAL UDP CREATE_CLIENT csomagokat, de a válasz STATUS reply nem ér vissza, és host→Pico ping is 100% loss. Az `ip neigh show 192.168.68.200` a chip *régi/default* MAC-jét mutatta (`00:00:00:01:02:03`), nem a hwinfo-alapú `0c:2f:94:30:58:11`-et.

Root cause: a W6100 **MACRAW módban** fut (socket 0), így a Zephyr L2 réteg építi a teljes Ethernet keretet szoftveresen, és a src MAC-et az `iface->link_addr`-ból olvassa. A `w6100_set_config(MAC_ADDRESS)` frissítette a chip SHAR-t és a `ctx->mac_addr`-t, **de nem hívta** `net_if_set_link_addr`-et — a kimenő keret src MAC-je ezért a `w6100_iface_init`-ben beállított kezdő értéken ragadt.

Javítás (`app/modules/w6100_driver/drivers/ethernet/eth_w6100.c`): a SHAR write után `net_if_set_link_addr(ctx->iface, ctx->mac_addr, 6, NET_LINK_ETHERNET)` hívás hozzáadva (részletes naplózás: ERRATA.md §ERR-031).

**End-to-end verifikáció (2026-04-19, ERR-031 javítás után):**
- `ip neigh show 192.168.68.200` → `lladdr 0c:2f:94:30:58:11 REACHABLE`
- `ping -c 3 192.168.68.200` → 0% loss, ~4.3 ms RTT
- Agent log: `client_key: 0x5496464D` (valid session)
- `ros2 node list` → `/robot/pedal` megjelent
- `ros2 topic hz /robot/heartbeat` → 1.00 Hz stabil (`std_msgs/msg/Bool`)
