# W6100 EVB Pico — Zephyr + micro-ROS Universal Bridge

> **Developer Reference Documentation**
> Last updated: 2026-03-06 | Version: v2.0 | Author: Eduard Sik — [eduard@nowlab.eu](mailto:eduard@nowlab.eu)

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
├── TECHNICAL_OVERVIEW.md            ← Deep-dive for senior devs and robot integrators
├── Makefile                         ← build, flash, monitor commands
│
├── docker/
│   └── Dockerfile                   ← Docker image: zephyrprojectrtos/ci:v0.28.8
│
├── tools/
│   ├── upload_config.py             ← Python config uploader (Mac/Linux)
│   ├── test_bridge.py               ← Lightweight ROS2 topic/service smoke test
│   ├── stress_test.py               ← v1.5 shell-only tests (legacy)
│   ├── stress_report.json           ← v1.5 test report
│   ├── v2_stress_test.py            ← v2.0 full test suite (74 auto + 12 manual)
│   ├── v2_stress_report.json        ← v2.0 auto-generated test report
│   ├── flash.sh                     ← Flash firmware via 'bridge bootsel' (no button needed)
│   ├── docker-run-agent-udp.sh      ← Start micro-ROS Jazzy agent in Docker (UDP)
│   ├── docker-run-ros2.sh           ← Start ROS2 Jazzy interactive shell in Docker
│   └── start-eth.sh                 ← Launch full test environment (agent + shell, 2 terminals)
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
    │   └── w5500_evb_pico.overlay   ← DTS overlay: USB CDC ACM, LED, ADC, LittleFS, W6100
    │
    └── src/
        ├── main.c                   ← Entry point, reconnection loop, watchdog
        │
        ├── config/
        │   ├── config.h             ← bridge_config_t struct, API declarations
        │   └── config.c             ← LittleFS mount, JSON parser, config_set/save/load
        │
        ├── shell/
        │   └── shell_cmd.c          ← 'bridge' shell commands (show/set/save/load/reset/reboot/bootsel)
        │
        ├── bridge/
        │   ├── channel.h            ← channel_t, channel_state_t, msg_type_t, channel_value_t
        │   ├── channel_manager.c/.h ← Pub/sub framework, IRQ path, entity lifecycle
        │   ├── diagnostics.c/.h     ← /diagnostics topic publisher (5s period)
        │   ├── param_server.c/.h    ← rclc_parameter_server (period_ms, enabled per channel)
        │   └── service_manager.c/.h ← std_srvs SetBool / Trigger service registration
        │
        ├── drivers/
        │   ├── drv_gpio.c/.h        ← E-Stop input (GP15) + relay-brake output (GP14)
        │   └── drv_adc.c/.h         ← Battery voltage ADC (GP26, channel 0)
        │
        └── user/
            ├── user_channels.h      ← Declaration: user_register_channels()
            ├── user_channels.c      ← WRITE YOUR CHANNELS HERE (the only file you edit)
            ├── test_channels.h      ← Built-in test channels (no hardware required)
            └── test_channels.c      ← counter / heartbeat / echo — remove when adding real hardware
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

Build stats: ~418 KB flash (2.49% of 16 MB), ~262 KB RAM (97% of 264 KB). Heap: 96 KB (`CONFIG_HEAP_MEM_POOL_SIZE=98304`).

### 4. Flash the firmware

**Option A — Without touching hardware buttons (recommended after first flash):**

```bash
tools/flash.sh
# or with explicit port:
tools/flash.sh /dev/tty.usbmodem231401
```

The script sends `bridge bootsel` over the serial shell, waits for `/Volumes/RPI-RP2` to mount, then copies the UF2 file automatically.

**Option B — Manual BOOTSEL (first flash, or if firmware is unresponsive):**

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
[main] Watchdog active (8000 ms timeout)
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
[main] micro-ROS session active. 3 channels, 1 subscribers.
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

### `bridge bootsel`

Reboot into USB bootloader (BOOTSEL mode) — mounts as `/Volumes/RPI-RP2`.
Use this to flash new firmware **without physically pressing the BOOTSEL button**:

