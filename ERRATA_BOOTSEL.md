# Errata: bridge bootsel — nem működik hibás session állapotban

**Dátum:** 2026-03-07 (eredeti) / 2026-04-22 (lezárás megerősítve)
**Állapot:** LEZÁRVA — BL-003 javítás óta a soft `bridge bootsel` megbízhatóan működik (lásd alább „Javítás — BL-003" + „Verifikáció — 2026-04-22").

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

---

## Verifikáció — 2026-04-22 (BL-020 diag patch flash)

A soft bootsel + remote flash flow zöld a BL-020 diagnosztikus patch
flash-elésekor (commit `b2cab49`):

```
$ tools/flash.sh /dev/ttyACM0
Platform: Linux
Port:     /dev/ttyACM0
Firmware: /home/eduard/Dev/ROS2-Bridge/tools/../workspace/build/zephyr/zephyr.uf2
Sending 'bridge bootsel'...
Flashing to /media/eduard/RPI-RP2 ...
Done. Pico is rebooting.
```

A teljes flash ciklus (USB shell parancs → BOOTROM → udisks2 mount →
`cp` UF2 → reboot) ~3-5 másodperc, fizikai gomb-érintés nélkül.

**Linux mount-pont eltérések:**

A `tools/flash.sh` mind a `/media/$USER/RPI-RP2` és `/run/media/$USER/RPI-RP2`
útvonalakat ellenőrzi (10 s timeout, 0.5 s polling). Distro-függő:
udisks2-vel (Ubuntu/Debian) általában `/media/$USER/RPI-RP2`, systemd-mount-tal
néha `/run/media/$USER/RPI-RP2`. Ha sehol nem jelenik meg, hibaüzenet
explicit jelzi: `RPI-RP2 volume did not appear`.

**Hibás állapotok, amik kézi BOOTSEL-t igényelnek:**

1. Üres flash (első flash, vagy `west flash --erase` után) — nincs
   futó firmware, ami a `bridge bootsel` parancsot fogadja.
2. Firmware crashelt, a `/dev/ttyACM0` nem elérhető (USB CDC nem indul) —
   `tools/flash.sh` hibaüzenetet ad: `Device not configured` /
   `No such device`.
3. Másik USB-n megjelenő ACM eszköz (pl. RC TX programmer) elsőnek vesz
   `/dev/ttyACM0`-t — explicit port kell: `tools/flash.sh /dev/ttyACM1`.

Ezekben a kivételes esetekben az eredeti workaround (kézi BOOTSEL gomb +
USB újradugás) változatlanul érvényes.
