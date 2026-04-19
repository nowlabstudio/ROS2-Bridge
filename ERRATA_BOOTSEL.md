# Errata: bridge bootsel — nem működik hibás session állapotban

**Dátum:** 2026-03-07
**Állapot:** Nyitott — workaround: kézi BOOTSEL gomb

---

## A hiba

A `bridge bootsel` shell parancs (`reset_usb_boot(0, 0)`) nem megbízható, ha
a firmware éppen egy ROS2 session inicializálási hibaciklusban van
(pl. `rclc_node_init error` miatti újrapróbálkozás).

```
[00:00:05.023,000] <err> main: rclc_node_init error
[00:00:05.031,000] <wrn> main: Session init failed, retrying...
```

Ebben az állapotban a `main()` folyamatosan próbálkozik a node init-tel,
és a Zephyr shell executor nem kap CPU időt a parancs végrehajtásához.

A `flash.sh` script is jelzi a problémát:

```
/dev/tty.usbmodem23401: Device not configured
```

Ez azt jelenti, hogy az `echo "bridge bootsel" > /dev/tty...` soros írás
sikertelen volt, mert a port nem volt elérhető.

---

## Kiváltó ok

Az `rclc_node_init error` az érvénytelen node névből ered:
- A ROS2 node név csak `[a-zA-Z][a-zA-Z0-9_]*` karaktereket tartalmazhat
- A `E-STOP` névben a **kötőjel (`-`) érvénytelen** — ezt az rcl API visszautasítja
- A firmware retry ciklusba kerül, blokkolja a shell executor futását

---

## Workaround

Ha a firmware hibaciklusban van és a `bridge bootsel` nem válaszol:
1. Húzd ki a USB kábelt
2. Tartsd nyomva a **BOOTSEL** gombot
3. Dugd vissza a USB kábelt
4. Engedd fel a gombot — `/Volumes/RPI-RP2` megjelenik
5. Másold az UF2-t: `cp workspace/build/zephyr/zephyr.uf2 /Volumes/RPI-RP2/`

---

## Megelőzés

**Érvényes node nevek:**
```bash
bridge config set ros.node_name E_STOP       # ✓ aláhúzás
bridge config set ros.node_name estop_bridge # ✓
bridge config set ros.node_name pico_bridge  # ✓
```

**Érvénytelen node nevek:**
```bash
bridge config set ros.node_name E-STOP       # ✗ kötőjel tiltott
bridge config set ros.node_name ROS2_E-STOP  # ✗ kötőjel tiltott
bridge config set ros.node_name 1sensor      # ✗ számmal kezdődik
```

ROS2 névszabály: `[a-zA-Z][a-zA-Z0-9_]*`

---

## Javítás — BL-003 LEZÁRVA 2026-04-19

A `config_set()` függvényben validáció került a `ros.node_name` és
`ros.namespace` mezőkhöz (lásd `app/src/config/config.c` — `is_valid_ros2_name`
és `is_valid_ros2_namespace`). Érvénytelen érték `-EINVAL`-t ad vissza, a
shell oldal pedig konkrét hibaüzenettel elutasítja, mielőtt flash-re kerülne:

```
uart:~$ bridge config set ros.node_name E-STOP
Invalid node_name: 'E-STOP'
ROS2 names must match [a-zA-Z][a-zA-Z0-9_]*
  OK:  pico_bridge, E_STOP, robot1
  BAD: E-STOP, 1robot, robot.pedal
```

Mivel az `upload_config.py` is a shell `bridge config set` parancson át dolgozik,
a validáció ezen az úton is automatikusan érvényesül — a BOOTSEL-lock már nem
reprodukálható érvénytelen `node_name`-mel.