```bash
bridge bootsel
# → /Volumes/RPI-RP2 appears automatically
cp workspace/build/zephyr/zephyr.uf2 /Volumes/RPI-RP2/
```

Or use the included script which does this in one step:

```bash
tools/flash.sh
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

// Const descriptor — stored in flash, never modified at runtime
typedef struct {
    const char    *name;         // Channel name (shown in logs)
    const char    *topic_pub;    // ROS2 publish topic (NULL = no publish)
    const char    *topic_sub;    // ROS2 subscribe topic (NULL = no subscribe)
    msg_type_t     msg_type;     // Message type (Bool/Int32/Float32)
    uint32_t       period_ms;    // Default publish period in milliseconds
    bool           irq_capable;  // true = GPIO interrupt triggers immediate publish
    int  (*init)(void);                        // Hardware init (can be NULL)
    void (*read)(channel_value_t *val);        // Read from hardware (can be NULL)
    void (*write)(const channel_value_t *val); // Write to hardware (can be NULL)
} channel_t;

// Mutable state — in RAM, modified by param server and ISR
typedef struct {
    uint32_t  period_ms;    // Active period (overrides descriptor default)
    bool      enabled;      // Enable/disable publish
    bool      invert_logic; // Invert bool value on publish/receive
    atomic_t  irq_pending;  // Set by ISR, cleared by main loop
} channel_state_t;
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
- Executor handle count = subscriber channels + `PARAM_SERVER_HANDLES` (6) + service count

---

### Built-in test channels (no hardware required)

The firmware ships with three pre-registered channels in `app/src/user/test_channels.c`.
They require no sensors, no wiring, and no `prj.conf` changes — useful for verifying the full
ROS2 pipeline before connecting real hardware.

| Channel | Topic (pub) | Topic (sub) | Type | Period | Description |
|---------|------------|-------------|------|--------|-------------|
| `test_counter` | `pico/counter` | — | INT32 | 500 ms | Counts up from 0 |
| `test_heartbeat` | `pico/heartbeat` | — | BOOL | 1000 ms | Toggles true/false |
| `test_echo` | `pico/echo_out` | `pico/echo_in` | INT32 | 1000 ms | Echoes received value |

**Verify on ROS2:**
```bash
ros2 topic list
ros2 topic echo /pico/counter              # INT32 counting up every 500 ms
ros2 topic echo /pico/heartbeat            # BOOL toggling every 1 s
ros2 topic pub /pico/echo_in std_msgs/msg/Int32 "data: 42"
ros2 topic echo /pico/echo_out             # echoes 42 back
```

**Remove when adding real hardware** — just delete the three `channel_register()` calls
from `user_channels.c` (and optionally remove `test_channels.c/h` from the build).

---

## Agent Connection — Status LED Example

The firmware lights up the built-in LED (GP25) when the micro-ROS agent connects, and turns it off when the connection is lost. This is the canonical pattern for reacting to agent connect/disconnect events.

The relevant code lives in `app/src/main.c`:

```c
/* LED descriptor — resolved at compile time from DTS alias "led0" = GP25 */
static const struct gpio_dt_spec status_led =
    GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static void led_set(bool on)
{
    gpio_pin_set_dt(&status_led, on ? 1 : 0);
}
```

Inside `bridge_run()`, the LED tracks the connection state:

```c
while (true) {

    /* Phase 1 — searching for agent: LED off */
    led_set(false);
    while (rmw_uros_ping_agent(200, 1) != RMW_RET_OK) {
        watchdog_feed();
        k_sleep(K_MSEC(2000));
    }

    /* Phase 2 — session init succeeded: LED on */
    if (ros_session_init()) {
        led_set(true);          // <-- agent connected, LED on

        /* Phase 3 — running loop */
        while (ping_ok) { ... }

        /* Phase 4 — connection lost: LED off */
        led_set(false);         // <-- agent lost, LED off
        ros_session_fini();
    }
}
```

**Expected behavior:**
- LED `OFF` — booting, waiting for Ethernet, or searching for agent
- LED `ON (steady)` — micro-ROS session active, agent reachable
- LED `OFF` again — agent lost, automatic reconnect in progress

To add your own logic on connect/disconnect, place it at the same points in `bridge_run()` — after `led_set(true)` for connect events, and before/after `ros_session_fini()` for disconnect.

---

## ROS 2 Services (v2.0)

Two built-in services handle safety-critical hardware:

| Service | Type | Description |
|---------|------|-------------|
| `/bridge/relay_brake` | `std_srvs/SetBool` | Engage (`true`) / release (`false`) brake relay on GP14 |
| `/bridge/estop` | `std_srvs/Trigger` | Read current E-Stop state (GP15 NC switch) |

```bash
# Engage brake
ros2 service call /bridge/relay_brake std_srvs/srv/SetBool "{data: true}"

