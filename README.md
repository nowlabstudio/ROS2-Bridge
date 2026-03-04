# W6100 EVB Pico — Zephyr + micro-ROS Universal Bridge

> **Developer Reference Documentation**
> Last updated: 2026-03-04 | Version: v1.4 | Author: Eduard Sik — [eduard@nowlab.eu](mailto:eduard@nowlab.eu)

---

## What is this project?

A **universal ROS2 bridge** for microcontrollers. The hardware is a **WIZnet W6100 EVB Pico** board — a Raspberry Pi RP2040 microcontroller paired with the WIZnet W6100 hardwired TCP/IP Ethernet chip.

The goal: connect physical devices (sensors, motors, GPIO, Serial, I2C, SPI, encoders) to a ROS2 network over Ethernet UDP. Every connected device appears as a ROS2 **node** on the network — capable of both **publishing** (device → ROS2) and **subscribing** (ROS2 → device).

```
[Physical Devices]
  Sensors, motors, GPIO, ADC, PWM, Serial, I2C, SPI, Encoders
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
- **Robust** — hardware watchdog, automatic reconnection state machine, DTR timeout for autonomous mode
- **Runtime configuration** — IP address, node name, namespace, agent IP — all changeable without recompilation

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
| GP25 | Built-in LED (status LED) | HIGH = LED on |
| GP26 | ADC0 | Analog input |
| GP27 | ADC1 | Analog input |
| GP28 | ADC2 | Analog input |
| GP0–GP1 | UART0 TX/RX | Serial devices |
| GP4–GP5 | I2C0 SDA/SCL | I2C devices |
| GP16–GP19 | SPI0 | SPI interface devices |

### Current Network Configuration

| Parameter | Value |
|-----------|-------|
| Pico IP | `192.168.68.114` (static, from config.json) |
| ROS2 host IP | `192.168.68.125` |
| micro-ROS agent port | `8888` (UDP) |

> These are configurable in config.json without recompilation.

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

## Directory Structure (complete, current)

```
W6100_EVB_Pico_Zephyr_MicroROS/
│
├── README.md                        ← this file (English)
├── README.hu.md                     ← Hungarian version
├── Makefile                         ← build, flash, monitor commands
│
├── docker/
│   └── Dockerfile                   ← Docker image: zephyrprojectrtos/ci:v0.28.8
│
├── tools/
│   ├── upload_config.py             ← Python config uploader (Mac/Linux)
│   ├── stress_test.py               ← Comprehensive stress test (automated + manual)
│   └── stress_report.json           ← Auto-generated test report
│
├── workspace/                       ← Zephyr workspace (result of west init, do NOT edit)
│   ├── zephyr/                      ← Zephyr RTOS source (main branch)
│   ├── modules/
│   │   └── lib/micro_ros_zephyr_module/  ← micro-ROS Zephyr module
│   └── build/
│       └── zephyr/
│           ├── zephyr.elf           ← ELF with debug symbols
│           └── zephyr.uf2           ← FLASHABLE FIRMWARE (copy this to Pico)
│
└── app/                             ← OUR CODE — work here
    ├── CMakeLists.txt               ← build config, add new .c files here
    ├── prj.conf                     ← Zephyr Kconfig (module enable/disable)
    ├── west.yml                     ← Zephyr + micro-ROS dependencies
    ├── config.json                  ← Reference config (Python uploader source)
    │
    ├── boards/
    │   └── w5500_evb_pico.overlay   ← DTS overlay: USB CDC ACM, LED, LittleFS, W6100
    │
    └── src/
        ├── main.c                   ← Entry point, reconnection loop, watchdog
        │
        ├── config/
        │   ├── config.h             ← bridge_config_t struct, API declarations
        │   └── config.c             ← LittleFS mount, JSON parser, config_set/save/load
        │
        ├── shell/
        │   └── shell_cmd.c          ← 'bridge' shell commands (show/set/save/load/reset/reboot)
        │
        ├── bridge/
        │   ├── channel.h            ← channel_t struct, msg_type_t, channel_value_t
        │   └── channel_manager.c/.h ← Pub/sub framework, entity lifecycle management
        │
        └── user/
            ├── user_channels.h      ← Declaration: user_register_channels()
            └── user_channels.c      ← WRITE YOUR CHANNELS HERE (the only file you edit)
