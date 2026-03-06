# W6100 EVB Pico — micro-ROS Bridge: Technical Overview

> Author: Eduard Sik — [eduard@nowlab.eu](mailto:eduard@nowlab.eu)
> Date: 2026-03-05 | Firmware version: v1.5
> Target audience: senior embedded/robotics engineers evaluating or extending this system

---

## 1. Purpose and Positioning

This project implements a **hardware-agnostic sensor/actuator bridge** between physical I/O
(GPIO, ADC, PWM, I²C, SPI, UART, encoders) and a ROS 2 computational graph, using a
WIZnet W6100 EVB Pico (RP2040 + W6100 hardwired Ethernet) as the edge node.

The bridge runs **Zephyr RTOS** (v4.3.99 main) with **micro-ROS** (Jazzy libraries, Humble
agent compatible) as the ROS 2 transport layer, over **UDP/IPv4 via hardwired Ethernet**.

### Compared to similar approaches

| Approach | Transport | RTOS | Config | Reconnect | Sensor API |
|---|---|---|---|---|---|
| **This project** | UDP / Ethernet (W6100) | Zephyr | JSON on flash | Automatic state machine | `channel_t` descriptor |
| rosserial (ROS 1) | UART | Bare metal | Compile-time | Manual reset required | Custom message classes |
| micro-ROS on Arduino | WiFi or UART | FreeRTOS / none | Compile-time | Partial (library) | Direct rcl/rclc calls |
| micro-ROS on ESP32 | WiFi or Ethernet | FreeRTOS | Compile-time | Agent-ping loop | Direct rcl/rclc calls |
| MicroPython + uros | UART or UDP | Cooperative | Compile-time | None | Python callbacks |
| Zenoh bridge (pico) | UDP / Ethernet | FreeRTOS | Compile-time | Partial | Zenoh topic API |

**Key differentiators of this system:**
- Runtime JSON configuration (IP, agent address, ROS namespace) stored on flash — no
  recompile needed to deploy to a different network or robot.
- Clean hardware abstraction: adding a new sensor requires writing exactly **one `channel_t`
  struct** and **one or two functions** (`read` / `write`). Zero changes to framework code.
- Full automatic reconnection with 4-phase state machine (link → DHCP → agent ping →
  session init → run → cleanup on loss).
- Hardware watchdog (RP2040 built-in WDT, 30 s) ensures recovery from any firmware hang.
- Hardwired Ethernet (W6100) eliminates WiFi association latency and interference — latency
  is bounded by 100 Mbit/s link + UDP RTT (typically 1–5 ms on LAN).

---

## 2. Hardware Platform

| Component | Detail |
|---|---|
| MCU | RP2040 (dual-core Cortex-M0+, 133 MHz, 264 KB SRAM) |
| Ethernet | WIZnet W6100 (hardwired TCP/IP + UDP/IPv4/IPv6 offload) |
| Flash | 16 MB QSPI (XIP), last 1 MB reserved for LittleFS config storage |
| Board | W6100 EVB Pico (Zephyr board name: `w5500_evb_pico`) |
| Console | USB CDC ACM UART (virtual serial over USB) |
| Status LED | GP25 (built-in) — ON = micro-ROS session active |
| WDT | RP2040 built-in watchdog, 30 s timeout, SoC reset on expire |

**RAM budget (v1.5):**

| Region | Total | Used | Free |
|---|---|---|---|
| SRAM | 264 KB | ~229 KB (86.6%) | ~35 KB |
| Flash | 16 MB | ~283 KB (1.7%) | ~15.7 MB |

Flash consumption is negligible — there is no practical limit to adding firmware features.
RAM is the real constraint; micro-ROS alone consumes ~128 KB heap.

---

## 3. Software Stack

```
┌─────────────────────────────────────────────────────────┐
│  User code  (app/src/user/)                             │
│    channel_t descriptors: read / write / init callbacks │
├─────────────────────────────────────────────────────────┤
│  Channel Manager  (app/src/bridge/)                     │
│    entity lifecycle · pub timer · executor dispatch     │
├────────────────────┬────────────────────────────────────┤
│  micro-ROS (rclc)  │  Config (LittleFS + JSON)          │
│  rcl / rmw         │  Shell (Zephyr shell subsystem)    │
├────────────────────┴────────────────────────────────────┤
│  Zephyr RTOS v4.3.99                                    │
│    net stack · USB CDC ACM · flash map · WDT · logging  │
├─────────────────────────────────────────────────────────┤
│  Hardware:  RP2040  +  WIZnet W6100                     │
└─────────────────────────────────────────────────────────┘
```

