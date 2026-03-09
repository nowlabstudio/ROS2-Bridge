# Errata — W6100 EVB Pico micro-ROS Bridge

**Utolsó frissítés:** 2026-03-09

Ez a dokumentum az összes ismert hibát tartalmazza, root cause elemzéssel és javítási státusszal.

---

## Tartalom

| ID | Rövid leírás | Súlyosság | Állapot |
|----|-------------|-----------|---------|
| [ERR-001](#err-001) | `param_server_init error: 11` | Közepes | Nyitott |
| [ERR-002](#err-002) | `/diagnostics` IP DHCP módban statikus IP-t mutatott | Alacsony | **Javítva** v2.0 |
| [ERR-003](#err-003) | Router-en az eszköz neve "zephyr" volt | Alacsony | **Javítva** v2.0 |
| [ERR-004](#err-004) | Több bridge ugyanazon a hálózaton: kapcsolatvesztés | Kritikus | **Javítva** v2.0 |
| [ERR-005](#err-005) | Hardcoded MAC — minden board azonos `00:00:00:01:02:03` | Kritikus | **Javítva** v2.0 |
| [ERR-006](#err-006) | Dupla namespace: `/robot/robot/estop` | Közepes | **Javítva** v2.1 |
| [ERR-007](#err-007) | `rclc_support_init` "dirty struct" — végtelen reconnect loop | Kritikus | **Javítva** v2.1 |
| [ERR-008](#err-008) | `docker-run-agent-udp.sh` bash-t indított az agent helyett | Kritikus | **Javítva** v2.1 |
| [ERR-009](#err-009) | DTR wait blokkolta az autonome bootot | Magas | **Javítva** v2.0 |
| [ERR-010](#err-010) | Több board publishol ugyanarra az estop topicra | Magas | **Javítva** v2.1 |
| [ERR-011](#err-011) | `roboclaw_tcp_node` hibás import path | Kritikus | **Javítva** |
| [ERR-012](#err-012) | Monkey-patch nem fedte a `basicmicro.Basicmicro` nevet | Kritikus | **Javítva** |
| [ERR-013](#err-013) | `safety_bridge` nem létező `/emergency_stop` subscriber-re épített | Magas | **Javítva** |
| [ERR-014](#err-014) | `RoboClawTCP` konstruktor szignatúra inkompatibilis volt | Kritikus | **Javítva** |

---

## ERR-001

### `param_server_init error: 11`

**Dátum:** 2026-03-06
**Állapot:** Nyitott — root cause ismeretlen, folytatás szükséges

#### A hiba

Boot-kor a következő log jelenik meg:

```
<err> param_server: param_server_init error: 11
<wrn> main: Param server init failed — continuing without params
```

A rendszer a hiba ellenére fut tovább. A param server nélkül:
- A JSON-ból betöltött paraméterek (`/lfs/ch_params.json`) érvényesülnek boot-kor
- A csatornák az elmentett / alapértelmezett értékekkel indulnak
- `ros2 param set/get/list` nem elérhető

#### Hibakód elemzés

`error: 11` = `RCL_RET_INVALID_ARGUMENT = 11` (közvetlen), VAGY
`RCL_RET_BAD_ALLOC (10) | RCL_RET_ERROR (1) = 11` (bitwise OR)

A `rclc_parameter_server_init_with_option` `ret |= ...` mintával OR-olja össze a 6 service init visszatérési értékét.

#### Kizárt okok

| # | Hipotézis | Eredmény |
|---|-----------|----------|
| 1 | `prj.conf` Kconfig idézőjel-csapda | **KIZÁRVA** |
| 2 | `libmicroros.a` rossz limitekkel fordult | **KIZÁRVA** — `MAX_SERVICES=8` a binárisban |
| 3 | Executor handle count hiány | **KIZÁRVA** — `PARAM_SERVER_HANDLES = 6` benne van |
| 4 | `rcl_interfaces` service típusok hiányoznak | **KIZÁRVA** — mind jelen van |
| 5 | Service névhossz overflow | **KIZÁRVA** — leghosszabb < 50 char |
| 6 | Heap kimerülés | **VALÓSZÍNŰTLEN** — 96KB heap, mért free > 95KB |

#### Legvalószínűbb területek

1. **XRCE session timeout** a service-ek DDS entitás-létrehozásakor
2. **`init_parameter_server_memory_low`** valamelyik allokációja meghiúsul

#### Mellékhatás (nem fatális)

- Csatornák normálisan publisholnak
- `/lfs/ch_params.json` betöltése rendben zajlik
- `ros2 param set/get/list` nem elérhető amíg a hiba fennáll

#### Érintett fájlok

| Fájl | Megjegyzés |
|------|-----------|
| `app/src/bridge/param_server.c` | A hiba itt jelentkezik |
| `app/src/main.c` | Executor handle count — rendben |

---

## ERR-002

### `/diagnostics` IP DHCP módban statikus IP-t mutatott

**Dátum:** 2026-03-07
**Állapot:** ✅ **Javítva** — v2.0, commit `c816b9e`

#### A hiba

Ha `"dhcp": true`, a bridge DHCP-vel kapott IP-t, de a `/diagnostics` topicban az `ip` mező a `config.json`-ban szereplő statikus IP-t mutatta.

#### Root cause

`diagnostics_publish()` a `g_config.network.ip` értékét másolta, ami DHCP módban nem frissült a ténylegesen kiosztott címre.

#### Javítás

`diagnostics_publish()` a Zephyr net stack-ből kérdezi le az aktuális IP-t:

```c
struct net_if *iface = net_if_get_default();
// net_if_ipv4_get_global_addr() → valódi IP
```

Plusz hozzáadva egy 6. KeyValue: `mac` — az aktuális MAC cím.

---

## ERR-003

### Router-en az eszköz neve "zephyr" volt, nem a konfigurált node_name

**Dátum:** 2026-03-07
**Állapot:** ✅ **Javítva** — v2.0, commit `c816b9e`

#### A hiba

A hálózati routeren minden board "zephyr" (vagy "zephir") névvel jelent meg. Több bridge esetén nem lehetett megkülönböztetni őket.

#### Root cause

A projektben nem volt `CONFIG_NET_HOSTNAME_ENABLE` beállítva, így a Zephyr alapértelmezett `"zephyr"` hostname-je érvényesült.

#### Javítás

- `CONFIG_NET_HOSTNAME_DYNAMIC=y` és `CONFIG_NET_HOSTNAME="ROS_Bridge"` hozzáadva a `prj.conf`-ba
- `main.c`-ben `net_hostname_set()` a `node_name`-ből: `ROS_Bridge_<node_name>`
- Eredmény: pl. `ROS_Bridge_estop`, `ROS_Bridge_rc` a routeren

---

## ERR-004

### Több bridge ugyanazon a hálózaton: kapcsolatvesztés

**Dátum:** 2026-03-07
**Állapot:** ✅ **Javítva** — v2.0 (ERR-005 root cause megoldva)

#### A hiba

Ha több W6100 bridge volt ugyanazon a hálózaton, egy idő után elvesztették a kapcsolatot:
- A user LED kialudt
- A micro-ROS agent továbbra is futott
- A bridge "Phase 1: Search for agent" állapotba kerül vissza

#### Root cause

Az azonos hardcoded MAC cím (ERR-005) okozta az ARP ütközést → azonos DHCP IP → azonos XRCE client key → session ütközés az agent oldalon.

**Láncolat:** ERR-005 → azonos IP → azonos XRCE key → agent eldobja az egyiket → LED kialszik → reconnect.

#### Javítás

Az ERR-005 javítása (egyedi MAC) automatikusan megoldotta ezt is.

---

## ERR-005

### Hardcoded MAC — minden board azonos `00:00:00:01:02:03`

**Dátum:** 2026-03-07
**Állapot:** ✅ **Javítva** — v2.0, commit `c816b9e`

#### A hiba

A board device tree-ben hardcoded MAC cím volt:
```dts
local-mac-address = [00 00 00 01 02 03];
```
Minden egyes W6100 EVB Pico board ugyanezt kapta.

#### Root cause

Az `app/boards/w5500_evb_pico.overlay` nem írta felül a DTS MAC-et.

#### Javítás

`apply_mac_address()` függvény a `main.c`-ben:

1. Ha `config.json`-ban van `"mac"` mező → azt parse-olja és beállítja
2. Ha nincs → `hwinfo_get_device_id()` → RP2040 flash unique ID → `02:xx:xx:xx:xx:xx`
3. Fallback: DTS MAC marad (log warning)

A MAC közvetlen a W6100 hardware regiszterébe kerül (`ethernet_api.set_config()`).

**Megjegyzés:** A `net_mgmt(NET_REQUEST_ETHERNET_SET_MAC_ADDRESS, ...)` nem működött — `EACCES` hibát adott vissza mert az interface már admin-up állapotban volt. A driver API közvetlen hívása megkerüli ezt az ellenőrzést.

---

## ERR-006

### Dupla namespace: `/robot/robot/estop`

**Dátum:** 2026-03-07
**Állapot:** ✅ **Javítva** — v2.1

#### A hiba

`ros2 topic list` kimenetében `/robot/robot/estop` jelent meg `/robot/estop` helyett.

#### Root cause

Az `estop.c`-ben a channel `topic_pub` hardcoden `"robot/estop"` volt beállítva. Mivel a node namespace-e `/robot`, a micro-ROS a teljes topic nevet úgy képezte: `/<namespace>/<topic_pub>` = `/robot/robot/estop`.

#### Javítás

`estop.c`-ben `topic_pub` javítva `"robot/estop"` → `"estop"`. A namespace-t a `config.json` `ros.namespace` mezője adja, nem a topic stringbe kell beleírni.

**Általános szabály:** A `channel_t.topic_pub` mindig namespace-relatív legyen — a framework a `/<namespace>/` előtagot automatikusan hozzáadja.

---

## ERR-007

### `rclc_support_init` "dirty struct" — végtelen reconnect loop

**Dátum:** 2026-03-08
**Állapot:** ✅ **Javítva** — v2.1, commit `8a30801`

#### A hiba

Ha az első `rclc_support_init` hívás sikertelen volt (az agent még nem futott bootkor), a board soha nem tudott csatlakozni — még akkor sem, ha az agent utána elindult. A log:

```
<err> main: rclc_support_init error
<wrn> main: Session init failed, retrying...
[2s later]
<err> main: rclc_support_init error
...  (végtelen)
```

#### Root cause

Az `rclc_support_t support` struct, a `rcl_node_t node` és az `rclc_executor_t executor` structs static változók. Ha az első `rclc_support_init` hívás sikertelen, a struct **részlegesen inicializált (dirty)** állapotban marad a memóriában. A reconnect loop `ros_session_fini()`-t nem hívta meg (mert `session_active == false` volt), ezért a következő `rclc_support_init` hívás ugyanezt a dirty struct-ot kapta — ami újból hibát okozott.

Ez a hiba csak akkor reprodukálódott, ha a board korábban bootolt fel, mint az agent — ami autonomus üzemben (USB serial nélküli indítás) mindig előfordul.

#### Javítás

`memset` hozzáadva a `ros_session_init()` elejére, minden egyes hívás előtt:

```c
static bool ros_session_init(void)
{
    allocator = rcl_get_default_allocator();

    /* Ensure clean state even if a previous attempt partially initialized */
    memset(&support,  0, sizeof(support));
    memset(&node,     0, sizeof(node));
    memset(&executor, 0, sizeof(executor));

    if (rclc_support_init(&support, 0, NULL, &allocator) != RCL_RET_OK) {
        LOG_ERR("rclc_support_init error");
        return false;
    }
    ...
```

#### Érintett fájl

| Fájl | Sor | Megjegyzés |
|------|-----|-----------|
| `app/src/main.c` | ~276 | `ros_session_init()` eleje |

---

## ERR-008

### `docker-run-agent-udp.sh` bash-t indított az agent helyett

**Dátum:** 2026-03-08
**Állapot:** ✅ **Javítva** — v2.1, commit `8a30801`

#### A hiba

A `tools/start-eth.sh` és `tools/docker-run-agent-udp.sh` scriptek futtatása után az agent Docker container elindult, de a micro-ROS agent folyamat **nem futott** benne — interaktív `bash` shell indult helyette.

#### Root cause

A `docker-run-agent-udp.sh` scriptben a `CMD` sor `bash`-t tartalmazott az agent parancs helyett:

```bash
# Hibás:
docker run ... microros/micro-ros-agent:jazzy bash
# Helyes:
docker run ... microros/micro-ros-agent:jazzy udp4 -p "$PORT" -v6
```

Ráadásul a script egy `$SCRIPT_DIR` változóra hivatkozott a volume mount-ban, ami nem volt definiálva, így a `cyclonedds.xml` sem töltődött be.

#### Javítás

A teljes script átírva: helyes agent parancs (`udp4 -p "$PORT" -v6`), a `$SCRIPT_DIR` undefined változó eltávolítva, a `cyclonedds.xml` mount javítva.

#### Érintett fájl

| Fájl | Megjegyzés |
|------|-----------|
| `tools/docker-run-agent-udp.sh` | Teljes újraírás |
| `tools/cyclonedds.xml` | `eth0` → `auto` interface, stale peer eltávolítva |

---

## ERR-009

### DTR wait blokkolta az autonome bootot

**Dátum:** 2026-03-07
**Állapot:** ✅ **Javítva** — v2.0

#### A hiba

Ha a board USB serial nélkül kapott tápot (pl. külső 5V adapter), **soha nem indult el** — nem csatlakozott a hálózatra, az LED sem gyulladt ki.

#### Root cause

A `main.c`-ben a DTR wait ciklus blokkolta a továbblépést, amíg USB CDC ACM konzol nem csatlakozott:

```c
// Hibás — végtelen blokkolás USB nélkül:
while (!dtr) {
    uart_line_ctrl_get(uart_dev, UART_LINE_CTRL_DTR, &dtr);
    k_sleep(K_MSEC(100));
}
```

#### Javítás

A blokkoló várakozás **500ms-os non-blocking poll**-ra cserélve:

```c
// Helyes:
int64_t dtr_deadline = k_uptime_get() + 500;
while (k_uptime_get() < dtr_deadline) {
    uart_line_ctrl_get(uart_dev, UART_LINE_CTRL_DTR, &dtr);
    if (dtr) break;
    k_sleep(K_MSEC(50));
}
// dtr true = console connected, false = autonomous mode
```

#### Érintett fájl

| Fájl | Megjegyzés |
|------|-----------|
| `app/src/main.c` | DTR wait ciklus |

---

## ERR-010

### Több board publishol ugyanarra az estop topicra

**Dátum:** 2026-03-07
**Állapot:** ✅ **Javítva** — v2.1

#### A hiba

Az E-Stop nyomógomb helyes értéket adott vissza (`false` = normál, `true` = megnyomva), de a `ros2 topic echo /robot/estop` kimenetében folyamatosan váltakozott a `true` és `false` érték — még akkor is, ha a gomb fizikailag fix állásban volt.

#### Root cause

Minden board ugyanazt a firmware-t futtatja, és az `estop` csatorna alapból regisztrálva volt **minden boardon**. Így:

- Az `estop` board (GP27-re kötött NC kapcsoló) helyesen `false`-t publisholt
- A `rc` és `pedal` board-okon a GP27 pin lebegő (floating) volt → pullup → `true` értéket publisholtak

Három board egyszerre publisholt ugyanarra a `/robot/estop` topicra, a ROS subscriber mind a háromtól kapott üzenetet — ezért váltakozott az érték.

#### Javítás

**Csatorna enable/disable a config.json-ból.** Minden board csak a saját fizikailag bekötött csatornáit aktiválja:

```json
// E_STOP board:
"channels": { "estop": true, "rc_ch1": false, ... }

// RC board:
"channels": { "estop": false, "rc_ch1": true, ... }
```

A `user_channels.c` `register_if_enabled()` wrapper-je ellenőrzi a config-ot regisztráció előtt.

Ezzel együtt **50ms GPIO debounce** is implementálva (`drv_gpio.c`) a fizikai kapcsoló kontaktus-bounce szűrésére.

#### Érintett fájlok

| Fájl | Megjegyzés |
|------|-----------|
| `app/src/user/user_channels.c` | `register_if_enabled()` wrapper |
| `app/src/drivers/drv_gpio.c/.h` | 50ms debounce |
| `devices/*/config.json` | Per-board channel enable/disable |

---

## ERR-011

### `roboclaw_tcp_node` hibás import path

**Dátum:** 2026-03-09
**Súlyosság:** Kritikus — a node egyáltalán nem indult volna
**Állapot:** **Javítva**
**Komponens:** `host_ws/src/roboclaw_tcp_adapter/roboclaw_tcp_adapter/roboclaw_tcp_node.py`

#### Leírás

A monkey-patch entry point a driver `main()` függvényét `basicmicro_driver.basicmicro_driver` modulból próbálta importálni. Ez a modul **nem létezik** — az upstream csomag neve `basicmicro_driver.basicmicro_node`.

#### Root cause

Az upstream `basicmicro_ros2` csomag `setup.py` entry point-ja: `basicmicro_node = basicmicro_driver.basicmicro_node:main`. A kód írása során tévesen `basicmicro_driver` volt feltételezve modul névként.

#### Javítás

```python
# Hibás:
from basicmicro_driver.basicmicro_driver import main as driver_main
# Javított:
from basicmicro_driver.basicmicro_node import main as driver_main
```

---

## ERR-012

### Monkey-patch nem fedte a `basicmicro.Basicmicro` nevet

**Dátum:** 2026-03-09
**Súlyosság:** Kritikus — a TCP adapter sosem aktiválódott volna
**Állapot:** **Javítva**
**Komponens:** `host_ws/src/roboclaw_tcp_adapter/roboclaw_tcp_adapter/roboclaw_tcp_node.py`

#### Leírás

A monkey-patch csak `basicmicro.controller.Basicmicro`-t cserélte ki, de az upstream driver `basicmicro_node.py` így importál:

```python
from basicmicro import Basicmicro  # basicmicro/__init__.py re-export
```

A Python `from X import Y` az import pillanatában köti a nevet. Mivel `basicmicro.__init__.py` a `from basicmicro.controller import Basicmicro` sort futtatja a modul betöltésekor, `basicmicro.Basicmicro` egy **független** referencia lett az eredeti osztályra. A `basicmicro.controller.Basicmicro` patchelése **nem propagálódik** a `basicmicro.Basicmicro`-ra.

#### Root cause

Python name binding mechanizmus: `from X import Y` másolatot hoz létre a névről, nem referenciát a modul attribútumra.

#### Javítás

Mindkét helyen patchelni kell:

```python
import basicmicro
import basicmicro.controller
basicmicro.controller.Basicmicro = RoboClawTCP  # a definiáló modul
basicmicro.Basicmicro = RoboClawTCP             # az __init__.py re-export
```

---

## ERR-013

### `safety_bridge` nem létező `/emergency_stop` subscriber-re épített

**Dátum:** 2026-03-09
**Súlyosság:** Magas — E-Stop nem állította volna meg a motorokat
**Állapot:** **Javítva**
**Komponens:** `host_ws/src/roboclaw_tcp_adapter/roboclaw_tcp_adapter/safety_bridge_node.py`

#### Leírás

A `safety_bridge_node` `std_msgs/Empty` üzeneteket publisholt `/emergency_stop` topicra. Az upstream `basicmicro_ros2` driver dokumentációja említi ezt a topic-ot, de a **tényleges kódban nincs** rá subscriber. Az egyetlen parancs bemenet a `cmd_vel` (geometry_msgs/Twist).

#### Root cause

A `basicmicro_ros2` repo dokumentációja és kódja inkonzisztens. A `docs/architecture.md` és `README.md` említi az `/emergency_stop` topic-ot és service-t, de az aktuális `basicmicro_node.py` csak `cmd_vel` subscription-t tartalmaz.

#### Javítás

A safety bridge mostantól **zero Twist-et publishol a `cmd_vel` topicra** 10 Hz-en amíg az E-Stop aktív. Ez a driver tényleges, működő interfészét használja. Az `/emergency_stop` publish megmaradt forward-kompatibilitásnak.

---

## ERR-014

### `RoboClawTCP` konstruktor szignatúra inkompatibilis volt

**Dátum:** 2026-03-09
**Súlyosság:** Kritikus — a driver `TypeError`-t dobott volna
**Állapot:** **Javítva**
**Komponens:** `host_ws/src/roboclaw_tcp_adapter/roboclaw_tcp_adapter/basicmicro_tcp.py`

#### Leírás

Az upstream driver így hívja a konstruktort:
```python
self.controller = Basicmicro(self.port, self.baud)  # ("tcp://192.168.68.60:8234", 115200)
```

A `RoboClawTCP.__init__` eredeti szignatúrája:
```python
def __init__(self, host: str, port: int, ...)  # (host, port) — más!
```

A `host` megkapta volna a teljes `"tcp://..."` stringet, a `port` pedig az `115200` baud rate-et.

#### Root cause

A monkey-patch lényege, hogy az eredeti osztályt **transparensen** helyettesíti. Ehhez **azonos konstruktor szignatúra** kell: `(comport: str, rate: int, ...)`.

#### Javítás

`RoboClawTCP.__init__` mostantól az eredeti `Basicmicro(comport, rate, timeout, retries, verbose)` szignatúrát fogadja, és a `comport` stringből parse-olja a TCP host:port párost (`tcp://host:port` vagy `host:port` formátum).

---

## Megjegyzések

- **ERR-007 (dirty struct) és ERR-008 (broken agent script) egymást erősítette**: az agent ténylegesen nem futott, de ha futott volna is, a firmware végtelen reconnect loopba ragadt volna az első failed init után. Mindkettőt javítani kellett.
- **ERR-010 (topic collision) root cause-a az architektúra**, nem bug: ugyanaz a firmware fut minden boardon. A megoldás a config-driven channel enable/disable — ez tisztán és skálázhatóan kezeli a problémát.
- **ERR-001 (param server)** és az összes többi hiba egymástól független. Az ERR-001 a micro-XRCE-DDS rétegben van, a többi firmware vagy tool szintű hiba volt.
- **ERR-011..014 (host_ws bugok)** mind a pre-deployment validáció során derültek ki. A négy hiba együttesen megakadályozta volna a `roboclaw_tcp_adapter` működését. Mindegyik a kód review fázisban javítva, hardveres teszt előtt.
