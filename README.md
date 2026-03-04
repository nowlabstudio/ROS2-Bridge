# W6100 EVB Pico — Zephyr + micro-ROS Universal Bridge

## Mi ez?

Ez egy univerzális ROS2 bridge mikrokontrollerre (Raspberry Pi Pico / RP2040 + WIZnet W6100 Ethernet chip).

A Pico fizikai eszközöket (szenzorok, motorok, GPIO, Serial, I2C, SPI) köt össze a ROS2 hálózattal Ethernet-en keresztül. Minden csatolt eszköz ROS2 node-ként jelenik meg a hálózaton — publish és subscribe irányban egyaránt.

```
[Szenzorok / Motorok / GPIO / Serial / I2C / SPI]
                      │
             [W6100 EVB Pico]
          Zephyr RTOS + micro-ROS
                      │ Ethernet UDP
             [micro-ros-agent]
                      │ DDS
               [ROS2 hálózat]
```

---

## Hardver

| Komponens | Leírás |
|-----------|--------|
| Board | WIZnet W6100 EVB Pico |
| MCU | Raspberry Pi RP2040 (dual-core Cortex-M0+) |
| Ethernet chip | WIZnet W6100 (hardwired TCP/IP) |
| Flash | 16MB |
| RAM | 264KB |
| PIO | 2 db (enkóderhez használva) |

**Hálózati beállítások (jelenlegi):**
- Pico IP: `192.168.68.114` (statikus, `config.json`-ban — DHCP-re is átállítható)
- ROS2 gép IP: `192.168.68.125`
- micro-ros-agent port: `8888` (UDP)

---

## Szoftver stack

| Réteg | Technológia |
|-------|-------------|
| RTOS | Zephyr RTOS (main branch) |
| Hálózat | Zephyr networking, W6100 driver |
| ROS2 kommunikáció | micro-ROS (Jazzy libs, Humble agent kompatibilis) |
| Transport | UDP (custom Zephyr transport) |
| Konfiguráció | LittleFS + JSON — **KÉSZ, MŰKÖDIK** |
| Shell | Zephyr shell, USB CDC ACM konzolon |
| Build | west (Zephyr build tool), Docker |

---

## Könyvtárstruktúra

```
W6100_EVB_Pico_Zephyr_MicroROS/
│
├── README.md                        ← ez a fájl
├── Makefile                         ← build, flash, monitor parancsok
│
├── docker/
│   └── Dockerfile                   ← Docker image definíció
│
├── tools/
│   └── upload_config.py             ← Python konfig feltöltő (Mac-ről)
│
├── workspace/                       ← Zephyr workspace (west init, NE szerkeszd)
│   ├── zephyr/
│   ├── modules/lib/micro_ros_zephyr_module/
│   └── build/zephyr/zephyr.uf2      ← fordítás kimenete, ez kerül a Picóra
│
└── app/                             ← A MI KÓDUNK — itt dolgozunk
    ├── CMakeLists.txt               ← build konfiguráció, új fájlokat ide kell felvenni
    ├── prj.conf                     ← Zephyr Kconfig (modulok engedélyezése)
    ├── west.yml                     ← függőségek (Zephyr, micro-ROS)
    ├── config.json                  ← konfig minta / referencia dokumentum
    │
    ├── boards/
    │   └── w5500_evb_pico.overlay   ← hardver overlay (W6100, USB CDC, LittleFS partíció)
    │
    └── src/
        ├── main.c                   ← belépési pont
        ├── config/
        │   ├── config.h             ← bridge_config_t struktúra, API deklarációk
        │   └── config.c             ← LittleFS mount, JSON read/write, config_set()
        └── shell/
            └── shell_cmd.c          ← 'bridge' shell parancsok
```

---

## Tervezett könyvtárstruktúra (következő lépések után)

```
app/src/
├── main.c
├── config/
│   ├── config.h / config.c          ← KÉSZ
├── shell/
│   └── shell_cmd.c                  ← KÉSZ
├── bridge/
│   ├── channel_manager.h / .c       ← csatornák dinamikus kezelése (TERVEZETT)
│   └── ros_bridge.h / .c            ← micro-ROS pub/sub dinamikusan (TERVEZETT)
├── drivers/                         ← beépített driverek (NE SZERKESZD)
│   ├── drv_gpio.h / .c
│   ├── drv_serial.h / .c
│   ├── drv_adc.h / .c
│   ├── drv_pwm.h / .c
│   └── drv_i2c.h / .c
└── user/                            ← TE IDE ÍRSZ
    ├── user_channels.c              ← csatornák regisztrálása
    ├── motor_left.c                 ← bal motor + enkóder + PID
    ├── motor_right.c                ← jobb motor + enkóder + PID
    └── custom_sensor.c              ← egyéb eszköz
```

---

## Belépési pontok fejlesztéshez

### 1. Konfiguráció megváltoztatása — újrafordítás NÉLKÜL

