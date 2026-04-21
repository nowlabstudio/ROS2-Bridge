# W6100 EVB Pico — Zephyr + micro-ROS Universal Bridge

> **Developer Reference Documentation**
> Last updated: 2026-04-21 (BL-014 Fázis 2 + BL-017 lezárva) | Version: v2.3 | Author: Eduard Sik — [eduard@nowlab.eu](mailto:eduard@nowlab.eu)

---

## What is this project?

A **universal ROS2 bridge** for microcontrollers. The hardware is a **WIZnet W6100 EVB Pico** board — a Raspberry Pi RP2040 microcontroller paired with the WIZnet W6100 hardwired TCP/IP Ethernet chip.

The goal: connect physical devices (sensors, motors, GPIO, RC receivers, encoders) to a ROS2 network over Ethernet UDP. Every connected device appears as a ROS2 **node** on the network — capable of both **publishing** (device → ROS2) and **subscribing** (ROS2 → device).

```
[Physical Devices]
  Sensors, motors, GPIO, E-Stop, RC receiver, PWM, ADC, encoders
            │
   [W6100 EVB Pico]
  Zephyr RTOS + micro-ROS
  USB CDC ACM console (debug/config)
            │ Ethernet UDP
  [micro-ros-agent]
  (running on ROS2 host, Docker)
            │ DDS (cyclonedds)
     [ROS2 Network]
  ros2 topic list / echo / pub
```

### Why this approach?

- **No custom driver needed** on the ROS2 host — the agent is the single entry point
- **Flexible channel system** — adding a new sensor requires editing just one file
- **Config-driven** — channel enable/disable, topic names, MAC, trim values — all from `config.json` without recompilation
- **Multi-board ready** — unique MAC per board (auto or config), unique hostname, unique node name
- **Robust** — hardware watchdog, automatic reconnection state machine, 500ms DTR timeout for autonomous mode

---

## Hardware

### Board Specification

| Component | Details |
|-----------|---------|
| Board name | WIZnet W6100 EVB Pico |
| MCU | Raspberry Pi RP2040 (dual-core Cortex-M0+ @ 133MHz) |
| RAM | 264 KB SRAM (internal) |
| Flash | 16 MB (W25Q128JV, SPI) |
| Ethernet chip | WIZnet W6100 (hardwired TCP/IP stack, SPI interface) |
| USB | Micro-USB, USB 2.0 Full Speed (CDC ACM console) |
| GPIO | 26 user GPIO pins |
| PIO | 2 PIO blocks (for encoders, custom protocols) |
| ADC | 3 channels (GP26, GP27, GP28) + internal temperature |
| PWM | 8 PWM slices, 16 channels |
| I2C | 2 I2C peripherals |
| SPI | 2 SPI peripherals |
| UART | 2 UARTs |

### Pinout — Important Pins

Pin-kiosztás **per-device**: BL-015 óta minden device saját overlay-t kap
(`apps/<device>/boards/w5500_evb_pico.overlay`), és csak a saját csatornáit
regisztrálja. Nincs cross-device pin-konfliktus.

| Pin | E_STOP | RC | PEDAL |
|-----|--------|----|-------|
| GP25 | user_led (bridge-online indikátor) | user_led | user_led |
| GP27 | estop_btn (NC, IRQ edge-both) | — | — |
| GP2  | mode_auto (ACTIVE_LOW, rotary) | rc_ch1 (PWM-in) | — |
| GP3  | mode_follow (ACTIVE_LOW, rotary) | rc_ch2 (PWM-in) | — |
| GP4  | okgo_btn_a (ACTIVE_LOW, IRQ) | rc_ch3 (PWM-in) | — |
| GP5  | okgo_btn_b (ACTIVE_LOW, IRQ) | rc_ch4 (PWM-in) | — |
| GP6  | — | rc_ch5 (PWM-in) | — |
| GP7  | — | rc_ch6 (PWM-in) | — |
| GP22 | okgo_led (Bool sub, ACTIVE_HIGH out) | — | — |
| GP26 | — | — | ADC0 (BL-012 későbbi) |
| GP28 | — | — | ADC2 (BL-012 későbbi) |
| GP0–GP1 | UART0 (shared) |||
| GP16–GP19 | SPI0 (W6100, shared) |||

> **Zephyr board name note:** The overlay file is named `w5500_evb_pico.overlay` because the Zephyr board target is `w5500_evb_pico`. The actual chip is W6100 — the overlay sets `compatible = "wiznet,w6100"`.

---

## Software Stack

| Layer | Technology | Notes |
|-------|-----------|-------|
| RTOS | Zephyr RTOS (main branch, v4.3.99) | Real-time kernel |
| Network stack | Zephyr networking, W6100 driver | UDP, DHCP, static IP |
| ROS2 comms | micro-ROS (Jazzy libs) | Compatible with Humble agent |
| Transport | UDP custom Zephyr transport | `microros_transports.h` |
| Configuration | LittleFS + custom JSON parser | Flash at 15MB offset, 1MB partition |
| Shell | Zephyr shell, USB CDC ACM | `bridge` commands |
| Build | west (Zephyr build tool), Docker | `w6100-zephyr-microros:latest` |

---

## Directory Structure