---

## 4. ROS 2 Side — Capabilities and Behaviour

### 4.1 Transport

- **Protocol:** UDP/IPv4 over 100 Mbit/s hardwired Ethernet.
- **Agent:** standard `micro_ros_agent` (Jazzy libs; Humble agent compatible).
  ```bash
  ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888
  ```
- **Agent discovery:** the Pico pings the configured agent IP (`network.agent_ip`) every
  2 seconds until the agent responds. Once found, a full rcl session is established.
  If the agent disappears (ping timeout > 200 ms), the session is torn down cleanly and
  the ping loop restarts — no watchdog trip, no manual intervention.

### 4.2 Node identity

- **Node name:** configurable at runtime via `ros.node_name` in `config.json` (default:
  `pico_bridge`).
- **Namespace:** configurable at runtime via `ros.namespace` (default: `/`).
- Changes take effect after `bridge reboot` — no recompile needed.

### 4.3 Topics

Each registered `channel_t` creates **up to two topics** on the ROS 2 graph:

| Direction | Condition | ROS 2 message type |
|---|---|---|
| Pico → ROS 2 (publish) | `topic_pub != NULL && read != NULL` | `std_msgs/Bool`, `std_msgs/Int32`, `std_msgs/Float32` |
| ROS 2 → Pico (subscribe) | `topic_sub != NULL && write != NULL` | same types |

Topics are named exactly as the `topic_pub` / `topic_sub` strings in the channel descriptor,
prefixed with the node namespace. Example: namespace `/`, topic `"robot/motor_left/position"`
→ ROS 2 full topic name `/robot/motor_left/position`.

### 4.4 Publish rate

Each channel has an independent `period_ms` (e.g., 500 ms for a counter, 50 ms for a motor
encoder). The publish timer is checked every 10 ms in the main loop via
`channel_manager_publish()`. Actual publish jitter is ≤ 10 ms + UDP RTT.

### 4.5 Subscribe / inbound data flow

Inbound messages are processed by `rclc_executor_spin_some()` with a 10 ms budget per
main loop iteration. When a subscribed message arrives, the executor calls the channel's
`write()` callback synchronously, which delivers the `channel_value_t` to the actuator
driver. Latency from ROS 2 publish to `write()` call: typically 5–20 ms on LAN.

### 4.6 Message types currently supported

| `msg_type_t` | ROS 2 type | C field | Typical use |
|---|---|---|---|
| `MSG_BOOL` | `std_msgs/Bool` | `val.b` | GPIO, relay, LED, switch, enable/disable |
| `MSG_INT32` | `std_msgs/Int32` | `val.i32` | Encoder ticks, PWM duty (0–65535), discrete index |
| `MSG_FLOAT32` | `std_msgs/Float32` | `val.f32` | ADC voltage, temperature, speed, PID output |

> **Extension:** adding a new type (e.g., `std_msgs/String`, `geometry_msgs/Twist`) requires
> extending `msg_type_t`, `channel_value_t`, and the switch statements in
> `channel_manager.c`. The rest of the framework is unchanged.

### 4.7 Built-in test channels (v1.5)

Three channels are pre-registered in `test_channels.c` for end-to-end pipeline validation
without any physical hardware:

| Name | Pub topic | Sub topic | Type | Period |
|---|---|---|---|---|
| `test_counter` | `pico/counter` | — | INT32 | 500 ms |
| `test_heartbeat` | `pico/heartbeat` | — | BOOL | 1000 ms |
| `test_echo` | `pico/echo_out` | `pico/echo_in` | INT32 | 1000 ms |

```bash
ros2 topic echo /pico/counter           # INT32 incrementing
ros2 topic echo /pico/heartbeat         # BOOL toggling
ros2 topic pub /pico/echo_in std_msgs/msg/Int32 "data: 42"
ros2 topic echo /pico/echo_out          # echoes 42 back
```

Remove by deleting the three `channel_register()` calls in `user_channels.c`.

---

## 5. Bridge Side — Capabilities and Behaviour

### 5.1 Channel abstraction (`channel_t`)

The central design element is the `channel_t` struct in `app/src/bridge/channel.h`:

