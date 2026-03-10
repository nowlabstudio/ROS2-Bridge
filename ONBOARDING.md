# W6100 EVB Pico + RoboClaw — Rendszer onboarding

> **Naprakész tartalom.** A rendszer változásával ezt a dokumentumot a CHANGELOG/ERRATA frissítése mellett kell frissíteni (lásd `.cursor/rules/`).

---

## 1. Mi ez a rendszer?

**Univerzális ROS2 bridge** Ethernet alapú robotkonfigurációhoz:

- **Tier 1 (MCU):** W6100 EVB Pico táblák (Zephyr + micro-ROS) — E-Stop, RC vevő, szenzorok → UDP → agent
- **Tier 2 (Linux host):** Dockerben futó micro-ROS agent + C++ ros2_control RoboClaw driver + RC teleop + ROS2 Jazzy
- **Motorvezérlés:** RoboClaw (Basicmicro) TCP-n keresztül (USR-K6 Ethernet–Serial)

Minden **Etherneten** megy; a hoston nincs USB/serial függőség. A ROS2 gráfban minden node (Pico-k, RoboClaw driver) egy helyen látszik.

---

## 2. Architektúra

### RC csatorna kiosztás

| Csatorna | Pico GPIO | Topic | Funkció |
|----------|-----------|-------|---------|
| CH1 | GP2 | `/robot/rc_ch1` | **Jobb motor** [-1..+1] |
| CH2 | GP3 | `/robot/rc_ch2` | **Bal motor** [-1..+1] |
| CH3 | GP4 | `/robot/rc_ch3` | Nincs bekötve |
| CH4 | GP5 | `/robot/rc_ch4` | Nincs bekötve |
| CH5 | GP6 | `/robot/rc_ch5` | **ROS/RC mode switch** — azonnali átváltás (safety!) |
| CH6 | GP7 | `/robot/rc_ch6` | **Winch** (később) |

### Adatfolyam diagram

```
┌───────────────────┐
│  TÁVIRÁNYÍTÓ       │  Tank mix: CH1=jobb, CH2=bal motor
│  (PWM 1000-2000µs)│  CH5=ROS/RC switch, CH6=winch
└────────┬──────────┘
         │ PWM jelvezeték (6 csatorna)
         ▼
┌───────────────────┐
│  W6100 PICO       │  rc_ch1..ch6 → normalize [-1..+1]
│  (Zephyr+µROS)    │  → Float32 topic-ok (50Hz stick / 20Hz switch)
└────────┬──────────┘
         │ UDP :8888
         ▼
┌───────────────────┐
│  micro-ROS Agent  │  UDP → DDS/ROS2 topic-ok
└────────┬──────────┘
         │ /robot/rc_ch1..ch6, /robot/estop
         ▼
┌───────────────────┐
│  rc_teleop_node   │  CH5 figyeli → RC/ROS mód váltás
│  (roboclaw-hw)    │  RC módban: tank→arcade konverzió:
│                   │    linear.x  = (ch2+ch1)/2 * max_speed
│                   │    angular.z = (ch2-ch1)/2 * max_angular
│                   │  ROS módban: nem publikál (Nav2 veszi át)
│                   │  E-Stop → mindig zero, bármelyik módban
└────────┬──────────┘
         │ /diff_drive_controller/cmd_vel (TwistStamped, 20Hz)
         ▼
┌───────────────────┐
│  diff_drive_      │  ros2_control: cmd_vel → bal/jobb rad/s
│  controller       │  cmd_vel_timeout: 0.5s (safety)
└────────┬──────────┘
         │ velocity command
         ▼
┌───────────────────┐
│  RoboClawHardware │  C++ SystemInterface, 100Hz RT loop
│  (ros2_control)   │  TCP → USR-K6 → UART → RoboClaw
└────────┬──────────┘
         │ TCP :8234 (Ethernet)
         ▼
┌───────────────────┐
│  RoboClaw 2x60A   │  M1 = bal motor, M2 = jobb motor
│  + USR-K6 bridge  │  Encoder feedback → position/velocity
└───────────────────┘
```