# Query E-Stop state
ros2 service call /bridge/estop std_srvs/srv/Trigger "{}"
```

Both services use fully **static message memory** — no heap allocation at request time.
Register additional services in `app/src/user/user_channels.c` via `service_register_set_bool()` / `service_register_trigger()`.

---

## Parameter Server (v2.0)

The bridge exposes a `rclc_parameter_server` (Jazzy, `low_mem_mode=true`) that creates three parameters per registered channel:

| Parameter | Type | Description |
|-----------|------|-------------|
| `ch.<name>.period_ms` | INT | Publish period override (ms) |
| `ch.<name>.enabled` | BOOL | Enable / disable channel |
| `ch.<name>.invert_logic` | BOOL | Invert bool value on publish/receive |

```bash
# List all bridge parameters
ros2 param list /pico_bridge

# Set counter publish period to 200 ms
ros2 param set /pico_bridge ch.test_counter.period_ms 200

# Disable heartbeat
ros2 param set /pico_bridge ch.test_heartbeat.enabled false

# Dump all values
ros2 param dump /pico_bridge
```

**Persistence:** changes are automatically saved to `/lfs/ch_params.json` and restored on next boot / reconnect.

> **Note:** String-type parameters are unavailable in `low_mem_mode`. Only BOOL and INT are supported.

---

## Diagnostics (v2.0)

The bridge publishes a `diagnostic_msgs/DiagnosticArray` on `/diagnostics` every **5 seconds**.

```bash
ros2 topic echo /diagnostics diagnostic_msgs/msg/DiagnosticArray
```

Key-value fields in the status message:

| Key | Example value | Description |
|-----|--------------|-------------|
| `uptime_s` | `"3742"` | Seconds since boot |
| `channels` | `"3"` | Number of registered channels |
| `reconnects` | `"1"` | Agent reconnection counter |
| `firmware` | `"v2.0-W6100"` | Firmware version string |
| `ip` | `"192.168.68.114"` | Active IP address of the bridge |

Compatible with `rqt_robot_monitor` and any Nav2 diagnostics aggregator.

---

## Robustness and Reliability

### Hardware Watchdog

The RP2040 internal watchdog timer runs with an **8-second timeout** (RP2040 hardware maximum is ~8388 ms). If the firmware freezes (deadlock, infinite loop, stack overflow), the watchdog automatically reboots the board.

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

### Recommended: Docker script (included)

```bash
# Start agent + ROS2 shell in two terminal windows (Ubuntu + GNOME Terminal):
./tools/start-eth.sh

# Or start just the agent:
./tools/docker-run-agent-udp.sh           # default port 8888
./tools/docker-run-agent-udp.sh 9999      # custom port
```

Uses `microros/micro-ros-agent:jazzy` Docker image with `--net=host`. No installation needed beyond Docker.

### Manual (in any ROS2 Jazzy environment)

```bash
source /opt/ros/jazzy/setup.bash
ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888

# If not installed:
apt install ros-jazzy-micro-ros-agent
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

`tools/v2_stress_test.py` is the v2.0 full test suite (74 automated + 12 manual). The legacy `tools/stress_test.py` covers v1.5 shell tests only.

### Usage