```

---

## Developer Environment Setup (first time)

### Prerequisites

- **macOS** (arm64 or x86_64) or Linux
- **Docker Desktop** installed and running
- **Python 3** (for config uploader): `pip3 install pyserial`
- **Git**

### 1. Build the Docker image (once)

```bash
make docker-build
```

This builds the `w6100-zephyr-microros:latest` Docker image, which contains:
- Zephyr SDK 0.17.4 (ARM Cortex-M0+ toolchain)
- Python west
- micro-ROS build tools

### 2. Download the Zephyr workspace (once, ~2GB)

```bash
make workspace-init
```

This downloads Zephyr RTOS and the micro-ROS module into the `workspace/` directory.

> **Important:** The `workspace/` directory must NOT be inside Dropbox. Docker's virtiofs
> and Dropbox sync cause a deadlock. Keep the workspace in `~/Dev/`.

### 3. Build the firmware

```bash
make build
```

Output: `workspace/build/zephyr/zephyr.uf2`

Build stats: ~289 KB flash (1.72% of 16 MB), ~220 KB RAM (81% of 264 KB).

### 4. Flash the firmware

Put the Pico in BOOTSEL mode:
1. Hold down the **BOOTSEL** button
2. Connect USB to the computer
3. Release the button
4. The `/Volumes/RPI-RP2/` drive will appear

```bash
cp workspace/build/zephyr/zephyr.uf2 /Volumes/RPI-RP2/
```

The Pico reboots automatically after flashing.

### 5. Open the serial monitor

```bash
make monitor
```

This runs: `screen /dev/tty.usbmodem231401 115200`

Exit screen:
```
Ctrl+A  then  K  then  Y
```

If port is busy (previous screen session didn't close):
```bash
screen -ls           # list running sessions
screen -X quit       # close all sessions
```

---

## Normal Boot Sequence (what you see on the console)

```
*** Booting Zephyr OS build v4.3.99 ***
[main] Watchdog active (30000 ms timeout)
[main] USB console connected          ← or: "autonomous mode" if no DTR
[config] LittleFS mounted: /lfs
[config] Config loaded: /lfs/config.json
[config] --- Bridge configuration ---
[config]   dhcp: false
[config]   ip: 192.168.68.114
[config]   agent_ip: 192.168.68.125
[config]   agent_port: 8888
[main] W6100 EVB Pico - micro-ROS Bridge
[main] Node: /pico_bridge
[main] Agent: 192.168.68.125:8888
[main] Waiting for Ethernet link (max 15s)...
[main] Ethernet link UP
[main] Network: static IP 192.168.68.114
[main] Starting bridge main loop
[main] Searching for agent: 192.168.68.125:8888 ...
[main] Agent found — initializing session
[main] micro-ROS session active. 0 channels, 0 subscribers.
```

**LED behavior:**
- `OFF` = booting / searching for agent
- `ON (steady)` = micro-ROS session active, agent connected

---

## Shell Commands (detailed)

The shell is accessible on the USB CDC ACM console (`make monitor`).

### `bridge config show`

Display the current configuration (in RAM):

```
--- Bridge configuration ---
[network]
  dhcp:       false
  ip:         192.168.68.114
  netmask:    255.255.255.0
  gateway:    192.168.68.1
  agent_ip:   192.168.68.125
  agent_port: 8888
[ros]
  node_name:  pico_bridge
  namespace:  /