```
W6100_EVB_Pico_Zephyr_MicroROS/
│
├── README.md                        ← this file
├── ERRATA.md                        ← known bugs, root cause analysis, status
├── CHANGELOG_DEV.md                 ← development log, per-session changes
├── Makefile                         ← build, flash, monitor commands
│
├── docker/
│   ├── Dockerfile                   ← Docker image: Zephyr SDK + micro-ROS tools
│   └── Dockerfile.foxglove          ← Foxglove Bridge image (ROS Jazzy + foxglove-bridge)
│
├── tools/
│   ├── upload_config.py             ← Python config uploader (serial port)
│   ├── flash.sh                     ← Flash firmware via 'bridge bootsel' (no button needed)
│   ├── docker-run-agent-udp.sh      ← Start micro-ROS Jazzy agent in Docker (UDP)
│   ├── docker-run-ros2.sh           ← Start ROS2 Jazzy interactive shell in Docker
│   ├── start-eth.sh                 ← Launch agent + ROS2 shell
│   ├── start-foxglove.sh            ← Launch Foxglove Bridge (WebSocket on port 8765)
│   ├── start-all.sh                 ← Launch everything: agent + bridge + ROS2 + Studio
│   └── cyclonedds.xml               ← CycloneDDS config (auto interface, DDS tuning)
│
├── devices/                         ← Per-device config.json files
│   ├── E_STOP/config.json           ← E-Stop board: estop channel only
│   ├── PEDAL/config.json            ← Pedal board: (example)
│   └── RC/config.json               ← RC board: all 6 RC channels, rc_trim
│
├── workspace/                       ← Zephyr workspace (west init output, do NOT edit)
│   ├── zephyr/                      ← Zephyr RTOS source
│   ├── modules/
│   │   └── lib/micro_ros_zephyr_module/
│   └── build/
│       └── zephyr/
│           ├── zephyr.elf           ← ELF with debug symbols
│           └── zephyr.uf2           ← FLASHABLE FIRMWARE
│
├── common/                          ← SHARED FIRMWARE LAYER (all devices use this)
│   ├── CMakeLists.txt               ← target_sources(app PRIVATE …) pattern
│   └── src/
│       ├── main.c                   ← Entry point, reconnection loop, watchdog
│       │                              (param_server_init call REMOVED — BL-017/ERR-032)
│       ├── config/
│       │   ├── config.h             ← bridge_config_t, API (CFG_MAX_CHANNELS=12)
│       │   └── config.c             ← LittleFS, JSON parser, config_set/save/load
│       ├── shell/
│       │   └── shell_cmd.c          ← 'bridge' shell commands
│       ├── bridge/
│       │   ├── channel.h            ← channel_t, channel_state_t, msg_type_t
│       │   ├── channel_manager.c/.h ← Pub/sub framework, topic override
│       │   ├── diagnostics.c/.h     ← /diagnostics publisher (IP+MAC, 5s period)
│       │   ├── param_server.c/.h    ← DEAD CODE (not called anywhere since BL-017)
│       │   └── service_manager.c/.h ← std_srvs SetBool / Trigger
│       ├── drivers/
│       │   ├── drv_gpio.c/.h        ← GPIO input IRQ + debounce + setup_output
│       │   ├── drv_adc.c/.h         ← ADC voltage input (opt-in via DT_PATH(zephyr_user))
│       │   └── drv_pwm_in.c/.h      ← RC PWM input driver
│       └── user/
│           └── user_channels.h      ← Shared header (register_if_enabled, channel_t)
│
├── apps/                            ← PER-DEVICE FIRMWARE — work here
│   ├── estop/
│   │   ├── CMakeLists.txt           ← adds own src/ + common/ layer
│   │   ├── prj.conf                 ← Zephyr Kconfig (no ADC/PWM-in subsystems)
│   │   ├── west.yml                 ← Zephyr + micro-ROS deps
│   │   ├── boards/w5500_evb_pico.overlay   ← GP25 + GP27 + mode(GP2/3) + okgo(GP4/5) + GP22
│   │   └── src/
│   │       ├── user_channels.c      ← registers: estop, mode, okgo_btn, okgo_led
│   │       ├── estop.{c,h}          ← E-Stop NC switch (GP27)
│   │       ├── mode.{c,h}           ← 3-state rotary (GP2/3, Int32 0/1/2)
│   │       ├── okgo_btn.{c,h}       ← 2-pin AND safety button (GP4/5, Bool)
│   │       └── okgo_led.{c,h}       ← ROS→firmware LED (GP22, Bool SUB)
│   ├── rc/
│   │   ├── CMakeLists.txt
│   │   ├── prj.conf
│   │   ├── west.yml
│   │   ├── boards/w5500_evb_pico.overlay   ← GP25 + GP2..GP7 PWM inputs
│   │   └── src/
│   │       ├── user_channels.c      ← registers: rc_ch1..6
│   │       └── rc.{c,h}             ← RC receiver CH1–CH6, normalization + trim
│   └── pedal/
│       ├── CMakeLists.txt
│       ├── prj.conf
│       ├── west.yml
│       ├── boards/w5500_evb_pico.overlay   ← GP25 only (ADC pins reserved for BL-012)
│       └── src/
│           ├── user_channels.c      ← registers: pedal_heartbeat
│           └── pedal.{c,h}          ← /robot/heartbeat (Bool 1 Hz)
│
└── modules/
    └── w6100_driver/                ← Out-of-tree W6100 SPI MACRAW driver (BL-010)
```

