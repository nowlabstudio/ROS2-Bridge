# W6100 EVB Pico — Zephyr + micro-ROS Universal Bridge

> **Developer Reference Documentation**
> Last updated: 2026-03-08 | Version: v2.1 | Author: Eduard Sik — [eduard@nowlab.eu](mailto:eduard@nowlab.eu)

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

| Pin | Function | Note |
|-----|----------|------|
| GP25 | Built-in LED (status LED) | HIGH = LED on, shows ROS agent connection |
| GP27 | E-Stop input | NC (normally closed) switch, pull-up, GPIO IRQ |
| GP2–GP7 | RC receiver CH1–CH6 | PWM pulse-width input, 50ms debounce on GPIO IRQ |
| GP26 | ADC0 | Analog input |
| GP28 | ADC2 | Analog input |
| GP0–GP1 | UART0 TX/RX | Serial devices |
| GP4–GP5 | I2C0 SDA/SCL | I2C devices |
| GP16–GP19 | SPI0 | SPI interface devices |

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
│   └── Dockerfile                   ← Docker image: Zephyr SDK + micro-ROS tools
│
├── tools/
│   ├── upload_config.py             ← Python config uploader (serial port)
│   ├── flash.sh                     ← Flash firmware via 'bridge bootsel' (no button needed)
│   ├── docker-run-agent-udp.sh      ← Start micro-ROS Jazzy agent in Docker (UDP)
│   ├── docker-run-ros2.sh           ← Start ROS2 Jazzy interactive shell in Docker
│   ├── start-eth.sh                 ← Launch full environment (agent + ROS2 shell)
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
└── app/                             ← FIRMWARE SOURCE — work here
    ├── CMakeLists.txt               ← add new .c files here
    ├── prj.conf                     ← Zephyr Kconfig
    ├── west.yml                     ← Zephyr + micro-ROS dependencies
    ├── config.json                  ← Template config (Python uploader source)
    │
    ├── boards/
    │   └── w5500_evb_pico.overlay   ← DTS: USB CDC, LED, LittleFS, W6100, RC inputs
    │
    └── src/
        ├── main.c                   ← Entry point, reconnection loop, watchdog
        │
        ├── config/
        │   ├── config.h             ← bridge_config_t, API declarations
        │   └── config.c             ← LittleFS, JSON parser, config_set/save/load
        │
        ├── shell/
        │   └── shell_cmd.c          ← 'bridge' shell commands
        │
        ├── bridge/
        │   ├── channel.h            ← channel_t, channel_state_t, msg_type_t
        │   ├── channel_manager.c/.h ← Pub/sub framework, topic override, entity lifecycle
        │   ├── diagnostics.c/.h     ← /diagnostics publisher (IP+MAC, 5s period)
        │   ├── param_server.c/.h    ← rclc_parameter_server (low_mem_mode)
        │   └── service_manager.c/.h ← std_srvs SetBool / Trigger
        │
        ├── drivers/
        │   ├── drv_gpio.c/.h        ← GPIO input with 50ms debounce (E-Stop GP27)
        │   ├── drv_adc.c/.h         ← ADC voltage input (GP26)
        │   └── drv_pwm_in.c/.h      ← RC PWM input driver (pulse-width, k_cycle_get_32)
        │
        └── user/
            ├── user_channels.h/.c   ← Channel registration (config-driven enable/disable)
            ├── test_channels.h/.c   ← Built-in test channels (counter/heartbeat/echo)
            ├── estop.h/.c           ← E-Stop NC switch on GP27
            └── rc.h/.c              ← RC receiver CH1–CH6, normalization + trim
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

### 3. Build the firmware

```bash
make build
```

Output: `workspace/build/zephyr/zephyr.uf2`

Build stats: ~426 KB flash (2.54% of 16 MB), ~263 KB RAM (97.49% of 264 KB). Heap: 96 KB.

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
    "rc_ch1": { "enabled": true, "topic": "motor_left" },
    "rc_ch2": { "enabled": true, "topic": "motor_right" },
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
    "deadzone": 20
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

