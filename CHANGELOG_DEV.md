# Fejlesztési napló — W6100 EVB Pico micro-ROS Bridge

Folyamatos haladáskövetés. Minden munkamenet változásai időrendben.

---

## 2026-03-09 (11) — Portainer custom template (W6100 Robot Stack)

### Új

- **tools/portainer-templates.json:** Portainer CE custom app template — egy „W6100 Robot Stack” bejegyzés (type 3 = Compose stack), repository URL placeholder, stackfile: `docker-compose.yml`, env: `AGENT_PORT`, `ROBOCLAW_HOST`, `ROBOCLAW_PORT` (alapértelmezettekkel). A `note` mező (HTML) elmagyarázza az előfeltételt (relatív útvonalak), az ajánlott Web editor + stack path módszert és az alternatívát (deploy from repo + `make host-build-docker` a klónban).
- **ONBOARDING.md:** Új alfejezet „Portainer: custom template a robot stackhez” — template hozzáadása (Settings → Custom templates, URL vagy beillesztés), repository URL cseréje, két deploy módszer (Web editor + path vs Repository + build a klónban). A dokumentumtáblázatban megjelenik a `tools/portainer-templates.json`.

### Megjegyzés

A compose relatív volume-okat használ (`./host_ws`, `./tools`); a „Deploy from repository” csak akkor működik készen, ha a klónban már létezik a `host_ws/install` (pl. az első deploy után a hoston a klón könyvtárában `make host-build-docker`, majd stack restart).

---

## 2026-03-09 (10) — ONBOARDING.md + szabály a naprakészen tartásra

### Új

- **ONBOARDING.md** (repo gyökér): Teljes rendszer onboarding — architektúra, előfeltételek, első alkalom (klón + host-build-docker), konfig (robot_network.yaml), napi használat (robot-start/stop, logs, shell, Portainer), mi fut és hol, Foxglove, parancs referencia, dokumentáció index, gyors hibaelhárítás.
- **.cursor/rules/errata-changelog.mdc:** 3. pont: ONBOARDING.md frissítése, ha a rendszer fejlődik (új parancs, szolgáltatás, architektúra, hibaelhárítás). A rule description bővítve az ONBOARDING.md-re.

---

## 2026-03-09 (9) — Portainer CE hozzáadva (profile: management)

### Változtatások

- **docker-compose.yml:** Portainer CE service hozzáadva `profiles: ["management"]` — nem indul `make robot-start`-tal, külön kell: `make portainer-start`. Port: 9443 (HTTPS), 8000 (Edge Agent). Docker socket mount (ro), persistent volume (portainer_data).
- **Makefile:** `make portainer-start` / `make portainer-stop` + help szöveg.

### Használat

Ha van internet (első alkalommal image pull): `make portainer-start`, majd böngésző: `https://<robot-ip>:9443`. Ha nincs internet, a robot stack (`make robot-start`) normálisan indul, a Portainer nem blokkolja.

---

## 2026-03-09 (8) — Docker Compose: teljes robot rendszer

### Döntés

A tmux/gnome-terminal tabs helyett **Docker Compose** — mivel már minden Docker konténerben fut, a compose a legátláthatóbb és legkezelhetőbb megoldás.

### Új fájlok

- **docker-compose.yml** (repo gyökér): 3 service:
  - `agent` — microros/micro-ros-agent:jazzy, UDP, `restart: unless-stopped`
  - `roboclaw` — ros:jazzy, depends_on agent, mount host_ws + cyclonedds, ros2 launch
  - `ros2-shell` — ros:jazzy, depends_on agent, stdin_open + tty, interaktív bash

### Makefile parancsok

| Parancs | Docker Compose | Leírás |
|---------|---------------|--------|
| `make robot-start` | `docker compose up -d` | Minden elindul háttérben |
| `make robot-stop` | `docker compose down` | Minden leáll |
| `make robot-logs` | `docker compose logs -f` | Összes log követése |
| `make robot-logs-roboclaw` | `docker compose logs -f roboclaw` | Csak roboclaw log |
| `make robot-shell` | `docker compose exec ros2-shell bash` | ROS2 shell belépés |
| `make robot-ps` | `docker compose ps` | Konténer állapot |