- **docker-compose** indítja: `agent`, `roboclaw`, `foxglove`, `ros2-shell`. Opcionálisan: `portainer`.
- **Foxglove** = ROS2 vizualizáció (topicok, robot működés).
- **Portainer** = konténerek, logok, restart, shell (rendszer szint).

### ROS/RC mode switch (CH5)

A CH5 kapcsoló **azonnali** átváltást biztosít ROS (autonóm) és RC (kézi) üzemmód között.
RC módra váltás safety funkcióként is szolgál: azonnal átveszi az irányítást a ROS-tól.

| CH5 állapot | Mód | Ki publikál cmd_vel-t? | Viselkedés |
|-------------|------|------------------------|------------|
| < 0 (vagy center) | **ROS** | Nav2 / teleop | rc_teleop_node nem publikál |
| > 0 | **RC** | rc_teleop_node | Nav2 cmd_vel figyelmen kívül (rc_teleop felülírja) |

Átváltáskor az `rc_teleop_node` **azonnal** zero cmd_vel-t küld (pillanatnyi megállás),
majd az RC botok aktuális pozícióját kezdi konvertálni.

### Autonóm üzemmód (Nav2)

CH5 ROS módban Nav2 publikálja a `cmd_vel`-t:

```
Nav2 → /diff_drive_controller/cmd_vel → diff_drive_controller → RoboClawHardware → motorok
```

Egyetlen interface (`cmd_vel`) — az RC/ROS mode switch (CH5) választ köztük.

---

## 3. Előfeltételek

- **Linux** (Ubuntu ajánlott; `--net=host` miatt)
- **Docker** + **Docker Compose**
- **Git** (repo + submodule-ok)
- Hálózat: a host és a Pico-k/RoboClaw (USR-K6) egy LAN-on (pl. 192.168.68.0/24)

---

## 4. Első alkalom: klón + build

Minden parancs a **repo gyökérből** (ahol a `Makefile` és a `docker-compose.yml` van).

```bash
# 1. Klón + submodule-ok
git clone <repo-url>
cd W6100_EVB_Pico_Zephyr_MicroROS
git submodule update --init --recursive

# 2. Host workspace build (C++ driver + RC teleop, Dockerben — nem kell natív ROS2)
make host-build

# 3. Foxglove bridge image (egyszer, ha Foxglove-ot használni akarsz)
make foxglove-build
```

Ez létrehozza a `host_ws/install/`-t (`roboclaw_hardware`, `roboclaw_tcp_adapter`). Ha a kódot később módosítod, `make host-build` újra.

---

## 5. Konfiguráció (egy helyen)

**Környezeti változók** (docker-compose.yml, felülírható `.env` fájlból vagy parancssorból):

| Változó | Alapértelmezett | Hol használják |
|---------|-----------------|----------------|
| `AGENT_PORT` | 8888 | micro-ROS Agent UDP port |
| `ROBOCLAW_HOST` | 192.168.68.60 | USR-K6 IP (Ethernet-Serial bridge) |
| `ROBOCLAW_PORT` | 8234 | USR-K6 TCP port |

Felülírás: `ROBOCLAW_HOST=192.168.68.61 make robot-start`

**Pico konfiguráció:** Minden Pico saját `config.json`-ban tartja az `agent_ip`/`agent_port` értékeket (192.168.68.201–203).

---

## 6. Napi használat

### Indítás / leállítás

```bash
make robot-start    # docker compose up -d
make robot-stop     # docker compose down
```

### Állapot, logok, shell

```bash
make robot-ps              # futó konténerek
make robot-logs            # összes log (Ctrl+C = kilépés, konténerek futnak)
make robot-logs-roboclaw   # csak roboclaw
make robot-shell           # belépés a ROS2 konténerbe (Ctrl+P Ctrl+Q = detach, ne exit)
```

### ROS2 gyors ellenőrzés (robot-shell-ben vagy `docker compose exec ros2-shell bash -c "..."`)

```bash
ros2 node list
ros2 topic list
ros2 topic echo /robot/estop std_msgs/msg/Bool
ros2 topic echo /robot/rc_ch2 std_msgs/msg/Float32
```

### Portainer (opcionális, ha van internet)