---

## Developer Environment Setup

### Prerequisites

- **macOS** (arm64 or x86_64) or **Linux**
- **Docker Desktop** installed and running
- **Python 3** (for config uploader): `pip3 install pyserial`
- **Git**

### 1. Build the Docker image (once)

```bash
make docker-build
```

Contains: Zephyr SDK 0.17.4 (ARM Cortex-M0+ toolchain), west, micro-ROS build tools.

### 2. Download the Zephyr workspace (once, ~2 GB)

```bash
make workspace-init
```

> **Important:** The `workspace/` directory must NOT be inside Dropbox/iCloud. Docker's virtiofs and cloud sync cause deadlocks. Keep in `~/Dev/`.

### 3. Build the firmware (per-device)

```bash
make build DEVICE=estop   # or rc, pedal
```

Output: `workspace/build/zephyr/zephyr.uf2`

Per-device build stats (E_STOP, BL-014 Fázis 2):
- FLASH: ~431 KB (2.57% of 16 MB)
- RAM: ~263 KB (97.42% of 264 KB)
- Heap: 96 KB

Each device has its own `apps/<device>/prj.conf` and overlay — the UF2
from one device **cannot** be flashed onto another device without rebuild
(overlay DT differs).

### 4. Flash the firmware

**Option A — Without touching hardware buttons (recommended):**

```bash
tools/flash.sh
# or with explicit port:
tools/flash.sh /dev/tty.usbmodem231401
```

The script sends `bridge bootsel` over serial, waits for `/Volumes/RPI-RP2`, then copies the UF2 automatically.

**Option B — Manual BOOTSEL (first flash or unresponsive firmware):**

1. Hold **BOOTSEL**, connect USB, release
2. `/Volumes/RPI-RP2/` appears
3. `cp workspace/build/zephyr/zephyr.uf2 /Volumes/RPI-RP2/`

**Flash multiple boards at once (if all in BOOTSEL mode):**

```bash
UF2=workspace/build/zephyr/zephyr.uf2
cp "$UF2" "/Volumes/RPI-RP2/" &
cp "$UF2" "/Volumes/RPI-RP2 1/" &
cp "$UF2" "/Volumes/RPI-RP2 2/" &
wait
```

### 5. Open the serial monitor

```bash
make monitor   # screen /dev/tty.usbmodem231401 115200
```

Exit: `Ctrl+A` then `K` then `Y`

---

## Normal Boot Sequence

```
*** Booting Zephyr OS build v4.3.99 ***
[main] Watchdog active (8000 ms timeout)
[main] USB console connected          ← or: "autonomous mode" if no DTR within 500ms
[config] LittleFS mounted: /lfs
[config] Config loaded: /lfs/config.json
[eth_w6100] w5500@0 MAC set to 0c:2f:94:30:58:03
[main] MAC: 0c:2f:94:30:58:03 (config)
[main] Hostname: ROS_Bridge_estop
[main] Waiting for Ethernet link (max 15s)...
[main] Ethernet link UP
[main] Network: DHCP starting...
[net_dhcpv4] Received: 192.168.68.130
[channel_manager] Channel registered: estop
[main] micro-ROS session active. 1 channels, 0 subscribers.
[main] Agent found — initializing session
[main] micro-ROS session active.
```

**LED behavior:**
- `OFF` = booting / searching for agent
- `ON (steady)` = micro-ROS session active
- `OFF` again = agent lost, reconnecting

---

## Shell Commands

The shell is accessible on the USB CDC ACM console (`make monitor`).

### `bridge config show`

Display the current configuration (RAM):

```
--- Bridge configuration ---
[network]
  mac:        0c:2f:94:30:58:03
  dhcp:       true
  ip:         192.168.68.203
  agent_ip:   192.168.68.125
  agent_port: 8888
[ros]
  node_name:  estop
  namespace:  /robot
[channels]
  estop:      enabled
  test_counter: disabled
```

### `bridge config set <key> <value>`

Set a value in RAM (does **not** save automatically):

```bash
# Network
bridge config set network.mac         02:AA:BB:CC:DD:EE
bridge config set network.dhcp        true
bridge config set network.ip          192.168.68.203
bridge config set network.netmask     255.255.255.0
bridge config set network.gateway     192.168.68.1
bridge config set network.agent_ip    192.168.68.125
bridge config set network.agent_port  8888

# ROS
bridge config set ros.node_name       estop
bridge config set ros.namespace       /robot

# Channels (enable/disable, optional topic override)
bridge config set channels.estop      true
bridge config set channels.estop      false
bridge config set channels.rc_ch1     true
bridge config set channels.rc_ch1.topic  motor_left

# RC trim
bridge config set rc_trim.ch1_min     1000
bridge config set rc_trim.ch1_center  1500
bridge config set rc_trim.ch1_max     2000
bridge config set rc_trim.deadzone    20
bridge config set rc_trim.ema_alpha   0.3
```