### Teszt eredmények

- `make robot-start` — 3 konténer elindult (~6s)
- `make robot-ps` — mind UP
- `docker compose logs roboclaw` — safety_bridge OK, roboclaw_tcp_node OK (TCP timeout ha nincs hardver, normális)
- `docker compose exec ros2-shell bash -c "ros2 node list"` — 3 node látható (/robot/estop, /robot/pedal, /robot/rc)
- `ros2 topic list` — 17 topic (cmd_vel, odom, diagnostics, estop, motor_left/right, stb.)
- `make robot-stop` — mind leállt

### Eltávolított/deprecált

- `make robot-start-tmux` eltávolítva a Makefile-ból (a shell scriptek megmaradnak visszafelé kompatibilitáshoz)
- `make robot-start` (tabs) lecserélve compose-ra

---

## 2026-03-09 (7) — RoboClaw TCP driver MŰKÖDIK + tabs javítás

### Siker

A RoboClaw TCP driver **sikeresen csatlakozott** a motorvezérlőhöz Docker konténerből:
```
Connected to controller: USB Roboclaw 2x60A v4.4.3
Port: tcp://192.168.68.60:8234, Baud: 38400, Address: 128
```
A safety_bridge_node is rendben elindult. Ctrl+C-re a motorokat leállította.

### Javítások

- **start-robot-tabs.sh:** `gnome-terminal --window` hozzáadva a `--tab` elé. Enélkül egyes rendszereken a gnome-terminal a meglévő ablakhoz próbálta hozzáadni a tabokat, és csak az elsőt (agent) nyitotta meg. A `--window` biztosítja, hogy egy új ablak nyíljon mind a 3 tabbal.

### Nyitott

- [ ] `make robot-start` (tabs) teljes teszt a `--window` javítás után
- [ ] ROS2 shell topicok ellenőrzése (ros2 topic list, echo) amíg a driver fut

---

## 2026-03-09 (6) — RoboClaw: No module named 'basicmicro_driver' — root cause

### Hiba

`roboclaw_tcp_node` indításakor: `No module named 'basicmicro_driver'`. A basicmicro_ros2 CMakeLists.txt a driver scripteket **programként** (symlink) telepíti `lib/basicmicro_ros2/` alá, de **nem Python csomagként** (nincs `__init__.py`). A `basicmicro_driver` Python package (`__init__.py` + modulok) kizárólag a **forrásban** van: `host_ws/src/basicmicro_ros2/basicmicro_driver/`.

### Javítások

- **docker-run-roboclaw.sh, docker-run-ros2.sh:** PYTHONPATH = `/host_ws/src/basicmicro_ros2:/host_ws/src/basicmicro_python:...` — a **forrás** könyvtár, nem az install lib. Így `from basicmicro_driver.basicmicro_node import main` megtalálja a csomagot.
- **roboclaw_tcp_node.py:** Visszaegyszerűsítve egyetlen importra: `from basicmicro_driver.basicmicro_node import main`.

### Következő lépés

Nem kell újraépíteni (symlink-install). Indítsd újra a roboclaw tabot.

---

## 2026-03-09 (5) — Terminal fülek (tabs) + robot-stop

### Kérés

- Maradjanak a terminal fülek (kézre esik), ne tmux.
- Legyen robot-stop (vagy flag), ami leállít minden futó folyamatot.

### Változtatások

- **tools/start-robot-tabs.sh** (új): Egy GNOME Terminal ablak 3 tabbal — [1] agent, [2] roboclaw, [3] ros2-shell. A 2. és 3. tab vár, amíg az agent konténer fut, majd indítja a roboclaw-ot és a ros2-shellt. Konfig: host_ws/config/robot_network.yaml.
- **tools/stop-robot.sh** (új): Leállítja a három Docker konténert (w6100_bridge_agent_udp, w6100_bridge_roboclaw, w6100_bridge_ros2) és a tmux session "robot"-ot (ha van).
- **Makefile:** `robot-start` most a **start-robot-tabs.sh**-t hívja (terminal tabs). Új: `robot-start-tmux` → start-robot.sh (tmux, pl. SSH-hoz). Új: `robot-stop` → stop-robot.sh. A help szöveg frissítve.