```bash
make portainer-start   # első alkalommal letölti az image-et
# Böngésző: https://<robot-ip>:9443
make portainer-stop
```

### Portainer: custom template a robot stackhez

A repóban a **W6100 Robot Stack** egyéni template van: `tools/portainer-templates.json`. Így jelenik meg a stack a Portainer App Templates listájában (egy kattintásos deploy opcióval).

**Template hozzáadása Portainerben:**

1. Portainer → **Settings** → **Custom templates**.
2. **Add custom template**: URL mezőbe add meg a template JSON nyers URL-jét (pl. `https://raw.githubusercontent.com/<org>/<repo>/<branch>/tools/portainer-templates.json`), vagy másold be a `tools/portainer-templates.json` tartalmát (egy template tömböt vár a Portainer).
3. A templateben a **repository URL** placeholder (`YOUR_ORG`); cseréld ki a saját repóddal/forkkal, vagy deploy előtt a Portainer űrlapon módosítsd a repo URL-t.

**Deploy két gyakori módszer:**

- **Web editor (ajánlott):** Add stack → **Web editor** → beilleszted a `docker-compose.yml` tartalmát. A stack **path** legyen a repo gyökere a hoston (ahol már lefutott a `make host-build`), hogy a `./host_ws` és `./tools` mountok és a `host_ws/install` létezzen.
- **Deploy from repository:** Add stack → **Repository** → repo URL + `docker-compose.yml`. Portainer klónozza a repót; a klónban nincs `host_ws/install`, ezért az első deploy után a klónolt könyvtárban futtasd a hoston: `make host-build`, majd a stacket indítsd újra.

**Indítás / leállítás:** A Portainerből a „Start this stack” és a „Stop this stack” működik. Ha mégsem állna le: a hoston `make robot-stop` (vagy `docker compose down`).

---

## 7. Mi fut és hol?

| Szolgáltatás | Konténer | Kép | Mit csinál |
|--------------|----------|-----|------------|
| agent | w6100_bridge_agent_udp | microros/micro-ros-agent:jazzy | UDP :8888, micro-ROS → DDS |
| roboclaw | w6100_roboclaw | ros:jazzy | C++ ros2_control driver + rc_teleop (controller_manager, diff_drive, diagnostics, RC teleop) |
| foxglove | w6100_foxglove_bridge | w6100-foxglove (build) | WebSocket :8765 → Foxglove Studio |
| ros2-shell | w6100_bridge_ros2 | ros:jazzy | Interaktív bash, ROS2 + host_ws be van töltve |
| portainer | portainer | portainer/portainer-ce | Web UI (profile: management) |

Mindegyik `network_mode: host`, így ugyanaz a DDS látótér (CycloneDDS, `tools/cyclonedds.xml`).

---

## 7b. RC → Motor összekötés (teljes adatfolyam)

A távirányító **tank módban** ad jelet (CH1 = jobb motor, CH2 = bal motor).
A robot oldalán ez `cmd_vel`-re konvertálódik, így az RC és az autonóm navigáció
egyetlen interface-en (`/diff_drive_controller/cmd_vel`) osztozik.
A **CH5** kapcsoló azonnal átválthat RC és ROS mód között (safety funkció).

### Topic huzalozás

| # | Topic | Típus | Forrás | Cél | Frekvencia |
|---|-------|-------|--------|-----|------------|
| 1 | `/robot/rc_ch1` | Float32 | Pico (µROS) | rc_teleop_node | 50 Hz |
| 2 | `/robot/rc_ch2` | Float32 | Pico (µROS) | rc_teleop_node | 50 Hz |
| 3 | `/robot/rc_ch5` | Float32 | Pico (µROS) | rc_teleop_node | 20 Hz |
| 4 | `/robot/rc_ch6` | Float32 | Pico (µROS) | (winch — később) | 20 Hz |
| 5 | `/robot/estop` | Bool | Pico (µROS) | rc_teleop_node | 10 Hz |
| 6 | `/diff_drive_controller/cmd_vel` | TwistStamped | rc_teleop_node / Nav2 | diff_drive_controller | 20 Hz |
| 7 | `/diff_drive_controller/odom` | Odometry | diff_drive_controller | Nav2 / Foxglove | 50 Hz |
| 8 | `/dynamic_joint_states` | DynamicJointState | diagnostics_broadcaster | Foxglove | 100 Hz |