```

### `bridge config set <key> <value>`

Set a value in RAM (**does NOT save automatically**):

```bash
bridge config set network.dhcp        true
bridge config set network.dhcp        false
bridge config set network.ip          192.168.68.114
bridge config set network.netmask     255.255.255.0
bridge config set network.gateway     192.168.68.1
bridge config set network.agent_ip    192.168.68.125
bridge config set network.agent_port  8888
bridge config set ros.node_name       my_robot
bridge config set ros.namespace       /robot1
```

> **Important:** `set` only changes RAM. Use `save` to persist to flash.
> Values only take effect after a reboot if saved.

### `bridge config save`

Persist RAM → flash (`/lfs/config.json`):

```bash
bridge config save
```

### `bridge config load`

Reload flash → RAM (refreshes RAM during running session):

```bash
bridge config load
```

### `bridge config reset`

Restore factory defaults in RAM (does not save):

```bash
bridge config reset
bridge config save  # if you want to persist
```

### `bridge reboot`

Reboot the device (to activate saved config):

```bash
bridge reboot
```

---

## Configuration — Detailed Reference

### The config.json structure

`config.json` is stored on the LittleFS flash partition at: `/lfs/config.json`

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

### Three ways to modify config

**1. Via serial shell (`make monitor`):**
```bash
bridge config set network.agent_ip 192.168.1.100
bridge config save
bridge reboot
```

**2. Via Python uploader (from Mac, after editing `app/config.json`):**
```bash
python3 tools/upload_config.py
# or with explicit port:
python3 tools/upload_config.py --port /dev/tty.usbmodem231401
```

> **Important:** The Python uploader and `make monitor` cannot run simultaneously —
> both use the serial port. Close screen (`Ctrl+A K Y`) before running the script.

**3. Programmatically from C (at initialization):**
```c
config_reset_defaults();
SAFE_STRCPY(g_config.network.agent_ip, "192.168.1.100");
config_save();
```

### DHCP vs Static IP

Switch to DHCP:
```bash
bridge config set network.dhcp true
bridge config save
bridge reboot
```

Switch back to static:
```bash
bridge config set network.dhcp false
bridge config set network.ip 192.168.68.114
bridge config set network.netmask 255.255.255.0
bridge config set network.gateway 192.168.68.1
bridge config save
bridge reboot
```

---

## Channel Manager System

### Concepts

A **channel** is a logical bridge between a physical device and a ROS2 topic.

Each channel can:
- **Publish**: read from hardware (`read()`) → send to ROS2 topic periodically
- **Subscribe**: receive from ROS2 topic → write to hardware (`write()`)
- Be publish-only, subscribe-only, or bidirectional
- Run at a configurable period (e.g., every 100ms)

### The `channel_t` struct

```c
// app/src/bridge/channel.h

typedef enum {
    MSG_BOOL    = 0,   // std_msgs/Bool
    MSG_INT32   = 1,   // std_msgs/Int32
    MSG_FLOAT32 = 2,   // std_msgs/Float32
} msg_type_t;

typedef union {
    bool    b;     // for MSG_BOOL
    int32_t i32;   // for MSG_INT32
    float   f32;   // for MSG_FLOAT32
} channel_value_t;

typedef struct {
    const char    *name;       // Channel name (shown in logs)
    const char    *topic_pub;  // ROS2 publish topic (NULL = no publish)
    const char    *topic_sub;  // ROS2 subscribe topic (NULL = no subscribe)
    msg_type_t     msg_type;   // Message type (Bool/Int32/Float32)
    uint32_t       period_ms;  // Publish period in milliseconds
    int  (*init)(void);                        // Hardware init (can be NULL)
    void (*read)(channel_value_t *val);        // Read from hardware (can be NULL)
    void (*write)(const channel_value_t *val); // Write to hardware (can be NULL)
} channel_t;
```

### How to add a new sensor or actuator

#### Step 1 — Create a device file (e.g., `app/src/user/my_sensor.c`)

```c
#include "bridge/channel.h"
#include <zephyr/drivers/gpio.h>

/* --- Hardware init --- */
static int my_sensor_init(void)
{
    // Initialize GPIO, ADC, I2C, SPI, etc.
    return 0;  // return 0 on success, negative on error
}

/* --- Read data (publish direction) --- */
static void my_sensor_read(channel_value_t *val)
{
    // Read a value from hardware
    val->f32 = 23.5f;  // e.g., temperature in Celsius
}

/* --- Channel definition --- */
const channel_t my_sensor_channel = {
    .name       = "my_sensor",
    .topic_pub  = "robot/temperature",   // ROS2 topic name
    .topic_sub  = NULL,                  // no subscribe direction
    .msg_type   = MSG_FLOAT32,
    .period_ms  = 1000,                  // publish once per second
    .init       = my_sensor_init,
    .read       = my_sensor_read,
    .write      = NULL,
};
```

#### Step 2 — Declare in a header (`app/src/user/my_sensor.h`)

```c
#ifndef MY_SENSOR_H
#define MY_SENSOR_H
#include "bridge/channel.h"
extern const channel_t my_sensor_channel;
#endif
```

#### Step 3 — Register in `user_channels.c`

```c
// app/src/user/user_channels.c
#include "user_channels.h"
#include "bridge/channel_manager.h"
#include "my_sensor.h"