### Használat

- Indítás: `make robot-start` (3 tab egy ablakban).
- Leállítás: `make robot-stop`.
- Tmux (headless): `make robot-start-tmux`; leállítás: `make robot-stop` vagy `tmux kill-session -t robot`.

---

## 2026-03-09 (4) — RoboClaw Docker: serial modul + safety_bridge logger javítás

### Hibák (make robot-start / docker-run-roboclaw.sh)

1. **roboclaw_tcp_node:** `ModuleNotFoundError: No module named 'serial'` — A `basicmicro` a `pyserial`-t használja (`import serial`); a ros:jazzy konténerben ez a csomag nem volt telepítve.
2. **safety_bridge_node:** `TypeError: RcutilsLogger.info() takes 2 positional arguments but 6 were given` — Az rclpy logger nem fogad több argumentumot (format + args), csak egy stringet.

### Javítások

**1. serial (pyserial) a konténerben**
- `tools/docker-run-roboclaw.sh`: induláskor `apt-get update && apt-get install -y python3-serial` (csendesen), így a `serial` modul elérhető.
- `tools/docker-run-ros2.sh`: ugyanez, hogy a ros2-shellből futtatva is működjön a roboclaw_tcp_node.

**2. safety_bridge_node.py logger**
- `host_ws/src/roboclaw_tcp_adapter/roboclaw_tcp_adapter/safety_bridge_node.py`: a többargumentumos `get_logger().info("fmt", a, b, c, d)` helyett egy formázott string: `get_logger().info("fmt" % (a, b, c, d))`. Ugyanígy a `.warn("EMERGENCY STOP: %s", reason)` → `.warn("EMERGENCY STOP: %s" % reason)`.

### Következő lépés

A safety_bridge forrást módosítottuk; a futó kód a build mappából jön. **Újra kell építeni:** `make host-build-docker`, majd `make robot-start` (vagy csak a roboclaw ablak újraindítása).

---

## 2026-03-09 (3) — ROS2 Dockerből: host-build-docker, RoboClaw + shell konténerben

### Kontextus

A felhasználó jelzi: a ROS2 Dockerből fut (nincs natív ROS2 a hoston). A korábbi utasítások (make host-build, make robot-start host-on) ezért nem megfelelőek.

### Változtatások

**1. Makefile**
- **host-build-docker:** Colcon build a `ros:jazzy` konténerben; a `host_ws` mountolva van. A konténerben törli a build/log cache-t (path ütközés elkerülésére), majd `colcon build --packages-select basicmicro_ros2 roboclaw_tcp_adapter --symlink-install`. A `basicmicro_python` nem ament csomag, ezért nem épül colcon-nal; futásidőben a `PYTHONPATH=/host_ws/src/basicmicro_python` biztosítja az importot.
- A help szöveg bővítve: host-build-docker, és hogy a robot-start ROS2-t Dockerből indítja.

**2. tools/docker-run-ros2.sh**
- Mount: `host_ws` → `/host_ws` (rw).
- Belépéskor: `PYTHONPATH=/host_ws/src/basicmicro_python`, majd `source /opt/ros/jazzy/setup.bash`, majd ha létezik `source /host_ws/install/setup.bash`. Így a shellben elérhetők a roboclaw_tcp_adapter és basicmicro_ros2 csomagok.
- Prerequisíte a kommentben: make host-build-docker.

**3. tools/docker-run-roboclaw.sh (új)**
- A RoboClaw TCP adapter (driver + safety bridge) a `ros:jazzy` konténerben indul: mount `host_ws`, CYCLONEDDS, env `ROBOCLAW_HOST`, `ROBOCLAW_PORT`, majd `ros2 launch roboclaw_tcp_adapter roboclaw.launch.py ...`.
- Használat: `./docker-run-roboclaw.sh [HOST] [PORT]` (default 192.168.68.60 8234).

**4. tools/start-robot.sh**
- A [1] roboclaw ablak már nem a hoston futtatja a `ros2 launch`-t, hanem a `docker-run-roboclaw.sh`-t hívja (ugyanaz a host/port kiolvasás a config-ból).

