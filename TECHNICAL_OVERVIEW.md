# W6100 EVB Pico — Zephyr + micro-ROS Bridge v2.0
## Technical Overview for Senior Developers and Robot Integrators

**Target audience:** Senior embedded developer familiar with Zephyr RTOS, and/or ROS 2 integrator who needs to understand the full system.

---

## 1. Hardware Platform

| Component | Details |
|-----------|---------|
| MCU | RP2040 (dual-core Cortex-M0+, 133 MHz) |
| Flash | 16 MB QSPI (W25Q128JV) |
| RAM | 264 KB SRAM |
| Ethernet | WIZnet W6100 (hardwired TCP/IP, SPI) |
| Board | WIZnet W6100 EVB Pico (Zephyr board ID: `w5500_evb_pico`) |
| USB | USB CDC ACM — serial console + Zephyr Shell |
| GPIO | GP14 (relay-brake output), GP15 (E-Stop input, NC + pull-up), GP25 (status LED) |
| ADC | GP26 = ADC channel 0 — battery voltage (placeholder, 12-bit, internal reference) |
| Watchdog | RP2040 hardware WDT, 30 s timeout |

> **Board name mismatch:** WIZnet ships the board as "W6100 EVB Pico" but the Zephyr board definition is `w5500_evb_pico` (inherited from the W5500 variant). The overlay file `app/boards/w5500_evb_pico.overlay` patches the `compatible` property to `"wiznet,w6100"` at build time.

---

## 2. Software Stack

```
┌─────────────────────────────────────────────────────────────────┐
│  ROS 2 (Jazzy)  —  micro-ROS agent  —  host PC / edge computer │
│                         UDP / Ethernet                          │
└────────────────────────────┬────────────────────────────────────┘
                             │  W6100 SPI Ethernet
┌────────────────────────────▼────────────────────────────────────┐
│                     Zephyr RTOS 4.3.99                          │
│  ┌─────────────┐  ┌──────────────┐  ┌──────────────────────┐   │
│  │  micro-ROS  │  │  LittleFS    │  │  Zephyr Shell        │   │
│  │  (Jazzy)    │  │  /lfs/...    │  │  (USB CDC ACM)       │   │
│  └──────┬──────┘  └──────┬───────┘  └──────────────────────┘   │
│         │                │                                       │
│  ┌──────▼──────────────────────────────────────────────────┐    │
│  │                   Bridge Core (app/src/)                 │    │
│  │  channel_manager | param_server | service_manager        │    │
│  │  diagnostics     | config       | shell_cmd              │    │
│  └──────┬──────────────────────────────────────────────────┘    │
│         │                                                         │
│  ┌──────▼──────────────────────────────────────────────────┐    │
│  │              Hardware Drivers                            │    │
│  │  drv_gpio (estop, relay)  |  drv_adc (battery voltage)  │    │
│  └──────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

### Versions

| Layer | Version |
|-------|---------|
| Zephyr RTOS | 4.3.99 (main branch, pinned west manifest) |
| micro-ROS | Jazzy (ROS 2 Jazzy Jalisco) |
| rmw_microxrcedds | latest Jazzy |
| micro-ROS Zephyr module | `micro_ros_zephyr_module` |
| Zephyr SDK | 0.17.4 |
| CMake | 3.28.3 |

---

## 3. Repository Structure

```
W6100_EVB_Pico_Zephyr_MicroROS/
├── app/                          # Zephyr application
│   ├── CMakeLists.txt
│   ├── prj.conf                  # Kconfig (all tunables here)
│   ├── boards/
│   │   └── w5500_evb_pico.overlay  # DTS patches: W6100, ADC, GPIO, LittleFS
│   └── src/
│       ├── main.c                # Entry point, reconnect loop
│       ├── bridge/
│       │   ├── channel.h         # channel_t, channel_state_t, channel_value_t
│       │   ├── channel_manager.c # Core: registration, entities, publish, IRQ
│       │   ├── diagnostics.c     # /diagnostics topic publisher
│       │   ├── param_server.c    # rclc_parameter_server (period_ms, enabled)
│       │   └── service_manager.c # SetBool / Trigger services
│       ├── config/
│       │   └── config.c          # LittleFS JSON config (IP, agent, node name)
│       ├── drivers/
│       │   ├── drv_adc.c         # ADC channel driver (battery voltage)
│       │   └── drv_gpio.c        # GPIO driver (E-Stop, relay-brake)
│       ├── shell/
│       │   └── shell_cmd.c       # `bridge` shell subcommands
│       └── user/
│           ├── user_channels.c   # INTEGRATION POINT: register your channels here
│           └── test_channels.c   # Built-in test channels (counter, heartbeat, echo)
├── workspace/                    # West workspace
│   ├── zephyr/                   # Zephyr RTOS source
│   ├── modules/lib/
│   │   └── micro_ros_zephyr_module/
│   │       └── modules/libmicroros/
│   │           ├── libmicroros.mk    # colcon build orchestration
│   │           ├── libmicroros.a     # Pre-built static library (31 MB, 1336 objects)
│   │           ├── colcon.meta       # Package selection + rmw limits
│   │           ├── Kconfig           # string-typed MICROROS_* symbols
│   │           └── include/          # All micro-ROS headers
│   └── build/zephyr/
│       └── zephyr.uf2            # Flash image for BOOTSEL mode
└── tools/
    ├── v2_stress_test.py         # Automated stress test suite (74 + 12 tests)
    └── stress_test.py            # Legacy v1.5 shell-only tests
