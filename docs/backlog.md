# docs/backlog.md — Nyitott feladatok és TODO-k

> Utolsó frissítés: 2026-04-19 (BL-003, BL-004 lezárva; BL-009 PR-anyag előkészítve)
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

### BL-003 — `bridge config set` node_name validáció — LEZÁRVA 2026-04-19

A `config_set()` mostantól ROS2 identifier szabály szerint validálja a `ros.node_name` (`[a-zA-Z][a-zA-Z0-9_]*`) és a `ros.namespace` (`/` vagy `/seg[/seg...]` szegmensenként ugyanez a szabály) mezőket. Érvénytelen érték `-EINVAL`-t ad, a shell oldal konkrét hibaüzenettel utasítja el (`OK/BAD` példákkal). Mivel az `upload_config.py` is a `bridge config set`-en át dolgozik, a validáció ezen az úton is érvényesül — ezzel megszűnik a BOOTSEL-lock reprodukciója érvénytelen `node_name`-mel (ERRATA_BOOTSEL.md). Érintett fájlok: `app/src/config/config.c` (is_valid_ros2_name + is_valid_ros2_namespace), `app/src/shell/shell_cmd.c` (hibaüzenet `-EINVAL`-ra), `ERRATA_BOOTSEL.md`.

### BL-004 — ERR-001 diagnosztika mélyebbre — LEZÁRVA 2026-04-19

`CONFIG_SYS_HEAP_RUNTIME_STATS=y` + `param_server.c:117–137` logolja a heap állapotot az `rclc_parameter_server_init_with_option` **előtt és után** (`Heap before/after param_server: free=... alloc=... max=...`). Ezzel a következő `error: 11` reprodukciónál egy pillantásból eldönthető, hogy `RCL_RET_BAD_ALLOC` (heap szűkösség) vagy `RCL_RET_INVALID_ARGUMENT` a gyökérok — lásd `ERRATA.md` §ERR-001. A diagnosztikai infrastruktúra kész, a feladat az első reprodukciónál vált élesbe.

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