void user_register_channels(void)
{
    channel_register(&my_sensor_channel);
}
```

#### Step 4 — Add to the build (`app/CMakeLists.txt`)

```cmake
target_sources(app PRIVATE
    src/main.c
    src/config/config.c
    src/shell/shell_cmd.c
    src/bridge/channel_manager.c
    src/user/user_channels.c
    src/user/my_sensor.c          # ← new line
)
```

#### Step 5 — Build and flash

```bash
make build
cp workspace/build/zephyr/zephyr.uf2 /Volumes/RPI-RP2/
```

#### Step 6 — Verify on ROS2

```bash
ros2 topic list                         # shows /robot/temperature
ros2 topic echo /robot/temperature      # continuous data stream
```

### Subscribe direction (ROS2 → Pico)

For actuators (motor, LED, relay) that receive commands from ROS2:

```c
static void my_led_write(const channel_value_t *val)
{
    gpio_pin_set(led_dev, LED_PIN, val->b ? 1 : 0);
}

const channel_t my_led_channel = {
    .name       = "my_led",
    .topic_pub  = NULL,              // no publish
    .topic_sub  = "robot/led",       // receives from ROS2
    .msg_type   = MSG_BOOL,
    .period_ms  = 0,                 // irrelevant for subscribe-only
    .init       = my_led_init,
    .read       = NULL,
    .write      = my_led_write,
};
```

Send a command from ROS2:
```bash
ros2 topic pub /robot/led std_msgs/Bool "data: true"
```

### Bidirectional channel (publish + subscribe)

Motor example: publish encoder position, subscribe to setpoint:

```c
const channel_t motor_channel = {
    .name       = "motor_left",
    .topic_pub  = "robot/motor_left/position",   // encoder position → ROS2
    .topic_sub  = "robot/motor_left/setpoint",   // ROS2 command → motor
    .msg_type   = MSG_INT32,
    .period_ms  = 50,                            // 20 Hz position update
    .init       = motor_left_init,
    .read       = motor_left_read_position,
    .write      = motor_left_write_setpoint,
};
```

### Message type selection guide

| Type | ROS2 message | Use for |
|------|-------------|---------|
| `MSG_BOOL` | `std_msgs/Bool` | GPIO, relay, switch, LED, digital I/O |
| `MSG_INT32` | `std_msgs/Int32` | Encoder ticks, PWM duty cycle, discrete values |
| `MSG_FLOAT32` | `std_msgs/Float32` | ADC, temperature, speed, PID output, distance |

### Constraints

- Maximum **16 channels** (`CHANNEL_MAX = 16` in `channel_manager.h`)
- Executor handle count = number of subscriber channels

---

## Robustness and Reliability

### Hardware Watchdog

The RP2040 internal watchdog timer runs with a **30-second timeout**. If the firmware freezes (deadlock, infinite loop, stack overflow), the watchdog automatically reboots the board.

`wdt_feed()` is called in:
- Agent search loop
- ROS2 session run loop
- DTR wait loop
- Every boot phase

### Reconnection State Machine

`bridge_run()` implements a **perpetual reconnection loop**:

```
┌─────────────────────────────────────────────────────┐
│  Phase 1: Agent search                              │
│  rmw_uros_ping_agent(200ms, 1) — until agent found  │
│  → watchdog_feed() + 2s delay per iteration         │
├─────────────────────────────────────────────────────┤
│  Phase 2: Session initialization                    │
│  ros_session_init() — support, node, entities, exec │
│  → on failure: 2s delay + retry                     │
├─────────────────────────────────────────────────────┤
│  Phase 3: Running                                   │
│  executor_spin + channel_publish + watchdog_feed    │
│  → ping every 1s → if disconnected: break           │
├─────────────────────────────────────────────────────┤
│  Phase 4: Cleanup                                   │
│  ros_session_fini() → LED off → back to Phase 1    │
└─────────────────────────────────────────────────────┘
```

### DTR Timeout (autonomous mode)

If USB console is **not connected** (e.g., robot running standalone), the firmware waits at most **3 seconds** for DTR signal, then continues automatically. No USB monitor is needed for normal operation.

### Ethernet Link UP Wait

After boot, the firmware waits up to **15 seconds** for the `NET_EVENT_IF_UP` event before applying IP configuration. This prevents "ping: host unreachable" errors during cable-less boot.

---

## micro-ROS Agent Setup (on the ROS2 host)

### In Docker

```bash
# Enter the ROS2 Docker container
docker exec -it <container_name> bash

