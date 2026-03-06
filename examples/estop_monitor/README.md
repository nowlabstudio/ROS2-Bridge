# E-Stop Monitor — példaprogram

NC (normally closed) vészleállító gomb integrálása a bridge-be, IRQ-vezérelt
ROS2 publish-sal. A gomb felengedésétől számítva **<2ms** latenciával jelenik
meg az üzenet a `robot/estop` topicon.

---

## Mappa tartalma

```
examples/estop_monitor/
├── README.md          ← ez a fájl
├── estop.h            ← firmware: channel descriptor deklaráció
├── estop.c            ← firmware: GPIO init, ISR, read függvény
└── estop_monitor.py   ← ROS2 oldal: topic figyelő Python szkript
```

Az `estop.h` és `estop.c` referencia másolatok — a tényleges forrás:
`app/src/user/estop.h` és `app/src/user/estop.c`.

---

## Hardver

```
RP2040 GP27 ──[NC gomb]── GND
             (belső pull-up)
```

| Állapot | Gomb | GP27 (fizikai) | `robot/estop` |
|---------|------|----------------|---------------|
| Normál  | zárt | LOW            | `false`       |
| E-Stop  | nyílt | HIGH (pull-up) | `true`        |

> **NC = Normally Closed.** Ha a kábel megszakad, az E-Stop automatikusan
> aktív lesz — ez a fail-safe viselkedés.

---

## Firmware integrálás — lépésről lépésre

### 1. `app/boards/w5500_evb_pico.overlay` — pin hozzárendelés

A `gpio_channels` blokkban definiáld az `estop_btn` aliast:

```dts
gpio_channels {
    compatible = "gpio-keys";

    estop_btn: estop_btn {
        gpios = <&gpio0 27 (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>;
        label = "E-Stop button (NC)";
    };
};

aliases {
    estop-btn = &estop_btn;
};
```

> Pin csere esetén **csak a `27` számot kell átírni** — a C kód változatlan.

---

### 2. Másold be a firmware fájlokat

```bash
cp examples/estop_monitor/estop.h app/src/user/estop.h
cp examples/estop_monitor/estop.c app/src/user/estop.c
```

---

### 3. `app/src/user/user_channels.c` — regisztráció

```c
#include "estop.h"       // ← add hozzá a többi include mellé

void user_register_channels(void)
{
    // ... meglévő csatornák ...

    channel_register(&estop_channel);   // ← add hozzá
}
```

---

### 4. `app/CMakeLists.txt` — forrás felvétele

```cmake
target_sources(app PRIVATE
    # ... meglévő fájlok ...
    src/user/estop.c        # ← add hozzá
)
```

---

## Build és flash

```bash
west build -b w5500_evb_pico app/
west flash
```

Várt boot log (soros konzolon):

```
[INF] Channel registered: estop
[INF] estop init OK
[INF] GPIO IRQ configured: pin 27, channel_idx 3
[INF] estop publisher: robot/estop
```

---

## ROS2 monitor futtatása

### 1. Terminal — micro-ROS agent

```bash
bash tools/docker-run-agent-udp.sh
```

### 2. Terminal — E-Stop monitor

```bash
source /opt/ros/jazzy/setup.bash
python3 examples/estop_monitor/estop_monitor.py
```

### 3. Topic közvetlen figyelése

```bash
ros2 topic echo /robot/estop
# std_msgs/msg/Bool:
# data: false
```

---

## Várt kimenet

```
[INFO]  Subscribed to robot/estop — waiting for messages...
[INFO]  E-Stop cleared — system OK  (robot/estop = false)
[ERROR] *** E-STOP ACTIVE ***  (robot/estop = true)
[INFO]  E-Stop cleared — system OK  (robot/estop = false)
```

Csak állapotváltáskor logol — a 500ms-os periodikus üzenetek nem jelennek meg
ismételten, ha az állapot nem változott.

---

## Integráció saját AGV node-ba

```python
from std_msgs.msg import Bool

# A motorvezérlő node-odban:
self.create_subscription(Bool, "robot/estop", self._estop_cb, 10)

def _estop_cb(self, msg: Bool):
    if msg.data:
        self._stop_motors_immediately()
        self.get_logger().fatal("E-Stop! All motion halted.")
```

---

## Paraméterek ROS2-ből

```bash
# E-Stop logika invertálása (NO gombhoz):
ros2 param set /pico_bridge estop.invert_logic true

# Publish periódus módosítása:
ros2 param set /pico_bridge estop.period_ms 200

# Csatorna letiltása:
ros2 param set /pico_bridge estop.enabled false
```

---

## Megjegyzések

- **IRQ-vezérelt**: `GPIO_INT_EDGE_BOTH` — mindkét élen (gomb nyit/zár) azonnal publikál.
- **Periodic fallback**: `period_ms = 500` — 500ms-onként mindig küld, így az agent
  újracsatlakozás után is megkapja az aktuális állapotot.
- **Fail-safe**: NC gomb + pull-up → kábeltörés esetén E-Stop automatikusan aktív.
- **ISR-safe**: az ISR csak egy atomikus flag-et állít, az actual publish a főciklusban
  (1ms latencia) történik — nincs heap allokáció az ISR-ben.
