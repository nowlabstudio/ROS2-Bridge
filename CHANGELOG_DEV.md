# Fejlesztési napló — W6100 EVB Pico micro-ROS Bridge

Folyamatos haladáskövetés. Minden munkamenet változásai időrendben.

---

## 2026-03-10 (23e) — RC → Motor összekötés terv dokumentálása

### RC csatorna kiosztás (végleges)

| Csatorna | Funkció |
|----------|---------|
| CH1 | Jobb motor (tank) |
| CH2 | Bal motor (tank) |
| CH3 | Nincs bekötve |
| CH4 | Nincs bekötve |
| CH5 | ROS/RC mode switch — azonnali átváltás, safety funkció |
| CH6 | Winch (később) |

### Döntés: tank RC → cmd_vel konverzió

A távirányító tank módban ad jelet (CH1 = jobb motor, CH2 = bal motor). Az RC jeleket
a `rc_teleop_node` konvertálja arcade formátumra (`cmd_vel`), így:

- **RC mód (CH5 > 0):** rc_teleop_node → `/diff_drive_controller/cmd_vel`
- **ROS mód (CH5 ≤ 0):** Nav2 → `/diff_drive_controller/cmd_vel`, rc_teleop_node hallgat

CH5 RC módra váltás **azonnal** leválasztja a ROS-t és átveszi az irányítást (safety).

### Tank → Arcade formula

```
linear.x  = (ch2 + ch1) / 2.0 * max_speed      # ch2=bal, ch1=jobb
angular.z = (ch2 - ch1) / 2.0 * max_angular     # ch2>ch1 → balra fordul
```

### Dokumentáció frissítés

- **ONBOARDING.md** 2. szekció: teljes architektúra diagram (csatorna táblázat, adatfolyam,
  ROS/RC mode switch leírás, Nav2 autonóm mód)
- **ONBOARDING.md** 7b. szekció: RC→Motor összekötés terv (topic huzalozás, paraméterek,
  5 szintű safety réteg, Foxglove debug panelek)

### Implementáció TODO

- [ ] `rc_teleop_node.py` — `mixing_mode=tank` + CH5 mode switch + TwistStamped kimenet
- [ ] `roboclaw.launch.py` — rc_teleop_node spawner hozzáadása
- [ ] Foxglove layout mentése az ajánlott panelekkel

---

## 2026-03-10 (23d) — Architektúra refaktor: 100Hz→50Hz, thread→rotating diag, WiFi latencia azonosítás

### Probléma: folyamatos overrun 100Hz-en

A C++ driver 100Hz update rate mellett **minden másodpercben többször overrunolt** (read time 10-23ms a 10ms budget helyett).
Három egymásra épülő megközelítéssel próbáltuk megoldani:

1. **Háttérszál (diag_thread + protocol_mutex_):** A diagnosztikát külön thread-be tettük. Eredmény: rosszabb,
   mert a `std::mutex` nem fair — a diag thread 4 TCP parancsra egyben tartotta a lock-ot (~20-40ms),
   a RT loop várt. Szeparált lock-ok + `yield()` + `sleep_for(2ms)` sem segített eléggé.

2. **Diagnosztika teljes kikapcsolása (izolációs teszt):** *Csak GetEncoders, semmi más* — az overrun
   TOVÁBBRA IS fennállt (read 10-23ms). Ez bizonyította: nem a mutex/thread a gond, hanem maga a
   TCP round-trip.

3. **Root cause azonosítás: WiFi latencia.** A fejlesztő laptop WiFi-n csatlakozik a USR-K6-hoz
   (laptop → WiFi → router → switch → switch → USR-K6). Ping mérés: **avg 4.2ms, max 9.3ms**.
   Egy GetEncoders kétszer megy át a WiFi-n (oda+vissza) → **8-18ms csak WiFi overhead**.
   A roboton közvetlen Ethernet kapcsolat lesz → ~0.5ms RTT → GetEncoders **~3ms**.

### USR-K6 adatlap megerősítés

| Paraméter | Érték |
|-----------|-------|
| Serial packing delay (115200 baud) | 0.35ms (4 byte idle time) |
| Max package length | 400 byte |
| Átlagos transport delay | < 10ms (LAN-on jellemzően 1-2ms) |
| Baud rate (K6 és RoboClaw) | 115200 (max: 460800) |

### Architekturális döntés

Az eredeti Python driver stratégiáját követjük — szétválasztott motor control és diagnostics,
de a végleges Ethernet mérés alapján **100Hz-re emeltük vissza** a loop rate-et:

| | Eredeti Python | Előző C++ (thread) | Végleges C++ (rotating) |
|--|---------------|--------------------|--------------------|
| Motor control rate | 50Hz | 100Hz | **100Hz** |
| Diagnostics rate | 1Hz (burst) | 4Hz (250ms thread) | **25Hz** (rotating, minden ciklus) |
| Thread/mutex | Nincs (ROS2 timer) | Igen (2 thread, 2 mutex) | **Nincs** |
| TCP hívás/ciklus | 2 (enc+speed) | 1 (enc) + háttér | **2** (enc + 1 diag) |

### Változtatások

**roboclaw_hardware.hpp:**
- Eltávolítva: `<atomic>`, `<mutex>`, `<thread>` include-ok
- Eltávolítva: `DiagShadow` struct, `diag_thread_`, `diag_mutex_`, `diag_shadow_`, `diag_running_`, `protocol_mutex_`
- Hozzáadva: `diag_slot_`, `diag_cycle_counter_`, `kDiagSlotCount=4`, `kDiagIntervalCycles=1`
- `diag_thread_func()` → `read_one_diagnostic()`

**roboclaw_hardware.cpp:**
- `diag_thread_func()` teljes eltávolítás
- `read()`: mutex lock eltávolítva, rotating diag hozzáadva (minden ciklusban 1 TCP diag read)
- `write()`: mutex lock eltávolítva
- `on_activate()`: thread indítás eltávolítva, `diag_slot_` és `diag_cycle_counter_` init
- `on_deactivate()`: thread join eltávolítva
- `read_one_diagnostic()`: 4-slot switch (volts/temps/error/currents), ciklikusan forgat

**diff_drive_controllers.yaml:**
- `update_rate: 100` (változatlan — az Ethernet mérés igazolta)

### Ethernet mérés eredménye (végleges)

A laptop kábeles Ethernet-re kötve, közvetlen hálózati útvonal a USR-K6-hoz:

| Mérés | WiFi | Ethernet |
|-------|------|----------|
| Ping RTT (avg / max) | 4.2ms / 9.3ms | **1.4ms / 1.9ms** |
| Overrun / 30s (100Hz, teljes diag) | ~30-50 | **1** (10.2ms, alig a határ felett) |

- GetEncoders + 1 diag: ~6ms Ethernet-en
- 100Hz budget: 10ms → **4ms tartalék**
- Diagnosztika: 4 slot × 100Hz = **25Hz** teljes frissítés (25x jobb mint az eredeti Python 1Hz)
- Overrun: **gyakorlatilag nulla** Ethernet-en

### Tanulság

A fejlesztési környezet (WiFi) nem reprezentálja a végleges topológiát (Ethernet).
A WiFi RTT (avg 4.2ms) többszörösen meghaladja az Ethernet-ét (1.4ms).
Az overrunok nem szoftver, hanem hálózati latencia eredetűek.

---

## 2026-03-10 (23) — C++ ros2_control RoboClaw hardware interface (ROS2_RoboClaw)

### Döntés
A Python driver javítása helyett teljes C++ újraírás ros2_control pluginként.
A régi Python rendszer (monkey-patch + safety_bridge) hibái (ERR-019 szaggatott motor)
strukturálisak — a ros2_control determinisztikus 100Hz loopja megoldja.