```c
typedef struct {
    const char  *name;         // log identifier
    const char  *topic_pub;    // ROS2 publish topic   (NULL = no publish)
    const char  *topic_sub;    // ROS2 subscribe topic (NULL = no subscribe)
    msg_type_t   msg_type;     // MSG_BOOL / MSG_INT32 / MSG_FLOAT32
    uint32_t     period_ms;    // publish interval in ms (0 = event-driven)

    int  (*init)(void);                        // hardware init (called once at boot)
    void (*read)(channel_value_t *out);        // Pico → ROS2: read sensor value
    void (*write)(const channel_value_t *in);  // ROS2 → Pico: drive actuator
} channel_t;
```

A channel is **statically allocated** (typically `const` in BSS/rodata). The framework holds
up to `CHANNEL_MAX = 16` channel pointers. All three function pointers are optional — set to
`NULL` to disable the corresponding direction or skip hardware init.

### 5.2 Channel Manager lifecycle

```
boot
 └─ user_register_channels()      ← populates channels[] array
 └─ channel_manager_init_channels()  ← calls ch->init() for each channel
 └─ micro-ROS session init
     └─ channel_manager_create_entities()  ← creates pub/sub RCL entities
     └─ channel_manager_add_subs_to_executor()
     └─ main run loop:
         ├─ rclc_executor_spin_some()   ← dispatches write() callbacks
         └─ channel_manager_publish()   ← timer-driven read() + rcl_publish()
 └─ on session loss:
     └─ channel_manager_destroy_entities()
     └─ (reconnection loop restarts)
```

`channel_manager_destroy_entities()` cleanly calls `rcl_publisher_fini` and
`rcl_subscription_fini` for all active entities before reconnection. This prevents micro-ROS
memory leaks across reconnection cycles.

### 5.3 Reconnection state machine (4 phases)

```
Phase 1: Agent search
  └─ rmw_uros_ping_agent(200 ms, 1) every 2 s
  └─ WDT fed every 2.2 s (within 30 s budget)
  └─ LED OFF

Phase 2: Session init
  └─ rcl_init → node_init → create_entities → executor_init
  └─ on failure: back to Phase 1 after 2 s

Phase 3: Run
  └─ executor_spin_some(10 ms) + channel_manager_publish() + wdt_feed()
  └─ agent liveness check every 1 s via ping (200 ms timeout)
  └─ on ping failure: Phase 4
  └─ LED ON

Phase 4: Cleanup
  └─ destroy_entities → node_fini → support_fini
  └─ 2 s cooldown → back to Phase 1
  └─ LED OFF
```

This loop is **never-exiting**. The WDT is fed in all phases. If the firmware hangs anywhere
for > 30 s (e.g., a blocking driver call), the SoC resets via WDT.

### 5.4 Runtime configuration

Configuration is stored as JSON in a **LittleFS partition** (last 1 MB of the 16 MB flash,
offset `0xF00000`). The `config.json` file is read at boot and written on demand.

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

**Three ways to modify config at runtime:**

1. **Serial shell** (USB CDC ACM, any terminal at 115200):
   ```
   bridge config set network.agent_ip 192.168.1.50
   bridge config save
   bridge reboot
   ```

2. **Python uploader** (automated, from host):
   ```bash
   python3 tools/upload_config.py --agent_ip 192.168.1.50 --node_name arm_pico
   ```

3. **Direct JSON edit** — mount the Pico in mass-storage mode (UF2), write `config.json`
   to the LittleFS partition via a LittleFS host tool.

**Config key validation:** `config_set()` rejects values longer than 47 chars
(`-ENAMETOOLONG`) and unknown keys (`-ENOENT`). The shell reports the error and continues —
no crash on malformed input.

### 5.5 Shell commands

```
bridge config show              — print current config
bridge config set <key> <value> — set a field in RAM
bridge config save              — persist to LittleFS (config.json)
bridge config load              — reload from flash (discard RAM changes)
bridge config reset             — restore factory defaults to RAM
bridge reboot                   — cold reboot (sys_reboot)
```

**Shell robustness parameters (v1.5):**

| Parameter | Value | Rationale |
|---|---|---|
| `SHELL_CMD_BUFF_SIZE` | 256 B | Prevents crash on commands > 128 B (Zephyr default) |
| `SHELL_BACKEND_SERIAL_RX_RING_BUFFER_SIZE` | 512 B | Rapid-fire command burst buffering |
| `SHELL_BACKEND_SERIAL_TX_RING_BUFFER_SIZE` | 8192 B | Prevents shell thread blocking on TX full (50 cmd burst ≈ 5.4 KB output) |
| `SHELL_STACK_SIZE` | 8192 B | Deep call chain headroom under log backend load |
| `LOG_PROCESS_THREAD_STACK_SIZE` | 2048 B | Log thread calls shell log backend: 768 B default was overflowing under RX burst |