> **Important:** `set` only changes RAM. Use `save` to persist, then `reboot` to activate.

### `bridge config save` / `load` / `reset`

```bash
bridge config save    # RAM → /lfs/config.json
bridge config load    # flash → RAM
bridge config reset   # restore factory defaults in RAM (does not save)
```

### `bridge reboot` / `bridge bootsel`

```bash
bridge reboot         # reboot (activates saved config)
bridge bootsel        # reboot into BOOTSEL mode for firmware flashing
```

---

## Configuration Reference

### config.json — Full Structure

`config.json` lives on the LittleFS flash partition at `/lfs/config.json`.

```json
{
  "network": {
    "mac":        "0C:2F:94:30:58:03",
    "dhcp":       true,
    "ip":         "192.168.68.203",
    "netmask":    "255.255.255.0",
    "gateway":    "192.168.68.1",
    "agent_ip":   "192.168.68.125",
    "agent_port": 8888
  },

  "ros": {
    "node_name": "estop",
    "namespace": "/robot"
  },

  "channels": {
    "estop":         true,
    "test_counter":  false,
    "test_heartbeat": false,
    "test_echo":     false,
    "rc_ch1": { "enabled": true, "topic": "motor_right" },
    "rc_ch2": { "enabled": true, "topic": "motor_left" },
    "rc_ch5": { "enabled": true, "topic": "rc_mode" },
    "rc_ch6": { "enabled": true, "topic": "winch" }
  },

  "rc_trim": {
    "ch1_min": 1000, "ch1_center": 1500, "ch1_max": 2000,
    "ch2_min": 1000, "ch2_center": 1500, "ch2_max": 2000,
    "ch3_min": 1000, "ch3_center": 1500, "ch3_max": 2000,
    "ch4_min": 1000, "ch4_center": 1500, "ch4_max": 2000,
    "ch5_min": 1000, "ch5_center": 1500, "ch5_max": 2000,
    "ch6_min": 1000, "ch6_center": 1500, "ch6_max": 2000,
    "deadzone": 20,
    "ema_alpha": 0.3
  }
}
```

### Field Reference

**`network`**

| Field | Type | Description |
|-------|------|-------------|
| `mac` | string | Hardware MAC address `"02:XX:XX:XX:XX:XX"`. If empty or absent, auto-generated from RP2040 flash UID. Use `02:` prefix (locally administered). |
| `dhcp` | bool | `true` = DHCP, `false` = static IP from `ip` field |
| `ip` | string | Static IP (ignored when `dhcp: true`) |
| `netmask` | string | Subnet mask |
| `gateway` | string | Default gateway |
| `agent_ip` | string | IP of the ROS2 host running the micro-ROS agent |
| `agent_port` | int | UDP port of the agent (default: 8888) |

**`ros`**

| Field | Type | Description |
|-------|------|-------------|
| `node_name` | string | ROS2 node name. Also sets the network hostname to `ROS_Bridge_<node_name>`. Must be unique per board. |
| `namespace` | string | ROS2 namespace (e.g. `"/robot"`). All topics are published under this namespace. |

**`channels`**

A map of channel names to their configuration. Three formats are accepted:

```json
"channel_name": true              // enable with default topic from C code
"channel_name": false             // disable — channel not registered
"channel_name": { "enabled": true, "topic": "custom_topic" }  // enable + topic override
```

Channels not listed in `config.json` are **enabled by default** (backward compatibility).

**`rc_trim`** — RC receiver calibration (used only by the `rc` board)

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `chN_min` | int | 1000 | Raw PWM value (µs) at minimum stick position |
| `chN_center` | int | 1500 | Raw PWM value (µs) at center/neutral |
| `chN_max` | int | 2000 | Raw PWM value (µs) at maximum stick position |
| `deadzone` | int | 20 | ±deadzone (µs) around center treated as 0.0 |
| `ema_alpha` | float | 1.0 | EMA smoothing factor (0.0–1.0). Lower = smoother. 1.0 = no filtering. Recommended: 0.3 |

### Three ways to modify config

**1. Serial shell (`make monitor`):**
```bash
bridge config set network.agent_ip 192.168.1.100
bridge config save
bridge reboot
```

**2. Python uploader (from host, after editing `devices/<BOARD>/config.json`):**
```bash
python3 tools/upload_config.py --config devices/E_STOP/config.json
python3 tools/upload_config.py --config devices/RC/config.json --port /dev/tty.usbmodem101
```

> Close `make monitor` (serial port) before running the uploader.

**3. From C code:**
```c
config_reset_defaults();
SAFE_STRCPY(g_config.network.agent_ip, "192.168.1.100");
config_save();
```

---

## Multi-Board Setup

Multiple boards can run on the same network and connect to the same micro-ROS agent. Three things must be unique per board:

| Parameter | How | Automatic? |
|-----------|-----|------------|
| MAC address | Auto-generated from RP2040 flash UID, or set in `config.json` | Yes (auto) |
| Hostname | Set to `ROS_Bridge_<node_name>` automatically | Yes |
| ROS `node_name` | Set in `config.json` | **Manual** — you configure this |
| Active channels | Set in `config.json` `channels` section | **Manual** |

Each board has its own `config.json` in `devices/<BOARD>/config.json`. Upload with:

```bash
python3 tools/upload_config.py --config devices/E_STOP/config.json
python3 tools/upload_config.py --config devices/PEDAL/config.json
python3 tools/upload_config.py --config devices/RC/config.json
```

### Topic isolation

All boards share the same firmware binary. Channel enable/disable in `config.json` controls which channels are active on each board. Example:

| Board | Active channels | Published topics |
|-------|----------------|-----------------|
| `estop` | `estop` only | `/robot/estop` |
| `rc` | `rc_ch1`–`rc_ch6` | `/robot/motor_left`, `/robot/motor_right`, `/robot/rc_mode`, `/robot/winch`, etc. |
| `pedal` | custom | `/robot/<custom>` |

---

## Channel System

### Concepts

A **channel** is a logical bridge between a physical device and a ROS2 topic.

Each channel can:
- **Publish**: read from hardware (`read()`) → send to ROS2 topic periodically
- **Subscribe**: receive from ROS2 topic → write to hardware (`write()`)
- Be publish-only, subscribe-only, or bidirectional
- Run at a configurable period (e.g., every 100ms)
- Be enabled/disabled and have its topic name overridden from `config.json`

### The `channel_t` struct

```c
typedef enum {
    MSG_BOOL    = 0,   // std_msgs/Bool
    MSG_INT32   = 1,   // std_msgs/Int32
    MSG_FLOAT32 = 2,   // std_msgs/Float32
} msg_type_t;

typedef union {
    bool    b;
    int32_t i32;
    float   f32;
} channel_value_t;

typedef struct {
    const char    *name;         // Channel name (key in config.json channels)
    const char    *topic_pub;    // Default ROS2 publish topic (overridable from config)
    const char    *topic_sub;    // Default ROS2 subscribe topic (overridable from config)
    msg_type_t     msg_type;
    uint32_t       period_ms;
    bool           irq_capable;  // true = GPIO interrupt triggers immediate publish
    int  (*init)(void);
    void (*read)(channel_value_t *val);
    void (*write)(const channel_value_t *val);
} channel_t;
```

### How to add a new sensor or actuator

#### Step 1 — Create a device file (`apps/<device>/src/my_sensor.c`, or `common/src/user/` if shared across devices)

```c
#include "bridge/channel.h"

static int my_sensor_init(void) { return 0; }

static void my_sensor_read(channel_value_t *val)
{
    val->f32 = 23.5f;
}

const channel_t my_sensor_channel = {
    .name       = "my_sensor",          // key used in config.json channels section
    .topic_pub  = "temperature",        // relative to namespace — becomes /robot/temperature
    .topic_sub  = NULL,
    .msg_type   = MSG_FLOAT32,
    .period_ms  = 1000,
    .init       = my_sensor_init,
    .read       = my_sensor_read,
    .write      = NULL,
};
```

#### Step 2 — Declare in a header (`apps/<device>/src/my_sensor.h`)

```c
#pragma once
#include "bridge/channel.h"
extern const channel_t my_sensor_channel;
```

#### Step 3 — Register in `user_channels.c`

```c
#include "my_sensor.h"

void user_register_channels(void)
{
    // existing channels...
    register_if_enabled(&my_sensor_channel);
}
```

`register_if_enabled()` checks `config.json` — if `"my_sensor": false` in the channels section, the channel is not registered.

#### Step 4 — Add to build (`apps/<device>/CMakeLists.txt`)

```cmake
target_sources(app PRIVATE
    ...
    src/my_sensor.c
)
```

#### Step 5 — Enable in `config.json`

```json
"channels": {
  "my_sensor": true
}
```

Or with topic override:
```json
"channels": {
  "my_sensor": { "enabled": true, "topic": "custom_name" }
}
```

#### Step 6 — Build, flash, verify

```bash
make build DEVICE=<device>
tools/flash.sh
ros2 topic echo /robot/temperature
```

### Message type selection

| Type | ROS2 message | Use for |
|------|-------------|---------|
| `MSG_BOOL` | `std_msgs/Bool` | GPIO, relay, switch, LED, E-Stop |
| `MSG_INT32` | `std_msgs/Int32` | Encoder ticks, PWM duty, discrete values |
| `MSG_FLOAT32` | `std_msgs/Float32` | ADC, temperature, speed, normalized RC input |

### Constraints

- Maximum **12 channels** (`CFG_MAX_CHANNELS = 12` in `config.h`)
- Executor handle count = subscriber channels + service count (BL-017 /
  ERR-032: the `PARAM_SERVER_HANDLES` (6) slot was removed because
  `rclc_parameter_server_init_with_option` returns `RCL_RET_INVALID_ARGUMENT`
  on our setup and leaves the 6 service handles in a `initialized=true`
  but corrupted state, which poisons the executor dispatch loop — real
  subscription callbacks stop firing. Therefore the interactive `ros2 param`
  interface is **not** available; channel parameters live in
  `devices/<DEVICE>/config.json` only.)

---

## Built-in Channels

### E-Stop channels (`apps/estop/`)

The E_STOP board owns 4 channels — 3 publishers + 1 subscriber:

| Channel | Default topic | Type | Pin(s) | Period | Description |
|---------|--------------|------|--------|--------|-------------|
| `estop` | `estop` | BOOL | GP27 NC + PULL_UP | 50 ms + IRQ | `true` = circuit open (button pressed). Edge-both IRQ, 50 ms debounce, 20 Hz fallback. |
| `mode` | `mode` | INT32 | GP2 (auto) + GP3 (follow), ACTIVE_LOW + PULL_UP | 100 ms + IRQ | 3-state rotary: 0=LEARN, 1=FOLLOW, 2=AUTO. Both pins → common channel_idx, either pin's IRQ triggers publish. |
| `okgo_btn` | `okgo_btn` | BOOL | GP4 + GP5, ACTIVE_LOW + PULL_UP | 100 ms + IRQ | Safety 2-pin AND: `true` only when **both** pins active. Edge-both IRQ per pin. |
| `okgo_led` | `okgo_led` | BOOL | GP22 ACTIVE_HIGH OUTPUT | — (sub) | ROS → firmware. Subscribing to `/robot/okgo_led` drives GP22. |

Located in `apps/estop/src/{estop,mode,okgo_btn,okgo_led}.{c,h}`.

```bash
ros2 topic echo /robot/estop     # false = normal, true = E-Stop active
ros2 topic echo /robot/mode      # 0/1/2 enum
ros2 topic echo /robot/okgo_btn  # AND of GP4 and GP5
ros2 topic pub /robot/okgo_led std_msgs/msg/Bool "{data: true}"   # LED on
```

### RC receiver channels (`apps/rc/`)

Located in `apps/rc/src/rc.c`. Uses `common/src/drivers/drv_pwm_in.c` (pulse-width input on GP2–GP7).

| Channel | GPIO | Default topic | Type | Description |
|---------|------|--------------|------|-------------|
| `rc_ch1` | GP2 | `rc_ch1` | FLOAT32 | CH1 normalized −1.0…+1.0 |
| `rc_ch2` | GP3 | `rc_ch2` | FLOAT32 | CH2 normalized −1.0…+1.0 |
| `rc_ch3` | GP4 | `rc_ch3` | FLOAT32 | CH3 |
| `rc_ch4` | GP5 | `rc_ch4` | FLOAT32 | CH4 |
| `rc_ch5` | GP6 | `rc_ch5` | FLOAT32 | CH5 — mode switch (1.0 = ON, −1.0 = OFF) |
| `rc_ch6` | GP7 | `rc_ch6` | FLOAT32 | CH6 — winch (1.0 = push, −1.0 = release) |

**Normalization:** raw PWM µs → FLOAT32 using `rc_trim` values:
- `center` → `0.0`
- `max` → `+1.0`
- `min` → `−1.0`
- ±`deadzone` µs around center → `0.0`
- Optional EMA smoothing: `filtered = α × sample + (1−α) × prev`. Set `ema_alpha` in `rc_trim` (0.3 recommended, 1.0 = off).

**Failsafe:** No firmware-level timeout. The RC transmitter's own failsafe output is trusted — the last received value is held.

**Typical `rc` board config:**
```json
"channels": {
  "rc_ch1": { "enabled": true, "topic": "motor_right" },
  "rc_ch2": { "enabled": true, "topic": "motor_left" },
  "rc_ch5": { "enabled": true, "topic": "rc_mode" },
  "rc_ch6": { "enabled": true, "topic": "winch" },
  "estop": false
}
```

> **Channel assignment:** CH1 = steering (left-right), CH2 = throttle (forward-backward). The RC transmitter applies tank mixing, so CH1 output = right motor, CH2 output = left motor.

---

## micro-ROS Agent Setup (ROS2 host)

### Recommended: Full stack with one command

```bash
# Start everything: agent + Foxglove bridge + ROS2 shell + Foxglove Studio:
./tools/start-all.sh

# Stop everything:
./tools/start-all.sh --stop
```

Idempotent — re-running skips services that are already up.

### Individual scripts

```bash
# Agent + ROS2 shell only (no Foxglove):
./tools/start-eth.sh

# Just the agent:
./tools/docker-run-agent-udp.sh           # default port 8888
./tools/docker-run-agent-udp.sh 9999      # custom port

# Just the ROS2 shell:
./tools/docker-run-ros2.sh

# Just the Foxglove bridge:
./tools/start-foxglove.sh
./tools/start-foxglove.sh --stop
```

Uses `microros/micro-ros-agent:jazzy` with `--net=host`. No installation needed beyond Docker.

### Manual (any ROS2 Jazzy environment)

```bash
source /opt/ros/jazzy/setup.bash
ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888
```

### Verify after boards connect

```bash
ros2 node list
# /robot/estop
# /robot/rc
# /robot/pedal

ros2 topic list
# /robot/estop
# /robot/mode
# /robot/okgo_btn
# /robot/okgo_led
# /robot/motor_left
# /robot/motor_right
# /robot/rc_mode
# /robot/winch
# /robot/heartbeat
# /diagnostics

ros2 topic echo /robot/estop
ros2 topic echo /robot/motor_left
```

---

## Parameter handling (config.json only)