### Új repo
- **ROS2_RoboClaw** — önálló git repo, submodule-ként `host_ws/src/roboclaw_hardware`
- Négyrétegű architektúra:
  1. `RoboClawTcp` — POSIX TCP socket (TCP_NODELAY, 50ms timeout, flush, reconnect)
  2. `RoboClawProtocol` — CRC16 lookup table, 20 parancs, 3x retry
  3. `UnitConverter` — encoder counts <-> radians/m/s, overflow védelem
  4. `RoboClawHardware` — ros2_control SystemInterface, lifecycle management

### Funkciók (21 + 2 stub)
- **Lifecycle:** on_init (URDF param extraction), on_configure (TCP connect, controller detect), on_activate (state init, SetTimeout), on_deactivate (motor stop)
- **read/write:** GetEncoders @ 100Hz + velocity delta-ból, write-on-change (keepalive-al), 4 motion stratégia (duty, duty_accel, speed, speed_accel)
- **Diagnostics:** rotating 1Hz (4 slot × 25 ciklus: volts, temps, error, currents)
- **Position:** absolute position, distance command, servo errors
- **Stubs:** configure_servo_parameters, perform_auto_homing

### Konfiguráció
- `diff_drive_controllers.yaml`: 100Hz, 4.5 m/s max (~16 km/h), cmd_vel_timeout=0.5s
- URDF xacro: TCP host/port paraméterek, wheel_radius=0.2, gear_ratio=16.0
- Launch: controller_manager + diff_drive_controller + joint_state_broadcaster

### Monorepo integráció
- Submodule: `host_ws/src/roboclaw_hardware`
- docker-compose: `roboclaw-hw` service (ros2control profile)
- Makefile: `host-build-roboclaw-hw`, `robot-hw-start`, `robot-hw-stop`, `robot-hw-logs`, `robot-hw-motor-test`

### Tesztek
- `test_unit_converter.cpp` — matek, overflow, gear ratio
- `test_protocol_crc.cpp` — CRC16 table vs reference, buffer interpretation

### Teljesítmény-optimalizálás (session 23b — HW teszt)

TCP-over-serial latency (~7ms/parancs) miatt három optimalizálás:

1. **GetSpeeds eliminálás** — velocity encoder pozíció-deltából (saves ~8ms/ciklus)
2. **Write-on-change** — motor parancs csak ha cmd_vel változott (Write: 6-9ms → 4-6µs)
3. **Rotating diagnostics** — 4 slot × 25 ciklus = 1Hz teljes frissítés (burst helyett)

Eredmény 100Hz-en: normál ciklusok 85%-a belefér 10ms-be (1 TCP hívás ~7ms).

### Hardware teszt eredmények

- **Build**: tiszta, 0 hiba, 20/20 unit teszt zöld
- **TCP connect**: 192.168.68.60:8234 — `USB Roboclaw 2x60A v4.4.3` azonosítva
- **Motor teszt**: 0.05 m/s `cmd_vel` → **sima, rángatásmentes forgás** ✅
- **Encoder read**: mindkét csatorna él, 100Hz-en folyamatos adat ✅
- **Overrun**: 99% ciklus belefér 10ms-be, alkalmi 10-14ms TCP jitter overrun
- **docker-compose YAML fix**: `>` block scalar extra indent → csomagneveket parancsként futtatott

### Javított fájlok (docker-compose, launch)
- `docker-compose.yml` — roboclaw-hw command YAML formázás javítás
- `launch/roboclaw.launch.py` — `ParameterValue(value, value_type=str)` Jazzy fix

### Nyitott pontok
- [ ] GitHub remote beállítása az ROS2_RoboClaw repohoz
- [ ] Encoder rögzítése motortengelyre → odometria validálás → `open_loop: false` visszaváltás
- [x] ~~GPIO diagnostics state interfaces (URDF deklarációval)~~ → kész (session 23c)
- [x] ~~Makefile `robot-hw-motor-test` frissítés TwistStamped-re (Jazzy)~~ → kész
- [ ] Overrun csökkentés vizsgálata (diagnosztika intervallum / 50Hz)

---

## 2026-03-10 (23c) — Protokoll bugfixek, motor stop safety, GPIO diagnostics

### Kritikus bugfixek

1. **ERR-020: SetTimeout protokoll korrupció** — `send_long()` (4 byte) → `send_byte()` (1 byte).
   A RoboClaw CMD 14 egyetlen byte-ot vár 10ms egységben. A 3 extra byte elrontotta a
   protokoll szinkronizációt → ERR LED. Javítás: `send_byte(timeout_ms / 10)`.

2. **ERR-021: Motor nem állt le cmd_vel timeout után** — `SpeedAccelM1M2(0,0)` a RoboClaw
   belső PID-jét használja, ami az encoder-ből ellenőriz. Lebegő encoder → PID végtelen
   korrekciós loop. Javítás: velocity=0 esetén mindig `DutyM1M2(0,0)` (PWM stop, PID bypass).

3. **ERR-022: Motor induláskor forgott** — `cmd_vel_dirty_ = true` + `open_loop: false` +
   zajos encoder → diff_drive_controller korrekciós parancsot küldött. Javítás:
   - `on_activate`: explicit `DutyM1M2(0,0)` motor stop
   - `cmd_vel_dirty_ = false` alapértelmezés
   - `open_loop: true` amíg encoder nincs motorra rögzítve

### GPIO diagnostics state interfaces

- URDF: `<gpio name="diagnostics">` blokk 5 state interface-szel
- `export_state_interfaces()`: GPIO regisztráció (`main_battery_v`, `temperature_c`, `error_status`, `current_left_a`, `current_right_a`)
- Dedikált `diagnostics_broadcaster` (joint_state_broadcaster instance) a GPIO értékek publikáláshoz
- Launch file: spawner chain (joint_state → diagnostics → diff_drive)

### Segfault fix

A `RCLCPP_INFO_THROTTLE` + `rclcpp::Clock::make_shared()` temporális objektum élettartam
probléma → Segmentation fault. Eltávolítva, mert a GPIO diagnostics kiváltja.

### Verifikált működés

- ✅ Motor áll induláskor (DutyM1M2(0,0) on_activate)
- ✅ Motor reagál cmd_vel-re (SpeedAccelM1M2)
- ✅ Motor megáll cmd_vel timeout után (DutyM1M2(0,0) PID bypass)
- ✅ GPIO state interfaces regisztrálva (`ros2 control list_hardware_interfaces`)
- ✅ `diagnostics_broadcaster` aktív

---

## 2026-03-10 (22) — DDS domain rögzítés: ROS_DOMAIN_ID=0, cyclonedds Domain id=0

### Probléma
A `/robot/diagnostics` topic metaadata látszott (`ros2 topic info` mutat 1 publisher-t), de az üzenetek **nem érkeztek meg** sem a ros2-shell-ben, sem a Foxglove-ban — valószínű DDS discovery / partíció különbség a konténerek között.

### Változtatások
- **docker-compose.yml:** Minden ROS2 konténerhez (roboclaw, foxglove, ros2-shell) **ROS_DOMAIN_ID=0** környezeti változó.
- **tools/cyclonedds.xml:** `<Domain id="any">` → `<Domain id="0">`, hogy minden folyamat ugyanazon a domainen legyen.
- **Makefile:** Új cél `make robot-diagnostics-echo` — a roboclaw konténeren belül 8 másodpercig echo-olja a `/robot/diagnostics`-t (ha itt érkeznek az üzenetek, a publish működik; ha csak itt érkeznek, a gond a konténerek közötti DDS).

### Teszt
1. Stack újraindítás: `docker compose down && docker compose up -d`.
2. `make robot-diagnostics-echo` — ha üzenetek jönnek, a driver publish rendben; ha a ros2-shell-ben is jönnek, a domain fix segített.
3. Ros2-shell-ben: `ros2 topic echo /robot/diagnostics diagnostic_msgs/msg/DiagnosticArray`.