---

## 6. External Communication Channel Integration

This section describes how to connect any physical or logical interface to the ROS 2 graph
using the channel system.

### 6.1 The single-file model

Adding a new peripheral requires exactly **two files** (one `.c`, one `.h`) and **two lines**
in existing files (`user_channels.c` registration + `CMakeLists.txt` source list).
No changes to main.c, channel_manager.c, or any framework file.

### 6.2 GPIO (digital I/O)

```c
// Publish-only: read a button or limit switch
static void button_read(channel_value_t *val) {
    val->b = gpio_pin_get_dt(&button_spec);
}

const channel_t button_channel = {
    .name = "door_sensor", .topic_pub = "robot/door", .topic_sub = NULL,
    .msg_type = MSG_BOOL, .period_ms = 100,
    .init = button_init, .read = button_read, .write = NULL,
};

// Subscribe-only: drive a relay or LED from ROS2
static void relay_write(const channel_value_t *val) {
    gpio_pin_set_dt(&relay_spec, val->b ? 1 : 0);
}
```

### 6.3 ADC (analogue sensors)

```c
static void adc_read(channel_value_t *val) {
    int16_t raw;
    adc_read(&adc_seq);
    val->f32 = (float)raw * ADC_REF_MV / ADC_RESOLUTION;
}

const channel_t voltage_channel = {
    .name = "battery_voltage", .topic_pub = "robot/battery_v",
    .msg_type = MSG_FLOAT32, .period_ms = 1000,
    .init = adc_init, .read = adc_read, .write = NULL,
};
```

### 6.4 PWM (servo, motor speed)

```c
static void pwm_write(const channel_value_t *val) {
    // val->i32 = duty cycle 0–65535
    pwm_set_dt(&servo_spec, PWM_PERIOD_NS, val->i32 * PWM_PERIOD_NS / 65535);
}

const channel_t servo_channel = {
    .name = "servo_pan", .topic_pub = NULL, .topic_sub = "robot/servo/pan",
    .msg_type = MSG_INT32, .period_ms = 0,
    .init = servo_init, .read = NULL, .write = pwm_write,
};
```

### 6.5 I²C sensors (temperature, IMU, ToF distance)

```c
#include <zephyr/drivers/i2c.h>

static void imu_read(channel_value_t *val) {
    uint8_t buf[2];
    i2c_burst_read_dt(&imu_i2c, REG_ACCEL_X, buf, 2);
    val->f32 = (int16_t)((buf[0] << 8) | buf[1]) * ACCEL_SCALE;
}
```

### 6.6 SPI sensors (high-speed ADC, magnetic encoders)

```c
#include <zephyr/drivers/spi.h>

static void encoder_read(channel_value_t *val) {
    uint8_t tx[2] = {0}, rx[2];
    spi_transceive_dt(&enc_spi, &tx_buf, &rx_buf);
    val->i32 = ((rx[0] & 0x3F) << 8) | rx[1];  // 14-bit AS5048A
}
```

### 6.7 UART peripherals (LIDAR, GPS, serial sensors)

```c
#include <zephyr/drivers/uart.h>

static void lidar_read(channel_value_t *val) {
    // poll internal ring buffer populated by UART ISR
    val->f32 = g_lidar_distance_m;
}
```

### 6.8 PIO / encoder (RP2040-specific)

The RP2040's PIO state machines are ideal for quadrature encoder reading without CPU overhead.
Zephyr exposes PIO via the `pio-qdec` driver:

```c
#include <zephyr/drivers/sensor.h>

static void encoder_read(channel_value_t *val) {
    struct sensor_value ticks;
    sensor_sample_fetch(qdec_dev);
    sensor_channel_get(qdec_dev, SENSOR_CHAN_ROTATION, &ticks);
    val->i32 = ticks.val1;
}
```

### 6.9 Software / computed channels

A channel does not need to represent physical hardware. Examples:
- **Uptime counter**: `val->i32 = (int32_t)(k_uptime_get() / 1000);`
- **CPU temperature** (RP2040 ADC ch 4): reads the built-in temperature sensor
- **PID output**: reads setpoint from one subscribe channel, reads encoder, publishes
  control effort — all in `read()` and `write()` with shared state via file-scope variables