### Tank → Arcade konverzió (rc_teleop_node)

A Pico normalizált jeleket küld: `rc_ch1` = jobb motor [-1..+1], `rc_ch2` = bal motor [-1..+1].
Az `rc_teleop_node` visszakonvertálja arcade formátumra a `diff_drive_controller` számára:

```
linear.x  = (ch2 + ch1) / 2.0 * max_linear_speed     # előre/hátra
angular.z = (ch2 - ch1) / 2.0 * max_angular_speed     # fordulás
```

(ch2 = bal, ch1 = jobb → ha ch2 > ch1: robot balra fordul → pozitív angular.z)

### Paraméterek (`rc_teleop_node`)

| Paraméter | Alapértelmezett | Leírás |
|-----------|----------------|--------|
| `mixing_mode` | `tank` | `tank` vagy `arcade` (jövőben váltható) |
| `left_topic` | `/robot/motor_left` | Bal motor csatorna (CH2 a Pico config-ban) |
| `right_topic` | `/robot/motor_right` | Jobb motor csatorna (CH1 a Pico config-ban) |
| `mode_switch_topic` | `/robot/rc_mode` | ROS/RC mode switch (CH5 a Pico config-ban) |
| `max_linear_speed` | 4.5 m/s | Robot max sebesség (~16 km/h) |
| `max_angular_speed` | 3.0 rad/s | Robot max fordulási sebesség |
| `deadzone` | 0.05 | 5% — joystick/trim holtjáték |
| `publish_rate` | 20.0 Hz | cmd_vel publikálási frekvencia |
| `estop_topic` | `/robot/estop` | E-Stop gating: aktív → zero output |

### Fontos controller / driver paraméterek

| Fájl | Paraméter | Érték | Leírás |
|------|-----------|-------|--------|
| `diff_drive_controllers.yaml` | `linear.x.max_acceleration` | 6.0 m/s² | Gyors RC reakció; Nav2 velocity smoother felülírhatja |
| `diff_drive_controllers.yaml` | `angular.z.max_acceleration` | 6.0 rad/s² | Gyors fordulás RC-vel |
| `diff_drive_controllers.yaml` | `cmd_vel_timeout` | 0.5 s | Motor stop ha nem jön parancs |
| `diff_drive_controllers.yaml` | `open_loop` | true | Amíg enkóderek nincsenek bekötve |
| `roboclaw_diff_drive.urdf.xacro` | `motion_strategy` | duty | Open-loop PWM (nem kell enkóder) |
| `roboclaw_diff_drive.urdf.xacro` | `duty_max_rad_s` | 22.5 | Max kerékszögsebesség → 100% PWM |
| `roboclaw_diff_drive.urdf.xacro` | `encoder_stuck_limit` | 0 | Letiltva (nincs enkóder) |

### Safety rétegek (5 szint)

1. **CH5 mode switch** → RC módra váltás azonnal leválasztja a ROS-t, rc_teleop átveszi
2. **E-Stop (Pico hardver)** → `rc_teleop_node` zero output, bármelyik módban
3. **cmd_vel_timeout (0.5s)** → `diff_drive_controller` leállítja a motorokat ha nincs cmd_vel
4. **RoboClaw serial timeout** → motorvezérlő leáll ha nincs parancs
5. **Encoder health monitoring** → emergency stop stuck/runaway/comm failure esetén

### Foxglove debug panelek

| Panel típus | Topic | Mit mutat |
|-------------|-------|-----------|
| Plot | `/robot/rc_ch1`, `/robot/rc_ch2` | RC motor jelek (jobb/bal) |
| Plot | `/robot/rc_ch5` | ROS/RC mode switch állapot |
| Plot | `/diff_drive_controller/cmd_vel` | linear.x, angular.z (konvertált parancs) |
| Plot | `/diff_drive_controller/odom` | Tényleges odometria |
| Gauge | `/dynamic_joint_states` → `main_battery_v` | Akkufeszültség |
| Gauge | `/dynamic_joint_states` → `current_left_a/right_a` | Motoráram |
| Indicator | `/robot/estop` | E-Stop állapot |