### 22e — Motor Duty teszt (encoderek nélkül)
- **robot-motor-test-standalone:** Működik, sima forgás. Közvetlen TCP, roboclaw stop.
- **robot-motor-test-m1/m2 (ROS2):** A driver futó TCP kapcsolata miatt a motor rángat, nem forog simán. Encoderek nélkül a standalone-ot használd.
- **motor_duty_m1, motor_duty_m2** topicok: közvetlen PWM (DutyM1/DutyM2), encoderek nélkül működnek.
- **robot-motor-test-m1, robot-motor-test-m2:** 100% duty, 3s. INFO log: "DutyM1: duty=1.0 raw=32767 ok" vagy "FAILED".
- **robot-motor-test-standalone:** Közvetlen TCP teszt (roboclaw stop) — tools/test_motor_duty.py, M1 majd M2 50% 2s. Ha ez sem mozgat, a gond a RoboClaw/hardware oldalon van.

### 22d — Motor teszt (cmd_vel)
- **robot-motor-test-m2:** Csak M2 (bal kerék) teszt, 50% sebesség (0.5 m/s), 3 s. Diff drive: linear=0.25, angular=-1.67 → jobb kerék 0, bal 0.5 m/s.
- **Makefile:** `make robot-motor-test` — publish cmd_vel (linear.x=0.05 m/s) 3 másodpercig, 10 Hz. Paraméterek: `LINEAR=0.05 DURATION=3`. E-Stopnak released-nek kell lennie (Pico publikálja).

### 22c — 50Hz szenzor stream + teljes diagnostics visszakapcsolás
- **Szenzor timer visszakapcsolva:** 50 Hz (0.02s) — GetEncoders + GetSpeeds → `joint_states` (50 Hz) + `odom` (50 Hz). TCP round-trip ~15ms, stabil.
- **Diagnostics teljes:** GetVolts + GetTemps + ReadCurrents + ReadError mind visszakapcsolva (1 Hz).
- **Error flag kódok bővítve:** `_ERROR_FLAG_NAMES` dict kiegészítve a 16–28 bitekkel (OverCurrent/Bat/Temp warnings, S4/S5, Speed/Pos error limit). Új `_ERROR_MASK = 0x0000FFFF` — csak alsó 16 bit (hard error) triggerel WARN szintet. A `0x20000000` (firmware status) mostantól nem generál hamis riasztást.

### 22b — Root cause: Fast DDS shared memory transport konténerek között nem működik
- Mindkét konténer `rmw_fastrtps_cpp`-t használ (NEM CycloneDDS-t) — a `CYCLONEDDS_URI` ignorálva volt.
- A `ros2 topic list -v` a ros2-shell-ben látta a publishert (discovery működött), de `ros2 topic echo` nem kapott adatot.
- `/robot/estop` (agent konténerből) **megérkezett** a ros2-shell-be, de a roboclaw konténerből semmi.
- **Root cause:** Fast DDS alapértelmezetten **SHM (shared memory) transport**-ot használ, ha a participantek azonos hoston vannak. Docker `network_mode: host`-tal a hálózat közös, de az **IPC namespace** konténerenként külön van → az SHM csendben elbukik, az adat nem jut át, és nincs fallback UDP-re.
- **Javítás:** `FASTDDS_BUILTIN_TRANSPORTS=UDPv4` env var minden ROS2 konténerhez (roboclaw, foxglove, ros2-shell) → SHM kikapcsolva, csak UDP, az üzenetek megérkeznek.
- Teszt: `ros2 topic pub /test_cross2` roboclaw-ból + `ros2 topic echo` ros2-shell-ből → **üzenetek jönnek**.

---

## 2026-03-10 (21) — Read timeout után flush + terhelés csökkentés (diagnostics)

### Probléma
A driver csatlakozott, de GetSpeeds (cmd 0x6c) timeout után nem jött joint_states/diagnostics; a motor nem mozdult. Timeout már 0.5 s volt, paraméterek rendben.

### 1. lépés — Timeout után port flush
- **basicmicro_node.py:** `_flush_controller_input()` helper — ha a controllernek van `_port`-ja, meghívja a `flushInput()`-ot.
- Minden controller read utáni `except Exception` ágban meghívva: read_sensors (encoder, speed), perform_health_check, publish_diagnostics (voltage, temps, currents, error). Így sikertelen read után a buffer ürül, a következő parancs szinkronban marad.

### 2. lépés — Terhelés csökkentés (teszt)
- Szenzor timer: 50 Hz → **25 Hz** (0.02 → 0.04 s).
- Diagnostics: **ReadCurrents** és **ReadError** hívások kikommentezve (volt, temp marad). Ha a link stabil, később vissza lehet kapcsolni.

### 3. lépés (21b) — Read timeout 0.5 s + csatlakozás után 0.2 s delay
- **basicmicro_tcp.py:** RoboClawTCP alapértelmezett timeout 0.05 → **0.5 s** (TCP round-triphez).
- **basicmicro_node.py:** Új paraméter `read_timeout` (default 0.5), átadva a Basicmicro(port, baud, read_timeout) konstruktornak. Sikeres connect + ResetEncoders után **time.sleep(0.2)** mielőtt return — a vezérlő legyen kész a következő parancsra.
- **config/roboclaw_params.yaml:** `read_timeout: 0.5` a roboclaw_driver alatt.

### Teszt
1. `make host-build-docker`, majd `docker compose restart roboclaw`.
2. flush + csökkentett terhelés + **0.5 s timeout + 0.2 s delay** — `ros2 topic echo /robot/diagnostics`, `ros2 topic echo /robot/joint_states`, majd `ros2 topic pub --rate 10 /robot/cmd_vel ...`.

---

## 2026-03-09 (20) — make robot-shell: ROS2 környezet betöltése

- **Makefile:** `robot-shell` cél most a belépéskor betölti a ROS2-öt: `source /opt/ros/jazzy/setup.bash` + opcionálisan `source /host_ws/install/setup.bash`, majd `exec bash`. Így a `ros2` parancs és a workspace azonnal elérhető a shellben.

---

## 2026-03-09 (19) — Driver telemetria bővítés: áram, error kód, odometria

### Változtatások

- **basicmicro_ros2/basicmicro_driver/basicmicro_node.py:**
  - **Motoráram** (`ReadCurrents`) hozzáadva a diagnostics-hoz — `current_m1`, `current_m2` (Amper)
  - **Error/warning kód** (`ReadError`) hozzáadva — `error_code` (hex), `error_flags` (emberi olvasható flag dekódolás: E-Stop, Temp, BatHigh/Low, M1/M2Current stb.)
  - **Odometria** (`odom` topic) implementálva — diff drive forward kinematics: encoder delta → x, y, theta, linear/angular velocity. Frame: odom → base_link.
  - **JSON status** (`basicmicro/status`) most 1 Hz-en publikál (connected, firmware, connection_state, error_count)
  - Firmware verzió mentése csatlakozáskor, megjelenik a diagnostics-ban
  - `_to_signed32` helper, `_yaw_to_quaternion`, `_decode_error_flags`, `_ERROR_FLAG_NAMES` dict

### Diagnostics topic tartalma (1 Hz)

| Mező | Forrás |
|------|--------|
| main_battery_voltage | GetVolts |
| logic_battery_voltage | GetVolts |
| temperature_1, temperature_2 | GetTemps |
| current_m1, current_m2 | ReadCurrents |
| error_code, error_flags | ReadError |
| firmware, port, address | Induláskor |
| connection_state, error_count, consecutive_errors | Health monitor |

---

## 2026-03-09 (18) — RC Teleop node (arcade), motor_left/right revert

### Döntés