```

---

## 4. Channel System — Core Abstraction

The channel is the fundamental unit of the bridge. Each channel maps one physical device (GPIO pin, ADC, PWM output, I2C sensor, UART device) to one or two ROS 2 topics.

### 4.1 Data Structures

```c
// channel.h

typedef struct {
    const char  *name;        // Unique ID, shown in logs and parameters
    const char  *topic_pub;   // Publish topic (NULL = no publish)
    const char  *topic_sub;   // Subscribe topic (NULL = no subscribe)
    msg_type_t   msg_type;    // MSG_BOOL | MSG_INT32 | MSG_FLOAT32
    uint32_t     period_ms;   // Default publish period
    bool         irq_capable; // true = interrupt-driven publish

    int  (*init)(void);                     // Hardware init
    void (*read)(channel_value_t *out);     // Read sensor → ROS2
    void (*write)(const channel_value_t *in); // ROS2 → actuator
} channel_t;  // const — stored in flash

typedef struct {
    uint32_t  period_ms;    // Runtime override (by param server)
    bool      enabled;      // Runtime enable/disable
    bool      invert_logic; // Bool polarity inversion
    atomic_t  irq_pending;  // ISR → main loop flag
} channel_state_t;  // mutable — in RAM
```

The descriptor (`channel_t`) is intentionally `const`-qualified and lives in flash. All runtime-mutable fields are in `channel_state_t`, which is indexed in parallel. This separation allows the parameter server to safely modify state without touching the descriptor.

### 4.2 Registration Flow

```
main() → user_register_channels()
           └─ channel_register(&my_channel)   // adds to channels[] array
         → channel_manager_init_channels()    // calls ch->init() on all
         → [network up, agent found]
         → ros_session_init()
              ├─ channel_manager_create_entities()  // creates pubs + subs
              ├─ channel_manager_add_subs_to_executor()
              └─ param_server_load_from_config()    // restores periods/enabled
```

### 4.3 Adding a New Channel

Edit `app/src/user/user_channels.c`:

```c
#include "my_sensor.h"   // your channel descriptor

void user_register_channels(void)
{
    channel_register(&my_sensor_channel);
    channel_register(&my_actuator_channel);
    // test channels can be removed when hardware is ready
    channel_register(&test_counter_channel);
}
```

Implement `my_sensor.h` defining a `channel_t`:

```c
static void my_sensor_init(void) { /* gpio/i2c/spi setup */ }
static void my_sensor_read(channel_value_t *out) { out->f32 = /* reading */; }

const channel_t my_sensor_channel = {
    .name        = "battery_voltage",
    .topic_pub   = "/agv/battery_voltage",
    .topic_sub   = NULL,
    .msg_type    = MSG_FLOAT32,
    .period_ms   = 1000,
    .irq_capable = false,
    .init        = my_sensor_init,
    .read        = my_sensor_read,
    .write       = NULL,
};
```

**Maximum channels:** `CHANNEL_MAX = 16`.

### 4.4 Publish Paths

Two separate paths converge at `perform_channel_publish()`:

```
Periodic path:
  main loop (1ms)
  └─ channel_manager_publish()
       └─ for each enabled channel: if (now - last_pub) >= period_ms
            └─ perform_channel_publish(i)