**Soros shell-en** (`make monitor`):
```
bridge config show
bridge config set network.dhcp false
bridge config set network.ip 192.168.68.114
bridge config set network.netmask 255.255.255.0
bridge config set network.gateway 192.168.68.1
bridge config set network.agent_ip 192.168.68.125
bridge config set network.agent_port 8888
bridge config set ros.node_name my_robot
bridge config set ros.namespace /robot1
bridge config save
bridge reboot
```

**Python feltöltővel** (Mac-ről, `app/config.json` szerkesztése után):

Telepítési feltétel (egyszer):
```bash
pip3 install pyserial
```

Használat:
```bash
# Szerkeszd az app/config.json-t, majd:
python3 tools/upload_config.py

# Ha a port nem található automatikusan:
python3 tools/upload_config.py --port /dev/tty.usbmodem231401

# Egyedi config fájl:
python3 tools/upload_config.py --config app/config.json --port /dev/tty.usbmodem231401
```

**Fontos:** A Python uploader és a `make monitor` (screen) NEM futhatnak egyszerre — mindkettő a soros portot használja. Zárd be a screen-t (`Ctrl+A K Y`) mielőtt a Python scriptet futtatod.

### 2. Saját szenzor / aktuátor hozzáadása

**Fájl:** `app/src/user/` könyvtár *(tervezett, még nem létezik)*

A bridge core nem változik — csak ide kell megírni az eszköz logikát, és regisztrálni a channel manager-ben.

### 3. Új forrásfájl hozzáadása a buildhez

**Fájl:** `app/CMakeLists.txt`
```cmake
target_sources(app PRIVATE
    src/main.c
    src/config/config.c
    src/shell/shell_cmd.c
    src/user/motor_left.c      ← új fájl
)
```

---

## Build és flash parancsok

```bash
# Docker image build (csak egyszer kell)
make docker-build

# Zephyr workspace letöltés (csak egyszer kell, ~2GB)
make workspace-init

# Firmware fordítás
make build

# Flash (BOOTSEL gomb + USB csatlakoztatás, majd)
cp workspace/build/zephyr/zephyr.uf2 /Volumes/RPI-RP2/

# Soros monitor (115200 baud, DTR trigger)
make monitor

# Docker shell (manuális parancsokhoz)
make shell

# Build törlése
make clean
```

---

## Soros konzol és shell

A board USB-n CDC ACM soros portként jelenik meg.

**macOS port:** `/dev/tty.usbmodem231401`

**Fontos:** A firmware a DTR jelet várja induláshoz — a board csak akkor indul el teljesen, ha a soros monitor csatlakozva van.

### Monitor indítása és kilépés

```bash
make monitor          # megnyitja: screen /dev/tty.usbmodem231401 115200
```

Kilépés a screen-ből:
```
Ctrl+A  majd  K  majd  Y
```

Ha a port foglalt (pl. előző screen nem zárult be):
```bash
screen -ls            # listázza a futó screen session-öket
screen -X quit        # bezárja az összes screen session-t
```

### Elérhető shell parancsok

```
bridge config show              ← teljes konfig megjelenítése
bridge config set <key> <val>   ← érték beállítása (NEM ment automatikusan)
bridge config save              ← mentés flash-be (LittleFS /lfs/config.json)
bridge config load              ← újratöltés flash-ből
bridge config reset             ← gyári alapértékek visszaállítása (nem ment)
bridge reboot                   ← újraindítás (betölti az elmentett konfigot)
```

**Tipikus munkafolyamat:**
```
bridge config set ros.node_name my_robot
bridge config set network.agent_ip 192.168.1.100
bridge config save
bridge reboot
```

**Érvényes kulcsok a `config set`-hez:**

Minden kulcs újrafordítás NÉLKÜL változtatható (reboot után él):
```
network.dhcp        ← DHCP be/ki: true vagy false
network.ip          ← Pico saját IP-je (ha dhcp=false)
network.netmask     ← alhálózati maszk (ha dhcp=false)
network.gateway     ← alapértelmezett átjáró (ha dhcp=false)
network.agent_ip    ← micro-ROS agent IP (ROS2 gép)
network.agent_port  ← micro-ROS agent port (alapértelmezett: 8888)
ros.node_name       ← ROS2 node neve
ros.namespace       ← ROS2 namespace (pl. /robot1)
```

---

## micro-ROS agent indítása (ROS2 gépen, Docker-ben)

A ROS2 egy Docker containerben fut a `192.168.68.125` gépen.

```bash
# Belépés a futó ROS2 containerbe
docker exec -it <container_neve> bash

# Majd a containerben:
source /opt/ros/humble/setup.bash
ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888
```

Ha a micro_ros_agent nincs telepítve:
```bash
apt install ros-humble-micro-ros-agent
```

**A Pico csatlakozása után ellenőrzés:**
```bash
ros2 topic list                       # látszik a /counter topic
ros2 topic echo /counter              # növekvő Int32 értékek
ros2 node list                        # látszik a /pico_bridge node
```