A teljes driver refactor (DRIVER_REFACTOR_PLAN.md) elhalasztva. Helyette: **monkey-patch marad** + önálló `rc_teleop_node` fordítja az RC jeleket `cmd_vel` Twist-re (arcade stílusú: throttle + steering). A driver `basicmicro_node.py` kizárólag `cmd_vel`-t fogad.

### Változtatások

- **basicmicro_ros2/basicmicro_driver/basicmicro_node.py:** Revert — eltávolítva: `use_direct_wheel_inputs`, `estop_topic` paraméterek, `motor_left`/`motor_right`/`estop` subscription-ök, `_motor_left_cb`, `_motor_right_cb`, `_estop_cb`, `_send_direct_wheel_speeds` metódusok. A driver ismét csak `cmd_vel` (Twist) bemenetet fogad.
- **roboclaw_tcp_adapter/rc_teleop_node.py:** Új node — arcade RC → cmd_vel fordító. Paraméterezhető topic nevek (`throttle_topic`, `steering_topic`), deadzone (5%), PWM normalizálás opció (`is_pwm_input`), E-Stop gating, 20 Hz biztonsági timer.
- **launch/roboclaw.launch.py:** 3 node: roboclaw_tcp_node + safety_bridge + rc_teleop.
- **setup.py:** `rc_teleop_node` entry point hozzáadva.
- **config/roboclaw_params.yaml:** `rc_teleop` szekció (max speed, deadzone, topic nevek, publish rate). `use_direct_wheel_inputs`/`estop_topic` eltávolítva.
- **ONBOARDING.md:** „Mi fut és hol" táblázat frissítve (roboclaw: driver + safety_bridge + rc_teleop).

### Architektúra

```
Pico rc_ch1/ch2 (Float32) → rc_teleop_node (normalize, deadzone, E-Stop) → cmd_vel (Twist) → basicmicro_node (diff drive) → SpeedM1M2 → RoboClaw
```

---

## 2026-03-09 (17) — Driver refactor terv (DRIVER_REFACTOR_PLAN.md) — elhalasztva

### Új

- **host_ws/src/roboclaw_tcp_adapter/DRIVER_REFACTOR_PLAN.md:** Teljes refaktor terv — saját motorvezérlő ROS2 node (roboclaw_driver_node.py), monkey-patch és basicmicro_ros2 futásidejű függőség nélkül. Funkciólista (Core A: 10, Services B: 7, Későbbi C: 6), célarchitektúra, fájl struktúra, migráció lépései. **Állapot: elhalasztva — a jelenlegi monkey-patch + rc_teleop architektúra elegendő.**

---

## 2026-03-09 (16) — Motorvezérlő: motor_left/right a driverben, rc_to_cmd_vel eltávolítva — visszavonva

### (Visszavonva a (18)-as sessionben — a motor_left/right kód eltávolítva, rc_teleop_node váltja ki.)

---

## 2026-03-09 (15) — RC ch2 → M2: rc_to_cmd_vel bridge

### (Elavult — a (18)-as session rc_teleop_node-ja váltja ki.)

---

## 2026-03-09 (14) — Foxglove: build lépés, hibaelhárítás

### Probléma

A Foxglove konténer nem indult, a localhost:8765 nem volt elérhető: a Portainer (repository deploy) nem futtatja a `docker compose build`-et, ezért a `w6100-foxglove:latest` image hiányzott.

### Változtatások

- **Makefile:** `make foxglove-build` — buildeli a Foxglove image-et (`docker compose build foxglove`). Első alkalommal a hoston futtatandó, utána a stack (Portainer vagy make robot-start) elindítja a konténert.
- **docker-compose.yml:** Foxglove parancs: `address:=0.0.0.0` hozzáadva; komment: first time `make foxglove-build`.
- **ONBOARDING.md:** Foxglove szekció: image build kötelező elsőre (`make foxglove-build`), majd stack restart. Hibaelhárítás: „Foxglove konténer nem indul / localhost:8765 nem elérhető” → `make foxglove-build`, stack újraindítás, `docker compose logs foxglove`.

---

## 2026-03-09 (13) — Foxglove Bridge a compose-ban (Portainer indítja)

### Változtatások

- **docker-compose.yml:** `foxglove` szolgáltatás hozzáadva — build `docker/Dockerfile.foxglove`, image `w6100-foxglove:latest`, host network, CycloneDDS, `ros2 launch foxglove_bridge foxglove_bridge_launch.xml port:=8765`. A Portainer „Start this stack” indítja, ha az image létezik.
- **ONBOARDING.md:** „Mi fut és hol” táblázat + Foxglove szekció: a bridge a stack része.

### Megjegyzés

A Portainer repo-deploy nem buildel; elsőre a hoston: `make foxglove-build`. A `tools/start-foxglove.sh` továbbra használható.

---

## 2026-03-09 (12) — Portainer: indítás/leállítás

### Dokumentáció

- **ONBOARDING.md:** A Portainerből a „Start/Stop this stack” működik; tartalék: hoston `make robot-stop`.

---

## 2026-03-09 (11) — Portainer custom template (W6100 Robot Stack)

### Új

- **tools/portainer-templates.json:** Portainer CE custom app template — egy „W6100 Robot Stack” bejegyzés (type 3 = Compose stack), repository URL placeholder, stackfile: `docker-compose.yml`, env: `AGENT_PORT`, `ROBOCLAW_HOST`, `ROBOCLAW_PORT` (alapértelmezettekkel). A `note` mező (HTML) elmagyarázza az előfeltételt (relatív útvonalak), az ajánlott Web editor + stack path módszert és az alternatívát (deploy from repo + `make host-build-docker` a klónban).
- **ONBOARDING.md:** Új alfejezet „Portainer: custom template a robot stackhez” — template hozzáadása (Settings → Custom templates, URL vagy beillesztés), repository URL cseréje, két deploy módszer (Web editor + path vs Repository + build a klónban). A dokumentumtáblázatban megjelenik a `tools/portainer-templates.json`.

### Megjegyzés

A compose relatív volume-okat használ (`./host_ws`, `./tools`); a „Deploy from repository” csak akkor működik készen, ha a klónban már létezik a `host_ws/install` (pl. az első deploy után a hoston a klón könyvtárában `make host-build-docker`, majd stack restart).

---

## 2026-03-09 (10) — ONBOARDING.md + szabály a naprakészen tartásra

### Új

- **ONBOARDING.md** (repo gyökér): Teljes rendszer onboarding — architektúra, előfeltételek, első alkalom (klón + host-build-docker), konfig (robot_network.yaml), napi használat (robot-start/stop, logs, shell, Portainer), mi fut és hol, Foxglove, parancs referencia, dokumentáció index, gyors hibaelhárítás.
- **.cursor/rules/errata-changelog.mdc:** 3. pont: ONBOARDING.md frissítése, ha a rendszer fejlődik (új parancs, szolgáltatás, architektúra, hibaelhárítás). A rule description bővítve az ONBOARDING.md-re.

---

## 2026-03-09 (9) — Portainer CE hozzáadva (profile: management)

### Változtatások

- **docker-compose.yml:** Portainer CE service hozzáadva `profiles: ["management"]` — nem indul `make robot-start`-tal, külön kell: `make portainer-start`. Port: 9443 (HTTPS), 8000 (Edge Agent). Docker socket mount (ro), persistent volume (portainer_data).
- **Makefile:** `make portainer-start` / `make portainer-stop` + help szöveg.

### Használat

Ha van internet (első alkalommal image pull): `make portainer-start`, majd böngésző: `https://<robot-ip>:9443`. Ha nincs internet, a robot stack (`make robot-start`) normálisan indul, a Portainer nem blokkolja.

---

## 2026-03-09 (8) — Docker Compose: teljes robot rendszer

### Döntés

A tmux/gnome-terminal tabs helyett **Docker Compose** — mivel már minden Docker konténerben fut, a compose a legátláthatóbb és legkezelhetőbb megoldás.

### Új fájlok