IRQ path (low-latency):
  GPIO ISR
  └─ channel_manager_signal_irq(idx)  // atomic_set_bit — ISR safe

  main loop (1ms) — FIRST check, before executor spin
  └─ channel_manager_handle_irq_pending()
       └─ for each irq_capable channel: if atomic_test_and_clear_bit(...)
            └─ perform_channel_publish(i)
```

The IRQ path is designed for E-Stop signals where sub-millisecond detection is critical. The ISR only sets an atomic flag; the actual `rcl_publish()` call (which is not ISR-safe) happens in the main thread context at the top of the next 1ms iteration.

---

## 5. ROS 2 Interface

### 5.1 Node

| Parameter | Value / Source |
|-----------|---------------|
| Node name | `pico_bridge` (configurable via `bridge config set`) |
| Namespace | `/` (configurable) |
| RMW | `rmw_microxrcedds` (Micro XRCE-DDS) |
| Transport | UDP — W6100 → micro-ROS agent |
| Agent discovery | `rmw_uros_ping_agent(200ms, 1 attempt)`, retry every 2s |

### 5.2 Topics

#### Test Channels (built-in, no hardware required)

| Topic | Type | Direction | Period | Description |
|-------|------|-----------|--------|-------------|
| `pico/counter` | `std_msgs/Int32` | PUB | 500ms | Monotonic counter |
| `pico/heartbeat` | `std_msgs/Bool` | PUB | 1000ms | Alternating true/false |
| `pico/echo_in` | `std_msgs/Int32` | SUB | — | Echo input |
| `pico/echo_out` | `std_msgs/Int32` | PUB | 1000ms | Last received echo value |

#### Hardware Channels (register in `user_channels.c`)

Topics are defined by the channel descriptor. Naming convention (recommended):
- `/agv/<sensor_name>` — sensor readings
- `/agv/cmd/<actuator_name>` — actuator commands
- `/agv/status/<component>` — status outputs

#### Diagnostics

| Topic | Type | Period | Description |
|-------|------|--------|-------------|
| `/diagnostics` | `diagnostic_msgs/DiagnosticArray` | 5s | System health |

Diagnostics KV fields:
```
uptime_s    — seconds since boot (int string)
channels    — number of registered channels
reconnects  — number of agent reconnections
firmware    — "v2.0-W6100"
```

### 5.3 Services

| Service | Type | Handler | Description |
|---------|------|---------|-------------|
| `/bridge/relay_brake` | `std_srvs/SetBool` | `drv_gpio` | Engage/release brake relay (GP14) |
| `/bridge/estop` | `std_srvs/Trigger` | `drv_gpio` | Read current E-Stop state |

**Calling from ROS 2 CLI:**
```bash
# Engage brake
ros2 service call /bridge/relay_brake std_srvs/srv/SetBool "{data: true}"

# Query E-Stop state
ros2 service call /bridge/estop std_srvs/srv/Trigger "{}"
```

`SetBool` returns `{success, message}`. `Trigger` returns `{success: <estop_active>, message: "E-Stop active|inactive"}`.

All service request/response message memory is statically allocated (no heap). Response string buffers are 64 bytes each, bound to the `rosidl_runtime_c__String` struct at callback time.

### 5.4 Parameter Server

Implemented with `rclc_parameter_server` in `low_mem_mode = true` (mandatory for RP2040).

Parameters auto-created for each registered channel:

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `ch.<name>.period_ms` | INT | from descriptor | Publish period (ms) |
| `ch.<name>.enabled` | BOOL | true | Channel on/off switch |

**Important:** String-type parameters are NOT available in `low_mem_mode`. Only BOOL and INT (mapped from DOUBLE in some rmw versions — treated as integer).

**Persistence:** Parameter values are saved to `/lfs/ch_params.json` via a callback triggered on every parameter change. On the next boot (or reconnect), `param_server_load_from_config()` restores all values before the main loop starts.

**From ROS 2 CLI:**
```bash
# List all bridge parameters
ros2 param list /pico_bridge

# Set counter publish period to 200ms
ros2 param set /pico_bridge ch.test_counter.period_ms 200

