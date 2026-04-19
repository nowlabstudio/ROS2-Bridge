# docs/backlog.md — Nyitott feladatok és TODO-k

> Utolsó frissítés: 2026-04-19 (BL-010 end-to-end zöld, ERR-031 lezárva)
> Hatókör: a teljes ROS2-Bridge repo.
> Szabály (`policy.md#2`): minden TODO ide kerül; minden bejegyzés tartalmazza
> a **kontextust**, az **okot** és az **érintett fájlokat**.

Rendezés: súlyosság szerint csökkenő. Lezárt tételek a fájl alján, dátummal.

---

_(BL-001, BL-002 lezárva — lásd a „Lezárt tételek" szekciót.)_

---

## BL-003 — `bridge config set` node_name validáció

- **Kontextus:** `ERRATA_BOOTSEL.md` szerint az `E-STOP` (kötőjeles) node_name
  `rclc_node_init` hibaciklust okoz, ami miatt a shell nem reagál, és a
  `bridge bootsel` sem működik. Csak fizikai BOOTSEL gombbal lehet kilábalni.
- **Ok:** Prevenció > recovery. A ROS2 név szabály `[a-zA-Z][a-zA-Z0-9_]*`;
  érdemes a `bridge config set ros.node_name <val>` shell oldalon elutasítani
  az érvénytelen értékeket, mielőtt menthető lenne.
- **Érintett fájlok:**
  - `app/src/config/config.c` (setter),
  - `app/src/shell/shell_cmd.c` (szűrés + hibaüzenet),
  - `ERRATA_BOOTSEL.md` (állapot frissítés javítás után).

---

## BL-004 — ERR-001 diagnosztika mélyebbre (KÖZEPES)

- **Kontextus:** `param_server_init error: 11` időnként boot-kor; a board
  nélküle is fut, csak a `ros2 param` interface nem elérhető. Már megvan a
  `CONFIG_SYS_HEAP_RUNTIME_STATS=y`.
- **Ok:** Egy XRCE/Kconfig/limit allokációs hiba megmaradt. A jelenlegi
  gyanúhalmazon (heap, service count, timeout) túl kell lépni — runtime
  heap log közvetlenül az init előtt és után adná meg a választ.
- **Érintett fájlok:** `app/src/bridge/param_server.c`, `app/src/main.c`
  (heap stat lekérdezés + LOG_INF), `ERRATA.md` (ERR-001 frissítés).

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

## BL-009 — Upstream PR-ok a micro_ros_zephyr_module-hoz (KÖZEPES)

- **Kontextus:** A `tools/patches/apply.sh` két lokális patchet alkalmaz a
  jazzy HEAD-en (UDP transport header POSIX includeok, `libmicroros.mk`
  std_srvs COLCON_IGNORE). Ezek **upstream hibák**, nem projektspecifikusak.
- **Ok:** Egy upstream PR véglegessé tenné a javítást mindenkinek. Amíg nem
  merged, a patch script kell, de ha PR ment, akkor tovább pinelhetünk egy
  olyan commitre, ami már tartalmazza ezeket a fixeket, és a patch-szkript
  törölhető.
- **Érintett fájlok:**
  - A forrás oldalán: `micro_ros_zephyr_module/modules/libmicroros/microros_transports/udp/microros_transports.h`, `modules/libmicroros/libmicroros.mk`.
  - Nálunk megszűnik: `tools/patches/apply.sh`, Makefile `apply-patches` target, `build:` függőség.
- **Ajánlott tartalom:** egy PR mindkét fájlra; commit message hivatkozzon az
  ERR-027 és ERR-028 kontextusra (build reprodukcióra).

---

_(BL-010 lezárva — lásd a „Lezárt tételek" szekciót.)_

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