- **docker-compose.yml** (repo gyökér): 3 service:
  - `agent` — microros/micro-ros-agent:jazzy, UDP, `restart: unless-stopped`
  - `roboclaw` — ros:jazzy, depends_on agent, mount host_ws + cyclonedds, ros2 launch
  - `ros2-shell` — ros:jazzy, depends_on agent, stdin_open + tty, interaktív bash

### Makefile parancsok

| Parancs | Docker Compose | Leírás |
|---------|---------------|--------|
| `make robot-start` | `docker compose up -d` | Minden elindul háttérben |
| `make robot-stop` | `docker compose down` | Minden leáll |
| `make robot-logs` | `docker compose logs -f` | Összes log követése |
| `make robot-logs-roboclaw` | `docker compose logs -f roboclaw` | Csak roboclaw log |
| `make robot-shell` | `docker compose exec ros2-shell bash` | ROS2 shell belépés |
| `make robot-ps` | `docker compose ps` | Konténer állapot |

### Teszt eredmények

- `make robot-start` — 3 konténer elindult (~6s)
- `make robot-ps` — mind UP
- `docker compose logs roboclaw` — safety_bridge OK, roboclaw_tcp_node OK (TCP timeout ha nincs hardver, normális)
- `docker compose exec ros2-shell bash -c "ros2 node list"` — 3 node látható (/robot/estop, /robot/pedal, /robot/rc)
- `ros2 topic list` — 17 topic (cmd_vel, odom, diagnostics, estop, motor_left/right, stb.)
- `make robot-stop` — mind leállt

### Eltávolított/deprecált

- `make robot-start-tmux` eltávolítva a Makefile-ból (a shell scriptek megmaradnak visszafelé kompatibilitáshoz)
- `make robot-start` (tabs) lecserélve compose-ra

---

## 2026-03-09 (7) — RoboClaw TCP driver MŰKÖDIK + tabs javítás

### Siker

A RoboClaw TCP driver **sikeresen csatlakozott** a motorvezérlőhöz Docker konténerből:
```
Connected to controller: USB Roboclaw 2x60A v4.4.3
Port: tcp://192.168.68.60:8234, Baud: 38400, Address: 128
```
A safety_bridge_node is rendben elindult. Ctrl+C-re a motorokat leállította.

### Javítások

- **start-robot-tabs.sh:** `gnome-terminal --window` hozzáadva a `--tab` elé. Enélkül egyes rendszereken a gnome-terminal a meglévő ablakhoz próbálta hozzáadni a tabokat, és csak az elsőt (agent) nyitotta meg. A `--window` biztosítja, hogy egy új ablak nyíljon mind a 3 tabbal.

### Nyitott

- [ ] `make robot-start` (tabs) teljes teszt a `--window` javítás után
- [ ] ROS2 shell topicok ellenőrzése (ros2 topic list, echo) amíg a driver fut

---

## 2026-03-09 (6) — RoboClaw: No module named 'basicmicro_driver' — root cause

### Hiba

`roboclaw_tcp_node` indításakor: `No module named 'basicmicro_driver'`. A basicmicro_ros2 CMakeLists.txt a driver scripteket **programként** (symlink) telepíti `lib/basicmicro_ros2/` alá, de **nem Python csomagként** (nincs `__init__.py`). A `basicmicro_driver` Python package (`__init__.py` + modulok) kizárólag a **forrásban** van: `host_ws/src/basicmicro_ros2/basicmicro_driver/`.

### Javítások

- **docker-run-roboclaw.sh, docker-run-ros2.sh:** PYTHONPATH = `/host_ws/src/basicmicro_ros2:/host_ws/src/basicmicro_python:...` — a **forrás** könyvtár, nem az install lib. Így `from basicmicro_driver.basicmicro_node import main` megtalálja a csomagot.
- **roboclaw_tcp_node.py:** Visszaegyszerűsítve egyetlen importra: `from basicmicro_driver.basicmicro_node import main`.

### Következő lépés

Nem kell újraépíteni (symlink-install). Indítsd újra a roboclaw tabot.

---

## 2026-03-09 (5) — Terminal fülek (tabs) + robot-stop

### Kérés

- Maradjanak a terminal fülek (kézre esik), ne tmux.
- Legyen robot-stop (vagy flag), ami leállít minden futó folyamatot.

### Változtatások

- **tools/start-robot-tabs.sh** (új): Egy GNOME Terminal ablak 3 tabbal — [1] agent, [2] roboclaw, [3] ros2-shell. A 2. és 3. tab vár, amíg az agent konténer fut, majd indítja a roboclaw-ot és a ros2-shellt. Konfig: host_ws/config/robot_network.yaml.
- **tools/stop-robot.sh** (új): Leállítja a három Docker konténert (w6100_bridge_agent_udp, w6100_bridge_roboclaw, w6100_bridge_ros2) és a tmux session "robot"-ot (ha van).
- **Makefile:** `robot-start` most a **start-robot-tabs.sh**-t hívja (terminal tabs). Új: `robot-start-tmux` → start-robot.sh (tmux, pl. SSH-hoz). Új: `robot-stop` → stop-robot.sh. A help szöveg frissítve.

### Használat

- Indítás: `make robot-start` (3 tab egy ablakban).
- Leállítás: `make robot-stop`.
- Tmux (headless): `make robot-start-tmux`; leállítás: `make robot-stop` vagy `tmux kill-session -t robot`.

---

## 2026-03-09 (4) — RoboClaw Docker: serial modul + safety_bridge logger javítás

### Hibák (make robot-start / docker-run-roboclaw.sh)

1. **roboclaw_tcp_node:** `ModuleNotFoundError: No module named 'serial'` — A `basicmicro` a `pyserial`-t használja (`import serial`); a ros:jazzy konténerben ez a csomag nem volt telepítve.
2. **safety_bridge_node:** `TypeError: RcutilsLogger.info() takes 2 positional arguments but 6 were given` — Az rclpy logger nem fogad több argumentumot (format + args), csak egy stringet.

### Javítások

**1. serial (pyserial) a konténerben**
- `tools/docker-run-roboclaw.sh`: induláskor `apt-get update && apt-get install -y python3-serial` (csendesen), így a `serial` modul elérhető.
- `tools/docker-run-ros2.sh`: ugyanez, hogy a ros2-shellből futtatva is működjön a roboclaw_tcp_node.

**2. safety_bridge_node.py logger**
- `host_ws/src/roboclaw_tcp_adapter/roboclaw_tcp_adapter/safety_bridge_node.py`: a többargumentumos `get_logger().info("fmt", a, b, c, d)` helyett egy formázott string: `get_logger().info("fmt" % (a, b, c, d))`. Ugyanígy a `.warn("EMERGENCY STOP: %s", reason)` → `.warn("EMERGENCY STOP: %s" % reason)`.

### Következő lépés

A safety_bridge forrást módosítottuk; a futó kód a build mappából jön. **Újra kell építeni:** `make host-build-docker`, majd `make robot-start` (vagy csak a roboclaw ablak újraindítása).

---

## 2026-03-09 (3) — ROS2 Dockerből: host-build-docker, RoboClaw + shell konténerben

### Kontextus

A felhasználó jelzi: a ROS2 Dockerből fut (nincs natív ROS2 a hoston). A korábbi utasítások (make host-build, make robot-start host-on) ezért nem megfelelőek.

### Változtatások

**1. Makefile**
- **host-build-docker:** Colcon build a `ros:jazzy` konténerben; a `host_ws` mountolva van. A konténerben törli a build/log cache-t (path ütközés elkerülésére), majd `colcon build --packages-select basicmicro_ros2 roboclaw_tcp_adapter --symlink-install`. A `basicmicro_python` nem ament csomag, ezért nem épül colcon-nal; futásidőben a `PYTHONPATH=/host_ws/src/basicmicro_python` biztosítja az importot.
- A help szöveg bővítve: host-build-docker, és hogy a robot-start ROS2-t Dockerből indítja.