# Disable heartbeat
ros2 param set /pico_bridge ch.test_heartbeat.enabled false

# Dump all
ros2 param dump /pico_bridge
```

### 5.5 Entity Limits (rmw_microxrcedds)

Configured in `prj.conf` as `string` type (required by Kconfig definition):

```ini
CONFIG_MICROROS_PUBLISHERS="20"   # 16 channels + diagnostics + param_event + spare
CONFIG_MICROROS_SUBSCRIBERS="16"  # 16 channels worst case
CONFIG_MICROROS_SERVERS="8"       # 6 param server + 2 user services
CONFIG_MICROROS_CLIENTS="0"
CONFIG_MICROROS_NODES="1"
```

These values propagate via `libmicroros.mk` → `colcon.meta` → `rmw_microxrcedds/configuration/rmw_microros_user_config.h` → static allocation tables inside `libmicroros.a`. **Changing them requires a full library rebuild** (see Section 9).

---

## 6. Safety Architecture

### 6.1 E-Stop (GP15)

```
Physical:   NC limit switch wired to GP15 + GND
GPIO config: GPIO_ACTIVE_LOW | GPIO_PULL_UP
Logic:      switch OPEN (tripped) → GPIO reads LOW → active_low fires
```

E-Stop flow:
1. GPIO ISR fires on both edges (`GPIO_INT_EDGE_BOTH`)
2. ISR calls `channel_manager_signal_irq(estop_channel_idx)` — sets atomic flag, returns immediately
3. Main loop calls `channel_manager_handle_irq_pending()` first — detects flag, calls `rcl_publish()`
4. E-Stop state published to `/agv/estop` with `MSG_BOOL` type
5. ROS 2 Nav2 / safety node subscribes and issues `cmd_vel = {0}` stop

**Target latency:** GPIO edge → ROS 2 message < 100ms at P99 under full load.

Latency breakdown:
- ISR to flag: < 1 µs
- Flag to `rcl_publish()`: 0–1ms (main loop period)
- DDS framing + UDP send: < 5ms (Micro XRCE-DDS)
- Agent to subscriber: network RTT < 1ms (LAN)
- **Total P50: ~5ms, P99 target: <100ms**

### 6.2 Relay Brake (GP14)

```
GPIO config: GPIO_ACTIVE_HIGH
Logic:       true = relay engaged (brake ON), false = brake released
```

Controlled via `std_srvs/SetBool` service at `/bridge/relay_brake`. The GPIO write is synchronous in the service callback — relay switches within the same executor spin cycle as the service request arrives.

### 6.3 Hardware Watchdog

```c
#define WDT_TIMEOUT_MS 30000  // 30 seconds
// WDT_FLAG_RESET_SOC — hard reset on timeout
// WDT_OPT_PAUSE_HALTED_BY_DBG — pauses during JTAG halt
```

`watchdog_feed()` is called:
- Inside every `bridge_run()` iteration (main loop)
- Inside the agent search retry loop
- During boot sequence (USB wait, network up)

If the main thread hangs (e.g., deadlock, infinite wait in a broken driver), the WDT fires a hard reset after 30s.

---

## 7. Connection Lifecycle

```
boot
 │
 ├─ Hardware init (WDT, USB, GPIO, ADC)
 ├─ config_init() — mount LittleFS, load /lfs/config.json
 ├─ user_register_channels() + channel_manager_init_channels()
 ├─ apply_network_config() — wait link UP, DHCP or static IP
 └─ rmw_uros_set_custom_transport() — UDP via W6100
      │
      ▼
  ┌──────────────────────────────────┐
  │  SEARCHING (LED off)             │
  │  ping_agent(200ms) every 2s     │◄─────────┐
  └──────────────┬───────────────────┘          │
                 │ agent responds               │
                 ▼                              │
  ┌──────────────────────────────────┐          │
  │  SESSION INIT                    │          │
  │  support → node → channel_ents  │          │
  │  → diagnostics → executor       │          │
  │  → add_subs → param_server      │          │
  │  → service_manager              │          │
  │  → param_server_load_from_config│          │
  └──────────────┬───────────────────┘          │
                 │ init OK (LED on)             │
                 ▼                              │
  ┌──────────────────────────────────┐          │
  │  RUNNING (LED on)                │          │
  │  Every 1ms:                      │          │
  │    handle_irq_pending()          │          │
  │    executor_spin_some(1ms)       │          │
  │    channel_manager_publish()     │          │
  │    watchdog_feed()               │          │
  │  Every 1s: ping_agent()          │          │
  │  Every 5s: diagnostics_publish() │          │
  └──────────────┬───────────────────┘          │
                 │ ping timeout / ping fails     │
                 ▼                              │
  ┌──────────────────────────────────┐          │
  │  SESSION CLEANUP (reverse order) │          │
  │  service_manager_fini()          │          │
  │  param_server_fini()             │          │
  │  diagnostics_fini()              │          │
  │  channel_manager_destroy_ents()  │          │
  │  executor_fini()                 │          │
  │  node_fini() → support_fini()    │          │
  └──────────────┬───────────────────┘          │
                 │ g_reconnect_count++           │
                 └───────────────────────────────┘
