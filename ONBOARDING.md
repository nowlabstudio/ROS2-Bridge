# W6100 EVB Pico + RoboClaw — Rendszer onboarding

> **Naprakész tartalom.** A rendszer változásával ezt a dokumentumot a CHANGELOG/ERRATA frissítése mellett kell frissíteni (lásd `.cursor/rules/`).

---

## 1. Mi ez a rendszer?

**Univerzális ROS2 bridge** Ethernet alapú robotkonfigurációhoz:

- **Tier 1 (MCU):** W6100 EVB Pico táblák (Zephyr + micro-ROS) — E-Stop, RC vevő, szenzorok → UDP → agent
- **Tier 2 (Linux host):** Dockerben futó micro-ROS agent + RoboClaw TCP driver + safety bridge + ROS2 Jazzy
- **Motorvezérlés:** RoboClaw (Basicmicro) TCP-n keresztül (USR-K6 Ethernet–Serial)

Minden **Etherneten** megy; a hoston nincs USB/serial függőség. A ROS2 gráfban minden node (Pico-k, RoboClaw driver) egy helyen látszik.

---

## 2. Architektúra (egy mondatban)

```
[Pico E-Stop, RC, pedal] --UDP:8888--> [Agent] --DDS--> [RoboClaw driver + safety bridge]
                                              \__ [ROS2 shell, Foxglove, Portainer]
[RoboClaw] --TCP:8234--> [USR-K6] ----------> RoboClaw driver (Docker)
```

- **docker-compose** indítja: `agent`, `roboclaw`, `ros2-shell`. Opcionálisan: `portainer` (management UI).
- **Foxglove** = ROS2 vizualizáció (topicok, robot működés).
- **Portainer** = konténerek, logok, restart, shell (rendszer szint).

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

# 2. Host workspace build (ROS2 csomagok Dockerben — nem kell natív ROS2)
make host-build-docker
```

Ez létrehozza a `host_ws/install/`-t (basicmicro_ros2, roboclaw_tcp_adapter). Ha a kódot később módosítod, ugyanezt futtasd újra.

---

## 5. Konfiguráció (egy helyen)

**Fájl:** `host_ws/config/robot_network.yaml`

| Jelentés | Alapértelmezett | Hol használják |
|----------|-----------------|----------------|
| Agent port | 8888 | compose: `AGENT_PORT` |
| RoboClaw host | 192.168.68.60 | compose: `ROBOCLAW_HOST` |
| RoboClaw port | 8234 | compose: `ROBOCLAW_PORT` |
| Pico-k IP (estop, rc, pedal) | 192.168.68.201–203 | dokumentáció / Pico config.json |

Compose-ban a környezeti változókat át lehet írni (pl. `.env` vagy `ROBOCLAW_HOST=192.168.68.61 make robot-start`). A YAML a single source of truth a doksi és a scriptek számára.

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
ros2 topic echo /robot/motor_left std_msgs/msg/Float32
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

- **Web editor (ajánlott):** Add stack → **Web editor** → beilleszted a `docker-compose.yml` tartalmát. A stack **path** legyen a repo gyökere a hoston (ahol már lefutott a `make host-build-docker`), hogy a `./host_ws` és `./tools` mountok és a `host_ws/install` létezzen.
- **Deploy from repository:** Add stack → **Repository** → repo URL + `docker-compose.yml`. Portainer klónozza a repót; a klónban nincs `host_ws/install`, ezért az első deploy után a klónolt könyvtárban futtasd a hoston: `make host-build-docker`, majd a stacket indítsd újra.

**Indítás / leállítás:** A Portainerből a „Start this stack” és a „Stop this stack” működik. Ha mégsem állna le: a hoston `make robot-stop` (vagy `docker compose down`).

---

## 7. Mi fut és hol?

| Szolgáltatás | Konténer | Kép | Mit csinál |
|--------------|----------|-----|------------|
| agent | w6100_bridge_agent_udp | microros/micro-ros-agent:jazzy | UDP :8888, micro-ROS → DDS |
| roboclaw | w6100_bridge_roboclaw | ros:jazzy | roboclaw_tcp_adapter launch (driver + safety_bridge) |
| foxglove | w6100_foxglove_bridge | w6100-foxglove (build) | WebSocket :8765 → Foxglove Studio |
| ros2-shell | w6100_bridge_ros2 | ros:jazzy | Interaktív bash, ROS2 + host_ws be van töltve |
| portainer | portainer | portainer/portainer-ce | Web UI (profile: management) |

Mindegyik `network_mode: host`, így ugyanaz a DDS látótér (CycloneDDS, `tools/cyclonedds.xml`).

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
| `make robot-start` | Stack indítása |
| `make robot-stop` | Stack leállítása |
| `make robot-ps` | Konténer állapot |
| `make robot-logs` | Logok követése |
| `make robot-logs-roboclaw` | Csak roboclaw log |
| `make robot-shell` | ROS2 shell (detach: Ctrl+P, Ctrl+Q) |
| `make host-build-docker` | host_ws újraépítése Dockerben |
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

- **RoboClaw "timed out":** USR-K6 + RoboClaw elérhető? `ROBOCLAW_HOST`/`ROBOCLAW_PORT` és a hálózat rendben?
- **"No module named 'basicmicro_driver'":** `make host-build-docker` lefutott? PYTHONPATH a compose-ban a forrásra mutat (`host_ws/src/basicmicro_ros2`).
- **Pico-k nem látszanak:** Agent fut? `make robot-ps`. Pico-k `agent_ip`/`agent_port` a config.json-ban = host IP és 8888?
- **Portainer nem indul:** Van internet? Első indításkor image pull kell. Ha nincs net: `make robot-start` ettől még működik (Portainer profile külön indul).
- **Foxglove konténer nem indul / localhost:8765 nem elérhető:** A Foxglove image a Portainer (repo) deploy-nál nem épül meg. A hoston futtasd egyszer: `make foxglove-build`, majd a stacket indítsd újra (Portainerben vagy `make robot-start`). Log: `docker compose logs foxglove`.