- **Watchdog health beat**: publish a heartbeat; if it stops, the ROS 2 side detects the
  failure via topic timeout

### 6.10 Bidirectional example (motor + encoder)

```c
static int32_t g_setpoint = 0;

static void motor_write(const channel_value_t *val) {
    g_setpoint = val->i32;
    // apply to PWM driver
}

static void motor_read(channel_value_t *val) {
    val->i32 = encoder_get_ticks();  // current position
}

const channel_t motor_channel = {
    .name      = "motor_left",
    .topic_pub = "robot/motor_left/ticks",    // encoder → ROS2, 20 Hz
    .topic_sub = "robot/motor_left/setpoint", // ROS2 → PWM duty
    .msg_type  = MSG_INT32,
    .period_ms = 50,
    .init      = motor_init,
    .read      = motor_read,
    .write     = motor_write,
};
```

---

## 7. Robustness and Failure Handling

| Failure scenario | Detection | Recovery |
|---|---|---|
| micro-ROS agent unreachable at boot | ping timeout (200 ms, every 2 s) | wait in Phase 1 loop; WDT fed; LED stays OFF |
| Agent disappears mid-session | ping failure in Phase 3 (1 s interval) | Phase 4 cleanup → Phase 1 |
| Ethernet cable unplugged | net_if DOWN event or ping timeout | reconnect on re-plug |
| USB console unplugged | DTR goes low (detected at TX interrupt) | autonomous mode continues; bridge unaffected |
| Firmware hang (> 30 s) | RP2040 WDT | SoC cold reset |
| LittleFS corrupt / missing | `config_init()` fallback | factory defaults loaded; continue |
| Shell command buffer overflow | `SHELL_CMD_BUFF_SIZE=256` | shell survives; garbled command → error printed |
| Rapid-fire shell commands (> 50/2s) | TX ring buffer 8192 B; log thread stack 2048 B | shell stays responsive |
| Config value too long (> 47 chars) | `config_set()` returns `-ENAMETOOLONG` | error message; no memory corruption |

---

## 8. Testing

### Automated stress test (`tools/stress_test.py`)

| Range | Category | Tests |
|---|---|---|
| T01–T05 | Basic communication | Shell alive, config roundtrip, save/load, reset |
| T06–T10 | Robustness / edge cases | Empty value, unknown key, 200-char overflow, special chars, 50-cmd rapid fire |
| T11–T15 | Flash integrity | 20× repeated save, DHCP toggle, port boundary, invalid IP |
| T16–T17 | Performance | Shell latency (10 samples), 100-command burst |

**All 17 automated tests pass on v1.5 firmware.**

Known root causes resolved during v1.5 development:
- **T08 (200-char overflow):** `SHELL_CMD_BUFF_SIZE` was 128 B (Zephyr default); 237-char
  command overflowed the shell's internal parser buffer → now 256 B + `config_set()`
  length guard.
- **T10 (50 rapid-fire):** `LOG_PROCESS_THREAD_STACK_SIZE` was 768 B (Zephyr default); the
  log process thread overflowed while routing `LOG_WRN("RX ring buffer full.")` through the
  shell log backend under burst load → now 2048 B. TX ring buffer increased 512 B → 8192 B
  to prevent `shell_pend_on_txdone(K_FOREVER)` from being triggered.

### Manual tests (M01–M20)

20 hardware-in-the-loop tests covering: Ethernet cable pull/swap, switch off, agent
kill/restart (5×), USB unplug during operation, power cut during config save, 10× rapid
power cycle, BOOTSEL press, DHCP↔static switch, 15-minute continuous run, vibration,
temperature stress, cable wiggling.

---

## 9. Known Limitations

| Limitation | Impact | Planned fix |
|---|---|---|
| Max 16 channels (`CHANNEL_MAX`) | 16 pub/sub pairs per node | Increase if needed; RAM is the real limit |
| Only `Bool`, `Int32`, `Float32` messages | No `String`, `Array`, `Twist`, `LaserScan` | Add types to `channel.h` + `channel_manager.c` |
| IP/namespace requires reboot to activate | Config change → `bridge reboot` needed | Runtime reload (Planned) |
| No ROS 2 services or actions | Only topics supported | Service support would require significant rclc additions |
| No TLS / authentication | UDP traffic is plaintext | Suitable for trusted LAN; VPN tunnel recommended for WAN |
| Publish jitter ≤ 10 ms | Due to 10 ms main loop tick | Reducible to ~1 ms by tightening the spin loop |
| Single namespace per node | All channels share one ROS 2 namespace | Workaround: include namespace in topic strings directly |