### Ellenőrzés

- `make host-build-docker` sikeres (2 csomag: basicmicro_ros2, roboclaw_tcp_adapter).
- Dockerben: `source /opt/ros/jazzy/setup.bash && source /host_ws/install/setup.bash && ros2 pkg list | grep -E 'roboclaw_tcp_adapter|basicmicro'` → basicmicro_ros2, roboclaw_tcp_adapter.

### Nyitott

- [ ] make robot-start (tmux) teljes teszt: agent + roboclaw (Docker) + ros2-shell (Docker).

---

## 2026-03-09 (2) — Host workspace tesztek előkészítése (Linux)

### Elvégzett lépések (repo gyökérből)

**1) Submodule állapot**
- `git submodule status`: mindkét submodule kicheckoutolva (space a commit előtt)
  - `host_ws/src/basicmicro_python` (3231645)
  - `host_ws/src/basicmicro_ros2` (dc75870)
- `git submodule update --init --recursive` nem kellett.

**2) Host workspace függőségek (`make host-install-deps`)**
- **Sikerült:** `pip3 install -e host_ws/src/basicmicro_python` — user install, basicmicro + pyserial telepítve.
- **Nem sikerült:** `rosdep install ...` — ROS_DISTRO nincs beállítva (ROS2 nincs a gépen), sudo kért jelszót; rosdep hibát dobott (std_msgs, robot_state_publisher definíciók, python3-serial apt). Venv nem kellett (pip nem externally-managed hibát dobott).

**3) Colcon build (`make host-build`)**
- **Nem sikerült:** `ament_cmake` nem található — a build előtt a ROS2 környezet nincs betöltve (nincs `/opt/ros`, `ros2` nincs a PATH-on). A `basicmicro_ros2` CMake ament_cmake-t keres, ezért a build abort.
- **Megjegyzés:** `host_ws/install/` már létezik korábbi (ROS2-s környezetben készült) buildből (basicmicro_ros2, roboclaw_tcp_adapter install fájlok megvannak).

**4) Ellenőrzés**
- `host_ws/install/setup.bash` **létezik**.
- `source host_ws/install/setup.bash && ros2 pkg list | grep -E "roboclaw_tcp_adapter|basicmicro"` **nem futtatható** — a gépen nincs ROS2 telepítve, így `ros2` parancs nincs.

### Összefoglaló

| Lépés | Eredmény | Megjegyzés |
|-------|----------|------------|
| Submodule status | OK | Mind kicheckoutolva |
| pip basicmicro_python | OK | User install |
| rosdep | Sikertelen | ROS_DISTRO nincs, sudo, rosdep keys |
| make host-build | Sikertelen | ament_cmake hiányzik (ROS2 nincs) |
| setup.bash létezik | OK | Korábbi build maradvány |
| ros2 pkg list | N/A | ROS2 nincs telepítve |

### Következő tesztlépés (ha ROS2 már telepítve)

- **ROS2 telepítés (Ubuntu):**  
  `sudo apt update && sudo apt install ros-jazzy-desktop` (vagy használt distro), majd `source /opt/ros/jazzy/setup.bash`. Ezután `make host-build` és a fenti ellenőrzés.
- **Robot indítás (tmux):**  
  `make robot-start` — ez a `tools/start-robot.sh`-t futtatja (agent, roboclaw, ros2-shell ablakok). Vagy manuálisan:  
  - 1. terminál: agent (ha kell)  
  - 2. terminál: `source host_ws/install/setup.bash && ros2 launch roboclaw_tcp_adapter roboclaw.launch.py`  
  - 3. terminál: `source host_ws/install/setup.bash && bash` (ROS2 shell)

### Nyitott pontok

- [ ] ROS2 telepítése a Linux hoston (pl. Jazzy), majd `make host-build` újra
- [ ] `make robot-start` (tmux) vagy manuális indítási parancsok tesztelése

---

## 2026-03-09 — RoboClaw Host Workspace implementáció

### Kiindulási állapot