**2. tools/docker-run-ros2.sh**
- Mount: `host_ws` → `/host_ws` (rw).
- Belépéskor: `PYTHONPATH=/host_ws/src/basicmicro_python`, majd `source /opt/ros/jazzy/setup.bash`, majd ha létezik `source /host_ws/install/setup.bash`. Így a shellben elérhetők a roboclaw_tcp_adapter és basicmicro_ros2 csomagok.
- Prerequisíte a kommentben: make host-build-docker.

**3. tools/docker-run-roboclaw.sh (új)**
- A RoboClaw TCP adapter (driver + safety bridge) a `ros:jazzy` konténerben indul: mount `host_ws`, CYCLONEDDS, env `ROBOCLAW_HOST`, `ROBOCLAW_PORT`, majd `ros2 launch roboclaw_tcp_adapter roboclaw.launch.py ...`.
- Használat: `./docker-run-roboclaw.sh [HOST] [PORT]` (default 192.168.68.60 8234).

**4. tools/start-robot.sh**
- A [1] roboclaw ablak már nem a hoston futtatja a `ros2 launch`-t, hanem a `docker-run-roboclaw.sh`-t hívja (ugyanaz a host/port kiolvasás a config-ból).

### Ellenőrzés

- `make host-build-docker` sikeres (2 csomag: basicmicro_ros2, roboclaw_tcp_adapter).
- Dockerben: `source /opt/ros/jazzy/setup.bash && source /host_ws/install/setup.bash && ros2 pkg list | grep -E 'roboclaw_tcp_adapter|basicmicro'` → basicmicro_ros2, roboclaw_tcp_adapter.

### Nyitott

- [ ] make robot-start (tmux) teljes teszt: agent + roboclaw (Docker) + ros2-shell (Docker).

---

## 2026-03-09 (2) — Host workspace tesztek előkészítése (Linux)

### Elvégzett lépések (repo gyökérből)

**1) Submodule állapot**
- `git submodule status`: mindkét submodule kicheckoutolva (space a commit előtt)
  - `host_ws/src/basicmicro_python` (3231645)
  - `host_ws/src/basicmicro_ros2` (dc75870)
- `git submodule update --init --recursive` nem kellett.

**2) Host workspace függőségek (`make host-install-deps`)**
- **Sikerült:** `pip3 install -e host_ws/src/basicmicro_python` — user install, basicmicro + pyserial telepítve.
- **Nem sikerült:** `rosdep install ...` — ROS_DISTRO nincs beállítva (ROS2 nincs a gépen), sudo kért jelszót; rosdep hibát dobott (std_msgs, robot_state_publisher definíciók, python3-serial apt). Venv nem kellett (pip nem externally-managed hibát dobott).

**3) Colcon build (`make host-build`)**
- **Nem sikerült:** `ament_cmake` nem található — a build előtt a ROS2 környezet nincs betöltve (nincs `/opt/ros`, `ros2` nincs a PATH-on). A `basicmicro_ros2` CMake ament_cmake-t keres, ezért a build abort.
- **Megjegyzés:** `host_ws/install/` már létezik korábbi (ROS2-s környezetben készült) buildből (basicmicro_ros2, roboclaw_tcp_adapter install fájlok megvannak).

**4) Ellenőrzés**
- `host_ws/install/setup.bash` **létezik**.
- `source host_ws/install/setup.bash && ros2 pkg list | grep -E "roboclaw_tcp_adapter|basicmicro"` **nem futtatható** — a gépen nincs ROS2 telepítve, így `ros2` parancs nincs.

### Összefoglaló

| Lépés | Eredmény | Megjegyzés |
|-------|----------|------------|
| Submodule status | OK | Mind kicheckoutolva |
| pip basicmicro_python | OK | User install |
| rosdep | Sikertelen | ROS_DISTRO nincs, sudo, rosdep keys |
| make host-build | Sikertelen | ament_cmake hiányzik (ROS2 nincs) |
| setup.bash létezik | OK | Korábbi build maradvány |
| ros2 pkg list | N/A | ROS2 nincs telepítve |

### Következő tesztlépés (ha ROS2 már telepítve)

- **ROS2 telepítés (Ubuntu):**  
  `sudo apt update && sudo apt install ros-jazzy-desktop` (vagy használt distro), majd `source /opt/ros/jazzy/setup.bash`. Ezután `make host-build` és a fenti ellenőrzés.
- **Robot indítás (tmux):**  
  `make robot-start` — ez a `tools/start-robot.sh`-t futtatja (agent, roboclaw, ros2-shell ablakok). Vagy manuálisan:  
  - 1. terminál: agent (ha kell)  
  - 2. terminál: `source host_ws/install/setup.bash && ros2 launch roboclaw_tcp_adapter roboclaw.launch.py`  
  - 3. terminál: `source host_ws/install/setup.bash && bash` (ROS2 shell)

### Nyitott pontok

- [ ] ROS2 telepítése a Linux hoston (pl. Jazzy), majd `make host-build` újra
- [ ] `make robot-start` (tmux) vagy manuális indítási parancsok tesztelése

---

## 2026-03-09 — RoboClaw Host Workspace implementáció

### Kiindulási állapot

- A motorvezérlő (Basicmicro RoboClaw) integrációs architektúra véglegesítve
- Döntés: közvetlen TCP socket adapter (socat nélkül), monorepo host_ws/ könyvtárban
- Terv: `/Users/m2mini/.cursor/plans/roboclaw_host_workspace_8223f7cd.plan.md`

### Elvégzett munka

**1. Git submodule-ok hozzáadása**
- `host_ws/src/basicmicro_python` — upstream Python library (Packet Serial, CRC, 150+ parancs)
- `host_ws/src/basicmicro_ros2` — upstream ROS2 driver (ros2_control, /cmd_vel, odometry)
- `.gitmodules` automatikusan frissítve

**2. `roboclaw_tcp_adapter` ROS2 csomag — 4 Python modul**
- `tcp_port.py` — `RoboClawTCPPort`: serial.Serial API-kompatibilis TCP socket adapter
  - `TCP_NODELAY=1`, `sendall()`, non-blocking `flushInput()`
- `basicmicro_tcp.py` — `RoboClawTCP(Basicmicro)`: csak `Open()` override, 150+ parancs öröklődik
- `safety_bridge_node.py` — `/robot/estop` (Bool) → `/emergency_stop` (Empty) bridge
  - Két trigger: aktív E-Stop + 2s silence watchdog
- `roboclaw_tcp_node.py` — monkey-patch entry point
  - `basicmicro.controller.Basicmicro` → `RoboClawTCP` csere import előtt

**3. ROS2 package infrastruktúra**
- `package.xml` (ament_python, rclpy, std_msgs)
- `setup.py` + `setup.cfg` (console_scripts: roboclaw_tcp_node, safety_bridge_node)
- `resource/roboclaw_tcp_adapter` (ament index marker)
- `config/roboclaw_params.yaml` (driver paraméterek)

**4. Launch és hálózati konfiguráció**
- `launch/roboclaw.launch.py` — két node (driver + safety bridge), paraméterezett
- `host_ws/config/robot_network.yaml` — egyetlen hálózati konfig forrás (agent, RoboClaw, Picók, ROS)

**5. Startup és build rendszer**
- `tools/start-robot.sh` — tmux session 3 ablakkal (agent, roboclaw, ros2-shell)
  - Headless, SSH-n is működik (gnome-terminal kiváltva)
  - robot_network.yaml-ból olvassa a hálózati paramétereket
- `Makefile` bővítve: `host-install-deps`, `host-build`, `host-shell`, `robot-start`