---

## 10. Extension Roadmap

The following extensions are architecturally straightforward and do not require framework
changes — only new `channel_t` implementations in `app/src/user/`:

1. **GPIO driver** — digital in/out, interrupt-driven or polled
2. **ADC driver** — RP2040 built-in 12-bit ADC (5 channels), temperature
3. **PWM / motor driver** — Zephyr `pwm` API, servo, H-bridge enable
4. **I²C driver** — IMU (MPU-6050, LSM6DS3), distance (VL53L1X), pressure (BMP280)
5. **SPI driver** — magnetic encoder (AS5048A), high-speed ADC (MCP3208)
6. **PIO quadrature encoder** — `pio-qdec` Zephyr driver for RP2040 PIO
7. **UART peripheral** — LIDAR (RPLIDAR A1), GPS (NMEA), RS-485 Modbus

The following would require framework changes:

8. **Additional message types** — `geometry_msgs/Vector3`, `std_msgs/String`
9. **Runtime channel config** — channel topic/period changeable from `config.json`
10. **ROS 2 services** — would require adding `rcl_service_t` management to `channel_manager`

---

## 11. Repository Structure

```
W6100_EVB_Pico_Zephyr_MicroROS/
│
├── app/                          ← Zephyr application
│   ├── CMakeLists.txt
│   ├── prj.conf                  ← Kconfig: networking, shell, micro-ROS, WDT
│   ├── boards/
│   │   └── w5500_evb_pico.overlay  ← USB CDC ACM + W6100 compat + LittleFS partition
│   └── src/
│       ├── main.c                ← Boot, WDT, reconnection state machine
│       ├── bridge/
│       │   ├── channel.h         ← channel_t / channel_value_t / msg_type_t
│       │   └── channel_manager.c ← Entity lifecycle, publish timers, sub dispatch
│       ├── config/
│       │   ├── config.h          ← bridge_config_t struct, API
│       │   └── config.c          ← LittleFS mount, JSON read/write, config_set()
│       ├── shell/
│       │   └── shell_cmd.c       ← Zephyr shell: bridge config / bridge reboot
│       └── user/
│           ├── user_channels.c   ← ★ THE ONLY FILE USER EDITS
│           ├── test_channels.c   ← Built-in test channels (remove for production)
│           └── test_channels.h
│
├── workspace/                    ← Zephyr west workspace (outside Dropbox)
│   └── build/zephyr/zephyr.uf2  ← flashable UF2 output
│
├── tools/
│   ├── stress_test.py            ← 17 auto + 20 manual tests
│   └── upload_config.py          ← Python config uploader via serial shell
│
├── Makefile                      ← make build / make docker-build / make monitor
├── README.md                     ← User-facing documentation
└── TECHNICAL_OVERVIEW.md         ← This document
```

---

## 12. Quick Reference: Adding a New Channel

```c
// 1. app/src/user/my_sensor.c

#include "my_sensor.h"
#include <zephyr/drivers/adc.h>

static int my_sensor_init(void) {
    // one-time hardware setup
    return adc_channel_setup_dt(&my_adc_ch);
}

static void my_sensor_read(channel_value_t *val) {
    // called every period_ms by channel_manager_publish()
    val->f32 = read_voltage_from_adc();
}

const channel_t my_sensor_channel = {
    .name      = "my_sensor",
    .topic_pub = "robot/my_sensor",   // publishes to ROS2
    .topic_sub = NULL,                // no subscribe
    .msg_type  = MSG_FLOAT32,
    .period_ms = 200,                 // 5 Hz
    .init      = my_sensor_init,
    .read      = my_sensor_read,
    .write     = NULL,
};

// 2. app/src/user/my_sensor.h
// extern const channel_t my_sensor_channel;

// 3. app/src/user/user_channels.c
// channel_register(&my_sensor_channel);

// 4. app/CMakeLists.txt
// src/user/my_sensor.c   ← add this line

// 5. Verify:
// ros2 topic echo /robot/my_sensor
```

**Total changes to framework code: zero.**

---

*Document generated from codebase v1.5 — 2026-03-05*