- A motorvezérlő (Basicmicro RoboClaw) integrációs architektúra véglegesítve
- Döntés: közvetlen TCP socket adapter (socat nélkül), monorepo host_ws/ könyvtárban
- Terv: `/Users/m2mini/.cursor/plans/roboclaw_host_workspace_8223f7cd.plan.md`

### Elvégzett munka

**1. Git submodule-ok hozzáadása**
- `host_ws/src/basicmicro_python` — upstream Python library (Packet Serial, CRC, 150+ parancs)
- `host_ws/src/basicmicro_ros2` — upstream ROS2 driver (ros2_control, /cmd_vel, odometry)
- `.gitmodules` automatikusan frissítve

**2. `roboclaw_tcp_adapter` ROS2 csomag — 4 Python modul**
- `tcp_port.py` — `RoboClawTCPPort`: serial.Serial API-kompatibilis TCP socket adapter
  - `TCP_NODELAY=1`, `sendall()`, non-blocking `flushInput()`
- `basicmicro_tcp.py` — `RoboClawTCP(Basicmicro)`: csak `Open()` override, 150+ parancs öröklődik
- `safety_bridge_node.py` — `/robot/estop` (Bool) → `/emergency_stop` (Empty) bridge
  - Két trigger: aktív E-Stop + 2s silence watchdog
- `roboclaw_tcp_node.py` — monkey-patch entry point
  - `basicmicro.controller.Basicmicro` → `RoboClawTCP` csere import előtt

**3. ROS2 package infrastruktúra**
- `package.xml` (ament_python, rclpy, std_msgs)
- `setup.py` + `setup.cfg` (console_scripts: roboclaw_tcp_node, safety_bridge_node)
- `resource/roboclaw_tcp_adapter` (ament index marker)
- `config/roboclaw_params.yaml` (driver paraméterek)

**4. Launch és hálózati konfiguráció**
- `launch/roboclaw.launch.py` — két node (driver + safety bridge), paraméterezett
- `host_ws/config/robot_network.yaml` — egyetlen hálózati konfig forrás (agent, RoboClaw, Picók, ROS)

**5. Startup és build rendszer**
- `tools/start-robot.sh` — tmux session 3 ablakkal (agent, roboclaw, ros2-shell)
  - Headless, SSH-n is működik (gnome-terminal kiváltva)
  - robot_network.yaml-ból olvassa a hálózati paramétereket
- `Makefile` bővítve: `host-install-deps`, `host-build`, `host-shell`, `robot-start`

**6. `host_ws/README.md` — részletes architektúra dokumentáció**
- 11 fejezet: bevezető, döntési napló, rendszer architektúra, hálózati topológia
- Komponens deep dive (TCPPort, RoboClawTCP, monkey-patch, safety bridge)
- Telepítés, konfiguráció, futtatás, hibakeresés, kiszakítási terv, roadmap

### Létrehozott fájlok (13 fájl)

| Fájl | Leírás |
|------|--------|
| `host_ws/README.md` | Verbose architektúra dokumentáció |
| `host_ws/config/robot_network.yaml` | Hálózati konfig SSOT |
| `host_ws/src/roboclaw_tcp_adapter/package.xml` | ROS2 csomag definíció |
| `host_ws/src/roboclaw_tcp_adapter/setup.py` | Python setup |
| `host_ws/src/roboclaw_tcp_adapter/setup.cfg` | Install scripts config |
| `host_ws/src/roboclaw_tcp_adapter/resource/roboclaw_tcp_adapter` | ament marker |
| `host_ws/src/roboclaw_tcp_adapter/config/roboclaw_params.yaml` | Driver paraméterek |
| `host_ws/src/roboclaw_tcp_adapter/launch/roboclaw.launch.py` | ROS2 launch file |
| `host_ws/src/roboclaw_tcp_adapter/roboclaw_tcp_adapter/__init__.py` | Package init |
| `host_ws/src/roboclaw_tcp_adapter/roboclaw_tcp_adapter/tcp_port.py` | TCP socket adapter |
| `host_ws/src/roboclaw_tcp_adapter/roboclaw_tcp_adapter/basicmicro_tcp.py` | Basicmicro subclass |
| `host_ws/src/roboclaw_tcp_adapter/roboclaw_tcp_adapter/safety_bridge_node.py` | Safety bridge |
| `host_ws/src/roboclaw_tcp_adapter/roboclaw_tcp_adapter/roboclaw_tcp_node.py` | Entry point |