**6. `host_ws/README.md` — részletes architektúra dokumentáció**
- 11 fejezet: bevezető, döntési napló, rendszer architektúra, hálózati topológia
- Komponens deep dive (TCPPort, RoboClawTCP, monkey-patch, safety bridge)
- Telepítés, konfiguráció, futtatás, hibakeresés, kiszakítási terv, roadmap

### Létrehozott fájlok (13 fájl)

| Fájl | Leírás |
|------|--------|
| `host_ws/README.md` | Verbose architektúra dokumentáció |
| `host_ws/config/robot_network.yaml` | Hálózati konfig SSOT |
| `host_ws/src/roboclaw_tcp_adapter/package.xml` | ROS2 csomag definíció |
| `host_ws/src/roboclaw_tcp_adapter/setup.py` | Python setup |
| `host_ws/src/roboclaw_tcp_adapter/setup.cfg` | Install scripts config |
| `host_ws/src/roboclaw_tcp_adapter/resource/roboclaw_tcp_adapter` | ament marker |
| `host_ws/src/roboclaw_tcp_adapter/config/roboclaw_params.yaml` | Driver paraméterek |
| `host_ws/src/roboclaw_tcp_adapter/launch/roboclaw.launch.py` | ROS2 launch file |
| `host_ws/src/roboclaw_tcp_adapter/roboclaw_tcp_adapter/__init__.py` | Package init |
| `host_ws/src/roboclaw_tcp_adapter/roboclaw_tcp_adapter/tcp_port.py` | TCP socket adapter |
| `host_ws/src/roboclaw_tcp_adapter/roboclaw_tcp_adapter/basicmicro_tcp.py` | Basicmicro subclass |
| `host_ws/src/roboclaw_tcp_adapter/roboclaw_tcp_adapter/safety_bridge_node.py` | Safety bridge |
| `host_ws/src/roboclaw_tcp_adapter/roboclaw_tcp_adapter/roboclaw_tcp_node.py` | Entry point |

### Módosított fájlok (1 fájl)

| Fájl | Változás |
|------|---------|
| `Makefile` | +4 target: host-install-deps, host-build, host-shell, robot-start |

### Új fájlok (git által kezelt)

| Fájl | Változás |
|------|---------|
| `.gitmodules` | 2 submodule (basicmicro_python, basicmicro_ros2) |
| `tools/start-robot.sh` | tmux startup script |

### Érintetlen maradt

- `app/` — Zephyr firmware (Tier 1)
- `devices/` — per-board config.json
- `tools/start-eth.sh`, `docker-run-agent-udp.sh` — visszafelé kompatibilitás

### Pre-deployment validáció — 4 bug javítva (ERR-011..014)

A kód review során 4 kritikus hiba derült ki, mind hardveres teszt előtt javítva:

**ERR-011: Hibás import path** — `basicmicro_driver.basicmicro_driver` → `basicmicro_driver.basicmicro_node`

**ERR-012: Hiányos monkey-patch** — Python `from X import Y` name binding: a patch nem propagálódott `basicmicro.Basicmicro`-ra. Javítás: mindkét helyen (`basicmicro.controller.Basicmicro` + `basicmicro.Basicmicro`) patchelni kell.

**ERR-013: Nem létező /emergency_stop subscriber** — Az upstream driver nem implementálta (a doksija említi, a kódja nem). Javítás: zero Twist a `cmd_vel`-re 10 Hz-en amíg E-Stop aktív.

**ERR-014: Konstruktor inkompatibilitás** — `RoboClawTCP(host, port)` vs upstream hívás `Basicmicro(comport, rate)`. Javítás: azonos `(comport, rate, ...)` szignatúra, TCP URL parse belülről.

| Fájl | Változás |
|------|---------|
| `roboclaw_tcp_node.py` | Import path fix + dual monkey-patch |
| `basicmicro_tcp.py` | Konstruktor átírás: `(comport, rate)` + URL parser |
| `safety_bridge_node.py` | Zero Twist cmd_vel override + /emergency_stop forward compat |
| `roboclaw.launch.py` | Új safety bridge paraméterek (cmd_vel_topic, rate) |

### Még nem tesztelt / nyitott

- [ ] Hardveres teszt: TCP kapcsolat a valós USR-K6 modulhoz
- [ ] `make host-install-deps` + `make host-build` teljes pipeline teszt
- [ ] `make robot-start` tmux session end-to-end
- [ ] Safety bridge E-Stop → zero cmd_vel → motor halt lánc validáció
- [ ] Odometria kalibrálás (wheel radius, separation, encoder CPR)
- [ ] Reconnect loop teszt: USR-K6 hálózati megszakítás kezelése

---

## 2026-03-08 — Foxglove Studio integráció, start-all.sh

### Elvégzett munka

**1. `tools/start-all.sh` — Teljes környezet egylépéses indítása**

Új script, ami sorrendben, egymásra várva indítja az összes szolgáltatást:

1. micro-ROS Agent (UDP :8888) — gnome-terminal ablakban
2. Foxglove Bridge (WS :8765) — háttér konténer, megvárja a port megnyílását
3. ROS2 Jazzy shell — gnome-terminal ablakban
4. Foxglove Studio — natív snap app (ha telepítve van)

Leállítás: `./tools/start-all.sh --stop`

**2. Foxglove Studio ajánlás dokumentálva**

- Bridge: Docker konténerben marad (Dockerfile.foxglove, start-foxglove.sh — már meglévő)
- Studio (kliens): natív snap telepítés (`sudo snap install foxglove-studio`)
- Architektúra: Pico → UDP → Agent → DDS → Foxglove Bridge → WS :8765 → Studio

### Érintett fájlok

| Fájl | Változás |
|------|---------|
| `tools/start-all.sh` | **Új** — teljes környezet indító script |

---

## 2026-03-08 — v2.1: Firmware javítások, RC input, config-driven channels

### Kiindulási állapot

- Commit: `50d7f9b` (közelítőleg) — v2.0 firmware, 3 board flashelve, de egyik sem csatlakozik az agentre

### Elvégzett munka

**1. ERR-007 javítás — rclc_support_init dirty struct** (`8a30801`)

A reconnect loop hosszú ideje meglévő bugja: ha az első `rclc_support_init` sikertelen volt, a `support`/`node`/`executor` structs részlegesen inicializált állapotban maradtak. Minden következő retry ugyanezt a dirty struct-ot kapta, ami végtelen hibás inicializációhoz vezetett.

Javítás: `memset(&support, 0, ...)` + `memset(&node, 0, ...)` + `memset(&executor, 0, ...)` hozzáadva a `ros_session_init()` elejére.

**2. ERR-008 javítás — docker-run-agent-udp.sh** (`8a30801`)

A script bash-t indított a micro-ROS agent helyett, `$SCRIPT_DIR` nem volt definiálva.
Javítás: script teljes újraírása, helyes `udp4 -p "$PORT" -v6` paranccsal.

**3. cyclonedds.xml javítás** (`8a30801`)

- `NetworkInterfaceAddress`: `eth0` → `auto` (nem kellett hardcoded interface)
- Stale `<Peer address="192.168.68.201"/>` eltávolítva

**4. start-eth.sh javítás** (`8a30801`)

Robusztus `docker ps` várakozási ciklus hozzáadva (30× 1s), hogy a script csak akkor folytasson, ha az agent container ténylegesen fut. Frissített echo üzenetek a jelenlegi topicokkal.

**5. docker-run-ros2.sh frissítés** (`8a30801`)

Help szöveg és quick-reference parancsok frissítve a jelenlegi `/robot` namespace és topicok szerint.

**6. Memória optimalizáció** (`8a30801`)

- `config.c`: két külön `static char buf[2048]` helyett egy közös `static char cfg_io_buf[1536]` — ~500 byte BSS megtakarítás
- `config.h`: `CFG_MAX_CHANNELS` 16 → 12 — ~80 byte BSS megtakarítás

**7. ERR-009 javítás — DTR wait** (korábbi munkamenet)

