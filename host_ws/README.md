# Host Workspace — RoboClaw TCP Adapter

> A Tier 2 (Linux Host) ROS2 workspace that connects a Basicmicro RoboClaw motor
> controller to the robot's ROS2 graph through a direct TCP socket, with no
> physical serial ports, no `socat`, and no kernel device dependencies.

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Architectural Decision Log](#2-architectural-decision-log)
3. [System Architecture](#3-system-architecture)
4. [Network Topology](#4-network-topology)
5. [Component Deep Dive](#5-component-deep-dive)
   - [RoboClawTCPPort](#51-roboclawtcpport)
   - [RoboClawTCP](#52-roboclawtcp)
   - [Monkey-Patch Entry Point](#53-monkey-patch-entry-point)
   - [Safety Bridge Node](#54-safety-bridge-node)
6. [Installation](#6-installation)
7. [Configuration](#7-configuration)
8. [Running the System](#8-running-the-system)
9. [Troubleshooting](#9-troubleshooting)
10. [Extraction to Standalone Repository](#10-extraction-to-standalone-repository)
11. [Development Roadmap](#11-development-roadmap)

---

## 1. Introduction

This workspace is part of the `W6100_EVB_Pico_Zephyr_MicroROS` monorepo and
lives in the `host_ws/` directory.  It provides the ROS2 driver layer for a
**Basicmicro RoboClaw** motor controller that is physically connected via
**Packet Serial** (115200 baud) to a **USR-K6 Ethernet-to-Serial converter**,
making it accessible over TCP on the robot's local network.

The workspace contains:

| Component | Purpose |
|---|---|
| `basicmicro_python` (submodule) | Upstream Python library — Packet Serial protocol, CRC, 150+ commands |
| `basicmicro_ros2` (submodule) | Upstream ROS2 driver — `ros2_control`, `/cmd_vel`, odometry, `/emergency_stop` |
| `roboclaw_tcp_adapter` (ROS2 package) | TCP socket transport, safety bridge, monkey-patch entry point |

The key innovation is that the host PC running the ROS2 stack has **zero
hardware dependencies**.  Every peripheral — the W6100 EVB Pico bridges
(micro-ROS over UDP) and the RoboClaw (Packet Serial over TCP via USR-K6) —
communicates exclusively over Ethernet.  The host can be a desktop, a laptop
on Wi-Fi, or a Docker container in the cloud.

---

## 2. Architectural Decision Log

This section documents the decisions that led to the current architecture,
including the alternatives we evaluated and rejected.

### 2.1 Why not integrate RoboClaw into the Pico firmware?

The W6100 EVB Pico bridges run Zephyr RTOS with micro-ROS and are already
at **~97.5% RAM utilisation**.  Adding a TCP client stack plus the Packet
Serial protocol engine would exceed the RP2040's 264 KB SRAM.  The RoboClaw
also requires a fundamentally different communication pattern (request/response
with CRC and retries) that doesn't map well to the channel abstraction model
used by the existing Pico firmware.

**Decision**: RoboClaw driver runs on the Linux host (Tier 2), not on an MCU.

### 2.2 Why not `socket://` URL with pyserial?

The `basicmicro_python` library instantiates `serial.Serial(port, baud)`.
We tested passing `socket://192.168.68.60:8234` as the `port` parameter.
Result: the library treats it as a filesystem path and looks for a file
named `socket://192.168.68.60:8234`.  `pyserial`'s URL handler requires
the `serial.serial_for_url()` factory function, which `basicmicro_python`
does not use.

**Decision**: Custom TCP adapter class that duck-types `serial.Serial`.

### 2.3 Why not `socat` for virtual serial ports?

`socat` can create a pseudo-TTY (`/dev/pts/X`) that tunnels data to/from a
TCP socket.  This works, but introduces critical deployment problems:

1. **Docker mount fragility**: If `socat` restarts, the kernel assigns a new
   PTY inode.  The Docker container's bind-mount (`--device=/dev/pts/X`)
   becomes stale — the device "disappears" from the container's perspective,
   and the entire container must be restarted.

2. **Extra process to manage**: `socat` must be started before the driver,
   health-checked, and restarted on failure — adding operational complexity.

3. **No reconnection**: A broken TCP connection means the PTY dies.  The
   serial driver sees a permanent EOF and cannot recover without a full
   process restart.

With a direct TCP socket in Python, a broken connection is just a
`socket.timeout` or `ConnectionResetError` — caught, logged, and recovered
with a reconnect loop.  No container restart needed.

**Decision**: Direct TCP socket via `RoboClawTCPPort`, no `socat`.

### 2.4 Why `TCP_NODELAY`?

The `basicmicro_python` library does **not** assemble a complete Packet Serial
frame into a single buffer before calling `write()`.  Instead, it makes
5-6 sequential `write()` calls per command (address, command byte, data bytes,
CRC bytes).  Without `TCP_NODELAY`, the Nagle algorithm would buffer these
tiny writes and combine them — potentially adding up to 200 ms of delay.

With `TCP_NODELAY=1`, each `write()` call produces an immediate TCP segment.
On a local Ethernet LAN (RTT < 0.5 ms), these segments arrive at the USR-K6
within microseconds of each other.  The K6's internal UART buffer streams
them out at 115200 baud (~86 us/byte), so from the RoboClaw's perspective
the bytes arrive in a continuous stream, well within the 10 ms inter-byte
timeout.

**Decision**: `TCP_NODELAY=1` on the socket.  Tested and validated.

### 2.5 Why a monorepo (for now)?

The RoboClaw driver, the Pico firmware, and the startup scripts are
architecturally coupled through:

- The E-Stop safety chain (Pico publishes → safety bridge relays → driver stops)
- Shared network configuration (`robot_network.yaml`)
- Coordinated startup (tmux session runs agent + driver + shell)

Splitting into a separate repository at this stage would create cross-repo
dependencies that are harder to maintain than a `host_ws/` directory.  The
`roboclaw_tcp_adapter` package is designed to be **extractable with a single
`git filter-branch` command** when the coupling loosens (e.g., when fleet-wide
deployment makes a standalone package more practical).

**Decision**: Monorepo now, with a clean extraction path documented.

---

## 3. System Architecture

```
┌──────────────────────────────────────────────────────────────────────┐
│                      TIER 2 — LINUX HOST (ROS2)                      │
│                                                                      │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐   │
│  │ micro-ROS Agent  │  │ roboclaw_tcp_node│  │ safety_bridge    │   │
│  │ (Docker, UDP)    │  │ (TCP adapter)    │  │ (E-Stop relay)   │   │
│  └────────┬─────────┘  └────────┬─────────┘  └────────┬─────────┘   │
│           │                     │                      │             │
│           │  ┌──────────────────┼──────────────────────┘             │
│           │  │                  │                                    │
│           │  │   ROS2 Graph (CycloneDDS, /robot namespace)          │
│           │  │   ┌──────────────────────────────────────┐           │
│           │  │   │ /robot/cmd_vel    /robot/odom         │           │
│           │  │   │ /robot/estop      /emergency_stop     │           │
│           │  │   │ /robot/diagnostics                    │           │
│           │  │   └──────────────────────────────────────┘           │
│           │  │                                                      │
└───────────┼──┼──────────────────────────────────────────────────────┘
            │  │
  UDP :8888 │  │ TCP → 192.168.68.60:8234
            │  │
┌───────────┼──┼──────────────────────────────────────────────────────┐
│           │  │           HARDWARE / TIER 1                           │
│           │  │                                                      │
│  ┌────────┴──┴───────┐                                              │
│  │  LAN 192.168.68.x │                                              │
│  └─────┬─────┬───────┘                                              │
│        │     │                                                      │
│  ┌─────┴───┐ │  ┌─────────────┐     ┌──────────────────┐           │
│  │ Pico    │ │  │ USR-K6      │ UART│ RoboClaw         │           │
│  │ Bridges │ │  │ Eth↔Serial  ├─────┤ Motor Controller │           │
│  │ (×3)    │ │  │ .68.60:8234 │     │ Addr 0x80        │           │
│  └─────────┘ │  └─────────────┘     └──────────────────┘           │
│              │                                                      │
└──────────────┴──────────────────────────────────────────────────────┘
```

### Data Flow

1. **Sensor path** (Tier 1 → Tier 2):
   Pico bridges publish micro-ROS topics over UDP → Agent → ROS2 Graph

2. **Motor command path** (Tier 2 → Hardware):
   `/robot/cmd_vel` → `roboclaw_tcp_node` → TCP socket → USR-K6 → UART → RoboClaw

3. **E-Stop path** (physical button → motor halt):
   Button → Pico (E-Stop board) → micro-ROS UDP → `/robot/estop` (Bool) →
   `safety_bridge_node` → `/emergency_stop` (Empty) → `roboclaw_tcp_node` →
   motor halt command via TCP → USR-K6 → UART → RoboClaw stops

4. **Watchdog fallback**:
   If `/robot/estop` goes silent for >2 seconds (Pico crash, network failure,
   agent death), `safety_bridge_node` autonomously triggers `/emergency_stop`.
   Additionally, the RoboClaw's hardware serial timeout halts motors if it
   receives no valid commands for a configurable period.

---

## 4. Network Topology

All parameters are defined in `host_ws/config/robot_network.yaml`.

| Device | IP | Port | Protocol | Role |
|---|---|---|---|---|
| Linux Host | 192.168.68.125 | — | — | ROS2 host, Docker, Agent |
| micro-ROS Agent | 192.168.68.125 | 8888 | UDP | Bridges micro-ROS ↔ ROS2 |
| W6100 Pico (E-Stop) | 192.168.68.203 | — | UDP | Publishes `/robot/estop` |
| W6100 Pico (RC) | 192.168.68.202 | — | UDP | RC PWM input channels |
| W6100 Pico (Pedal) | 192.168.68.201 | — | UDP | Pedal/throttle channels |
| USR-K6 | 192.168.68.60 | 8234 | TCP | Ethernet ↔ UART bridge |
| RoboClaw | — | — | Packet Serial | Motor controller, addr 0x80 |

In the test environment, all devices share a flat `192.168.68.0/24` network.
In production, Tier 1 devices will be on the `10.0.10.0/24` VLAN as defined
in `robot_architecture.md`, with the host bridging both networks.

---

## 5. Component Deep Dive

### 5.1 RoboClawTCPPort

**File**: `roboclaw_tcp_adapter/tcp_port.py`

A minimal class (~100 lines) that implements only the methods `basicmicro_python`
actually calls on its internal `_port` (a `serial.Serial` instance):

| Method | Behaviour |
|---|---|
| `write(data)` | `socket.sendall(data)` — kernel-level atomic send |
| `read(n)` | Loop of `socket.recv()` with timeout — returns short on timeout |
| `flushInput()` | Non-blocking drain via `setblocking(False)` + recv loop |
| `reset_input_buffer()` | Alias for `flushInput()` |
| `reset_output_buffer()` | No-op (TCP send buffer is kernel-managed) |
| `close()` | `socket.close()` with safe error handling |
| `is_open` | Boolean property tracking connection state |

**Why not use `socat` + `/dev/pts`?**  See [Decision 2.3](#23-why-not-socat-for-virtual-serial-ports).

**Socket options**:
- `TCP_NODELAY=1` — disables Nagle's algorithm for immediate segment dispatch
- `settimeout(inter_byte_timeout)` — 50 ms default, sufficient for LAN jitter

**Reconnection**: `RoboClawTCPPort` does not implement reconnection itself.
The upstream `Basicmicro.reconnect()` method calls `close()` + `Open()`,
which creates a new `RoboClawTCPPort` instance, building a new TCP connection
from scratch — identical to the reconnect pattern used by the Pico firmware
for its micro-ROS agent connections.

### 5.2 RoboClawTCP

**File**: `roboclaw_tcp_adapter/basicmicro_tcp.py`

A subclass of `basicmicro.controller.Basicmicro` that overrides exactly one
method: `Open()`.  Instead of calling `serial.Serial(port, baud)`, it creates
a `RoboClawTCPPort(host, port)`.

All 150+ Packet Serial command methods (e.g., `ForwardM1()`,
`ReadEncM1()`, `SetM1PID()`), CRC calculation, retry logic, and error
handling are inherited **without any modification** from the upstream library.

This design has a critical property: when `basicmicro_python` releases a new
version with bug fixes or new commands, **we get those changes for free** by
updating the submodule — zero merge conflicts, zero adaptation.

### 5.3 Monkey-Patch Entry Point

**File**: `roboclaw_tcp_adapter/roboclaw_tcp_node.py`

The `basicmicro_ros2` driver internally does:

```python
from basicmicro.controller import Basicmicro
self._dev = Basicmicro(port, baud, ...)
```

We cannot modify this code (it's a git submodule).  Instead, our entry point:

1. Imports `basicmicro.controller` (loads the module into `sys.modules`)
2. Replaces `basicmicro.controller.Basicmicro` with `RoboClawTCP`
3. Then imports and calls the driver's `main()`

When the driver later imports `Basicmicro`, Python's module cache returns
the already-loaded module with the patched class.  The driver instantiates
`RoboClawTCP` believing it's the original `Basicmicro`.

This pattern is a standard Python technique for dependency injection without
source modification.  The upstream `basicmicro_ros2` submodule stays pristine.

### 5.4 Safety Bridge Node

**File**: `roboclaw_tcp_adapter/safety_bridge_node.py`

Closes the E-Stop safety loop.  The physical emergency stop button is wired
to a W6100 EVB Pico running the `estop` firmware, which publishes
`/robot/estop` (std_msgs/Bool) over micro-ROS.

The `basicmicro_ros2` driver subscribes to `/emergency_stop` (std_msgs/Empty).
These are two different topic types on two different topic names.  The safety
bridge node translates between them:

```
/robot/estop (Bool, True=stop)  →  safety_bridge_node  →  /emergency_stop (Empty)
```

**Two failure modes are handled**:

| Failure Mode | Detection | Response |
|---|---|---|
| Button pressed | `msg.data == True` on `/robot/estop` | Publish `/emergency_stop` |
| Pico/Agent/Network down | `/robot/estop` silent >2 sec | Publish `/emergency_stop` |

**Third safety layer** (not in software): The RoboClaw's built-in hardware
serial timeout halts all motors if it receives no valid Packet Serial command
within a configurable period.  This catches the scenario where both the safety
bridge and the driver process are dead simultaneously.

---

## 6. Installation

### Prerequisites

- ROS2 Jazzy (or compatible) installed or available in Docker
- Python 3.10+
- `colcon` build tool
- `tmux` for the startup script

### Steps

```bash
# 1. Clone or update with submodules
git submodule update --init --recursive

# 2. Install basicmicro_python in editable mode
make host-install-deps

# 3. Build the workspace
make host-build
```

`make host-install-deps` runs:
- `pip3 install -e host_ws/src/basicmicro_python` — editable install of
  the serial library
- `rosdep install --from-paths host_ws/src --ignore-src -r -y` — ROS2
  dependency resolution

`make host-build` runs:
- `colcon build --symlink-install` inside `host_ws/` — builds all three
  packages (`basicmicro_python`, `basicmicro_ros2`, `roboclaw_tcp_adapter`)

---

## 7. Configuration

### `host_ws/config/robot_network.yaml`

Single source of truth for all network parameters.  Used by the tmux startup
script, the ROS2 launch file, and can be referenced by any future automation.

```yaml
robot_network:
  micro_ros_agent:
    ip: 192.168.68.125    # Host IP where agent binds
    port: 8888            # UDP port for micro-ROS

  roboclaw:
    host: 192.168.68.60   # USR-K6 IP address
    port: 8234            # USR-K6 TCP port
    baud: 115200          # USR-K6 UART baudrate (must match RoboClaw)
    address: 128          # RoboClaw Packet Serial address (0x80)
    inter_byte_timeout: 0.05  # TCP socket timeout (seconds)

  pico_bridges:
    estop: { ip: 192.168.68.203, node: estop }
    rc:    { ip: 192.168.68.202, node: rc    }
    pedal: { ip: 192.168.68.201, node: pedal }

  ros:
    namespace: /robot
    estop_topic: estop
    emergency_stop_topic: /emergency_stop
```

### `roboclaw_tcp_adapter/config/roboclaw_params.yaml`

Driver-specific parameters loaded by the launch file.  Physical robot
dimensions (wheel radius, separation, encoder counts) should be adjusted
to match the actual hardware.

---

## 8. Running the System

### Full startup (recommended)

```bash
make robot-start
```

This runs `tools/start-robot.sh`, which creates a tmux session named `robot`
with three windows:

| Window | Name | Content |
|---|---|---|
| 0 | `agent` | micro-ROS Agent Docker container (UDP :8888) |
| 1 | `roboclaw` | `roboclaw.launch.py` — TCP driver + safety bridge |
| 2 | `ros2-shell` | Interactive ROS2 Jazzy shell for debugging |

**tmux navigation**:

```bash
tmux attach -t robot       # Attach to session
Ctrl-B, 0/1/2              # Switch windows
Ctrl-B, d                  # Detach (session keeps running)
tmux kill-session -t robot  # Stop everything
```

### Manual launch (for development)

```bash
# Terminal 1: Source workspace
source host_ws/install/setup.bash

# Terminal 2: Launch driver + safety bridge
ros2 launch roboclaw_tcp_adapter roboclaw.launch.py \
    roboclaw_host:=192.168.68.60 \
    roboclaw_port:=8234

# Terminal 3: Verify
ros2 topic list
ros2 topic echo /robot/odom
```

---

## 9. Troubleshooting

### TCP connection refused

```
RoboClawTCP connection failed: [Errno 111] Connection refused
```

1. Verify USR-K6 is powered and on the network: `ping 192.168.68.60`
2. Verify TCP port is open: `nc -zv 192.168.68.60 8234`
3. Check USR-K6 configuration: TCP Server mode, port 8234, 115200/8N1

### Reconnection loop

If the USR-K6 loses power or the Ethernet cable is disconnected, the driver
will log connection errors and retry.  The `basicmicro_ros2` driver's built-in
reconnection logic (inherited by `RoboClawTCP`) handles this automatically.

Monitor: `ros2 topic echo /robot/diagnostics`

### E-Stop topic silent warning

```
EMERGENCY STOP: E-Stop topic SILENT for >2.0s — assuming failure
```

This means `safety_bridge_node` hasn't received any message on `/robot/estop`
for 2 seconds.  Possible causes:

1. E-Stop Pico bridge is not running or not connected
2. micro-ROS Agent is not running
3. Network partition between Pico and host

Check: `ros2 topic echo /robot/estop` — you should see periodic `data: false`
when the E-Stop is not pressed.

### CRC errors / command timeouts

If commands sporadically fail with CRC errors:

1. Verify USR-K6 baudrate matches RoboClaw (115200)
2. Ensure only one TCP client is connected to the K6 at a time
3. Check `inter_byte_timeout` — increase to 0.1 if the network is congested

---

## 10. Extraction to Standalone Repository

The `roboclaw_tcp_adapter` package is designed for easy extraction:

```bash
# Extract host_ws/ into a new repo preserving git history
git filter-branch --subdirectory-filter host_ws -- --all
```

After extraction:

1. The `basicmicro_python` and `basicmicro_ros2` submodules remain as
   submodules in the new repo
2. `roboclaw_tcp_adapter` becomes the primary package
3. `robot_network.yaml` becomes the repo's config
4. The parent monorepo replaces `host_ws/` with a git submodule pointing
   to the new repo

**When to extract**: When a second robot needs the same driver, or when the
team grows and separate CI pipelines make more sense than shared ones.

---

## 11. Development Roadmap

| Priority | Task | Description |
|---|---|---|
| P0 | Integration test | Validate TCP adapter with real hardware end-to-end |
| P1 | Odometry tuning | Calibrate wheel radius, separation, encoder CPR |
| P1 | Nav2 integration | Feed `/robot/odom` into Nav2 stack for autonomous navigation |
| P2 | Telemetry dashboard | RoboClaw battery voltage, motor current, temperature on diagnostics |
| P2 | PID auto-tuning | Script for motor PID gain calibration via ROS2 services |
| P3 | Fleet configuration | Multi-robot `robot_network.yaml` with per-robot overlays |
| P3 | systemd services | Replace tmux with proper service management for production |

---

## File Inventory

```
host_ws/
├── README.md                          ← This file
├── config/
│   └── robot_network.yaml             ← Network parameter source of truth
└── src/
    ├── basicmicro_python/             ← Git submodule (upstream)
    ├── basicmicro_ros2/               ← Git submodule (upstream)
    └── roboclaw_tcp_adapter/          ← Custom ROS2 package
        ├── package.xml
        ├── setup.py
        ├── setup.cfg
        ├── resource/
        │   └── roboclaw_tcp_adapter   ← ament index marker
        ├── config/
        │   └── roboclaw_params.yaml   ← Driver parameters
        ├── launch/
        │   └── roboclaw.launch.py     ← ROS2 launch file
        └── roboclaw_tcp_adapter/
            ├── __init__.py
            ├── tcp_port.py            ← RoboClawTCPPort (socket adapter)
            ├── basicmicro_tcp.py      ← RoboClawTCP (Basicmicro subclass)
            ├── safety_bridge_node.py  ← E-Stop safety bridge
            └── roboclaw_tcp_node.py   ← Monkey-patch entry point
```