```

**Critical:** Entities must be destroyed in reverse initialization order. Destroying the node before its children (subscriptions, publishers, services) causes use-after-free in rmw_microxrcedds. The current `ros_session_fini()` order is correct and must be preserved when adding new entities.

---

## 8. Configuration System

### 8.1 Files on LittleFS

| File | Content | Format |
|------|---------|--------|
| `/lfs/config.json` | Network + ROS config | Hand-rolled JSON parser |
| `/lfs/ch_params.json` | Channel periods + enabled flags | Hand-rolled JSON |

### 8.2 config.json Fields

```json
{
  "dhcp": false,
  "ip": "192.168.68.114",
  "netmask": "255.255.255.0",
  "gateway": "192.168.68.1",
  "agent_ip": "192.168.68.125",
  "agent_port": "8888",
  "node_name": "pico_bridge",
  "namespace": "/"
}
```

### 8.3 Shell Commands (USB CDC ACM)

Connect with any serial terminal (115200 baud or CDC ACM rate):

```
bridge config show              — Print current config
bridge config set ip <addr>     — Set static IP
bridge config set agent_ip <addr>
bridge config set agent_port <port>
bridge config set node_name <name>
bridge config set dhcp <0|1>
bridge config save              — Write to /lfs/config.json
bridge config load              — Reload from flash
bridge config reset             — Restore factory defaults
bridge reboot                   — sys_reboot(COLD)
```

### 8.4 Flash Partition Layout

```
Flash (16 MB):
  0x00000000 — 0x00EFFFFF  Application (15 MB, ~289 KB used)
  0x00F00000 — 0x00FFFFFF  LittleFS storage (1 MB)
```

---

## 9. Build System

### 9.1 Docker-Based Build

All builds run inside a Docker container to ensure reproducibility:

```bash
# Full build (auto-pristine)
make build

# Force full CMake regeneration (needed after include/ changes)
docker run --rm \
  -v $(pwd)/workspace:/workdir \
  -v $(pwd)/app:/workdir/app \
  w6100-zephyr-microros:latest bash -c \
  "cd /workdir && west build -b w5500_evb_pico app --pristine=always"

# Flash (copy UF2 to Pico in BOOTSEL mode)
make flash
```

### 9.2 libmicroros Build Pipeline

`libmicroros.mk` orchestrates a two-stage colcon build:

```
Stage 1 (micro_ros_dev): ament build tools
  └─ git clone micro_ros_setup
  └─ colcon build → install/bin/ros2run, ament_cmake, etc.