# Start the agent
source /opt/ros/humble/setup.bash
ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888
```

If not installed:
```bash
apt install ros-humble-micro-ros-agent
```

### Verify after Pico connects

```bash
ros2 node list                    # shows /pico_bridge (or your configured name)
ros2 topic list                   # shows registered topics
ros2 topic echo /robot/sensor     # data stream
```

### Stale nodes in node list

DDS (Cyclone DDS) caches node information. If old node names appear in `ros2 node list`:
```bash
# Restart the agent to clear the cache:
Ctrl+C   # stop agent
ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888
```

---

## Python Config Uploader — Detailed

`tools/upload_config.py` automatically uploads the contents of `app/config.json` to the Pico over the serial port.

### Usage

```bash
# Auto-detect port:
python3 tools/upload_config.py

# Explicit port:
python3 tools/upload_config.py --port /dev/tty.usbmodem231401

# Custom config file:
python3 tools/upload_config.py --config app/config.json --port /dev/tty.usbmodem231401
```

### What it does

1. Opens serial port (115200 baud)
2. Waits for DTR (Pico boot)
3. Sends `bridge config set <key> <value>` for each field
4. Sends `bridge config save` — persists to flash
5. Sends `bridge config show` — verifies the result
6. Does **NOT reboot** — run `bridge reboot` manually to activate

---

## Stress Test

`tools/stress_test.py` is a comprehensive test suite combining automated and manual tests.

### Usage

```bash
python3 tools/stress_test.py
# or
python3 tools/stress_test.py --port /dev/tty.usbmodem231401
# skip manual tests (CI/CD mode):
python3 tools/stress_test.py --skip-manual
```

### Test categories

**Automated (run by the script):**
- T01–T05: Basic communication, config roundtrip, save/load
- T06–T10: Edge cases (buffer overflow, empty values, special chars, rapid fire)
- T11–T15: Flash integrity (20× save, DHCP toggle, port boundary values)
- T16–T17: Performance (shell latency, 100 burst commands)

**Manual (you perform, script waits):**
- M01–M05: Network stress (Ethernet cable pull, agent kill/restart, switch off)
- M06–M10: Power stress (power cycle, pull during config save)
- M11–M13: USB console stress (disconnect during operation, autonomous boot)
- M14–M16: Config switching (DHCP↔static, agent IP change)
- M17–M20: Long-run (15 minutes, vibration, temperature, cable wiggling)

### Report

At the end of the test, `tools/stress_report.json` is generated with all results.

---

## Current Status

| Feature | Status | Since |
|---------|--------|-------|
| Zephyr base firmware | ✅ Done | v1.0 |
| W6100 Ethernet driver | ✅ Done | v1.0 |
| USB CDC ACM console | ✅ Done | v1.0 |
| DHCP / static IP (from config.json) | ✅ Done, tested | v1.1 |
| Ethernet link UP boot stability | ✅ Done | v1.1 |
| micro-ROS UDP transport | ✅ Done | v1.0 |
| ROS2 publish | ✅ Done, tested | v1.0 |
| Status LED (GP25) | ✅ Done, tested | v1.2 |
| LittleFS flash partition | ✅ Done | v1.1 |
| JSON config read/write | ✅ Done | v1.1 |
| Serial shell (bridge commands) | ✅ Done, tested | v1.1 |
| Python config uploader | ✅ Done, tested | v1.1 |
| Python stress test suite | ✅ Done | v1.4 |
| Channel Manager framework | ✅ Done | v1.3 |
| User code space (user_channels.c) | ✅ Done | v1.3 |
| Hardware watchdog (30s) | ✅ Done | v1.4 |
| Reconnection state machine | ✅ Done | v1.4 |
| Security audit (buffer overflow, null ptr) | ✅ Done | v1.3 |
| Subscribe direction (ROS2 → Pico) | ✅ Done (framework) | v1.3 |
| GPIO driver | 🔄 Planned | — |
| ADC driver | 🔄 Planned | — |
| PWM / motor driver | 🔄 Planned | — |
| Serial (UART) driver | 🔄 Planned | — |
| Encoder (PIO) driver | 🔄 Planned | — |
| PID controller | 🔄 Planned | — |
| I2C driver | 🔄 Planned | — |
| Channel config from JSON (runtime) | 🔄 Planned | — |
| Runtime IP reload (no reboot) | 🔄 Planned | — |

---

## Planned Next Steps

### Step 3 — Built-in hardware drivers

Will live in `app/src/drivers/`, each compatible with the `channel_t` interface:

```
drivers/
├── drv_gpio.h / .c     ← digital I/O (MSG_BOOL)
├── drv_adc.h  / .c     ← analog input (MSG_FLOAT32)
├── drv_pwm.h  / .c     ← PWM output, motor control (MSG_INT32)
├── drv_uart.h / .c     ← Serial communication
└── drv_i2c.h  / .c     ← I2C master devices
```

Each driver exports a `const channel_t drv_xxx_channel` global.

### Step 4 — PIO quadrature encoder driver

The RP2040 PIO hardware is ideal for quadrature encoder reading. Planned API:

```c
// 2 encoder channels (2 PIO blocks):
extern const channel_t encoder_left_channel;
extern const channel_t encoder_right_channel;
```

### Step 5 — PID controller

Soft real-time PID library with 10ms loop period:

```c
typedef struct {
    float kp, ki, kd;
    float setpoint;
    float integral;
    float last_error;
} pid_t;