500ms non-blocking poll váltotta fel a végtelen blokkoló DTR ciklust. A board USB serial nélkül is elindul.

**8. ERR-006 javítás — dupla namespace** (korábbi munkamenet)

`estop.c` topic_pub javítva: `"robot/estop"` → `"estop"`. A namespace-t a config adja, nem a topic stringbe kell beírni.

**9. ERR-010 javítás — topic collision, config-driven channels** (korábbi munkamenet)

- `user_channels.c`: `register_if_enabled()` wrapper — minden channel regisztrálás előtt ellenőrzi a config.json-t
- `config.h/.c`: `cfg_channel_entry_t` (name, enabled, topic override), `config_channel_enabled()`, `config_channel_topic()` API
- `channel_manager.c`: `config_channel_topic()` alapján felülírja a `topic_pub`/`topic_sub` értéket

**10. GPIO debounce — ERR-010 kiegészítés** (korábbi munkamenet)

- `drv_gpio.h`: `last_irq_ms` mező a `gpio_channel_cfg_t` structban
- `drv_gpio.c`: `gpio_isr_handler` 50ms DEBOUNCE_MS szűrővel (`k_uptime_get()`)

**11. RC PWM input driver** (korábbi munkamenet)

- `app/src/drivers/drv_pwm_in.h/.c`: pulse-width mérés `k_cycle_get_32()`-vel, GPIO IRQ rising/falling edge-en
- `app/src/user/rc.h/.c`: 6 csatorna (rc_ch1–rc_ch6), normalizáció `g_config.rc_trim` alapján
- `app/boards/w5500_evb_pico.overlay`: `rc_inputs` node, GP2–GP7 aliasok
- `app/CMakeLists.txt`: `drv_pwm_in.c` és `rc.c` hozzáadva

**12. RC trim konfiguráció** (korábbi munkamenet)

- `config.h`: `cfg_rc_trim_ch_t` (min/center/max) és `cfg_rc_trim_t` (6 channel + deadzone) structs
- `config.c`: `parse_rc_trim()`, `config_to_json()` frissítve, `config_reset_defaults()` alapértékekkel
- `tools/upload_config.py`: rc_trim szekció feltöltése dotted key-value párokkal

**13. Per-board config.json fájlok** (korábbi munkamenet)

- `devices/E_STOP/config.json`: csak `estop: true`, minden más false
- `devices/PEDAL/config.json`: saját konfig
- `devices/RC/config.json`: rc_ch1–6 enabled topic override-okkal, rc_trim section

### Érintett fájlok

| Fájl | Változás |
|------|---------|
| `app/src/main.c` | memset fix, DTR 500ms |
| `app/src/config/config.h` | channel entries, rc_trim structs, CFG_MAX_CHANNELS=12 |
| `app/src/config/config.c` | channel/rc_trim parse+save, shared cfg_io_buf[1536] |
| `app/src/bridge/channel_manager.c` | topic override config-ból |
| `app/src/user/user_channels.c` | register_if_enabled wrapper |
| `app/src/user/estop.c` | topic_pub "robot/estop" → "estop" |
| `app/src/user/rc.h/.c` | Új — RC csatornák, normalizáció |
| `app/src/drivers/drv_gpio.c/.h` | 50ms debounce |
| `app/src/drivers/drv_pwm_in.h/.c` | Új — PWM pulse-width driver |
| `app/boards/w5500_evb_pico.overlay` | rc_inputs node, GP2-7 aliasok |
| `app/CMakeLists.txt` | rc.c, drv_pwm_in.c hozzáadva |
| `tools/docker-run-agent-udp.sh` | Teljes újraírás |
| `tools/cyclonedds.xml` | auto interface, stale peer eltávolítva |
| `tools/start-eth.sh` | Robusztus agent wait loop |
| `tools/docker-run-ros2.sh` | Frissített help szöveg |
| `devices/E_STOP/config.json` | Per-board channel config |
| `devices/RC/config.json` | RC csatornák + rc_trim |

### Build és flash

- Build sikeres: `853504 bytes` UF2, RAM **97.49%**
- Commit: `8a30801` — 8 fájl, 67 sor hozzáadva, 86 törölve
- Flash: mindhárom board BOOTSEL módból egyszerre flashelve
- Teszt: mindhárom board csatlakozik az agentre ✅

---

## 2026-03-07 — Errata javítások (ERR-001 → ERR-005)

### Kiindulási állapot

- Commit: `202bbb1` — működő firmware, ismert hibákkal
- Egyetlen board működik a hálózaton, több board nem

### Elvégzett munka

**1. Errata konszolidáció** (`948d109`)
- `ERRATA_PARAM_SERVER.md` → egységes `ERRATA.md` (ERR-001 – ERR-005)
- 50 eszközös skálázási elemzés dokumentálva
- Root cause azonosítva: hardcoded MAC `00:00:00:01:02:03` a DTS-ben

**2. ERR-005 javítás — egyedi MAC cím** (`c816b9e`)
- `CONFIG_HWINFO=y` — RP2040 flash unique ID elérhetővé tétele
- `apply_mac_address()` függvény a `main.c`-ben:
  - Ha `config.json`-ban van `"mac"` mező → azt használja (parse + validáció)
  - Ha nincs → `hwinfo_get_device_id()` → `02:xx:xx:xx:xx:xx` (LAA)
  - Fallback: DTS MAC marad (de log warning)
- `cfg_network_t` bővítve `mac[20]` mezővel
- `config.c`: load, save, set, print, to_json, defaults mind frissítve
- Boot sorrend: MAC beállítás → hostname → link wait → DHCP/static IP

**3. ERR-003 javítás — egyedi hostname** (`c816b9e`)
- `CONFIG_NET_HOSTNAME_ENABLE=y`, `CONFIG_NET_HOSTNAME="ROS_Bridge"`
- `net_hostname_set_postfix()` a `node_name`-ből
- Eredmény: pl. `ROS_Bridge_E_STOP` a routeren

**4. ERR-002 javítás — diagnostics aktuális IP** (`c816b9e`)
- `diagnostics_publish()` a Zephyr net stack-ből olvassa az IP-t
- Új 6. KeyValue: `mac` — az aktuális MAC cím
- `DIAG_KV_COUNT` 5 → 6

**5. ERR-001 kutatás — heap stats** (`c816b9e`)
- `CONFIG_SYS_HEAP_RUNTIME_STATS=y`
- Heap free/alloc/max logolás a `rclc_parameter_server_init_with_option` előtt és után
- Eredmény: következő boot-nál a logban látható lesz a heap állapot

### Érintett fájlok

| Fájl | Változás |
|------|---------|
| `app/prj.conf` | +3 Kconfig szekció (hwinfo, hostname, heap stats) |
| `app/src/config/config.h` | `mac[20]` mező |
| `app/src/config/config.c` | MAC kezelés a teljes config pipeline-ban |
| `app/src/main.c` | `apply_mac_address()` + `net_hostname_set_postfix()` |
| `app/src/bridge/diagnostics.c` | Aktuális IP + MAC a /diagnostics-ban |
| `app/src/bridge/param_server.c` | Heap stats logolás |
| `ERRATA.md` | Állapot táblázat frissítve |

### Build és flash

- `CONFIG_NET_HOSTNAME_MAX_LEN=64` → **build hiba**: Kconfig range [1, 63] — javítva 63-ra
- Build sikeres: `843264 bytes` UF2, RAM 97.09%
- Flash: mind 3 eszköz BOOTSEL módból flashelve

### Még nem tesztelt / nyitott

- [ ] Hardveres teszt: 1 board — MAC log, hostname a routeren, /diagnostics
- [ ] Hardveres teszt: 2+ board — eltérő MAC, eltérő IP, stabil kapcsolat
- [ ] ERR-001: heap stats logok kiértékelése boot után
