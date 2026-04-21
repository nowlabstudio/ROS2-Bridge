# Errata — W6100 EVB Pico micro-ROS Bridge

**Utolsó frissítés:** 2026-04-21 (ERR-033 dokumentálva — RP2040 output pin readback `gpio_pin_get_dt()`-n 0-t ad, ha az input buffer nincs bekapcsolva; write-echo state cache workaround-ként)

Ez a dokumentum az összes ismert hibát tartalmazza, root cause elemzéssel és javítási státusszal.

---

## Tartalom

| ID | Rövid leírás | Súlyosság | Állapot |
|----|-------------|-----------|---------|
| [ERR-001](#err-001) | `param_server_init error: 11` | Közepes | Diagnosztika aktív (BL-004 lezárva) |
| [ERR-025](#err-025) | Zephyr SDK 0.17 → 1.0 drift: west `revision: main` → `find_package(Zephyr-sdk 1.0)` fail | Kritikus | **Javítva** (v4.2.2 pin) |
| [ERR-026](#err-026) | Architekturális döntés: W6100 → W5500 kompatibilis mód, stock board def használata | Informatív | **Lezárva** |
| [ERR-027](#err-027) | jazzy HEAD UDP transport header régi Zephyr POSIX layoutot vár (`<posix/sys/socket.h>`) | Kritikus | **Javítva** (tools/patches/apply.sh) |
| [ERR-028](#err-028) | jazzy HEAD `libmicroros.mk` `touch std_srvs/COLCON_IGNORE`-t tiltja le | Kritikus | **Javítva** (tools/patches/apply.sh) |
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
| [ERR-015](#err-015) | RoboClaw Docker: `ModuleNotFoundError: No module named 'serial'` | Magas | **Javítva** |
| [ERR-016](#err-016) | safety_bridge_node: `RcutilsLogger.info()` több argumentum → TypeError | Közepes | **Javítva** |
| [ERR-017](#err-017) | RoboClaw Docker: No module named 'basicmicro_driver' | Magas | **Javítva** |
| [ERR-018](#err-018) | /robot/diagnostics nem érkezik meg ros2-shell / Foxglove felé (Fast DDS SHM) | Kritikus | **Javítva** |
| [ERR-019](#err-019) | Motor szaggatottan forog ROS2 alatt (safety_bridge watchdog + bus contention) | Kritikus | **Javítva** (C++ ros2_control) |
| [ERR-020](#err-020) | SetTimeout 4 byte-ot küldött 1 helyett → protokoll korrupció, ERR LED | Kritikus | **Javítva** |
| [ERR-021](#err-021) | Motor nem állt le cmd_vel timeout után (PID + lebegő encoder) | Kritikus | **Javítva** |
| [ERR-022](#err-022) | Motor induláskor forgott (cmd_vel_dirty + open_loop: false) | Magas | **Javítva** |
| [ERR-023](#err-023) | Folyamatos overrun 100Hz-en — WiFi latencia, nem szoftver hiba | Közepes | **Megoldva** (Ethernet-en 100Hz OK) |
| [ERR-024](#err-024) | SEGFAULT: RCLCPP_WARN_THROTTLE temporális Clock shared_ptr lifetime | Kritikus | **Javítva** |
| [ERR-029](#err-029) | W5500 driver RTR register check -ENODEV a W6100 chipen — superseded by ERR-030 | Kritikus | **Elavult** (W5500 driver nincs használatban) |
| [ERR-030](#err-030) | W6100 chip nem válaszolt SPI-n: CIDR=0x00, minden regiszter olvasás 0x00 (hiányzó reset pulzus + CHPLCKR/NETLCKR lock) | Kritikus | **Javítva** (out-of-tree W6100 driver) |
| [ERR-031](#err-031) | W6100 `set_config(MAC_ADDRESS)` frissíti a chip SHAR-t, de nem az iface->link_addr-t — MACRAW keret src MAC beragad | Kritikus | **Javítva** (net_if_set_link_addr hívás hozzáadva) |
| [ERR-032](#err-032) | `rclc_parameter_server_init_with_option` `RCL_RET_INVALID_ARGUMENT` (11) bukás után a 6 részlegesen regisztrált service handle poisons the executor dispatch loop — valós subscription callback-ek (pl. okgo_led Bool write) sem futnak | Kritikus | **Kerülve** (param_server_init call eltávolítva a common/src/main.c-ből) |
| [ERR-033](#err-033) | RP2040 Zephyr GPIO driver: `gpio_pin_get_dt()` output pinen 0-t ad, ha `GPIO_OUTPUT_INACTIVE`-val konfiguráltuk — nincs input buffer a readback-hez | Közepes | **Megkerülve** (per-channel write-echo state cache a gpio_out.c-ben) |

---

## ERR-032

### rclc_parameter_server részleges handle-regisztráció megmérgezi az rclc_executor dispatch-et

**Tünet (2026-04-21, BL-017):** E_STOP boardon a `/robot/okgo_led`
`std_msgs/Bool` subscription callback (`okgo_led_write`) **soha nem fut**
publish hatására, pedig:
- A `channel_manager_add_subs_to_executor` `rclc_executor_add_subscription_with_context`
  hívása sikerrel visszatér (sub regisztrálva handle[0]-ként).
- A boot log szerint session aktív: `micro-ROS session active. 4 channels, 1 subscribers.`
- `ros2 topic echo /robot/okgo_led` látja a publish-olt Bool-t → DDS +
  agent szint rendben.
- Boot logban ott a `<err> param_server: param_server_init error: 11`
  (RCL_RET_INVALID_ARGUMENT), de a main.c ezt warning-ra veszi és tovább megy.

A diagnosztikai `LOG_DBG` a callback belsejében soha nem log-olódik,
ergo a dispatch maga a `rclc_executor_spin_some` szinten nem hívja meg
a callback-et — nem a GPIO driver, nem a `channel_value_t` konverzió,
nem a config, hanem a dispatch loop a problémás.

**Gyökérok:** a `common/src/main.c` `ros_session_init` így sorrendez:

```c
int handle_count = sub_count + PARAM_SERVER_HANDLES(6) + service_count();
rclc_executor_init(&executor, &support.context, handle_count, &allocator);
channel_manager_add_subs_to_executor(&executor);  // sub → handles[0]
if (param_server_init(&node, &executor) < 0) {
    LOG_WRN("Param server init failed — continuing without params");
}
```

A `param_server_init` a `rclc_parameter_server_init_with_option`-on
keresztül próbálja 6 paraméter-service-t (`get/get_types/set/
set_atomically/list/describe`) init-elni. A libmicroros `parameter_server.c`
implementációja a 6 `rclc_service_init` hívását `ret |= ` -gyel akkumulálja
— de a `rclc_executor_add_parameter_server_with_context` (ami a `main.c`-ben
a `rclc_parameter_server_init_with_option` UTÁN fut BE a `param_server_init`
belsejében) az executor handles[1..6]-be **beregisztrálja mind a 6 service
slot-ot `initialized=true`-ként** MIELŐTT észlelné az init hibát.

Az `rclc_executor.c:1824` spin_some loop ezen alapul:
```c
for (size_t i = 0; (i < executor->max_handles && executor->handles[i].initialized); i++) {
  ...
}
```

A loop végigmegy handle[0] (valós sub) + handle[1..6] (törött param services)
minden iterációjában. A 6 törött handle-on vagy a `rcl_take_response` /
`rcl_take_request` silent-fail, vagy belső state assert miatt a stream
consumer (uxr_buffer_request_data) pozíciója megakad, és az XRCE transport
a DATA payload-ot nem továbbítja tovább dispatch-re. Pontos mechanizmus
nincs izolálva (nem tudtunk step-debug-olni), de az empirikus bizonyíték
egyértelmű: **6 handle kivétele → minden sub rögtön dispatchel**.

Bizonyíték (ua. firmware, csak main.c `param_server_init` hívás eltávolítva):
```
<inf> main: Executor: 1 subs + 0 svcs = 1 handles
<inf> main: micro-ROS session active. 4 channels, 1 subscribers.
[publish /robot/okgo_led true]
<inf> okgo_led: write CB: val=1     ← ELŐZŐLEG SOHA NEM FUTOTT
```

**Miért nem lett önmagában ERR-001?** Az ERR-001 (`param_server_init
error: 11`) már 2026-04-19 óta ott van a boot logban (BL-004
diagnosztika), de addig ártalmatlannak tűnt, mert a main.c a warning-on
túlment és a többi csatorna publisher-only volt — dispatch-corruption csak
subscription esetén látszik. BL-014 Fázis 2 hozta be az első sub-ot
(`okgo_led`), ekkor lett látható az ERR-001 valós kockázata.

**Fix (alkalmazott 2026-04-21):** a `param_server_init(&node, &executor)`
hívás **törölve** a `common/src/main.c`-ből, és a `handle_count` számításból
kivett `PARAM_SERVER_HANDLES(6)`. Az interaktív `ros2 param` nem használható
többé (user explicit elvetette — a `devices/<device>/config.json` a single
source of truth). A `common/src/bridge/param_server.{c,h}` fájlok a fában
maradnak mint holt kód (backlog-ban BL-018 nyitva: ha valaha interaktív
paramserver kell, a `rclc_parameter_server_init_service` root-cause-t
(`rcl_node_get_name()` + service_name 32-byte stack buffer gyanús) kell
először javítani).

**Tanulság:** a rclc_executor `handles[]` tömbje olyan, mintha
megbízható-invariáns lenne, de valójában a `rclc_executor_add_*` függvények
**atomicity nélkül** állítják `initialized=true`-ra a handle-t, mielőtt a
belső init teljesen végigfutna. Bármilyen hibával kilépő
`rclc_executor_add_parameter_server_with_context` / service init /
subscription init **megmérgezi az egész dispatch loop-ot** — mivel a
spin_some az első uninitialized handle-nél kilép, és minden handle előtte
korruptan dispatchelődik. Jövőbeli fejlesztéseknél: ha egy sub callback
rejtélyesen silent, az ELSŐ dolog amit nézni kell: van-e másik handle (pl.
service, param, action) ugyanezen executor-on, ami bukott init után mégis
registered marad.

**Érintett fájlok:**
- `common/src/main.c` (`ros_session_init`) — fix
- `common/src/bridge/param_server.{c,h}` — holt kód (maradt)
- `apps/estop/src/okgo_led.c` — `LOG_DBG("write CB: val=%d")` dokumentáció

**Related:** ERR-001 (a `error: 11` diagnosztika), BL-017 (tracking),
BL-018 (jövőbeli param_server root-cause).

---

## ERR-033

### RP2040 GPIO output pin readback — `gpio_pin_get_dt()` 0-t ad input buffer nélkül

**Tünet (2026-04-21, BL-018):** RC boardon a GP8..GP11 output pinek `state`
publisher (`/robot/gpX_state`, 5 Hz) mindig `false` (0)-t küldött, pedig a
DMM szerint a pin ténylegesen magas (3.3 V) volt `ros2 topic pub /robot/gp8
std_msgs/msg/Bool "{data: true}"` után. A `read()` callback a
`common/src/drivers/drv_gpio.c`-ban a szabvány Zephyr `gpio_pin_get_dt(&cfg->spec)`-et
hívja, ami boolean 0-t adott vissza a kimeneti pineken, annak ellenére, hogy
a `write()` path helyes volt és a chip-oldali OUTPUT szint valóban 1 lett.

**Gyökérok:** a Zephyr RP2040 `gpio_rpi_pico` driver `pin_configure()`-je az
alapértelmezett `GPIO_OUTPUT` / `GPIO_OUTPUT_INACTIVE` flag-kombináció mellett
**nem kapcsolja be az input buffert** (SIO `GPIO_CTRL[OEOVER]` + `IE` regiszter
bit). Az input path így teljesen inaktív — `gpio_pin_get_dt()` olyan, mint
egy nyitott kapu: mindig 0-t ad. A readback csak akkor működne, ha:
1. a pin `GPIO_OUTPUT | GPIO_INPUT`-ként lenne konfigurálva (input buffer
   explicit bekapcsolva), vagy
2. a driver saját readback-et csinálna a `GPIO_OUT` regiszterből (írt érték
   shadow-olása).

Más Zephyr SoC-okon (pl. nRF, STM32) ez máshogy működhet — ott az input
buffer alapból él output módban is. Az RP2040 SIO-n explicit IE kell,
különben GPIO read nem értelmes.

**Mitigáció (alkalmazott):** a közös `drv_gpio` réteget nem módosítottuk
(másik kliens territóriuma, BL-015 után is közös). Helyette `apps/rc/src/gpio_out.c`-ben
minden pinnek van saját `static bool gpX_state_cache;` változója:

```c
static void gpio_write_common(gpio_channel_cfg_t *cfg, bool *cache,
                              const channel_value_t *val) {
    drv_gpio_write(cfg, val->b ? 1 : 0);
    *cache = val->b;                          // write-echo cache update
}
static void gp8_read(channel_value_t *out)  { out->b = gp8_state_cache; }
static void gp8_write(const channel_value_t *v)
                    { gpio_write_common(&gp8_cfg, &gp8_state_cache, v); }
```

Pure output pinen (nincs külső driver, nincs open-drain kontencjó) a cache
semantikailag azonos a fizikai pin-szintű readback-kel. Ha később short-
circuit-detektálásra vagy back-drive monitoring-ra lesz szükség (pl. relé
fault detection), akkor fel kell hozni egy igazi readback path-et a
`drv_gpio`-ban (`gpio_pin_configure` kiegészítés `GPIO_INPUT` flag-gel a
setup_output-ban), de most nem indokolt.

**Miért nem `drv_gpio` fix:** a `common/src/drivers/drv_gpio.c` a másik
device-okkal (E_STOP, PEDAL) osztott — azokon a driver OUTPUT pin (GP22
okgo_led, stb.) nem publikál state feedback-et (publisher-only vagy
subscriber-only csatornák), tehát ott nem jön ki ez a bug. Egyedül az RC
csinál bidirectional cmd+state pattern-t output pinen. Lokális fix a helyes
megközelítés: a közös réteget nem piszkálja, a viselkedés az RC app-ban
kontextuálisan dokumentált (`gpio_out.c` fájl-szintű komment).

**Verifikáció (2026-04-21, dev subnet):** `ros2 topic pub /robot/gp8 -r 2
std_msgs/msg/Bool "{data: true}"` → DMM magas, `/robot/gp8_state` 5 Hz-en
`True` → cache → pub path működik. `{data: false}` → DMM alacsony,
`/robot/gp8_state` `False`. Mind a 4 pin (GP8..GP11) ugyanúgy viselkedik.

**Érintett fájlok:**
- `apps/rc/src/gpio_out.c` — per-pin state cache + fájl-szintű komment
  magyarázza az RP2040 readback quirk-et.
- `apps/rc/src/gpio_out.h` — nincs változás (descriptor-listing).

**Related:** BL-018 (tracking), BL-012 (superseded parent ticket).

---

## ERR-028

### jazzy HEAD `libmicroros.mk` kizárja a std_srvs csomagot

| Mező | Érték |
|------|-------|
| **Súlyosság** | Kritikus (firmware app nem fordul) |
| **Állapot** | **Javítva** (`tools/patches/apply.sh` — Patch 2) |
| **Felfedezés** | 2026-04-19 |
| **Javítás** | 2026-04-19 |
| **Komponens** | `micro_ros_zephyr_module` jazzy @ 87dbe3a9 |

#### Tünet

```
app/src/bridge/service_manager.c:3:10: fatal error:
  std_srvs/srv/set_bool.h: No such file or directory
```

A `libmicroros.a` nem tartalmazza a `std_srvs` csomagot; a `set_bool.h` és
`trigger.h` header-ek hiányoznak az include fa-ból. A `service_manager.c`
ezeket `SetBool` (relay_brake) és `Trigger` (estop query) szolgáltatáshoz
használja.

#### Root cause

A `modules/libmicroros/libmicroros.mk:103`:

```makefile
touch src/common_interfaces/std_srvs/COLCON_IGNORE; \
```

Ez a sor a `micro_ros_src/src/common_interfaces/std_srvs/COLCON_IGNORE`
fájlt hozza létre, amit a colcon „skip this package" jelzésként olvas.
Így a std_srvs NEM épül be a libmicroros-ba.

A `TECHNICAL_OVERVIEW.md §9.3` szerint ez a v2.0 fejlesztéskor lokálisan
el lett távolítva a macOS gépen (commit `90935b9`), de az a módosítás
**sosem került sem a felhasználó repójába, sem upstream-be**. Amikor a
workspace újragenerálódott, a fix elveszett — pontosan a policy.md §2.8
„Pin mindent, ami driftelhet" kiindulópontja.

#### Javítás — `tools/patches/apply.sh` Patch 2

A Patch:
```makefile
- touch src/common_interfaces/std_srvs/COLCON_IGNORE; \
+ rm -f src/common_interfaces/std_srvs/COLCON_IGNORE; \
```

A `rm -f` szemantikailag a `touch` inverze: **biztosítja**, hogy a
COLCON_IGNORE fájl NEM létezik a build előtt. Ez idempotens — stale
állapotot is rendbe tesz (amit a korábbi buildek `touch`-oltak).

A `apply.sh` a stale COLCON_IGNORE fájlt explicit törli is, ha megtalálja,
egy korábbi build maradványaként.

#### Érintett fájlok

| Fájl | Megjegyzés |
|------|-----------|
| `tools/patches/apply.sh` | Patch 2 logika |
| `Makefile` | `apply-patches` target, `build:` függ tőle |
| `app/src/bridge/service_manager.c` | `SetBool` + `Trigger` használat (változatlan) |

---

## ERR-027

### jazzy HEAD UDP transport header régi Zephyr POSIX layoutot vár

| Mező | Érték |
|------|-------|
| **Súlyosság** | Kritikus (firmware app nem fordul) |
| **Állapot** | **Javítva** (`tools/patches/apply.sh` — Patch 1) |
| **Felfedezés** | 2026-04-19 |
| **Javítás** | 2026-04-19 |
| **Komponens** | `micro_ros_zephyr_module` jazzy @ 87dbe3a9 |

#### Tünet

```
modules/lib/micro_ros_zephyr_module/modules/libmicroros/microros_transports/
udp/microros_transports.h:21:10: fatal error:
  posix/sys/socket.h: No such file or directory
```

A `main.c:23` `#include <microros_transports.h>`-n keresztül behúzott UDP
transport header a régi bare `posix/` path-t használja, ami Zephyr v3.1+
után `zephyr/posix/`-ra költözött.

#### Root cause

A serial és serial-usb transport `.c` fájlokban **már** van feltételes
include:

```c
#if ZEPHYR_VERSION_CODE >= ZEPHYR_VERSION(3,1,0)
#include <zephyr/posix/unistd.h>
#else
#include <posix/unistd.h>
#endif
```

A UDP `microros_transports.h` a migrációnál **kimaradt**. Jazzy branch HEAD
(`87dbe3a9`) is ezt az állapotot tükrözi. Az upstream hiba, nem lokális.

#### Javítás — `tools/patches/apply.sh` Patch 1

Ugyanaz a conditional pattern mint a serial transportokban:

```c
- #include <sys/types.h>
- #include <posix/sys/socket.h>
- #include <posix/poll.h>
+ #include <sys/types.h>
+ #include <version.h>
+
+ #if ZEPHYR_VERSION_CODE >= ZEPHYR_VERSION(3,1,0)
+ #include <zephyr/posix/sys/socket.h>
+ #include <zephyr/posix/poll.h>
+ #else
+ #include <posix/sys/socket.h>
+ #include <posix/poll.h>
+ #endif
```

A `<version.h>` (Zephyr) ad `ZEPHYR_VERSION_CODE` és `ZEPHYR_VERSION` makrókat.

#### Hosszú távú megoldás

Upstream PR a `micro-ROS/micro_ros_zephyr_module`-ba: a UDP transport
header is használjon ugyanolyan `ZEPHYR_VERSION_CODE` guard-ot, mint
a serial transportok. Addig a lokális patch (BL-009, backlog) elegendő.

#### Érintett fájlok

| Fájl | Megjegyzés |
|------|-----------|
| `tools/patches/apply.sh` | Patch 1 logika |
| `workspace/modules/lib/micro_ros_zephyr_module/modules/libmicroros/microros_transports/udp/microros_transports.h` | patchelt, build után idempotens |

---

## ERR-026

### Architekturális döntés: W6100 → W5500 kompatibilis mód

| Mező | Érték |
|------|-------|
| **Súlyosság** | Informatív (nem hiba, dokumentált döntés) |
| **Állapot** | **Lezárva** — alkalmazva v2.2-ben |
| **Dátum** | 2026-04-19 |

#### Kontextus

A WIZnet W6100 chip a gyártó specifikációja szerint **visszafelé
kompatibilis a W5500-zal** IPv4 működésre (SPI regiszter szint, hardwired
TCP/IP stack). A W6100-specifikus funkciók (IPv6, hardveres SSL/TLS, dual
MAC) opcionálisak; a mi firmware-ünk egyiket sem használja (csak
`CONFIG_NET_IPV4=y` + UDP).

Eddig a board overlay felülírta a Zephyr DT stock `compatible` sztringet:

```dts
&ethernet {
    compatible = "wiznet,w6100";
};
```

és a `prj.conf` `CONFIG_ETH_W6100=y`-t használt.

#### Probléma

A Zephyr v4.2.2-ben (amin kényszerültünk a SDK 0.17.4 kompatibilitás miatt):

- `CONFIG_ETH_W6100` Kconfig symbol **nem létezik** (W6100 driver későbbi
  Zephyr release-ekben érkezett, SDK 1.0+-zal együtt).
- A `wiznet,w6100` DT binding sem létezik.

#### Döntés

A stock `w5500_evb_pico` board definíciót használjuk változtatás nélkül —
a W6100 chip W5500 kompatibilis módban fog üzemelni. Változások:

1. `app/prj.conf`: `CONFIG_ETH_W6100=y` → `CONFIG_ETH_W5500=y`
2. `app/boards/w5500_evb_pico.overlay`: az `&ethernet { compatible = ... }`
   blokk eltávolítva; a stock board-ban már `wiznet,w5500` a kompatibilis.

#### Veszteségek — **nulla** (funkcionálisan)

| Funkció | Használtuk? | Hatás a döntésre |
|---------|-------------|------------------|
| IPv6 (W6100 exkluzív) | Nem | 0 |
| Hardveres SSL/TLS | Nem | 0 |
| Dual MAC / IPRaw engine | Nem | 0 |

#### Nyereségek

- A W5500 driver évek óta stabil Zephyr-ben (~v2.x-től), kevesebb drift.
- Nincs overlay-hack: a stock board definíciót használjuk, upstream CI
  bármikor tud tesztelni minket.
- Szélesebb Zephyr upgrade-út (Zephyr újabb W5500 API-változásai kisebbek
  lesznek, mint W6100-é, amelyik új driver).
- Dokumentálható, tankönyvi döntés: „a chip W6100, de W5500-kompat módban
  driveljük, mivel a W6100-specifikus featurokra nincs szükségünk".

#### Visszaváltás feltétele

Ha valaha IPv6 vagy hardveres SSL/TLS kell, akkor:

- `app/prj.conf`: vissza `CONFIG_ETH_W6100=y`
- `app/boards/w5500_evb_pico.overlay`: overlay override visszaírás
- Zephyr verzió upgrade: a W6100 driver a Zephyr v4.3+ és SDK 1.0+ párossal
  érkezik; akkor a Docker base-t is emelni kell.

#### Érintett fájlok

| Fájl | Változás |
|------|----------|
| `app/prj.conf` | `CONFIG_ETH_W6100` → `CONFIG_ETH_W5500` |
| `app/boards/w5500_evb_pico.overlay` | `&ethernet` block törölve |

---

## ERR-025

### Zephyr SDK 0.17 → 1.0 drift, west `revision: main`

| Mező | Érték |
|------|-------|
| **Súlyosság** | Kritikus (firmware egyáltalán nem fordul) |
| **Állapot** | **Javítva** (west pin `v4.2.2`) |
| **Felfedezés** | 2026-04-19 |
| **Javítás** | 2026-04-19 |

#### Tünet

```
CMake Error at /workdir/zephyr/cmake/modules/FindZephyr-sdk.cmake:160:
  Could not find a configuration file for package "Zephyr-sdk" that is
  compatible with requested version "1.0".

  The following configuration files were considered but not accepted:
    /opt/toolchains/zephyr-sdk-0.17.4/cmake/Zephyr-sdkConfig.cmake,
    version: 0.17.4
```

A Docker image `zephyrprojectrtos/ci:v0.28.8` Zephyr SDK **0.17.4**-et
szállít. A `west init + west update` a Zephyr **main** branch aktuális
HEAD-jét húzta le (`v4.4.99`), ami `find_package(Zephyr-sdk 1.0)`
minimumot ír elő.

#### Root cause — két drift egyszerre

1. **`app/west.yml` `zephyr.revision: main`** — a branch HEAD driftel minden
   west update-tel. Ez a policy.md §2.8 („Pin mindent, ami driftelhet")
   direkt ellenkezője.
2. **Zephyr SDK minimum bump** — a v4.3.0 → v4.4.0 release közötti
   időszakban az upstream Zephyr emelte a minimum SDK verziót 0.17 → 1.0.

#### Kompatibilitási mátrix

| Zephyr tag | `SDK_VERSION` | `find_package(Zephyr-sdk X)` | SDK 0.17.4 OK? | `zephyr/posix/time.h` |
|------------|---------------|------------------------------|----------------|-----------------------|
| v4.1.0 | 0.17.0 | 0.16 | ✅ | ✅ |
| v4.2.1 | 0.17.4 | 0.16 | ✅ | ✅ |
| **v4.2.2** | **0.17.4** | **0.16** | **✅** | **✅** (utolsó!) |
| v4.3.0 | 0.17.4 | 0.16 | ✅ | ❌ (POSIX layout átrendezés) |
| v4.4.0 | 1.0.1 | 1.0 | ❌ | ❌ |

**v4.2.2** a sweet spot: az utolsó tag, ahol az SDK 0.17.4 kompat és a
`zephyr/posix/time.h` (amit a micro-ROS jazzy HEAD használ) egyaránt jó.

#### Javítás

`app/west.yml`:
```yaml
- name: zephyr
  remote: zephyrproject-rtos
  revision: v4.2.2          # volt: main
  import: true
```

#### Érintett fájlok

| Fájl | Változás |
|------|----------|
| `app/west.yml` | `zephyr.revision: main` → `v4.2.2` |
| `docs/backlog.md` | BL-001 lezárva, BL-009 új: patchek upstream PR |

---

## ERR-024

### SEGFAULT: RCLCPP_WARN_THROTTLE temporális Clock shared_ptr lifetime issue

| Mező | Érték |
|------|-------|
| **Súlyosság** | Kritikus |
| **Állapot** | **Javítva** |
| **Felfedezés** | 2026-03-10 (session 23h) |
| **Javítás** | 2026-03-10 (session 23h) |

#### Tünet

A `roboclaw` konténer váratlanul crashelt (Segmentation fault). A node nem reconnectelt — a konténert kézzel kellett újraindítani. A logban SEGFAULT jelent meg. Korábban TCP reconnect hibának tűnt, de a valódi ok a node crash volt.

#### Root cause

A `roboclaw_hardware.cpp` 455–457. sorában:

```cpp
// HIBÁS — segfault:
RCLCPP_WARN_THROTTLE(rclcpp::get_logger("RoboClawHardware"),
  *rclcpp::Clock::make_shared(), 2000,
  "Motor command failed -- check connection");
```

A `rclcpp::Clock::make_shared()` egy **temporális shared_ptr-t** hoz létre, ami a `RCLCPP_WARN_THROTTLE` makró kifejtése közben referenciát (`*`) kap. A makró belső `last_time` változója az első híváskor elmenti az `rclcpp::Clock&` referenciát, de a temporális objektum a kifejezés végén megsemmisül. A következő híváskor a tárolt referencia **dangling pointer** → **segfault**.

Ez nem minden híváskor történt meg — csak ha a `!ok` ág lefutott (motor command fail), ami jellemzően TCP reconnect közben fordult elő. Ezért tűnt úgy, mintha a reconnect logika lenne hibás.

#### Javítás

```cpp
if (!ok) {
  static rclcpp::Clock steady_clock(RCL_STEADY_TIME);
  RCLCPP_WARN_THROTTLE(rclcpp::get_logger("RoboClawHardware"),
    steady_clock, 2000,
    "Motor command failed -- check connection");
}
```

A `static rclcpp::Clock` a függvény első hívásakor jön létre és a process végéig él — nincs dangling reference.

#### Érintett fájl

| Fájl | Sor | Megjegyzés |
|------|-----|-----------|
| `host_ws/src/roboclaw_hardware/src/roboclaw_hardware.cpp` | 454–459 | `execute_velocity_command()` error path |

#### Tanulság

Az `RCLCPP_*_THROTTLE` makrók belsőleg referenciát tárolnak a Clock objektumra. **Soha ne adj nekik temporális objektumot** (`*make_shared()`, `Clock{}`). Használj `static` lokális változót vagy class member-t.

---

## ERR-023

### Folyamatos overrun 100Hz-en — WiFi latencia, nem szoftver hiba

**Dátum:** 2026-03-10
**Állapot:** **Megoldva**

A C++ ros2_control driver 100Hz-en folyamatosan overrunolt (read time 10-23ms a 10ms budget helyett).
Több megoldási kísérlet után (háttérszál + mutex, mutex szeparálás, yield/sleep, diagnosztika kikapcsolás)
derült ki, hogy az overrun nem szoftver eredetú.

**Izolációs teszt:** Diagnosztika thread teljes kikapcsolásával, *csak egyetlen GetEncoders* TCP hívással
a read time TOVÁBBRA IS 10-23ms volt — bizonyítva, hogy a probléma a hálózati rétegben van.

**Root cause:** A fejlesztő laptop WiFi-n csatlakozik a USR-K6-hoz (laptop → WiFi → router → switch →
switch → USR-K6). Ping mérés: avg **4.2ms**, max **9.3ms**. Egy GetEncoders round-trip kétszer megy
át ezen az útvonalon → **8-18ms WiFi overhead** per parancs.

**USR-K6 adatlap:** A konverter átlagos transport delay-e **< 10ms** (LAN-on 1-2ms), serial packing
delay 115200 baud-on **0.35ms**. Közvetlen Ethernet-en egy GetEncoders **~3ms** lesz.

**Megoldás:**
1. Háttérszál és mutex eltávolítva → egyszerűbb, single-threaded rotating diagnostics
2. Ethernet mérés: ping avg **1.4ms** (vs WiFi 4.2ms), 100Hz + teljes diag → **30s alatt 1 overrun** (10.2ms)
3. `update_rate: 100` megtartva — Ethernet-en a 100Hz működik teljes diagnosztikával

---

## ERR-018

### /robot/diagnostics nem érkezik meg ros2-shell / Foxglove felé

**Dátum:** 2026-03-10  
**Állapot:** **Javítva**

A roboclaw konténer logja szerint a node fut és publikálja a diagnostics-t; a discovery működött, de `ros2 topic echo` nem kapott adatot. **Root cause:** Fast DDS SHM transport Docker konténerek között nem működik (külön IPC namespace). **Javítás:** `FASTDDS_BUILTIN_TRANSPORTS=UDPv4` env var minden ROS2 konténerhez.

_(Részletes debug történet: a roboclaw konténer logja szerint a node fut és másodpercenként lefut a `publish_diagnostics`; a `ros2 topic info /robot/diagnostics -v` 1 publishernél és 1 subscribernél mutatja a topicot (típus, QoS rendben). Azonban `ros2 topic echo /robot/diagnostics` (ros2-shell-ben vagy hoston) és a Foxglove **nem kap üzenetet** — a topic „létezik”, de a payload nem jön át. Feltételezés: DDS discovery / partíció különbség a konténerek között (roboclaw vs ros2-shell / Foxglove). **Kísérleti javítás (22):** minden ROS2 konténerhez `ROS_DOMAIN_ID=0`, cyclonedds.xml-ben `<Domain id="0">`; teszt: `make robot-diagnostics-echo` (echo a roboclaw konténeren belül — ha itt jönnek az üzenetek, a publish működik). **Eredmény:** roboclaw konténerben az echo kapta az üzeneteket, ros2-shell-ben nem → a publish rendben, a gond a két konténer közötti adatátvitel. **Kísérleti javítás (22b):** CycloneDDS shared memory kikapcsolva (`<SharedMemory><Enable>false</Enable></SharedMemory>`), mert konténerekenként külön `/dev/shm` van, így a shm forgalom nem jut át; kényszerítve az UDP használatát.)_

---

## ERR-017

### RoboClaw Docker: No module named 'basicmicro_driver'

**Dátum:** 2026-03-09  
**Állapot:** **Javítva**

A basicmicro_ros2 csomagot a colcon **CMake**-kel építi. A CMakeLists.txt a `basicmicro_driver/*.py` scripteket **programként** (executables) telepíti a `lib/basicmicro_ros2/` alá, **de nem Python csomagként** (nincs `__init__.py`). A `basicmicro_driver` Python package (`__init__.py` + összes modul) kizárólag a **forrásban** van: `host_ws/src/basicmicro_ros2/basicmicro_driver/`. A `lib/python3.12/site-packages/basicmicro_ros2/` csak a rosidl-generált msg/srv típusokat tartalmazza.

**Javítás:** A Docker run scriptek PYTHONPATH-jába a **forrás** könyvtárat tesszük: `/host_ws/src/basicmicro_ros2`, így a `from basicmicro_driver.basicmicro_node import main` megtalálja a `basicmicro_driver` csomagot. A `roboclaw_tcp_node.py` importja egyszerűsítve erre az egy útvonalra.

---

## ERR-015

### RoboClaw Docker: No module named 'serial'

**Dátum:** 2026-03-09  
**Állapot:** **Javítva**

A `roboclaw_tcp_node` a `basicmicro` csomagot használja, ami `import serial`-t hív (pyserial). A ros:jazzy alapimage-ben nincs telepítve a pyserial / python3-serial, ezért a konténerben a node induláskor ModuleNotFoundError-t dobott.

**Javítás:** A `docker-run-roboclaw.sh` és `docker-run-ros2.sh` indulásakor `apt-get install -y python3-serial` (csendesen), így a konténerben elérhető a `serial` modul.

---

## ERR-016

### safety_bridge_node: RcutilsLogger.info() TypeError

**Dátum:** 2026-03-09  
**Állapot:** **Javítva**

Az rclpy `RcutilsLogger.info()` csak egy üzenetstringet fogad (plusz implícit self), nem printf-stílusú (msg, *args) hívást. A kód `self.get_logger().info("Safety bridge: %s -> ...", a, b, c, d)` formátumban hívta, ami "takes 2 positional arguments but 6 were given" hibát adott.

**Javítás:** Egyetlen formázott stringre cserélve: `self.get_logger().info("... %s ..." % (a, b, c, d))`. A `.warn("EMERGENCY STOP: %s", reason)` ugyanígy: `"... %s" % reason`.

---

## ERR-001

### `param_server_init error: 11`

**Dátum:** 2026-03-06
**Állapot:** Diagnosztika aktív (BL-004 lezárva 2026-04-19) — a hiba nem fatális, a heap statisztika logolás be van építve; várunk egy tényleges `error: 11` reprodukcióra az élő board-okon, hogy az új `Heap before/after param_server` logok alapján eldönthető legyen `RCL_RET_BAD_ALLOC` vs. `RCL_RET_INVALID_ARGUMENT`.

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
| `app/src/bridge/param_server.c` | A hiba itt jelentkezik; heap stat log hozzáadva `param_server_init` elejére/végére (`CONFIG_SYS_HEAP_RUNTIME_STATS` gatolt) |
| `app/src/main.c` | Executor handle count — rendben |

#### Diagnosztikai logok (BL-004)

`CONFIG_SYS_HEAP_RUNTIME_STATS=y` a `prj.conf`-ban, ezért `param_server_init()` most az `rclc_parameter_server_init_with_option` hívása **előtt és után** logolja a heap állapotot:

```
<inf> param_server: Heap before param_server: free=... alloc=... max=...
<inf> param_server: Heap after param_server:  free=... alloc=... max=...
```

Ha a következő reprodukció során `error: 11` jön, és a heap `free` érték drasztikusan csökkent a két log között (vagy a hívás előtt is < 1 KB), akkor a hiba `RCL_RET_BAD_ALLOC` eredetű (heap szűkösség). Ellenkező esetben valószínűbb az `RCL_RET_INVALID_ARGUMENT` — akkor egyesével kell kizárni a 6 belső service init hívást (az rclc forrásból).

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

---

## ERR-019

### Motor szaggatottan forog ROS2 alatt (safety_bridge watchdog + bus contention)

| Mező | Érték |
|------|-------|
| **Súlyosság** | Kritikus |
| **Állapot** | **Javítva** (C++ ros2_control) |
| **Felfedezés** | 2026-03-09 (session 22) |
| **Javítás** | 2026-03-10 (session 23) |

#### Tünet

A motor ROS2-n keresztül szaggatottan, rángatva forgott, miközben a standalone Python teszt (`tools/test_motor_duty.py`) simán futott. A log tele volt `EMERGENCY STOP: E-Stop topic SILENT for >2.0s` üzenetekkel.

#### Root cause

Három egymást erősítő hiba:

1. **safety_bridge watchdog:** A `safety_bridge_node` 2 másodpercenként zero `cmd_vel`-t küldött, mert a `/robot/estop` topicra senki nem publisholt (a Pico E-Stop board nem volt aktív). Ez felülírta a tényleges motor parancsokat.

2. **Bus contention:** Az eredeti Python driver egyszálú rclpy executorral futott, ahol a 50Hz sensor read timer, a diagnostic burst, és a cmd_vel callback mind ugyanazon a TCP kapcsolaton versengett. A soros jellegű kommunikáció (RoboClaw response timeout) blokkolta az egész rendszert.

3. **Strukturális probléma:** A Python monkey-patch megoldás nem volt determinisztikus — nem volt garantált sorrendje a read/write ciklusnak, és a safety_bridge egy külön node-ként a ROS2 topic szinten avatkozott be (nem a hardware szinten).

#### Javítás

Teljes C++ újraírás ros2_control `SystemInterface` pluginként (`ROS2_RoboClaw` repo, `roboclaw_hardware` package):

- **Determinisztikus loop:** A `controller_manager` 100Hz-es ciklusban hívja a `read()` és `write()` függvényeket — nincs bus contention.
- **Beépített timeout:** A `diff_drive_controller` `cmd_vel_timeout: 0.5s` paramétere pótolja a safety_bridge watchdogot.
- **Hardveres timeout:** A RoboClaw saját `SetTimeout(500ms)` az utolsó védelmi réteg.
- **Egyetlen TCP kapcsolat:** Nincs versengés — a read() és write() sorrendben fut egyetlen szálon.

#### Ellenőrzés — **SIKERES** (2026-03-10 session 23b)

1. ✅ `make host-build-roboclaw-hw` — tiszta build, 20/20 unit teszt zöld
2. ✅ `make robot-hw-start` — TCP connect OK, `USB Roboclaw 2x60A v4.4.3` azonosítva
3. ✅ Motor teszt: `cmd_vel 0.05 m/s` → **sima, rángatásmentes forgás** (az eredeti standalone scripttel megegyező minőség)
4. ✅ Encoder read: mindkét csatorna él, 100Hz-en folyamatos adat
5. ✅ 100Hz loop: 99% ciklus belefér 10ms-be (write-on-change + velocity delta optimalizálás)

---

## ERR-020

### SetTimeout 4 byte-ot küldött 1 helyett → protokoll korrupció, ERR LED

| Mező | Érték |
|------|-------|
| **Súlyosság** | Kritikus |
| **Állapot** | **Javítva** |
| **Felfedezés** | 2026-03-10 (session 23c) |
| **Javítás** | 2026-03-10 (session 23c) |

#### Tünet

A RoboClaw ERR LED világított a driver futása közben. A motor működött, de a vezérlő hibás állapotban volt.

#### Root cause

A RoboClaw command 14 (SET_TIMEOUT) **egyetlen byte-ot** vár 10ms egységekben (0-255). A C++ implementáció `send_long()` -gal **4 byte-ot** küldött. A RoboClaw az első byte-ot timeout-ként, a maradék 3-at a **következő parancs elejének** értelmezte → protokoll szinkronizáció elveszett → ERR LED.

Összehasonlítás az eredeti Python driver-rel:
- Python (helyes): `self._write(address, CMD, int(timeout * 10), types=["byte"])` → 1 byte
- C++ (hibás): `send_long(timeout_ms)` → 4 byte

#### Javítás

```cpp
uint8_t val = static_cast<uint8_t>(std::min(timeout_ms / 10u, 255u));
send_byte(val);  // 500ms → byte 50 (= 500ms)
```

---

## ERR-021

### Motor nem állt le cmd_vel timeout után (PID + lebegő encoder)

| Mező | Érték |
|------|-------|
| **Súlyosság** | Kritikus |
| **Állapot** | **Javítva** |
| **Felfedezés** | 2026-03-10 (session 23c) |
| **Javítás** | 2026-03-10 (session 23c) |

#### Tünet

A motor a `cmd_vel` timeout (500ms) letelte után is folyamatosan forgott. Csak a container leállításával állt meg.

#### Root cause

Két egymást erősítő probléma:

1. **SpeedAccelM1M2(0, 0) nem állította le a motort:** A RoboClaw belső PID-je `Speed` módban az encoder-ből ellenőrzi, hogy elérte-e a cél sebességet. Lebegő (nem motorra rögzített) encoderek elektromos zajt generálnak → a PID "mozgást" lát → korrekciót küld → a motor forog → végtelen kör.

2. **A serial timeout nem lépett működésbe:** A RoboClaw serial timeout (SetTimeout) BÁRMELY parancsra resetelődik, nem csak motor parancsra. A 100Hz-es `GetEncoders` olvasásunk folyamatosan resetelte → a timeout soha nem járt le.

#### Javítás

A `execute_velocity_command()` metódusban: ha a cél sebesség (0, 0), mindig `DutyM1M2(0, 0)`-t használunk `SpeedAccelM1M2(0, 0)` helyett. A `DutyM1M2` közvetlen PWM parancs — megkerüli a PID-et, nem használ encoder visszacsatolást, azonnali leállást garantál.

---

## ERR-022

### Motor induláskor forgott (cmd_vel_dirty + open_loop: false)

| Mező | Érték |
|------|-------|
| **Súlyosság** | Magas |
| **Állapot** | **Javítva** |
| **Felfedezés** | 2026-03-10 (session 23c) |
| **Javítás** | 2026-03-10 (session 23c) |

#### Tünet

A motor a driver indulásával egyidejűleg elkezdett forogni, anélkül hogy bármilyen cmd_vel-t publikáltunk volna.

#### Root cause

Két probléma együttese:

1. **`open_loop: false`** (diff_drive_controller zárt hurkú módban): A lebegő encoderek elektromos zajt generálnak, amit a controller valós mozgásként értelmez → korrekciós motorparancsokat küld.

2. **`cmd_vel_dirty_ = true`** (kényszerített első write): Az on_activate utáni első write() ciklusban mindenképp küldött motorparancsot, ami a diff_drive_controller transient értékeit (zajos encoder-ből) azonnal végrehajtotta.

#### Javítás

Három módosítás:

1. **`open_loop: true`** a diff_drive_controller-ben (amíg encoderek nincsenek motorra rögzítve)
2. **Explicit `DutyM1M2(0, 0)`** az `on_activate`-ban, mielőtt a control loop elindul
3. **`cmd_vel_dirty_ = false`** alapértelmezés — az első write csak akkor megy ki, ha a controller ténylegesen non-zero sebességet kér

---

## ERR-030

### W6100 chip nem válaszolt SPI-n (CIDR=0x00, minden regiszter 0x00)

| Mező | Érték |
|------|-------|
| **Súlyosság** | Kritikus (Ethernet nem elérhető → nincs micro-ROS kapcsolat) |
| **Állapot** | **Javítva** (out-of-tree W6100 driver: `app/modules/w6100_driver/`) |
| **Felfedezés** | 2026-04-18 (BL-010) |
| **Javítás** | 2026-04-19 |
| **Komponens** | Zephyr v4.2.2 W5500 driver használata W6100 EVB Pico-n |

#### Tünet

```
<inf> eth_w5500: Chip probe: [0x00]=0x00 [0x02]=0x00 [0x03]=0x00
<err> eth_w5500: Unable to read RTR register
<err> eth_w5500: Reset failed
```

A W5500 driver — próba patch-ekkel (Patch 3/4/5) W6100-detektáló logikával kiegészítve — sem volt képes a W6100 chipet kommunikációra bírni: minden regiszter olvasás 0x00-át adott vissza (CIDR, VER, RTR), pedig az SPI-fizika és a DT (GPIO 17 CS, GPIO 20 RESET, GPIO 21 INT, 8 MHz) helyesen volt bekötve.

#### Root cause

Két kritikus protokoll-eltérés a W6100 és a W5500 init szekvenciájában, amit a W5500 driver nem tartalmaz:

1. **Hiányzó reset pulzus.** A W6100 datasheet szerint a chip bootoláshoz aktív reset-pulzus kell (`T_RST ≥ 2 µs` aktív, majd `T_STA ≤ 100 ms` stable). A W5500 driver csak _elengedi_ a reset vonalat (GPIO inaktív állapotra), nem pulzál. A W6100 így soha nem lép ki a power-on állapotból, és a BSP-je nem inicializálódik.
2. **Hiányzó CHPLCKR/NETLCKR unlock.** A W6100 védelme: a Common Block (SYCR0, RTR stb.) és a Network Block (SHAR, GAR stb.) írása csak `CHPLCKR=0xCE` / `NETLCKR=0x3A` unlock után lehetséges. Reset után mindkét zár zárt, így **minden írás eldobódik**, olvasáskor pedig a chip 0x00-t ad (a W5500-zal ellentétben, ahol ilyen lock mechanizmus nincs).

A BL-010-ben felírt eredeti hipotézis (**„W6100 SPI protokoll nem kompatibilis a W5500-zal"**) **téves** volt — az SPI keret (3-byte header, BSB<<3, R/W bit) teljesen megegyezik. Az eltérés kizárólag a regiszter-térképben (SHAR, PHYSR, RTR címek) és a bekapcsolási szekvenciában van.

#### Javítás — out-of-tree W6100 driver backport

A Zephyr upstream PR #101753 (W6100 natív driver) backportolva v4.2.2-re, az alkalmazásfa alatt, out-of-tree Zephyr modulként.

| Fájl | Funkció |
|------|--------|
| `app/modules/w6100_driver/zephyr/module.yml` | modul regisztráció (cmake + kconfig path) |
| `app/modules/w6100_driver/zephyr/CMakeLists.txt` | source fordítási szabály |
| `app/modules/w6100_driver/zephyr/Kconfig` | `rsource "../drivers/ethernet/Kconfig.w6100"` |
| `app/modules/w6100_driver/zephyr/dts/bindings/ethernet/wiznet,w6100.yaml` | DT binding (`compatible: "wiznet,w6100"`) |
| `app/modules/w6100_driver/drivers/ethernet/eth_w6100.c` | driver (reset pulzus + CHPLCKR/NETLCKR unlock + natív register map) |
| `app/modules/w6100_driver/drivers/ethernet/eth_w6100_priv.h` | register címek, config/runtime struct |
| `app/modules/w6100_driver/drivers/ethernet/Kconfig.w6100` | `CONFIG_ETH_W6100` + thread paraméterek |

A v4.2.2 adaptáció három minimál változtatást igényelt az upstream (Zephyr main) forráshoz képest:

- `net_eth_mac_load()` + `NET_ETH_MAC_DT_INST_CONFIG_INIT()` → eltávolítva (v4.3.0-ban bevezetett API); helyettük W5500-mintás runtime `.mac_addr = DT_INST_PROP(0, local_mac_address)` inicializálás `COND_CODE_1(DT_NODE_HAS_PROP(…, local_mac_address), …, ())` őrzéssel
- `NET_AF_UNSPEC` → `AF_UNSPEC` (v4.3.0 alias)
- `SPI_DT_SPEC_INST_GET(inst, SPI_WORD_SET(8))` → `SPI_DT_SPEC_INST_GET(inst, SPI_WORD_SET(8), 0)` (v4.2.2 3-argumentumos API)

A board overlay (`app/boards/w5500_evb_pico.overlay`) `&ethernet { compatible = "wiznet,w6100"; };` hozzáfűzéssel átírja a stock board def `wiznet,w5500` compatible-ját. `prj.conf`: `CONFIG_ETH_W5500=n` + `CONFIG_ETH_W6100=y`. A korábbi Patch 3/4/5 (W5500 driver W6100-kompatibilitási hack) eltávolítva `tools/patches/apply.sh`-ból.

#### Érintett fájlok

| Fájl | Megjegyzés |
|------|-----------|
| `app/modules/w6100_driver/…` | új modul (7 fájl) |
| `app/boards/w5500_evb_pico.overlay` | `compatible = "wiznet,w6100"` override |
| `app/prj.conf` | W5500 → W6100 config váltás |
| `app/CMakeLists.txt` | `ZEPHYR_EXTRA_MODULES` kiegészítése a modul útjával |
| `tools/patches/apply.sh` | Patch 3/4/5 eltávolítása |

#### Verifikáció

```
<inf> eth_w6100: W6100 Initialized
<inf> eth_w6100: w5500@0 MAC set to 0c:2f:94:30:58:11
<inf> eth_w6100: w5500@0: Link up
<inf> eth_w6100: w5500@0: Link speed 10 Mb, half duplex
<inf> main: Ethernet link UP
<inf> main: Network: static IP 192.168.68.200
```

Fizikai link, MAC konfiguráció, PHY státusz, IP stack mind zöld. BL-010 lezárva.

---

## ERR-029

### W5500 driver RTR register check -ENODEV a W6100 chipen — superseded

| Mező | Érték |
|------|-------|
| **Súlyosság** | Elavult (nincs kitéve production-ban) |
| **Állapot** | **Elavult — ERR-030 superseded** (W5500 driver nincs használatban) |
| **Felfedezés** | 2026-04-18 |
| **Elavulás** | 2026-04-19 (ERR-030 javítással) |

#### Történelem

Az ERR-029 a `tools/patches/apply.sh` Patch 3-ához tartozott: a W5500 driver init során az RTR regiszter (0x0019) eltérő default értékét a W6100 compat-ban `return -ENODEV`-val kezelte. A fix warning-ot adott és az elvárt 2000-t visszaírta. Ez a patch ERR-030 javítás miatt elveszítette értelmét (a W5500 driver CONFIG_ETH_W5500=n miatt nem fordul le), és a Patch 3-at eltávolítottuk `apply.sh`-ból.

---

## ERR-031

### W6100 `set_config(MAC_ADDRESS)` nem frissíti az iface link_addr-t — MACRAW keret src MAC beragad

| Mező | Érték |
|------|-------|
| **Súlyosság** | Kritikus (host→Pico return path teljesen blokkolt) |
| **Állapot** | **Javítva** (`net_if_set_link_addr` hívás hozzáadva) |
| **Felfedezés** | 2026-04-19 (BL-010 end-to-end teszt során) |
| **Javítás** | 2026-04-19 |
| **Komponens** | `app/modules/w6100_driver/drivers/ethernet/eth_w6100.c` |

#### Tünet

A PEDAL firmware boot után:
- Soros log: `MAC set to 0c:2f:94:30:58:11` (hwinfo-alapú egyedi MAC) — helyesnek tűnik.
- Agent log: folyamatosan fogad UDP CREATE_CLIENT csomagokat (`client_key: 0x00000000`), válaszol XRCE STATUS reply-jel, de az új `client_key` soha nem jelenik meg a bejövő csomagokban → session nem épül fel.
- Host oldal: `ping 192.168.68.200` 100% loss.
- `ip neigh show 192.168.68.200` a chip *régi/default* MAC-jét mutatja (`00:00:00:01:02:03`), **nem** a `0c:2f:94:30:58:11`-et.

#### Root cause

A W6100 driver **MACRAW módban** használja a socket 0-t — ez azt jelenti, hogy a Zephyr L2 Ethernet réteg építi a teljes kimenő Ethernet keretet (dst MAC + src MAC + ethertype + payload), és a chipnek a kész keretet adja át. A **src MAC-et a Zephyr az `iface->link_addr`-ból olvassa**, nem a W6100 SHAR regiszterből.

A `w6100_set_config(ETHERNET_CONFIG_TYPE_MAC_ADDRESS)` helyesen frissítette a `ctx->mac_addr`-t és SHAR-t (CHPLCKR/NETLCKR unlock-lock sorozattal), **de nem hívta** `net_if_set_link_addr`-et. Az iface link_addr a `w6100_iface_init`-ben beállított kezdő értéken maradt (a DT binding else-ágán, ha nincs `local-mac-address` property, `{0}`-s vagy implicit default).

A kimenő ARP reply és minden UDP/ICMP válasz ezért a **régi src MAC-kel** ment ki. A host ARP cache ezt a régi MAC-et asszociálta az IP-hez, és onnantól minden visszairányú csomagot erre a MAC-re küldött — amit a chip (már `0c:2f:94:30:58:11` SHAR-rel) **nem fogadott el**.

#### Javítás

`app/modules/w6100_driver/drivers/ethernet/eth_w6100.c` — a SHAR write után:

```c
if (ctx->iface != NULL) {
    net_if_set_link_addr(ctx->iface, ctx->mac_addr,
                         sizeof(ctx->mac_addr),
                         NET_LINK_ETHERNET);
}
```

A hívás az `apply_mac_address()`-ből történik, amikor az iface még `net_if_carrier_off` állapotban van (PHY link még nem UP) → `NET_IF_RUNNING` flag nem áll, a `net_if_set_link_addr` nem ad `-EPERM`-et.

#### Verifikáció (2026-04-19)

- `ip neigh show 192.168.68.200` → `lladdr 0c:2f:94:30:58:11 REACHABLE`
- `ping -c 3 192.168.68.200` → 0% loss, ~4.3 ms RTT
- Agent log: `client_key: 0x5496464D` (valid session)
- `ros2 node list` → `/robot/pedal` megjelent
- `ros2 topic hz /robot/heartbeat` → 1.00 Hz stabil (`std_msgs/msg/Bool`, `data: false`)

#### Megjegyzés

Az upstream Zephyr main PR #101753 (amiből a drivert backportoltuk) ugyanezzel a bug-gal rendelkezik. A javítás upstreambe küldése opcionális — itt lokálisan tartjuk a v4.2.2-re adaptált driverben.