**The interactive `ros2 param` interface is NOT available** since 2026-04-21
(BL-017 / ERR-032). Root cause: the `rclc_parameter_server_init_with_option`
call returns `RCL_RET_INVALID_ARGUMENT`, but the preceding
`rclc_executor_add_parameter_server_with_context` already marked all 6
param-service handles as `initialized=true` in `executor.handles[]`. The
`rclc_executor_spin_some` loop then iterates over these corrupted handles +
the valid sub handles, and the corrupted dispatch fragments poison the real
subscription data-pull — real callbacks silently stop firing.

The `param_server_init` call has been removed from `common/src/main.c`
(`ros_session_init`). The `common/src/bridge/param_server.{c,h}` files remain
as dead code.

**Channel parameters are managed via `devices/<DEVICE>/config.json` only.**
Supported channel-level fields (subset per channel):

- `enabled` (bool) — register/skip this channel
- `period_ms` (int) — publish period override (ignored for subscribe-only channels)
- `invert_logic` (bool) — swap true/false for Bool channels
- `topic` (string) — override default topic name

Update flow:

```bash
# Edit devices/<DEVICE>/config.json, then:
python3 tools/upload_config.py --config devices/<DEVICE>/config.json --port /dev/ttyACM0
# upload_config does: bridge config set ... → bridge config save → bridge reboot
```

Or live (persisted to NVS, reboot to apply network changes):

```bash
# Over the USB CDC shell:
bridge config set channels.estop      true
bridge config set channels.rc_ch1     true
bridge config set channels.rc_ch1.topic  motor_left
bridge config save
bridge reboot
```

See `ERRATA.md` §ERR-032 for the full root-cause writeup; future re-enabling
is tracked as **BL-018**.

---

## Diagnostics

The bridge publishes `diagnostic_msgs/DiagnosticArray` on `/diagnostics` every **5 seconds**.

```bash
ros2 topic echo /diagnostics
```

| Key | Example | Description |
|-----|---------|-------------|
| `uptime_s` | `"3742"` | Seconds since boot |
| `channels` | `"4"` | Registered channels |
| `reconnects` | `"1"` | Agent reconnect counter |
| `firmware` | `"v2.0-W6100"` | Firmware version |
| `ip` | `"192.168.68.130"` | Active IP (DHCP-assigned or static) |
| `mac` | `"0c:2f:94:30:58:03"` | Active MAC address |

---

## Visualization with Foxglove Studio