**Fontos:** Az agent-nek futnia kell MIELŐTT a Pico bootol — ha az agent nem elérhető, a Pico az `RCCHECK` hibán megakad és újraindításig vár.

---

## Konfig fájl struktúra (LittleFS: /lfs/config.json)

```json
{
  "network": {
    "dhcp": false,
    "ip": "192.168.68.114",
    "netmask": "255.255.255.0",
    "gateway": "192.168.68.1",
    "agent_ip": "192.168.68.125",
    "agent_port": "8888"
  },
  "ros": {
    "node_name": "pico_bridge",
    "namespace": "/"
  }
}
```

Az `app/config.json` a teljes referencia konfig (csatornákkal együtt) — ez a dokumentáció és a Python uploader forrása.

---

## Jelenlegi állapot

| Funkció | Állapot |
|---------|---------|
| Zephyr alap firmware | ✅ KÉSZ |
| W6100 Ethernet driver | ✅ KÉSZ |
| DHCP / statikus IP (config.json-ból) | ✅ KÉSZ, tesztelt |
| Ethernet link UP detektálás (boot stabilitás) | ✅ KÉSZ |
| micro-ROS UDP transport | ✅ KÉSZ |
| ROS2 kapcsolat (publish) | ✅ KÉSZ, tesztelt |
| Státusz LED (GP25) — ROS2 agent jelzés | ✅ KÉSZ, tesztelt |
| LittleFS flash partíció | ✅ KÉSZ |
| JSON konfig read/write | ✅ KÉSZ |
| Soros shell (bridge parancsok) | ✅ KÉSZ, tesztelt |
| Python konfig uploader + tesztelő | ✅ KÉSZ, tesztelt |
| Channel Manager (pub/sub keretrendszer) | ✅ KÉSZ |
| User kódtér (user_channels.c) | ✅ KÉSZ |
| Kódbázis biztonsági audit + hardening | ✅ KÉSZ |
| Runtime IP konfig (reboot nélkül, hot-reload) | 🔄 TERVEZETT |
| GPIO driver | 🔄 TERVEZETT |
| Serial driver | 🔄 TERVEZETT |
| ADC driver | 🔄 TERVEZETT |
| PWM / motor driver | 🔄 TERVEZETT |
| Enkóder (PIO) | 🔄 TERVEZETT |
| PID szabályozó | 🔄 TERVEZETT |
| Subscribe irány (ROS2 → Pico) | 🔄 TERVEZETT |
| Channel konfig JSON-ból | 🔄 TERVEZETT |

---

## Implementációs sorrend (következő lépések)

### 2. lépés — Channel manager
- Csatorna struktúra (`channel_t`) definíció
- Csatornák regisztrálása config.json alapján
- Soft real-time loop (10ms, magas prioritású thread)
- `app/src/bridge/channel_manager.h/.c`

### 3. lépés — Built-in driverek
- GPIO (be/kimenet)
- ADC (analóg olvasás)
- PWM (motor, szervó)
- Serial (UART)
- `app/src/drivers/`

### 4. lépés — micro-ROS pub/sub dinamikusan
- Publisher és subscriber létrehozása a channel config alapján
- Üzenettípus kezelés (Bool, Int32, Float32, String)
- Subscribe irány: ROS2 parancs → Pico → aktuátor

### 5. lépés — Enkóder + PID
- PIO alapú enkóder driver (2 csatorna, hardware quadrature)
- PID könyvtár (soft real-time, ~10ms loop)
- Motor+enkóder csatorna típus

### 6. lépés — User kódtér
- `app/src/user/` könyvtár kialakítása
- API: `channel_register()`, `channel_publish()`, `channel_get_setpoint()`
- Példa kód: motor_left.c, custom_sensor.c

---

## Ismert korlátok

| Korlát | Magyarázat |
|--------|------------|
| Max 2 enkóder | RP2040 csak 2 PIO blokkot tartalmaz |
| RAM: ~48KB szabad | micro-ROS + shell + LittleFS ~216KB-t használ |
| Soft real-time | Hard real-time nem garantált Zephyrrel |
| IP reboot nélkül nem változtatható | `config save` + `bridge reboot` szükséges az IP-váltáshoz |
| Zephyr board neve `w5500_evb_pico` | Zephyr még nem tartalmaz `w6100_evb_pico` definíciót |

---

## Fejlesztői környezet

| | |
|-|-|
| Host gép | macOS |
| Build | Docker (`w6100-zephyr-microros:latest`) |
| Zephyr SDK | 0.17.4 (ARM toolchain, Cortex-M0+) |
| Zephyr verzió | main (v4.3.99) |
| micro-ROS | Jazzy libs, Humble agent kompatibilis |
| Serial port | `/dev/tty.usbmodem231401` |
| Firmware méret | 277KB flash, 216KB RAM |