```bash
# Prerequisites
pip install pyserial

# Full test suite (shell + ROS2)
python3 tools/v2_stress_test.py \
  --port /dev/tty.usbmodem231401 \
  --agent-ip 192.168.68.125

# Shell tests only (no ROS2 agent required)
python3 tools/v2_stress_test.py --port /dev/tty.usbmodem231401 --shell-only

# Skip manual tests (CI-friendly)
python3 tools/v2_stress_test.py \
  --port /dev/tty.usbmodem231401 \
  --agent-ip 192.168.68.125 \
  --skip-manual
```

### Test categories (v2.0)

| Section | IDs | Focus |
|---------|-----|-------|
| Shell / Config | T01–T17 | All `bridge config` subcommands, save/load/reset |
| ROS2 Topics | T20–T27 | Topic existence, publish rate, echo sub→pub |
| Services / E-Stop | T30–T38 | SetBool relay, Trigger estop, concurrent calls |
| Parameter Server | T40–T44 | Set/get period, enable/disable, persistence |
| Diagnostics | T50–T53 | `/diagnostics` rate, KV field presence |
| Reconnect Stress | T60–T65 | 5× rapid agent kill/restart |
| Safety / Latency | T70–T74 | E-Stop P99 < 100ms, topic flood under load |
| Manual | M01–M12 | Physical GPIO, brake relay, power cycle |

Key thresholds: E-Stop latency P99 < 100ms, service response < 500ms, topic rate > 80% of configured.

### Report

At the end of the test, `tools/v2_stress_report.json` is generated with all results.

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
| Python stress test suite v1.5 | ✅ Done, all 17 auto tests passing | v1.5 |
| Channel Manager framework | ✅ Done | v1.3 |
| User code space (user_channels.c) | ✅ Done | v1.3 |
| Built-in test channels (counter / heartbeat / echo) | ✅ Done, tested | v1.5 |
| Shell robustness (overflow protection, rapid-fire) | ✅ Done, tested | v1.5 |
| Hardware watchdog (8s) | ✅ Done | v1.4 |
| Reconnection state machine | ✅ Done | v1.4 |
| Security audit (buffer overflow, null ptr) | ✅ Done | v1.3 |
| Subscribe direction (ROS2 → Pico) | ✅ Done (framework) | v1.3 |
| IRQ-capable channels (atomic flag, 1ms main loop) | ✅ Done | v2.0 |
| GPIO driver (E-Stop GP15, relay-brake GP14) | ✅ Done | v2.0 |
| ADC driver (battery voltage, GP26) | ✅ Done | v2.0 |
| std_srvs services (SetBool, Trigger) | ✅ Done | v2.0 |
| Parameter server (period_ms, enabled, persistence) | ✅ Done | v2.0 |
| Diagnostics (/diagnostics, 5s period) | ✅ Done | v2.0 |
| v2.0 stress test suite (74 auto + 12 manual) | ✅ Done | v2.0 |
| PWM / motor driver | 🔄 Planned | — |
| Serial (UART) driver | 🔄 Planned | — |
| Encoder (PIO) driver | 🔄 Planned | — |
| PID controller | 🔄 Planned | — |
| I2C driver | 🔄 Planned | — |
| Channel config from JSON (runtime) | 🔄 Planned | — |
| Runtime IP reload (no reboot) | 🔄 Planned | — |

---

## Planned Next Steps

### Step 3 — Built-in hardware drivers ✅ (Done in v2.0)

`app/src/drivers/` contains:

```
drivers/
├── drv_gpio.c/.h  ← E-Stop input (GP15) + relay-brake output (GP14)
└── drv_adc.c/.h   ← Battery voltage ADC (GP26, channel 0)
```

Remaining planned drivers (PWM, UART, I2C) follow the same `channel_t` pattern.

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
| RAM: ~8KB static margin | 262KB / 264KB used (97%), 96KB heap | Avoid large stack allocations; monitor heap at runtime |
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

**Cause:** `wdt_feed()` not called for 8 seconds in some phase.
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
| Firmware size | ~418 KB flash, ~262 KB RAM (97%), 96 KB heap |
| GitHub | [nowlabstudio/ROS2-Bridge](https://github.com/nowlabstudio/ROS2-Bridge) |
| Author | Eduard Sik — [eduard@nowlab.eu](mailto:eduard@nowlab.eu) |