[Foxglove Studio](https://foxglove.dev/) provides real-time visualization of all ROS2 topics over a WebSocket connection.

### Architecture

```
[W6100 Pico boards] → UDP :8888 → [micro-ROS Agent (Docker)]
                                          │ DDS
                                   [Foxglove Bridge (Docker)]
                                          │ WebSocket :8765
                                   [Foxglove Studio (native app)]
```

### Setup

**1. Install Foxglove Studio (once):**

```bash
sudo snap install foxglove-studio
```

**2. Start everything:**

```bash
./tools/start-all.sh
```

This launches the agent, Foxglove Bridge, ROS2 shell, and Studio in the correct order.

**3. Connect in Studio:**

Open Connection → **Foxglove WebSocket** → `ws://localhost:8765`

> Use **Foxglove WebSocket**, not Rosbridge — they are different protocols.

### Available topics in Studio

| Topic | Type | Source |
|-------|------|--------|
| `/robot/estop` | `std_msgs/Bool` | E-Stop board |
| `/robot/motor_right` | `std_msgs/Float32` | RC CH1 (right motor) |
| `/robot/motor_left` | `std_msgs/Float32` | RC CH2 (left motor) |
| `/robot/rc_mode` | `std_msgs/Float32` | RC CH5 |
| `/robot/winch` | `std_msgs/Float32` | RC CH6 |
| `/diagnostics` | `diagnostic_msgs/DiagnosticArray` | All boards |

### Foxglove Bridge details

The bridge runs in Docker (`docker/Dockerfile.foxglove`) using `ros-jazzy-foxglove-bridge`. Managed by `tools/start-foxglove.sh`.

---

## Robustness

### Hardware Watchdog

RP2040 internal watchdog, **8-second timeout**. If the firmware freezes, the board reboots automatically.

### Reconnection State Machine

```
Phase 1 — Agent search:   rmw_uros_ping_agent(200ms, 1) until agent found
Phase 2 — Session init:   rclc_support_init + node + entities + executor
                          memset(support/node/executor) before each attempt
                          → on failure: 2s delay + retry (no dirty state)
Phase 3 — Running:        executor_spin + channel_publish + watchdog_feed
                          ping every 1s → if disconnected: break
Phase 4 — Cleanup:        ros_session_fini → LED off → back to Phase 1
```

> **Critical fix (v2.1):** Before each `rclc_support_init` call, `support`, `node`, and `executor` structs are zeroed with `memset`. This prevents a "dirty struct" bug that caused permanent connection failure when the first init attempt fails (e.g., agent not yet running at boot time).

### DTR Timeout (autonomous mode)

If USB console is not connected, the firmware waits at most **500ms** for DTR signal, then continues automatically. No serial monitor needed for standalone operation.

### Ethernet Link Wait

After boot, waits up to **15 seconds** for `NET_EVENT_IF_UP` before applying IP configuration.

---

## Python Config Uploader

`tools/upload_config.py` uploads `config.json` to the Pico over serial.

```bash
python3 tools/upload_config.py                               # auto-detect port
python3 tools/upload_config.py --port /dev/tty.usbmodem101  # explicit port
python3 tools/upload_config.py --config devices/RC/config.json
```

What it does:
1. Opens serial port (115200 baud)
2. Sends `bridge config set <key> <value>` for every field (network, ros, channels, rc_trim)
3. Sends `bridge config save`
4. Sends `bridge config show` to verify
5. Sends `bridge reboot` to activate

---

## Current Status

| Feature | Status | Since |
|---------|--------|-------|
| Zephyr base firmware | ✅ Done | v1.0 |
| W6100 Ethernet driver | ✅ Done | v1.0 |
| USB CDC ACM console | ✅ Done | v1.0 |
| DHCP / static IP | ✅ Done | v1.1 |
| LittleFS + JSON config | ✅ Done | v1.1 |
| Serial shell (bridge commands) | ✅ Done | v1.1 |
| Python config uploader | ✅ Done | v1.1 |
| Hardware watchdog (8s) | ✅ Done | v1.4 |
| Reconnection state machine | ✅ Done, fixed v2.1 | v1.4 |
| Channel Manager framework | ✅ Done | v1.3 |
| Built-in test channels | ✅ Done | v1.5 |
| GPIO driver + E-Stop (GP27) | ✅ Done | v2.0 |
| GPIO debounce (50ms) | ✅ Done | v2.1 |
| ADC driver (GP26) | ✅ Done | v2.0 |
| std_srvs services (SetBool, Trigger) | ✅ Done | v2.0 |
| Parameter server | ✅ Done (ERR-001 known) | v2.0 |
| Diagnostics (/diagnostics, IP+MAC) | ✅ Done | v2.0 |
| Unique MAC per board (auto + config) | ✅ Done | v2.0 |
| Dynamic hostname (ROS_Bridge_<node>) | ✅ Done | v2.0 |
| Config-driven channel enable/disable | ✅ Done | v2.1 |
| Config-driven topic name override | ✅ Done | v2.1 |
| RC PWM input driver (GP2–GP7, 6ch) | ✅ Done | v2.1 |
| RC trim calibration from config.json | ✅ Done | v2.1 |
| RC EMA smoothing filter (configurable) | ✅ Done | v2.2 |
| Multi-board tested (3 boards) | ✅ Done | v2.1 |
| Foxglove Studio visualization | ✅ Done | v2.1 |
| Serial (UART) driver | 🔄 Planned | — |
| Encoder (PIO) driver | 🔄 Planned | — |
| PID controller | 🔄 Planned | — |
| I2C driver | 🔄 Planned | — |

---

## Known Limitations

| Limitation | Explanation | Workaround |
|------------|-------------|------------|
| Max 12 channels | `CFG_MAX_CHANNELS = 12` | Increase value, costs BSS RAM |
| RAM: ~97% used | 263 KB / 264 KB, 96 KB heap | Avoid large stack allocs; monitor heap |
| Param server ERR-001 | `param_server_init error: 11` on some boots | Board runs normally without it |
| IP requires reboot | Config only activates after reboot | Runtime reload planned |
| Zephyr board name | `w5500_evb_pico` (target name) | Overlay sets W6100 compatible string |
| Message types | Only Bool / Int32 / Float32 | String, Array planned |
| Soft real-time | Hard real-time not guaranteed | Improve with thread priorities |

---

## Troubleshooting

### LED stays off after boot

The board cannot reach the micro-ROS agent.

```bash
# On ROS2 host — check agent is running:
docker ps | grep micro-ros
# Or start it:
./tools/start-eth.sh

# On Pico serial — check IP and agent address:
bridge config show
```

### All boards connect but topics look wrong

Check that each board has a unique `node_name` in its `config.json`, and that only the intended channels are `true`.

### Board not booting without USB serial

The DTR wait is max 500ms — it continues automatically. If it's stuck, check that there's no blocking code before the DTR poll.

### "rclc_support_init error" on boot

The agent is not yet running when the board tries to connect. This is normal — the board retries every 2 seconds. Start the agent with `./tools/start-eth.sh`, the board connects automatically on the next retry.

### Config doesn't load (LittleFS error)

```bash
bridge config reset
bridge config save
bridge reboot
```

### Shell not responding

```bash
screen -X quit     # close stuck screen sessions
make monitor       # open fresh
```

### Stale nodes in `ros2 node list`

DDS caches node info. Restart the agent to clear:
```bash
Ctrl+C   # stop agent
./tools/docker-run-agent-udp.sh
```

---

## Developer Environment Summary

| Parameter | Value |
|-----------|-------|
| Host OS | macOS (arm64) or Ubuntu |
| Build | Docker (`w6100-zephyr-microros:latest`) |
| Zephyr SDK | 0.17.4 (ARM Cortex-M0+ toolchain) |
| Zephyr version | main (v4.3.99) |
| micro-ROS | Jazzy libs, Humble agent compatible |
| Firmware size | ~426 KB flash, ~263 KB RAM (97.49%), 96 KB heap |
| Author | Eduard Sik — [eduard@nowlab.eu](mailto:eduard@nowlab.eu) |