---

## 8. Foxglove

- **Cél:** ROS2 topicok vizualizálása (robot működés).
- **Bridge:** A stack része (compose: `foxglove` szolgáltatás). A Portainer „Start this stack” indítja, **ha** a Foxglove image már meg van építve. Első alkalommal (vagy ha a konténer nem indul): a **hoston** futtasd egyszer: `make foxglove-build` (vagy `docker compose build foxglove`), majd indítsd újra a stacket. Alternatíva: `tools/start-foxglove.sh` a compose-on kívül.
- **Studio:** Connect → Foxglove WebSocket → `ws://<robot-ip>:8765` (vagy localhost, ha a Studio a hoston fut).

A rendszer szintű felügyelethez (konténerek, logok, restart) a **Portainer** való.

---

## 9. Parancs referencia (Makefile)

| Parancs | Rövid leírás |
|---------|----------------|
| `make help` | Összes cél |
| `make host-build` | C++ driver + RC teleop buildelése Dockerben |
| `make robot-start` | Stack indítása (agent + roboclaw + foxglove) |
| `make robot-stop` | Stack leállítása |
| `make robot-restart` | Stack újraindítása |
| `make robot-ps` | Konténer állapot |
| `make robot-logs` | Logok követése |
| `make robot-logs-roboclaw` | Csak RoboClaw driver log |
| `make robot-shell` | ROS2 shell (detach: Ctrl+P, Ctrl+Q) |
| `make robot-diagnostics` | GPIO diagnosztika echo (battery, temp, error, áram) |
| `make robot-motor-test` | Motor teszt cmd_vel-lel (LINEAR=0.05, DURATION=3) |
| `make robot-topics` | ROS2 topic lista |
| `make robot-controllers` | ros2_control kontrollerek listája |
| `make foxglove-build` | Foxglove bridge image buildelése (egyszer) |
| `make portainer-start` | Portainer indítása |
| `make portainer-stop` | Portainer leállítása |

**Zephyr (Tier 1) — ha firmware-t építesz:**
`make docker-build`, `make workspace-init`, `make build`, `tools/flash.sh`, `make monitor`. Részletek: `README.md`.

---

## 10. Dokumentáció a repóban

| Fájl | Tartalom |
|------|----------|
| **README.md** | Firmware (Zephyr, Pico), csatornák, config, shell parancsok, agent, Foxglove |
| **host_ws/README.md** | Host workspace, RoboClaw TCP adapter, döntési napló, hálózat |
| **ERRATA.md** | Ismert hibák, root cause, státusz |
| **CHANGELOG_DEV.md** | Sessionönkénti változások, nyitott pontok |
| **docker-compose.yml** | Szolgáltatások, portok, env, Portainer profile |
| **tools/portainer-templates.json** | Portainer custom template (W6100 Robot Stack), env: AGENT_PORT, ROBOCLAW_* |

---

## 11. Gyors hibaelhárítás

- **Driver nem indul:** `make host-build` lefutott? A roboclaw konténer logja: `make robot-logs-roboclaw`.
- **RoboClaw "timed out" / "ReadVersion failed":** USR-K6 + RoboClaw bekapcsolva és elérhető? `ROBOCLAW_HOST`/`ROBOCLAW_PORT` helyes?
- **Motor nem áll meg:** `cmd_vel_timeout: 0.5` a `diff_drive_controllers.yaml`-ban. Ha RC mód aktív, CH5-öt kapcsold ROS módra vagy engedd el a botokat.
- **Overrun figyelmeztetés:** Ethernet-en 100Hz-en normális teljesítmény (1 overrun/30s). WiFi-n gyakoribb — nem hiba, hálózati latencia (ERR-023).
- **Pico-k nem látszanak:** Agent fut? `make robot-ps`. Pico config.json-ban `agent_ip`/`agent_port` = host IP és 8888.
- **Portainer nem indul:** Van internet? Első indításkor image pull kell. `make robot-start` ettől még működik (Portainer profile külön indul).
- **Foxglove konténer nem indul:** `make foxglove-build` lefutott? Log: `docker compose logs foxglove`.