| Field | Description |
|-------|-------------|
| `chN_min` | Raw PWM value (µs) at minimum stick position (default: 1000) |
| `chN_center` | Raw PWM value (µs) at center/neutral (default: 1500) |
| `chN_max` | Raw PWM value (µs) at maximum stick position (default: 2000) |
| `deadzone` | ±deadzone (µs) around center treated as 0.0 (default: 20) |

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

#### Step 1 — Create a device file (`app/src/user/my_sensor.c`)

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

#### Step 2 — Declare in a header (`app/src/user/my_sensor.h`)

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

#### Step 4 — Add to build (`app/CMakeLists.txt`)

```cmake
target_sources(app PRIVATE
    ...
    src/user/my_sensor.c
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
make build
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
- Executor handle count = subscriber channels + `PARAM_SERVER_HANDLES` (6) + service count

---

## Built-in Channels

### Test channels (no hardware required)

Located in `app/src/user/test_channels.c`. Disabled by default in `config.json` — set to `true` to enable.

| Channel | Default topic | Type | Period | Description |
|---------|--------------|------|--------|-------------|
| `test_counter` | `pico/counter` | INT32 | 500 ms | Counts up from 0 |
| `test_heartbeat` | `pico/heartbeat` | BOOL | 1000 ms | Toggles true/false |
| `test_echo` | `pico/echo_out` / `pico/echo_in` | INT32 | 1000 ms | Echoes received value |

### E-Stop channel

Located in `app/src/user/estop.c`. GPIO IRQ-capable, 50ms hardware debounce.

| Channel | Default topic | Type | Description |
|---------|--------------|------|-------------|
| `estop` | `estop` | BOOL | NC switch on GP27. `true` = circuit open (button pressed). IRQ-triggered. |

> **Multi-board note:** Only enable `estop` on the board that physically has the E-Stop switch wired. Disable it on all other boards in their `config.json`.

```bash
ros2 topic echo /robot/estop     # false = normal, true = E-Stop active
```

### RC receiver channels

Located in `app/src/user/rc.c`. Uses `app/src/drivers/drv_pwm_in.c` (pulse-width input on GP2–GP7).

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

**Failsafe:** No firmware-level timeout. The RC transmitter's own failsafe output is trusted — the last received value is held.

**Typical `rc` board config:**
```json
"channels": {
  "rc_ch1": { "enabled": true, "topic": "motor_left" },
  "rc_ch2": { "enabled": true, "topic": "motor_right" },
  "rc_ch5": { "enabled": true, "topic": "rc_mode" },
  "rc_ch6": { "enabled": true, "topic": "winch" },
  "estop": false
}
```

---

## micro-ROS Agent Setup (ROS2 host)

### Recommended: Docker scripts (included)

```bash
# Start agent + ROS2 shell (Ubuntu, 2 terminal windows):
./tools/start-eth.sh

# Start just the agent:
./tools/docker-run-agent-udp.sh           # default port 8888
./tools/docker-run-agent-udp.sh 9999      # custom port

# Start interactive ROS2 shell:
./tools/docker-run-ros2.sh
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
# /robot/motor_left
# /robot/motor_right
# /robot/rc_mode
# /robot/winch
# /diagnostics

ros2 topic echo /robot/estop
ros2 topic echo /robot/motor_left
```

---

## Parameter Server

The bridge exposes an `rclc_parameter_server` (`low_mem_mode=true`) with three parameters per channel:

| Parameter | Type | Description |
|-----------|------|-------------|
| `ch.<name>.period_ms` | INT | Publish period override |
| `ch.<name>.enabled` | BOOL | Enable/disable channel |
| `ch.<name>.invert_logic` | BOOL | Invert bool value |

```bash
ros2 param list /robot/estop
ros2 param set /robot/estop ch.estop.period_ms 100
ros2 param dump /robot/estop
```

> **Known issue (ERR-001):** The param server may fail to initialize (`param_server_init error: 11`). The board runs normally without it — channels publish, but `ros2 param` is unavailable. See `ERRATA.md`.

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
| Multi-board tested (3 boards) | ✅ Done | v2.1 |
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
