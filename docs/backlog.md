# docs/backlog.md — Nyitott feladatok és TODO-k

> Utolsó frissítés: 2026-04-19
> Hatókör: a teljes ROS2-Bridge repo.
> Szabály (`policy.md#2`): minden TODO ide kerül; minden bejegyzés tartalmazza
> a **kontextust**, az **okot** és az **érintett fájlokat**.

Rendezés: súlyosság szerint csökkenő. Lezárt tételek a fájl alján, dátummal.

---

## BL-001 — `west.yml` pinelése konkrét Zephyr commitra (KRITIKUS)

- **Kontextus:** A jelenlegi manifest:
  ```yaml
  - name: zephyr
    remote: zephyrproject-rtos
    revision: main          # követi a Zephyr main branchet
  - name: micro_ros_zephyr_module
    remote: micro-ros
    revision: jazzy         # követi a micro-ROS jazzy branchet
  ```
  Minden `west update` újabb upstream snapshotot tölt le. A projektet
  macOS-en egyszer lefordítottuk (Zephyr v4.3.99-kor), majd egy újrabuild
  után „elveszett a környezet” — valószínűleg egy Zephyr `main` commit tört
  el nálunk az W6100 / net / WDT API valamelyikén.
- **Ok:** Reprodukálható build. A firmware **nem fordul** ismeretlen
  környezetben; minden gépre ugyanazt a Zephyr + micro-ROS bundle-t
  akarjuk telepíteni. Ameddig ez nincs pinelve, a build minden környezetben
  rulett.
- **Érintett fájlok:**
  - `app/west.yml` — pinelés (SHA vagy tag).
  - `README.md` — a „Zephyr version” táblázat frissítése a pineket tükrözve.
  - `memory.md` — a pinelt verziók rögzítése.
  - Opció: `docker/Dockerfile` — a `zephyr-sdk 0.17.4` mellett a west snapshot
    hashét is CI-title lehet vinni.
- **Terv:** az első sikeres Linux build után `git -C workspace/zephyr rev-parse HEAD`
  és `git -C workspace/modules/lib/micro_ros_zephyr_module rev-parse HEAD` →
  ezeket rögzítjük a `west.yml`-be `revision:` sorba.
- **Kockázat lezárva:** Ha PRO módban szeretnénk: digest pin a Docker base image-re
  is (`FROM zephyrprojectrtos/ci:v0.28.8@sha256:...`).

---

## BL-002 — Reprodukálható Pico firmware build Linuxon (KRITIKUS)

- **Kontextus:** A `workspace/` jelenleg hiányzik a gépről (`.gitignore`
  kizárja, 2 GB+). A Docker image (`w6100-zephyr-microros:latest`) a gépen van,
  de nem tudjuk, hogy a west pull még stabilan lefut-e.
- **Ok:** Amíg nem látjuk a **tényleges** hibaüzenetet a Linux build kísérletből,
  találgatunk. Az első lépés: elvégezni egy `make workspace-init && make build`
  próbát Linuxon és a logot szó szerint rögzíteni a `memory.md`-ben, majd
  célzottan javítani.
- **Érintett fájlok:** `app/west.yml`, `app/prj.conf`,
  `app/boards/w5500_evb_pico.overlay`, `app/src/**`, `docker/Dockerfile`.
- **Előfeltétel:** BL-001 pinelés után több értelme van; de az első próbát
  a jelenlegi állapotban is meg kell tenni, hogy a „mi tört el?” konkrét legyen.

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

## BL-007 — Flash port Linux-on — LEZÁRVA 2026-04-19

`tools/flash.sh` cross-platform adaptálva: `uname -s` alapján macOS `/Volumes/RPI-RP2` + `/dev/tty.usbmodem*`, Linux `/media/$USER/RPI-RP2` (+ `/run/media/$USER/RPI-RP2` fallback) + `/dev/ttyACM*`.
`Makefile` `FLASH_PORT` default: `ifeq ($(UNAME_S),Darwin)` branch → macOS/Linux automatikus. `?=` operátor, felülírható `FLASH_PORT=/dev/ttyACM1 make flash`-sal.

---

## BL-008 — Single central config.json template vs devices/ eltérés — LEZÁRVA 2026-04-19

`app/config.json` migrálva `10.0.10.x` subnetre: ip `10.0.10.20`, gateway+agent_ip `10.0.10.1`.
Logika: ez a firmware-be égetett fallback default; az `upload_config.py` futtatásával a board felülírja a `devices/*/config.json` értékével.
`10.0.10.20` szándékos placeholder (kívül a 21–23 device range-en) — DHCP-s boardoknak sem okoz ütközést.

---

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