Stage 2 (micro_ros_src): ROS 2 packages for Jazzy
  └─ ros2 run micro_ros_setup configure_firmware.py
  └─ ros2 run micro_ros_setup build_firmware.py
     └─ colcon build --packages-select <whitelist from colcon.meta>
        Building ~103 packages → install/lib/*.a

Archive step (ARM cross-toolchain):
  for each .a in install/lib/:
      ar x → extract *.obj
  ar rc libmicroros.a *.o*   → 31 MB, 1336 objects
  cp libmicroros.a $(COMPONENT_PATH)/libmicroros.a

Include step:
  cp -R install/include $(COMPONENT_PATH)/include
```

**Known bug in `cp -R include` step:** If `$(COMPONENT_PATH)/include/` already exists, Linux `cp -R src dst` creates `dst/src_basename/` instead of merging — resulting in `include/include/` nesting. This causes CMake to miss include paths on the next build.

**Workaround (manual, run inside Docker):**
```bash
rm -rf libmicroros/include
cp -R micro_ros_src/install/include libmicroros/include
```

**Permanent fix (not yet applied):** Add to `libmicroros.mk` before the `cp -R` line:
```makefile
rm -rf $(COMPONENT_PATH)/include
```

### 9.3 std_srvs Enablement (v2.0 change)

In v1.5, `libmicroros.mk` contained:
```makefile
touch src/common_interfaces/std_srvs/COLCON_IGNORE;
```

This line was **removed** in v2.0 commit `90935b9`. The library rebuild (task `bzkc3fx3v`) now includes `std_srvs` in the colcon build, making `SetBool` and `Trigger` service types available.

### 9.4 Kconfig Type System Pitfall

All `MICROROS_*` symbols in `modules/libmicroros/Kconfig` are `string` type:

```kconfig
config MICROROS_PUBLISHERS
    string "..."
    default "1"
```

Values in `prj.conf` **must** use double quotes:
```ini
CONFIG_MICROROS_PUBLISHERS="20"   # CORRECT
CONFIG_MICROROS_PUBLISHERS=20     # WRONG — "malformed string literal", assignment ignored
```

Assignment ignored silently means the library uses its default (`"1"` publisher), causing `RCL_RET_ERROR` when the second publisher tries to initialize — a very hard bug to diagnose.

### 9.5 Symbols NOT User-Configurable

```ini
# DO NOT put these in prj.conf — causes build abort:
# CONFIG_ATOMIC_OPERATIONS_BUILTIN=y  ← auto-set by Zephyr for RP2040
```

---

## 10. Memory Budget

### 10.1 Static (build time)

| Region | Used | Total | Headroom |
|--------|------|-------|---------|
| Flash | ~289 KB | 15 MB | 14.7 MB |
| RAM | 262,312 B | 270,336 B (264 KB) | ~7.8 KB static margin |

### 10.2 RAM Budget Breakdown

```
Zephyr kernel + stacks:    ~80 KB
  MAIN_STACK_SIZE:          16 KB
  SHELL_STACK_SIZE:          8 KB
  LOG_PROCESS_THREAD_STACK:  2 KB
  Networking (pkts+bufs):   ~24 KB

micro-ROS static tables:   ~30 KB
  rmw entity pools (20 pub, 16 sub, 8 srv): ~15 KB
  rclc executor:             ~4 KB
  parameter server:          ~8 KB

Bridge static data:        ~10 KB
  channel arrays (16×2 structs):  ~2 KB
  message buffers (pub+sub):      ~4 KB
  service req/res buffers:        ~2 KB
  config struct:                  ~1 KB

CONFIG_HEAP_MEM_POOL_SIZE: 96 KB   (reduced from 128 KB in v1.5)
```

**Why heap was reduced:** v2.0 adds `std_srvs`, `rclc_parameter_server`, and `diagnostic_msgs` static data, plus larger rmw entity tables. The original 128 KB heap caused a ~25 KB RAM overflow. Reducing to 96 KB leaves the static region viable with ~7.8 KB margin.

**Runtime heap usage:** micro-ROS uses heap for DDS session setup and internal message queues during initialization. The session init phase peaks at ~40–60 KB heap usage, then stabilizes. This has not been measured precisely on v2.0 — runtime heap monitoring is recommended for production.

---

## 11. Flashing

### Method 1: BOOTSEL (drag-and-drop)

1. Hold BOOTSEL button on the W6100 EVB Pico
2. Connect USB
3. A `RPI-RP2` drive appears
4. Copy `workspace/build/zephyr/zephyr.uf2` to the drive
5. Board resets and boots Zephyr

### Method 2: make flash

```bash
make flash   # Wraps picotool or UF2 copy depending on Makefile config
```

### Post-flash Boot Sequence

```
[USB console, 115200 baud or CDC ACM]

<board resets>
[Waiting up to 3s for USB DTR...]
[USB console connected]  OR  [No USB console — autonomous mode]
W6100 EVB Pico - micro-ROS Bridge
Node: /pico_bridge
Agent: 192.168.68.125:8888
Watchdog active (30000 ms timeout)
Waiting for Ethernet link (max 15s)...
Ethernet link UP
Network: DHCP starting...   OR   Network: static IP 192.168.68.114
Searching for agent: 192.168.68.125:8888 ...
<LED off during search>
Agent found — initializing session
<LED on when connected>
micro-ROS session active. 3 channels, 1 subscribers.
```

---

## 12. micro-ROS Agent Setup (Host)

```bash
# Install micro-ROS agent (ROS 2 Jazzy)
pip install micro-ros-agent   # or build from source

# Start agent (UDP, port 8888)
ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888

# Verify node appears
ros2 node list
# Expected: /pico_bridge

ros2 topic list
# Expected:
#   /pico/counter
#   /pico/heartbeat
#   /pico/echo_in
#   /pico/echo_out
#   /diagnostics
#   /rosout
#   /parameter_events
```

---

## 13. Stress Test Suite

`tools/v2_stress_test.py` covers 74 automated and 12 manual tests.

```bash
# Prerequisites
pip install pyserial

# Full test (shell + ROS2)
python3 tools/v2_stress_test.py \
  --port /dev/tty.usbmodem* \
  --agent-ip 192.168.68.125

# Shell tests only (no ROS2 required)
python3 tools/v2_stress_test.py --port /dev/tty.usbmodem* --shell-only

# Skip manual tests (CI-friendly)
python3 tools/v2_stress_test.py \
  --port /dev/tty.usbmodem* \
  --agent-ip 192.168.68.125 \
  --skip-manual
```

### Test Sections

| Section | IDs | Focus |
|---------|-----|-------|
| Shell / Config | T01–T17 | All `bridge config` subcommands, save/load/reset cycle |
| ROS2 Topics | T20–T27 | Topic existence, publish rate, echo sub→pub |
| Services / E-Stop | T30–T38 | SetBool relay, Trigger estop, concurrent calls, response content |
| Parameter Server | T40–T44 | Set/get period, enable/disable, persistence across reconnect |
| Diagnostics | T50–T53 | `/diagnostics` rate, KV field presence, value sanity |
| Reconnect Stress | T60–T65 | 5× rapid agent kill/restart, graceful recovery |
| Safety / Latency | T70–T74 | E-Stop P99 < 100ms, topic flood under service load |
| Manual | M01–M12 | Physical GPIO, brake relay click, Pico reboot behavior |

### Key Thresholds

| Metric | Threshold |
|--------|-----------|
| E-Stop latency P99 | < 100 ms |
| Service response | < 500 ms |
| Topic publish rate | > 0.8× configured rate |
| Reconnect time | < 30 s |
| Diagnostics period | < 10 s |

Results saved to `tools/v2_stress_report.json`.

---

## 14. Known Limitations and Future Work

| Item | Status |
|------|--------|
| `cp -R include` nesting bug in libmicroros.mk | Documented, manual workaround; needs one-line fix |
| Runtime heap monitoring | Not implemented; 97% static RAM usage warrants runtime tracking |
| PWM, I2C, UART drivers | Not yet implemented — add as new channels in `user_channels.c` |
| sensor_msgs/Imu channel | Headers present in libmicroros; channel not yet written |
| rclc_parameter string type | Not available in `low_mem_mode`; BOOL+INT only |
| Channel invert_logic for non-bool types | No-op for INT32/FLOAT32; only BOOL is inverted |
| IPv6 | W6100 supports it; not enabled in prj.conf |

---

## 15. Quick Reference

```bash
# Build
make build

# Flash (BOOTSEL)
cp workspace/build/zephyr/zephyr.uf2 /Volumes/RPI-RP2/

# Start agent
ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888

# Monitor topics
ros2 topic echo /pico/counter std_msgs/msg/Int32
ros2 topic hz /pico/counter

# Set parameter
ros2 param set /pico_bridge ch.test_counter.period_ms 200

# Call service
ros2 service call /bridge/relay_brake std_srvs/srv/SetBool "{data: true}"

# Diagnostics
ros2 topic echo /diagnostics diagnostic_msgs/msg/DiagnosticArray

# Shell
screen /dev/tty.usbmodem* 115200
bridge config show
bridge config set agent_ip 192.168.1.100
bridge config save
bridge reboot

# Stress test
python3 tools/v2_stress_test.py --port /dev/tty.usbmodem* --agent-ip 192.168.68.125
```