### Módosított fájlok (1 fájl)

| Fájl | Változás |
|------|---------|
| `Makefile` | +4 target: host-install-deps, host-build, host-shell, robot-start |

### Új fájlok (git által kezelt)

| Fájl | Változás |
|------|---------|
| `.gitmodules` | 2 submodule (basicmicro_python, basicmicro_ros2) |
| `tools/start-robot.sh` | tmux startup script |

### Érintetlen maradt

- `app/` — Zephyr firmware (Tier 1)
- `devices/` — per-board config.json
- `tools/start-eth.sh`, `docker-run-agent-udp.sh` — visszafelé kompatibilitás

### Pre-deployment validáció — 4 bug javítva (ERR-011..014)

A kód review során 4 kritikus hiba derült ki, mind hardveres teszt előtt javítva:

**ERR-011: Hibás import path** — `basicmicro_driver.basicmicro_driver` → `basicmicro_driver.basicmicro_node`

**ERR-012: Hiányos monkey-patch** — Python `from X import Y` name binding: a patch nem propagálódott `basicmicro.Basicmicro`-ra. Javítás: mindkét helyen (`basicmicro.controller.Basicmicro` + `basicmicro.Basicmicro`) patchelni kell.

**ERR-013: Nem létező /emergency_stop subscriber** — Az upstream driver nem implementálta (a doksija említi, a kódja nem). Javítás: zero Twist a `cmd_vel`-re 10 Hz-en amíg E-Stop aktív.

**ERR-014: Konstruktor inkompatibilitás** — `RoboClawTCP(host, port)` vs upstream hívás `Basicmicro(comport, rate)`. Javítás: azonos `(comport, rate, ...)` szignatúra, TCP URL parse belülről.

| Fájl | Változás |
|------|---------|
| `roboclaw_tcp_node.py` | Import path fix + dual monkey-patch |
| `basicmicro_tcp.py` | Konstruktor átírás: `(comport, rate)` + URL parser |
| `safety_bridge_node.py` | Zero Twist cmd_vel override + /emergency_stop forward compat |
| `roboclaw.launch.py` | Új safety bridge paraméterek (cmd_vel_topic, rate) |

### Még nem tesztelt / nyitott

- [ ] Hardveres teszt: TCP kapcsolat a valós USR-K6 modulhoz
- [ ] `make host-install-deps` + `make host-build` teljes pipeline teszt
- [ ] `make robot-start` tmux session end-to-end
- [ ] Safety bridge E-Stop → zero cmd_vel → motor halt lánc validáció
- [ ] Odometria kalibrálás (wheel radius, separation, encoder CPR)
- [ ] Reconnect loop teszt: USR-K6 hálózati megszakítás kezelése

---

## 2026-03-08 — Foxglove Studio integráció, start-all.sh

### Elvégzett munka

**1. `tools/start-all.sh` — Teljes környezet egylépéses indítása**

Új script, ami sorrendben, egymásra várva indítja az összes szolgáltatást:

1. micro-ROS Agent (UDP :8888) — gnome-terminal ablakban
2. Foxglove Bridge (WS :8765) — háttér konténer, megvárja a port megnyílását
3. ROS2 Jazzy shell — gnome-terminal ablakban
4. Foxglove Studio — natív snap app (ha telepítve van)

Leállítás: `./tools/start-all.sh --stop`

**2. Foxglove Studio ajánlás dokumentálva**

- Bridge: Docker konténerben marad (Dockerfile.foxglove, start-foxglove.sh — már meglévő)
- Studio (kliens): natív snap telepítés (`sudo snap install foxglove-studio`)
- Architektúra: Pico → UDP → Agent → DDS → Foxglove Bridge → WS :8765 → Studio

### Érintett fájlok

| Fájl | Változás |
|------|---------|
| `tools/start-all.sh` | **Új** — teljes környezet indító script |

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