float pid_compute(pid_t *pid, float measurement, float dt_s);
```

### Step 6 — Channel config from JSON

Goal: define channels (topic names, period) in `config.json` at runtime, not only in C.

---

## Known Limitations

| Limitation | Explanation | Workaround |
|------------|-------------|------------|
| Max 16 channels | `CHANNEL_MAX = 16` | Increase value, costs RAM |
| Max 2 encoders | RP2040 has only 2 PIO blocks | — |
| RAM: ~44KB free | 220KB / 264KB used | Avoid large stack allocations |
| Soft real-time | Hard real-time not guaranteed | Improve with thread priorities |
| IP requires reboot | IP config only activates after reboot | Runtime reload planned |
| Zephyr board name | `w5500_evb_pico` (not w6100) | Filename kept as-is |
| Message types | Only Bool/Int32/Float32 | String, Array planned |

---

## Troubleshooting

### "ping: host unreachable" after boot

**Cause:** Static IP was applied before Ethernet link came up.
**Solution:** The firmware waits for `NET_EVENT_IF_UP` event (max 15s). Check the cable.

### LED stays off

**Cause:** Agent is not reachable.
**Check:**
```bash
# On ROS2 host:
ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888
# In Pico log:
# [main] Searching for agent: 192.168.68.125:8888 ...
```

### Shell not responding

```bash
screen -X quit          # close any stuck screen sessions
make monitor            # open fresh
```

### Config doesn't load (LittleFS error)

**Cause:** First boot, or flash corruption.
**Fix:** On first boot, defaults are written automatically. If corrupted:
```bash
bridge config reset
bridge config save
bridge reboot
```

### Watchdog reboot loop

**Cause:** `wdt_feed()` not called for 30 seconds in some phase.
**Debug:** Check logs — which phase is hanging? If in agent search loop: agent unreachable but WDT feed missing (should not happen in current code).

---

## Developer Environment Summary

| Parameter | Value |
|-----------|-------|
| Host OS | macOS (arm64) |
| Build | Docker (`w6100-zephyr-microros:latest`) |
| Zephyr SDK | 0.17.4 (ARM Cortex-M0+ toolchain) |
| Zephyr version | main (v4.3.99) |
| micro-ROS | Jazzy libs, Humble agent compatible |
| Serial port | `/dev/tty.usbmodem231401` |
| Firmware size | ~289 KB flash, ~220 KB RAM |
| GitHub | [nowlabstudio/ROS2-Bridge](https://github.com/nowlabstudio/ROS2-Bridge) |
| Author | Eduard Sik — [eduard@nowlab.eu](mailto:eduard@nowlab.eu) |
