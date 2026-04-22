# docs/backlog.md — Nyitott feladatok és TODO-k

> Utolsó frissítés: 2026-04-22 (BL-020 nyitva — RC CH2 +27.5 µs bias, két diag mérés CÁFOLTA a tisztán szoftveres ISR preemption hipotézist; új gyanú: hardveres — TX vevő VCC sag, chip-belüli adjacent pin coupling, vagy keret-időzítés. HW-isolation diagnózis kell.)
> Hatókör: a teljes ROS2-Bridge repo.
> Szabály (`policy.md#2`): minden TODO ide kerül; minden bejegyzés tartalmazza
> a **kontextust**, az **okot** és az **érintett fájlokat**.

Rendezés: súlyosság szerint csökkenő. Lezárt tételek a fájl alján, dátummal.

---

## BL-020 — RC PWM-input ISR preemption bias (CH3 aktív → CH2 szisztematikus +27.5 µs eltolás)

> **Státusz:** nyitva, safety-impact. Diagnózis kész, fix másnapra halasztva
> (user döntés 2026-04-21 este). Részleges enyhítés már benn van a repóban
> (overlay PULL_DOWN + `rc_normalize` failsafe — lásd „Részleges enyhítés"
> szekció alább). Ez NEM oldja meg az ISR bias-t, csak a floating-pin
> mellékhibákat zárta ki.

### Tünet

RC módban, a TX CH3 kapcsoló aktiválásakor (CH3 stick ≈ +1.0) a **bal motor
random, ugráló mozgást végez** akkor is, ha a CH1/CH2 stickek középen vannak.
CH3-ot kihúzva az RC vevőből a hiba megszűnik — mintha „átvenné" CH3 a CH2-t.
A jobb motor (CH1) szintén érintve van, ha CH3 aktív és CH1 be van kötve;
a mix-effektus csak a két motor-csatornán jelentkezik, a többi CH-n nem.

### Kvantitatív mérés (2026-04-22)

`ros2 topic echo /robot/motor_left` CH1/CH2 stick-eket középen tartva,
CH3-on két állapotban:

| állapot | leírás | min | max | avg | std (becs.) |
|---|---|---|---|---|---|
| **K1** | CH3 stick inaktív (≈ -1.0, tx kapcsoló lenn) | -0.010 | +0.010 | -0.000 | ~0.006 |
| **K2** | CH3 stick aktív (≈ +1.0, tx kapcsoló fenn) | +0.045 | +0.065 | **+0.055** | ~0.006 |

**Δ = +0.055 normalizált szisztematikus bias** CH2-n, amikor CH3 aktív.
A bias DC (nem zaj) — az std alig változik a két állapot között, de a
középérték ugrik. Ez nem elektromos átsugárzás tünete (az szimmetrikus
zajként jelenne meg), hanem egy konkrét időzítési torzítás.

### Gyanús gyökérok — RP2040 GPIO bank0 shared IRQ dispatch latency

**Hipotézis:** az RP2040-en minden `GPx` pin ugyanazt az `IO_IRQ_BANK0`
vektort használja; az egy ISR sorban dispatcheli az aktív callback-eket.
A PWM input méréshez 6 csatornán (GP2..GP7) futó `GPIO_INT_EDGE_BOTH`
callback **`k_cycle_get_32()` timestamp-et vesz az ISR elején** a puls-szélesség
számításához (`common/src/drivers/drv_pwm_in.c`).

Amikor CH3 stick közel max (impulzus ≈ 1950–2000 µs), a CH3 falling edge-e
időben közel esik a CH2 falling edge-éhez (CH2 középen = 1500 µs, fázis-
eltolódás 50 Hz PPM/SPPM esetén változó). Amelyik ISR másodikként fut,
annak `k_cycle_get_32()` hívása **+ISR runtime-nyi késést** szenved —
ez a késés a CH2 mért puls-szélességét növeli.

**Kvantitatív egyezés:**
- 0.055 normalizált × (max-center = 500 µs range) = **+27.5 µs**
- Egy minimalis Zephyr GPIO callback + EDGE_BOTH dispatch path ~20–30 µs
  RP2040-en (133 MHz, no hardware prioritization a pin szinten).
- A „CH3 aktív" állapot a CH3 impulzus hosszát ~500 µs-ről ~1500 µs-re
  növeli, ami a falling edge-eket közelebb hozza a CH2 edge-hez → a mérési
  anomália csak aktív CH3-nál jelentkezik.

### Mi zárja ki a többi hipotézist

1. **Elektromos crosstalk (kábelköteg):** CH3-ot teljesen leforrasztottuk
   a Pico-ról (pin fizikailag lebegett) → hiba megmaradt. Tehát nem
   vezetőszintű átsugárzás.
2. **Floating pin ISR-storm:** 2,2 kΩ külső pull-down GP4↔GND-re →
   hiba megmaradt. A pin elektromosan stabil volt, az IRQ szoftveresen
   mégis „él" (mert a csatorna engedélyezve van a config.json-ban).
3. **Relé-spike GP8-on:** minden GP8..GP11 fizikailag bekötetlen volt a
   teszt alatt (a világítás-relé ki). Nincs tranziens.
4. **Host-oldali tank-mix kereszt-kontamináció:** a TX-en a hardver-mix
   ki volt kapcsolva (raw stick értékek mentek át), `rc_teleop_node.py`
   csak CH1/CH2/CH5-re subscribe-ol (nem CH3-ra). A bias közvetlenül a
   `/robot/motor_left` topic-on mérhető — mielőtt a host feldolgozná.
5. **CH5 (`rc_mode`) analógia:** CH5 ugyanabban a kábelkötegben és
   egyidejűleg magas jelet kap RC módban, mégsem okoz bias-t CH2-n.
   Magyarázat: CH5 impulzusa ~50 ms távolságra van CH2-től a PPM ciklusban
   (a TX keretsorrend másik végén), tehát az ISR-ek nem esnek egybe.

### Frissítés 2026-04-22: két diagnosztikus mérés CÁFOLTA a tisztán szoftveres ISR preemption hipotézist

A „Gyanús gyökérok" szekció és a „Mi zárja ki a többi hipotézist" §5
(CH5 mentes) **felülbírálva** az alábbi két mérés alapján.

**Mérés A — CH3 IRQ skip patch** (commit `b2cab49` → később revertálva
`f0f3d51`-ben). A `rc_pwm_init()` `if (i == 2) continue;` flag-gel a CH3
callback regisztráció kihagyva, `lights_input` topic konstans `0.000`.
Foxglove CSV (~750 mp aktív window):
- `rc_mode` (CH5) **HIGH** → `motor_left` avg = **+0.053**
- `rc_mode` (CH5) **MID/LOW** → `motor_left` avg ≈ **+0.001**

A CH5-tel korreláló bias látszólag a CH5 ISR preemption-jét vádolta —
a `Mi zárja ki §5`-öt megdöntötte.

**Mérés B — CH5 IRQ skip patch** (uncommitted, mérés után discard-olva).
A `rc_pwm_init()` `if (i == 4) continue;`, `rc_mode` topic konstans `0.000`.
Friss CSV (1580–1645 mp) sec-by-sec:
- `lights_input` (CH3) **LOW** (-1.0) → `motor_left` avg = **+0.012..+0.017**
- `lights_input` (CH3) **HIGH** (+0.93) → `motor_left` avg = **+0.06..+0.08**

A CH3-mal korreláló bias most VISZONT a CH3 ISR preemption-jét vádolta
— a két mérés összevetve **logikai paradox**:

| mérés | CH3 IRQ | CH5 IRQ | bias forrás (látszólag) | bias jelen? |
|---|---|---|---|---|
| A | **skip** | aktív | CH5 toggle | **igen, +0.05** |
| B | aktív | **skip** | CH3 toggle | **igen, +0.06** |

Ha az ISR preemption tisztán szoftveres lenne, akkor az IRQ kikapcsolt
csatorna nem tudna bias-t okozni. Mégis MINDKÉT mérésben volt bias —
a vádolt csatorna éppen az volt, amelyik IRQ-ja **aktív** maradt. A
korreláció valójában a TX-en a két stick **szinkronizált mozgatásával**
magyarázható: a tesztelő mindkét mérésben a CH3+CH5 kapcsolókat egyszerre
toggle-olta (vagy a TX-en egy rotary az obi-egyszerre vezérli), és a
kikapcsolt csatorna `0`-án maradt → a maradék (aktív) csatorna értéke
korrelált a bias-szal — **félrevezető függési illúzió**.

**Új interpretáció:** a bias forrása **független a Zephyr ISR feldolgozástól**.
A TX-en `bármelyik HIGH csatorna` (CH3 vagy CH5) jelenléte HW-szinten
okozza a +27.5 µs eltolódást a CH2-n. Lehetséges hardveres mechanizmusok:
1. **TX vevő VCC sag a magasabb duty-cycle-től:** ha CH3+CH5 hosszabb
   pulzusokat ad (HIGH = ~1.95 ms / 22 ms = 9% vs LOW ~5%), a vevő 3.3V
   regulator többet terhelődik → minden CH kimenet enyhén alacsonyabb
   feszültség → a CH2 falling edge a Schmitt küszöbön később detektálódik
   → mért pulse_us +27.5 µs-mal hosszabb.
2. **Chip-belüli adjacent pin coupling** (GP3 ↔ GP4, GP3 ↔ GP6): az
   RP2040 belső kapacitás-mátrixa az adjacent pinek edge-átmenetét
   átsugározza a CH2 vonalra → spurious Schmitt-trigger → torzult mérés.
3. **TX/vevő keret-szintű időzítés:** a vevő belső szekvenciája átrendezi
   a csatorna-kimeneteket a stick állása alapján — azonos cycle-en belül
   más csatorna HIGH = más relatív timing CH2-n.

**Következmények a fix-tervre:**
- A korábban tervezett **PIO-alapú driver** (Step 2) **NEM garantáltan
  oldja meg** a bias-t, ha a probléma a fizikai jel torzítása szintjén
  van (Schmitt-trigger, vevő VCC sag). A PIO ugyanazt a Schmitt-edge-et
  látja, csak ISR-mentesen.
- **Hardver-isolation diagnózis kell** mielőtt PIO-ra invesztálunk:
  - **HW-A:** Külön akkumulátor a TX vevőhöz (a Pico USB-ről táplálva
    marad), bias mérés CH3 HIGH/LOW két állapotában. Ha a bias eltűnik
    → vevő VCC sag a fő ok.
  - **HW-B:** Pull-up vagy R/C low-pass szűrő (10 kΩ + 100 pF) a CH2 vonalon
    → Schmitt-trigger küszöb tisztábban detektált → ha bias csökken, kapacitív
    coupling a fő ok.
  - **HW-C:** TX-en CH4 vagy CH6 stick HIGH-ra (CH3+CH5 LOW) → ha CH4/CH6
    is okoz bias-t, akkor minden HIGH csatorna szimmetrikus hatású →
    erősíti a vevő VCC sag hipotézist.
  - **HW-D:** Külső szignál-generátor a CH2 vonalra (1500 µs konstans pulzus,
    nem TX-vevő) → bias eltűnik-e? Ha igen, a probléma egyértelműen TX-vevő
    oldali, nem Pico/Zephyr oldali.

A korábbi tervezett 1-soros patch + PIO-driver (alább, megtartva
referenciaként) **csak akkor lesz végleges fix**, ha a HW-D mérés
megerősíti, hogy a Pico-szintű probléma a domináns. Egyébként a
megoldás hardveres (jobb vevő, külső táp, vagy szűrő).

### Eredeti javaslat: diagnosztikus megerősítő patch (1 sor) — VÉGREHAJTVA, paradox eredmény (lásd fent)

`common/src/drivers/drv_pwm_in.c` `rc_pwm_init()` loopjába ideiglenesen:

```c
for (size_t i = 0; i < n; i++) {
    if (i == 2) continue;   // SKIP CH3 IRQ regisztrációt — BL-020 diag
    // ... meglévő setup
}
```

Ezzel CH3 callback nem fut; ha a CH2 bias eltűnik aktív CH3 mellett, az
preemption-bias-t bizonyítja. Ha maradna, szoftveres logikai bug is
jöhet (kizárja a HW-szintű magyarázatot). Patch alkalmazása + build +
flash + K1/K2 mérés ismétlés → 15-30 perc, nem destruktív.

### Hosszú távú fix — RP2040 PIO-alapú PWM input capture

Az RP2040 **PIO state machine**-jei deterministic timing capture-t adnak
GPIO-szintű ISR nélkül: egy SM per csatorna (van 8 SM = 2 PIO × 4 SM),
8 ns felbontás, CPU-tól függetlenül mér. A Zephyr-ben elérhető a
`zephyr/drivers/misc/pio_rpi_pico` és a `CONFIG_PIO_RPI_PICO=y` —
mintaként a soft-UART és az LED-strip driverek használják.

Scope:
- `common/src/drivers/drv_pwm_in_pio.c` új driver, `pio_rpi_pico` allokátorral.
- PIO program pulse-width mérésre (falling-edge → counter, rising-edge →
  capture + reset). Referencia: Adafruit `pico-examples/pio/pwm_input/`.
- 6 SM-ből 6-ot foglalunk (PIO0 mind a 4 + PIO1 2-ből 2-t); marad 2 SM
  más drivernek (jelenleg egyik sem használ PIO-t).
- A `channel_t.read()` DMA-backed ring bufferből olvas — nincs ISR-contention.
- Regressziós gate: BL-011 `tools/rc_measure.py` std ≤ jelenlegi érték;
  K1/K2 mérés Δ ≤ 0.005 normalizált (a PWM mérésen belüli maradék zajszint).

### Interim mitigáció (opcionális, ha a PIO fix elhúzódik)

Host-oldal (`rc_teleop_node.py` vagy új filter node):
- Median filter window=5 a `/robot/motor_left`-on és `/robot/motor_right`-on.
- Enlarged deadzone: `|v| < 0.10` → 0.0 (jelenleg firmware-ben
  `deadzone=20 µs ≈ 0.04 normalizált`).
- Masked kihatás, de precízió-veszteség a finom trim-nél; a bias
  gyökerét nem kezeli. Dokumentáció alapú workaround, ne commitoljuk
  tartósan.

### Részleges enyhítés (commitolva 2026-04-22 `ebf26c3`-ben, NEM oldja meg a bias-t)

2026-04-21 este során két javítás született, amelyek 2026-04-22 reggel
kerültek commitba (`ebf26c3` — a tegnapi szándékot véglegesítettük):

- `apps/rc/boards/w5500_evb_pico.overlay` — `rc_ch1..rc_ch6` GPIO flag
  `0` → `GPIO_PULL_DOWN`. Kihúzott vezeték esetén a pin `0`-ra húz,
  nincs floating-jel miatti ISR-storm.
- `apps/rc/src/rc.c` — `rc_normalize()` elején `rc_pwm_valid()` check,
  invalid jelnél `ema_initialized = false` + `return 0.0f`. Timeout
  (`RC_PWM_TIMEOUT_MS = 500`) után a csatorna hard-nullára áll, EMA
  state nem „ragad be" a kihúzás előtti értéken. **Mérési hatás:**
  a 2026-04-22-i Foxglove CSV-ben látható, hogy TX off állapotban a
  `motor_left` 8 percen át tökéletes `0.000` (n=~12000 minta) — a
  failsafe stand-still állapotban garantáltan biztonságos.

Ezek kizárják a floating-pin mellékhatásokat (fail-safe OK volt a K1/K2
méréskor: CH1/CH2 kihúzva `/robot/motor_left = 0.000` stabilan).

### Érintett fájlok (várható)

**Diagnosztikus patch (BL-020 Step 1):**
- `common/src/drivers/drv_pwm_in.c` — ideiglenes `if (i == 2) continue;`

**PIO driver (BL-020 Step 2, hosszú táv):**
- `common/src/drivers/drv_pwm_in_pio.{c,h}` (új)
- `apps/rc/prj.conf` — `CONFIG_PIO_RPI_PICO=y`
- `apps/rc/boards/w5500_evb_pico.overlay` — PIO node bindings
- `tools/rc_measure.py` — regresszió mérés (meglévő eszköz, nem módosul)

### Definition of Done

- K1/K2 mérés CH2-n: |avg_K2 - avg_K1| < 0.005 normalizált.
- 10 perc folyamatos RC mód + CH3 aktív → nincs random motor-mozgás
  (sem bal, sem jobb oldalon), kerekek nyugalomban stickek középen.
- `tools/rc_measure.py stream 30s` CH3 konstans aktív alatt: CH2 std
  ≤ 0.010 (jelenlegi idle szint).
- ERRATA.md új bejegyzés: ERR-034 (RP2040 GPIO bank0 shared IRQ latency
  — pattern, nem bug; megoldás a PIO-ra áttérés).

### Miért nem fix ma

User explicit döntés 2026-04-21 este: „nem most készítjük el, hanem
holnap". Safety-impact ismert, de az RC módú motor-használat szünetel
addig (egyéb módok — learn, follow, auto — nem függnek a CH3-CH2
interaction-től, a TX CH3 inaktív hagyásával a hiba nem jelentkezik).

---

_(BL-001, BL-002 lezárva — lásd a „Lezárt tételek" szekciót.)_

---

_(BL-003, BL-004 lezárva — lásd a „Lezárt tételek" szekciót.)_

---

## BL-005 — Docker image reprodukálhatóság szigorítása

- **Kontextus:** `docker/Dockerfile` `FROM zephyrprojectrtos/ci:v0.28.8` —
  ez már egy jó pin, de tag mozoghat. A `pip3 install` verzió nélkül a
  dátum alapján másmás csomag-matrix-ot hoz.
- **Ok:** Ugyanazt kell buildelnünk, amikor vissza kell állni. A base tagot
  `@sha256:...` digestre szigoríthatjuk, és a Python csomagoknak minimum
  verziót adhatunk (vagy `requirements.txt`-t).
- **Érintett fájlok:** `docker/Dockerfile`.

---

## BL-006 — Board target név dokumentáció rendbetétel

- **Kontextus:** A Zephyr board target `w5500_evb_pico` (overlay-ben
  `compatible = "wiznet,w6100"`-val). Több doksiban emlegetjük „W6100 EVB
  Pico” néven a board-ot — könnyű összekeverni.
- **Ok:** Upstream Zephyr egyszer létrehozhat egy `w6100_evb_pico` targetet,
  és akkor migrálni kell. Érdemes ezt a BL-001 pinelés mellett nyomon
  követni.
- **Érintett fájlok:** `README.md` §Hardware, `TECHNICAL_OVERVIEW.md`,
  `app/boards/w5500_evb_pico.overlay`.

---

_(BL-007, BL-008 lezárva — lásd a „Lezárt tételek" szekciót.)_

---

## BL-009 — Upstream PR-ok (ELŐKÉSZÍTVE, ellenőrzésre vár)

- **Kontextus:** Három lokálisan hordozott fix, amelyek upstream bug-okat
  javítanak — mindegyikhez készen van a PR-anyag (diff + commit message +
  PR title/body) a `docs/upstream_prs.md`-ben:
  - **PR-A** (`micro_ros_zephyr_module`, ERR-027) — UDP transport header POSIX conditional
  - **PR-B** (`micro_ros_zephyr_module`, ERR-028) — libmicroros.mk ne zárja ki a std_srvs-t
  - **PR-C** (`zephyrproject-rtos/zephyr`, ERR-031) — eth_w6100 set_config propagálja az iface link_addr-t
- **Állapot:** a benyújtás **manuális lépés** a felhasználó részéről (fork,
  branch, DCO, stb.), a kódon nem kell több munka. Ha bármelyik merged,
  a megfelelő helyen `west.yml` SHA pint frissítjük és a patch-et töröljük
  (teljes flow: `docs/upstream_prs.md` — „Benyújtási ellenőrzőlista").
- **Érintett fájlok (ha merged):**
  - `west.yml` — új SHA-ra lépés,
  - `tools/patches/apply.sh` — Patch 1 és/vagy Patch 2 törlése,
  - ha mindkét micro-ROS patch merged: `apply.sh` teljesen törölhető,
  - `app/modules/w6100_driver/` — upstream driver használata (ha a fix
    v4.2.2-re back-portolt verzióra is rámegy).

---

_(BL-010, BL-011 lezárva — lásd a „Lezárt tételek" szekciót.)_

---

_(BL-012 superseded by BL-018 — RC GP8..GP11 generic Bool I/O csatornák 2026-04-21-én lezárva; A0 ADC csatorna még nincs, új ticket kell ha kell majd. Lásd a „Lezárt tételek" szekciót.)_

---

## BL-015 — Repo restructure: per-device app profile (közös `common/` + `apps/<device>/`)

> **Státusz:** LEZÁRVA (2026-04-21). Step 1-4 `apps/{estop,rc,pedal}/` +
> `common/` szétválasztással fut, Step 5 a régi `app/` fa és a
> `make build-legacy` target törlésével zárult.
> **Baseline tag:** `bl-014-phase2-done` (E_STOP 4 csatorna zöld, prod
> subnet restore-olva). Régi `bl-014-phase1-done` tag továbbra is a közös
> `app/` utolsó zöld verziójára mutat.

### Kontextus és ok

Jelenleg egyetlen Zephyr app (`app/`) fordul mindhárom device-nak (E_STOP,
RC, PEDAL), a runtime-viselkedést a `devices/<DEVICE>/config.json`
`channels.*` mezői döntik el (`app/src/user/user_channels.c`
`register_if_enabled()`). Ez működik, de:

1. **Pin-konfliktus:** a közös `app/boards/w5500_evb_pico.overlay`-ben
   GP2..GP7 már foglalt `rc_ch1..rc_ch6` aliasokkal, így E_STOP új
   csatornái (mode GP2/GP3, okgo GP4/GP5, okgo_led GP22) nem férnek be
   ugyanabba az overlay-be — Zephyr DT nem enged egy pint két `gpio-keys`
   node-ban.
2. **RAM feszes:** RP2040 264 KB SRAM 97.45%-on. Nincs tünet, de 3 új
   publisher/subscriber (Fázis 2) ~4-5 KB extra RAM — épphogy befér. A
   per-device `prj.conf`-fal kikapcsolhatók E_STOP-on az ADC, PWM-in stb.
   subsystem-ek (+5-15 KB margó).
3. **Architekturális tisztaság:** a user kifejezett kérése, hogy minden
   device "csak a saját funkcionalitását tudja" és ne legyen benne holt kód.

### Cél

Új struktúra (részletes fa: `docs/ARCHITECTURE.md` §4):

```
common/                     ← shared Zephyr lib (main.c, bridge/, drivers/, config/, shell/)
apps/estop/                 ← app: CMakeLists + prj.conf + boards/*.overlay + src/
apps/rc/
apps/pedal/
modules/w6100_driver/       ← változatlan
devices/<DEVICE>/config.json ← változatlan (runtime config)
Makefile                    ← DEVICE=estop|rc|pedal switch
```

### Stratégiai megkötések

- **A régi `app/` fa a restructure ALATT a repóban marad** párhuzamosan,
  de a restructure **befejező commit-jában törlődik**. Nem permanens
  coexistence — a git tag (`bl-014-phase1-done`) a biztonsági háló.
- **Minden migrációs lépés után build + flash + smoke teszt** mindhárom
  device-on, mielőtt a következő lépés indul.
- **`devices/<DEVICE>/config.json` változatlan** marad (csak ha Fázis 3
  subnet restore kell, de az BL-014 ügye).
- **`modules/w6100_driver/` változatlan** — közös out-of-tree modul, az
  `apps/<device>/CMakeLists.txt` mindegyik beemeli.
- **`host_ws/` érintetlen** (külön tier).
- **Ne csinálj közben stílus-refaktorokat** (policy §1: one-thing-at-a-time).

### Munkamenet-nyitó prompt (másold be új session-be)

```text
Kezdjük a BL-015 (repo restructure) munkát. Olvasd el:

1. policy.md
2. memory.md §0 (aktuális munkamenet állapot)
3. docs/backlog.md → BL-015 szekció (ez)
4. docs/ARCHITECTURE.md § 2 és § 4 (mostani + tervezett fa)

Majd ellenőrizd:
- git fetch && git tag | grep bl-014-phase1-done   (baseline tag megvan)
- git status (tiszta fa)
- make build (jelenlegi app/ még fordul baseline-ként)

Utána kezdd el a migrációt lépésenként, minden lépés végén STOP és jelezd
az állapotot a usernek, mielőtt a következő lépést indítod.
```

### Lépésterv (min. 5 commit, mindegyik után build + smoke teszt gate)

**Lépés 1 — `common/` létrehozása (nincs még build változtatás):**

- `common/` könyvtárfa: `src/main.c`, `src/bridge/`, `src/drivers/`,
  `src/config/`, `src/shell/` — a mostani `app/src/` megfelelő fájljainak
  **másolata** (nem move, hogy az eredeti app/ még fordítson).
- `common/CMakeLists.txt` — INTERFACE library vagy zephyr_library stílus,
  ami `apps/<device>/CMakeLists.txt`-ből hívható.
- Commit: `refactor(common): extract shared layer from app/ to common/`

**Lépés 2 — `apps/estop/` minimum set + build verifikáció:**

- `apps/estop/CMakeLists.txt` — beemeli `common/`-ot + saját `src/estop.c`,
  `src/user_channels.c` (csak estop registráció).
- `apps/estop/prj.conf` — `app/prj.conf` másolata, ezen a ponton még
  változatlan (subsystem trim majd Fázis 2 előtt).
- `apps/estop/boards/w5500_evb_pico.overlay` — csak `estop_btn` GP27 +
  `user_led` GP25 (RC aliasokat elhagyjuk).
- `apps/estop/src/estop.c` — másolat az `app/src/user/estop.c`-ből.
- `Makefile` — új `DEVICE ?= estop` változó, `APP_DIR = apps/$(DEVICE)`
  (a régi `app/` build ideiglenesen `make build-legacy` alatt megmarad).
- Build: `make build DEVICE=estop`, flash, `bridge config show` működik,
  `ros2 topic echo /robot/estop` 20 Hz-en jön (reprodukálja BL-014 Fázis 1
  mérést `tools/estop_measure.py`-vel).
- Commit: `feat(apps/estop): add per-device app variant (BL-015)`

**Lépés 3 — `apps/rc/`:**

- Ugyanaz a minta: `apps/rc/boards/*.overlay` GP2..GP7 PWM input, `src/rc.c`
  másolat, `user_channels.c` csak `rc_ch1..6` regisztrál.
- Build + flash RC boardra + `ros2 topic hz /robot/motor_left` → nem romlott.
- `tools/rc_measure.py`-val gyors regressziós pass.
- Commit: `feat(apps/rc): add per-device app variant (BL-015)`

**Lépés 4 — `apps/pedal/`:**

- PEDAL most `test_heartbeat` placeholder-t használ → hozzunk létre saját
  `apps/pedal/src/pedal.c`-t minimum /heartbeat publisher-rel (`std_msgs/Bool`
  1 Hz), hogy a test_channels.c-re ne legyen szükség.
- `apps/pedal/boards/*.overlay` — pedal ADC pinek (ld. drv_adc.c).
- Build + flash PEDAL boardra + `ros2 topic echo /robot/heartbeat` OK.
- Commit: `feat(apps/pedal): add per-device app variant with pedal.c (BL-015)`

**Lépés 5 — régi `app/` törlés + `common/` tisztítás:**

- Mindhárom device a saját `apps/<device>/`-ből flash-elhető és működik.
- `git rm -r app/` — a régi fa eltűnik.
- `common/src/user/test_channels.{c,h}` törlés (ha még megvan; dev helper,
  pedal.c leváltotta).
- `Makefile` — `make build-legacy` target törlése, `DEVICE ?= estop` default.
- `docs/ARCHITECTURE.md` §2 frissítés (jelenlegi → tervezett fa becserélés).
- Commit: `refactor(repo): remove legacy app/ after per-device migration (BL-015)`

### Smoke teszt checklist (minden lépés után)

- [ ] `make build DEVICE=<x>` exit 0
- [ ] UF2 fájl megvan (`workspace/build/zephyr/zephyr.uf2`)
- [ ] Flash-elés kézi BOOTSEL-lel OK (a soft bootsel-hiba külön ERR)
- [ ] `bridge config show` serial shell-ből OK
- [ ] `ros2 node list` mutatja a device nodeját
- [ ] device-specifikus topic olvasható / írható

### Megtartott assumption-ök (ne módosítsd BL-015 során)

- micro-ROS v Jazzy, Zephyr v4.2.2 pin (west.yml).
- W6100 driver out-of-tree (BL-010), MACRAW mode.
- `config.c` LittleFS `/lfs/config.json` séma.
- `channel_t` descriptor séma + `register_if_enabled()` pattern.
- `rclc_executor` handle count: `sub_count + PARAM_SERVER_HANDLES + service_count()`.

### Risk register

1. **Zephyr CMake misconfig** (a leggyakoribb hiba): `zephyr_library` vs
   `target_sources(app …)` keverés. Ha lépés 1 után build breaks a régi
   `app/`-ban, az azt jelenti, hogy a copy-t accident-eltük el move-nak.
2. **Overlay DT konfliktus** lépés 2-ben, ha véletlenül minden alias belekerül:
   csak a ténylegesen használt `gpio-keys` node-ok maradjanak meg.
3. **rcl session init fail** (`rclc_executor_init error`) ha a handle count
   nem stimmel → `common/main.c` `service_count()` számolás per-device OK.
4. **FLASH size break:** apps/<device>/ prj.conf trim után a micro-ROS
   kliens library relinkelődik — figyelj a `west build` cache invalidálásra
   (`west build -t pristine` ha gyanús).

### Érintett fájlok (várható)

- `common/**/*` (új)
- `apps/estop/**/*`, `apps/rc/**/*`, `apps/pedal/**/*` (új)
- `Makefile` (DEVICE switch)
- `docs/ARCHITECTURE.md` (frissítés)
- `docs/CHANGELOG_DEV.md`, `memory.md` (napló)
- `app/**/*` (törlés az utolsó commitban)

### Definition of Done

- Mindhárom device a saját `apps/<device>/`-ből flash-elhető egy
  `make build DEVICE=<x>` paranccsal.
- E_STOP 20 Hz rate reprodukálva (BL-014 Fázis 1 mérés nem regressziózott).
- RC csatornák helyesek (rc_measure.py nem jelez anomáliát).
- PEDAL saját `pedal.c`-vel fut, `test_channels.c` törölve.
- Régi `app/` fa eltávolítva a main branchről.
- `docs/ARCHITECTURE.md` § 2 frissítve (jelenlegi = új struktúra).
- Tag: `bl-015-done`, hogy BL-014 Fázis 2 innen induljon.

---

## BL-014 — E-stop bridge: kiolvasási ráta mérés/tuning + új input/output csatornák

- **Kontextus:** Az E-stop bridge jelenleg `app/src/user/estop.c` — `period_ms =
  500` (2 Hz fallback), `irq_capable = true` (50 ms debounce, edge-both IRQ).
  A user jelzése szerint a ROS oldalon tapasztalt kiolvasási sebesség túl
  alacsonynak tűnik (~1 Hz), a cél 10–20 Hz lenne. Emellett az E-stop board
  kap **három új GPIO funkciót**:
  - 3-állású forgókapcsoló (`follow` = GP3 aktív low, `learn` = közép, nincs
    aktív pin, `auto` = GP2 aktív low) → **1 `std_msgs/Int32` topic**, enum
    értékek `0 = learn, 1 = follow, 2 = auto`.
  - okgo nyomógomb (off-stabil) — **safety 2-pin AND**: GP4 ÉS GP5 egyszerre
    aktív low. Mindkét pinre IRQ, `read()` AND-olja.
  - okgo LED — GP22 aktív high (output, ROS→firmware). Subscribe Bool.
- **Ok:** Az estop reakcióidő biztonsági funkció — a lassú rate csökkenti a
  rendszer hatékonyságát; a 3-állású kapcsoló és az okgo gomb az üzemmód-
  választást és az engedélyező biztonsági láncot képezi; az LED operátor-
  visszajelzés.

### Scope — fázisokra bontva

**Fázis 1 — E-stop latency mérés + rate tuning (LEZÁRVA 2026-04-20):**

- `tools/estop_measure.py` (új, commit) — rclpy subscriber
  `std_msgs/Bool /robot/estop`, `stream` + `compare` subcommand; CSV
  summary + raw (`logs/estop_<ts>_<label>_*.csv`).
- `app/src/user/estop.c` — `period_ms 500 → 50` (20 Hz fallback); IRQ
  fast-path változatlan (`irq_capable=true`, `DEBOUNCE_MS=50`).
- `devices/E_STOP/config.json` ideiglenesen dev subnetre (`192.168.68.203`,
  DHCP, agent `192.168.68.125`) — **nincs commitolva**, Fázis 3 végén áll
  vissza prod subnetre (BL-013 pattern).
- **Mért eredmény** (15 s idle + 30 s edge teszt):

  | metrika | before (500 ms) | after (50 ms) |
  |---|---|---|
  | effektív rate | 2.46 Hz | **20.47 Hz** (+8.3×) |
  | gap median | 476 ms | **50 ms** |
  | gap p99 | 608 ms | **52 ms** |
  | gap std | 196 ms | **7.4 ms** |
  | IRQ min-gap | 0.77 ms | 0.77 ms |

  30 s edge teszt: 42 edge (21 PRESSED + 21 RELEASED), mindegyik edge→publish
  6.3…52 ms gap.

**Fázis 2 — Új input/output csatornák (FIRMWARE LEZÁRVA 2026-04-21):**

A BL-015 restructure lezárása után a Fázis 2 unblocked volt; a commit
alatt mind a 3 új csatorna firmware oldalon él és fordul az `apps/estop/`
app-ban (build zöld, RAM 97.42%, FLASH 2.57%). A DT overlay új alias-okat
deklarál a szabad pineken (mode-auto GP2, mode-follow GP3, okgo-btn-a GP4,
okgo-btn-b GP5, okgo-led GP22).

- **Új csatornák (implementálva):**
  - `mode` — `std_msgs/Int32`, GP2 + GP3 olvasás, enum 0/1/2 (learn/follow/auto),
    IRQ mindkét pinre (edge-both), közös `channel_idx`. Period 100 ms +
    IRQ fast-path. Debounce: `drv_gpio.c` global 50 ms — 30 ms tuning
    later, ha a rotary bounce zavar.
  - `okgo_btn` — `std_msgs/Bool`, GP4 ÉS GP5 AND (safety 2-pin),
    mindkét pinre IRQ, közös `channel_idx`. Period 100 ms + IRQ.
  - `okgo_led` — `std_msgs/Bool`, GP22 output, ROS subscribe →
    `drv_gpio_write`. Új `drv_gpio_setup_output()` helper a
    `common/src/drivers/drv_gpio.{c,h}`-ban.
- **Topic-ok:** `/robot/mode`, `/robot/okgo_btn`, `/robot/okgo_led`
  (C default + config-remap-elhetők a `channels:` object-form-mal, ha kell).
- **`devices/E_STOP/config.json`** — `mode:true, okgo_btn:true, okgo_led:true`
  bekapcsolva. **BL-016 E_STOP rész előrehozva:** a `test_*` és `rc_*`
  orphan kulcsok törölve (13 kulcs túllépte volna `CFG_MAX_CHANNELS=12`
  limitet). RC és PEDAL config cleanup marad BL-016-nak.

**Fázis 3 — Hardware verifikáció (LEZÁRVA 2026-04-21) + subnet restore:**

Flash megvolt, 4 csatorna regisztrálva, session aktív (dev subnet,
`192.168.68.203` DHCP, agent `192.168.68.125`).

| csatorna | hw-teszt | eredmény |
|---|---|---|
| `/robot/estop` (GP27 NC) | `ros2 topic echo` + gomb | ✅ False↔True helyes |
| `/robot/mode` (GP2/GP3) | `ros2 topic echo` + rotary 3 állás | ✅ 0=LEARN / 1=FOLLOW / 2=AUTO |
| `/robot/okgo_btn` (GP4+GP5 AND) | `ros2 topic echo` + gomb | ✅ AND + IRQ edge-both |
| `/robot/okgo_led` (GP22 out) | `ros2 topic pub` → LED | ✅ BL-017 után true→magas, false→alacsony |

**Hátralévő lépések:**

1. Subnet restore (BL-013 pattern): `devices/E_STOP/config.json` prod-ra
   (`ip=10.0.10.23`, `gateway=10.0.10.1`, `agent_ip=10.0.10.1`, `dhcp=false`),
   upload_config, commit.
2. `tools/estop_measure.py stream` regresszió check — 20 Hz még megvan.
3. Fázis 3 lezárás → git tag `bl-014-phase2-done` annotated tag.

### Érintett fájlok (állapot-jelzéssel)

Fázis 1 (LEZÁRVA):
- `app/src/user/estop.c` — `period_ms` 500 → 50
- `apps/estop/src/estop.c` — ugyanez, a BL-015 restructure során átkerült
- `tools/estop_measure.py` (új)

Fázis 2 firmware (LEZÁRVA 2026-04-21):
- `common/src/drivers/drv_gpio.{c,h}` — új `drv_gpio_setup_output()`
- `apps/estop/boards/w5500_evb_pico.overlay` — 4 új input alias + okgo_led output
- `apps/estop/src/mode.{c,h}` (új)
- `apps/estop/src/okgo_btn.{c,h}` (új)
- `apps/estop/src/okgo_led.{c,h}` (új)
- `apps/estop/src/user_channels.c` — 3 új register
- `apps/estop/CMakeLists.txt` — 3 új source
- `devices/E_STOP/config.json` — új csatornák + orphan cleanup (BL-016 előrehozott rész)

Fázis 3 (NYITVA, hw-access):
- `devices/E_STOP/config.json` — subnet restore prod-ra (BL-013 pattern)
- `memory.md`, `CHANGELOG_DEV.md` — Fázis 3 zárás
- `ERRATA.md` — ha új ERR jön a hw tesztből

---

## BL-019 — host-side `rc_lights_bridge` node (TX CH3 → `/robot/gp8`)

- **Kontextus:** A BL-018 lezárása után az RC bridge `/robot/rc_ch3`-on (topic
  alias: `/robot/lights_input`) publikálja a TX CH3 jelét `std_msgs/Float32`
  normalizált [-1..+1] tartományban; a GP8 relé a `/robot/gp8` `std_msgs/Bool`
  subscriber-en vár. A rádió kormányzás → világítás-kapcsolás integrációja
  host oldalon készül el, hogy ROS2 szinten más node-ok is tudjanak reagálni
  a CH3-ra (pl. horn, learn-mode enable stb.), ne firmware-be legyen égetve.
- **Hw-tesztelt prototípus** (2026-04-21, nem commitolva): `/tmp/rc_lights_bridge.py`
  rclpy Node, hiszterézissel (±0.2) kapcsolja a GP8-at. Működik, relé hall-
  hatóan kattan, lámpa ki/be — user megerősítette.
- **Scope:**
  - Új ROS2 Python package a `host_ws/src/` alatt (pl. `rc_bridge/rc_bridge/lights_bridge.py`).
    Nem a pico firmware része — a host Jetson-on fut.
  - Launch fájl integráció a meglévő robot-stack launchbe (ha van).
  - Config: threshold (`on_threshold`, `off_threshold`) ros2 params legyen,
    ne konstans.
  - Egység teszt a hiszterézisre.
  - Opció: bővíthető legyen CH5 (rc_mode) → horn vagy egyéb GPIO trigger-re.
- **Miért nem firmware:** a CH3 → GP8 mapping robot-policy, nem sensor/actuator
  funkcionalitás. Pico firmware maradjon dumb I/O (TX PWM in, GPIO out); a
  magasabb logika host-on rugalmasabb (ros2 param, runtime config), és ha
  később DDS-en más forrásból jön a kapcsolási jel, nem kell flash.
- **Érintett fájlok:** `host_ws/src/rc_bridge/` (új csomag), `host_ws/src/.../launch/*.launch.py`.

---

_(BL-013 lezárva — `devices/RC/config.json` 2026-04-21-én prod subnetre
állítva a BL-018 commit-ban. Lásd a „Lezárt tételek" szekciót.)_

---

## BL-016 — `devices/*/config.json` orphan key cleanup (BL-015 follow-up)

> **Részleges státusz (2026-04-21):** E_STOP és RC rész LEZÁRVA (lásd alább).
> Már csak a PEDAL config cleanup maradt (flash + hw-access), azt tartsuk
> nyitva a BL-016 ticket alatt.

- **Kontextus:** A BL-015 előtt egyetlen közös `app/` bináris futott minden device-on,
  ezért a `devices/<DEVICE>/config.json` `channels:` szekcióban minden device
  csatornája szerepelt (estop, test_*, rc_ch1..6) — runtime config döntött
  arról, hogy az adott device ténylegesen melyiket regisztrálja. BL-015 után
  a per-device bináris (`apps/<device>/`) csak a saját csatornáit ismeri, a
  többi kulcs orphan: ártalmatlan (`config_channel_enabled` egyszerűen ignorálja
  ismeretlen channel-name esetén), de zavaró és hibalehetőség.
- **Cleanup tartalom:**
  - ~~`devices/E_STOP/config.json`: csak `estop`, `mode`, `okgo_btn`,
    `okgo_led` maradjon.~~ **Kész 2026-04-21** (BL-014 Fázis 2 előrehozott része).
  - ~~`devices/RC/config.json`: csak `rc_ch1..6` (object form a topic-aliasokkal)
    + új `gp8..gp11` maradjon — `test_*`, `estop`, `lights` törlése.~~
    **Kész 2026-04-21** (BL-018 mellett; `rc_ch3.topic=lights_input` rename,
    `gp8..gp11` csatornák hozzáadva).
  - `devices/PEDAL/config.json`: csak `pedal_heartbeat` maradjon — `test_*`,
    `estop`, `rc_*` törlése. (A jelenlegi `test_heartbeat: true` orphan kulcs
    helyett az új csatorna-név kerüljön be: `pedal_heartbeat: true`.)
- **Érintett fájlok (nyitott):** `devices/PEDAL/config.json`.
- **Smoke gate:** PEDAL flash + `ros2 topic list` + `ros2 topic echo /robot/heartbeat`.

---

## Lezárt tételek

### BL-018 — RC GP8..GP11 generic Bool I/O csatornák (cmd sub + state pub) — LEZÁRVA 2026-04-21

**Cél:** Az RC bridge-en a GP8..GP11 digitális pinek legyenek ROS2 oldalról
vezérelhetők és visszaolvashatók. GP8 fizikailag a világítás-relét hajtja
(horgász-LED bar), a többi GP9..GP11 aktuálisan szabad — bármilyen
ACTIVE_HIGH relé / LED bedrótozható későbbi bővítéskor (nem kell firmware
rebuild). Ez a BL-012 utódja: az eredeti ticket GP07-et és A0-t is tartalmazott,
de GP07 hw-en nem elérhető, az A0 ADC-t külön tickethez pakolhatjuk ha
tényleg kell.

**Csatornák:**

| csatorna | pin | típus | irány | topic (default) | period |
|---|---|---|---|---|---|
| `gp8`  | GP8  | Bool | sub + state pub | `/robot/gp8` + `/robot/gp8_state`  | 200 ms (5 Hz) |
| `gp9`  | GP9  | Bool | sub + state pub | `/robot/gp9` + `/robot/gp9_state`  | 200 ms |
| `gp10` | GP10 | Bool | sub + state pub | `/robot/gp10` + `/robot/gp10_state` | 200 ms |
| `gp11` | GP11 | Bool | sub + state pub | `/robot/gp11` + `/robot/gp11_state` | 200 ms |

A `channel_t` descriptor `topic_pub` ÉS `topic_sub` mezeit egyszerre használja
— a `channel_manager` BL-015 után már mindkét irányt tudja regisztrálni
egyetlen csatornából.

**State feedback (ERR-033 workaround):** a `read()` nem a fizikai pint olvassa
vissza `gpio_pin_get_dt()`-vel, mert a Zephyr RP2040 driver
`GPIO_OUTPUT_INACTIVE`-val nem kapcsolja be az input buffert → output pinen
mindig 0-t ad. Helyette per-pin `static bool gpX_state_cache` változó tartja
a legutóbb kiadott parancsot; `write()` frissíti a cache-t, `read()` ezt
publikálja 5 Hz-en. Pure output pinen nincs külső hatás, tehát a write-echo
semantikailag azonos a tényleges pinszinttel. A `drv_gpio` közös réteget
nem kellett módosítani.

**Érintett fájlok:**
- `apps/rc/boards/w5500_evb_pico.overlay` — 4 új output alias (`gp8-out` ..
  `gp11-out`), a régi `lights_out` kulcs törölve.
- `apps/rc/src/gpio_out.{c,h}` (új) — 4 `channel_t` descriptor a fenti
  pattern szerint, közös `gpio_write_common()` helper.
- `apps/rc/src/user_channels.c` — `gp8..gp11` registerelve a
  `register_if_enabled()`-del, a régi egyedi `lights_*` kulcs eltávolítva.
- `apps/rc/CMakeLists.txt` — `src/gpio_out.c` hozzáadva, a régi `src/lights.c`
  törölve a target_sources listából.
- `devices/RC/config.json` — `channels:` blokkban `gp8..gp11: {enabled:true}`
  felvéve; régi orphan `test_*`, `estop`, `lights` kulcsok (BL-016 RC rész)
  törölve; `rc_ch3.topic` átállítva `lights_input`-ra a BL-019 host bridge
  subscribe-hoz.

**Hw-verifikáció (2026-04-21 dev subnet):** mind a 4 pin ACTIVE_HIGH,
`ros2 topic pub /robot/gpX std_msgs/msg/Bool "{data: true}" -r 2` → DMM magas,
`{data: false}` → DMM alacsony; mind a 4 `/robot/gpX_state` 5 Hz-en echózik.
GP8 esetén a világítás-relé kattan, a LED-bar ki/be megy — user megerősítette.

**Nyitott követő tétel:** BL-019 — host oldalon (rclpy) a TX CH3 →
`/robot/gp8` bridge formalizálása (prototípus `/tmp/rc_lights_bridge.py`-ban
működött).

---

### BL-013 — RC config: prod subnet visszaállítása teszt után — LEZÁRVA 2026-04-21

`devices/RC/config.json` visszaállítva prod subnetre (`ip=10.0.10.22`,
`gateway=10.0.10.1`, `agent_ip=10.0.10.1`, `dhcp=false`) a BL-018 commit-ban.
A dev subnetre (`192.168.68.202`, dhcp=true, agent `192.168.68.125`) csak a
fejlesztő laptopról történő teszteléshez váltsunk vissza ideiglenesen, majd
a session végén mindig vissza prod-ra push előtt (user explicit feedback).

**Érintett fájl:** `devices/RC/config.json`.

---

### BL-012 — RC bridge jövőbeni GPIO csatornák (GP07..GP11 + A0) — SUPERSEDED 2026-04-21

**Státusz:** superseded BL-018 által — az RC bridge-en GP8..GP11 Bool I/O
csatornák 2026-04-21-én implementálva + hw-verifikálva. A BL-012 eredeti
scope-jából:
- **GP8..GP11 Bool I/O:** kész — BL-018 (bidirectional cmd + state).
- **GP07:** a hw layouton nem érhető el erre a boardra, elhagyva.
- **A0 (ADC0) Float32:** nem implementálva. Ha tényleg kellene, nyissunk
  új ticketet — a scope BL-018-éval eltér (ADC1 channel read → Float32
  normalizált) és eltérő drv_adc integrációt igényel.

---

### BL-017 — `okgo_led` Bool subscribe callback nem futott — LEZÁRVA 2026-04-21

**Gyökérok:** a `param_server_init` belül bukott `RCL_RET_INVALID_ARGUMENT`-tel
(`error: 11`), de a `rclc_executor_add_parameter_server_with_context`
a belső service-init hiba ELŐTT már regisztrálta az összes 6 handle-t a
`rclc_executor.handles[]` tömbbe `initialized=true` állapotban. Emiatt a
`rclc_executor_spin_some` loop (`for (i=0; i<max_handles && handles[i].initialized; i++)`)
végigment mind a 6 törött param-service handle-ön ÉS a valós okgo_led sub
handle-ön is, de a törött handle-ok dispatch-fázisában vagy a belső state
inkonzisztens volt, vagy a DATA pull nem vitt adatot. Konkrét tünet:
`okgo_led_write()` callback **soha nem futott** a sub DATA érkezésére.

**Fix:** a `param_server_init` hívás eltávolítva a `common/src/main.c`-ből
(`ros_session_init`). Az executor handle_count-ból is kivéve a
`PARAM_SERVER_HANDLES (6)` konstans — executor most tiszta 1 handle-lel
működik az E_STOP-on. A `config.json` a jedina ill. egyetlen igazság-forrás
a csatorna-paraméterekhez; a `ros2 param` interaktív paraméterkezelés
nem elérhető, de a user explicit elvetette (nem kell).

**Verifikáció (2026-04-21, dev subnet 192.168.68.x):** flash után
`ros2 topic pub /robot/okgo_led std_msgs/msg/Bool "{data: true}"` →
Pico log: `<inf> okgo_led: write CB: val=1` → GP22 ACTIVE_HIGH magas →
LED világít. 5 publish-ből 4 callback entry log látszott (1 a capture-
start előtt veszett el); LED tényleges váltása vizuálisan megerősítve.

**Érintett fájlok:**
- `common/src/main.c` — `param_server_init` call kivéve, handle_count
  csökkentve, új `LOG_INF("Executor: %d subs + %d svcs = %d handles")`
- `apps/estop/src/okgo_led.c` — `LOG_DBG("write CB: val=%d")` a dispatch-path
  dokumentálására (default off)

**Nyitott követő tétel:** a `common/src/bridge/param_server.{c,h}` fájlok
maradnak a fában (nem hívott holt kód). BL-018 lesz: ha valaha visszakerül
az interaktív paramserver, root-cause kell az `error: 11`-re (valószínű
gyanúsítottak: node/namespace string-kezelés a `rclc_parameter_server_init_service`-ben
`rcl_node_get_name()`+service_name konkatenáció 32-byte buffer, vagy a
service init stream_id előkészítésében). Most nem prioritás.

---

### BL-001 — `west.yml` pinelése — LEZÁRVA 2026-04-19

A `zephyr.revision: main` → `v4.2.2`, `micro_ros_zephyr_module.revision: jazzy`
→ SHA `87dbe3a9` pinek alkalmazva. A Zephyr v4.2.2 a sweet spot (SDK 0.17.4
kompat + `zephyr/posix/time.h` még megvan). A micro_ros SHA-pin biztosítja,
hogy a west update ne driftelje az ismert-jó HEAD-et. Részletek: ERR-025.

### BL-002 — Reprodukálható Pico firmware build Linuxon — LEZÁRVA 2026-04-19

Egy tiszta `rm -rf workspace && make workspace-init && make build` flow
zöld, mert a `workspace-init` automatikusan hívja a `apply-patches`
targetet, a `build` szintén függ tőle. Lásd memory.md §0 és ERR-025..028.

### BL-007 — Flash port Linux-on — LEZÁRVA 2026-04-19

`tools/flash.sh` cross-platform adaptálva: `uname -s` alapján macOS `/Volumes/RPI-RP2` + `/dev/tty.usbmodem*`, Linux `/media/$USER/RPI-RP2` (+ `/run/media/$USER/RPI-RP2` fallback) + `/dev/ttyACM*`.
`Makefile` `FLASH_PORT` default: `ifeq ($(UNAME_S),Darwin)` branch → macOS/Linux automatikus. `?=` operátor, felülírható `FLASH_PORT=/dev/ttyACM1 make flash`-sal.

### BL-008 — Single central config.json template vs devices/ eltérés — LEZÁRVA 2026-04-19

`app/config.json` migrálva `10.0.10.x` subnetre: ip `10.0.10.20`, gateway+agent_ip `10.0.10.1`.
Logika: ez a firmware-be égetett fallback default; az `upload_config.py` futtatásával a board felülírja a `devices/*/config.json` értékével.
`10.0.10.20` szándékos placeholder (kívül a 21–23 device range-en) — DHCP-s boardoknak sem okoz ütközést.

### BL-003 — `bridge config set` node_name validáció — LEZÁRVA 2026-04-19

A `config_set()` mostantól ROS2 identifier szabály szerint validálja a `ros.node_name` (`[a-zA-Z][a-zA-Z0-9_]*`) és a `ros.namespace` (`/` vagy `/seg[/seg...]` szegmensenként ugyanez a szabály) mezőket. Érvénytelen érték `-EINVAL`-t ad, a shell oldal konkrét hibaüzenettel utasítja el (`OK/BAD` példákkal). Mivel az `upload_config.py` is a `bridge config set`-en át dolgozik, a validáció ezen az úton is érvényesül — ezzel megszűnik a BOOTSEL-lock reprodukciója érvénytelen `node_name`-mel (ERRATA_BOOTSEL.md). Érintett fájlok: `app/src/config/config.c` (is_valid_ros2_name + is_valid_ros2_namespace), `app/src/shell/shell_cmd.c` (hibaüzenet `-EINVAL`-ra), `ERRATA_BOOTSEL.md`.

### BL-004 — ERR-001 diagnosztika mélyebbre — LEZÁRVA 2026-04-19

`CONFIG_SYS_HEAP_RUNTIME_STATS=y` + `param_server.c:117–137` logolja a heap állapotot az `rclc_parameter_server_init_with_option` **előtt és után** (`Heap before/after param_server: free=... alloc=... max=...`). Ezzel a következő `error: 11` reprodukciónál egy pillantásból eldönthető, hogy `RCL_RET_BAD_ALLOC` (heap szűkösség) vagy `RCL_RET_INVALID_ARGUMENT` a gyökérok — lásd `ERRATA.md` §ERR-001. A diagnosztikai infrastruktúra kész, a feladat az első reprodukciónál vált élesbe.

### BL-011 — RC bridge zajmérés + EMA filter tuning — LEZÁRVA 2026-04-20

A user zajos kimenetet jelzett az RC bridge csatornáin. A diagnosztikához létrehozott `tools/rc_measure.py` (3 subcommand: `measure` fázisos, `stream` folyamatos, `compare` CSV delta) rclpy-alapú mérőeszközzel 12 s folyamatos felvétel készült három alpha értékre, a stickek folyamatos full-sweep mozgatása mellett. A konstans csatornák (CH3 = `rc_ch3`, CH5 = `rc_mode`) szolgáltatják a tiszta zajfloor-t, a mozgatott CH1/CH2 (`motor_right`/`motor_left`) a dinamikai ellenőrzést.

**Eredmények (std a konstans CH3/CH5-ön):**

| α     | CH3 std | CH5 std | zaj vs. floor | lag (≈1/α · 24 ms) |
| ----- | ------- | ------- | ------------- | ------------------ |
| 1.0   | 0.0436  | 0.0468  | **100%**      | ~24 ms (nincs)     |
| 0.30  | 0.0181  | 0.0217  | **~44%**      | ~80 ms             |
| 0.25  | 0.0207  | 0.0203  | **~45%**      | ~95 ms             |

A mozgatott CH1/CH2 mindhárom értéknél megtartja a ±1 full range-et (p2p ≈ 1.95–2.00), a 42 Hz publikációs ráta változatlan. Alpha 0.25 és 0.30 közti zajcsökkenés különbség a mérési varianciát nem lépi át (~5%), de a 0.30 **~15 ms-mal gyorsabb válaszidőt ad** ugyanazért a zajcsökkenésért.

**Választott érték:** `rc_trim.ema_alpha = 0.30` (gyorsabb response, azonos zajjavítás).

**Firmware-oldali megerősítés** (korábbi commit): az EMA kód (`app/src/user/rc.c`) `alpha >= 1.0f` esetén passthrough (filter off), `alpha < 1.0f` esetén `ema_state = α · norm + (1-α) · ema_state` első híváskor önmagát inicializálja. A `bridge config set rc_trim.ema_alpha <val>` reboot nélkül hat (live read a `g_config.rc_trim.ema_alpha`-ból minden normalizálási körben), így a tuning interaktív volt.

**Érintett fájlok:**
- `tools/rc_measure.py` (új) — mérőeszköz rclpy subscription + per-channel stat + summary/raw CSV export, compare tool pairwise std/p2p delta+ratio.
- `tools/docker-run-ros2.sh` — új mount pontok (`$SCRIPT_DIR:/tools:ro`, `logs:/logs:rw`) és `-w /logs` workdir, hogy a konténerből közvetlen legyen futtatható.
- `.gitignore` — `logs/` kizárva (CSV mérési output nem kerül verziókezelés alá).
- `devices/RC/config.json` — `rc_trim.ema_alpha: 0.30` commit + `upload_config.py` → flash save.

### BL-010 — W6100 chip Ethernet nem működik — LEZÁRVA 2026-04-19

**Eredeti hipotézis (téves):** a W6100 SPI protokoll nem kompatibilis a W5500-zal, ezért a common block olvasások mind 0x00-t adnak.

**Tényleges gyökérok (ERR-030 részletesen):** az SPI keretformátum (3-byte header, BSB<<3, R/W bit) teljesen megegyezik — a probléma az init szekvenciában volt:

1. **Hiányzó reset pulzus.** A W5500 driver csak elengedi a reset vonalat; a W6100-nak aktív pulzus kell (T_RST ≥ 2 µs + T_STA ≤ 100 ms).
2. **Hiányzó CHPLCKR/NETLCKR unlock.** A W6100 common és network blokkjai reset után zártak; írás/olvasás mindenhol 0x00-át eredményez, amíg `CHPLCKR=0xCE` és `NETLCKR=0x3A` unlock nem történik.

**Javítás:** Zephyr upstream W6100 driver (PR #101753) backportolva v4.2.2 alá **out-of-tree Zephyr modulként** az alkalmazás fa alatt. Nem patcheljük többé a W5500 drivert (Patch 3/4/5 eltávolítva `apply.sh`-ból — ezek az eredeti téves hipotézis kompenzációi voltak).

**Szükséges v4.2.2 API adaptációk** az upstream (Zephyr main) driverhez képest:
- `net_eth_mac_load()` + `NET_ETH_MAC_DT_INST_CONFIG_INIT()` → kivéve (v4.3.0 API); helyette W5500-mintás `.mac_addr = DT_INST_PROP(0, local_mac_address)` `COND_CODE_1(DT_NODE_HAS_PROP(..., local_mac_address), ..., ())`.
- `NET_AF_UNSPEC` → `AF_UNSPEC`.
- `SPI_DT_SPEC_INST_GET(inst, SPI_WORD_SET(8))` → `SPI_DT_SPEC_INST_GET(inst, SPI_WORD_SET(8), 0)` (3-argumentumos v4.2.2 API).

**Érintett fájlok:**
- Új: `app/modules/w6100_driver/` (7 fájl — `zephyr/module.yml`, `zephyr/CMakeLists.txt`, `zephyr/Kconfig`, `zephyr/dts/bindings/ethernet/wiznet,w6100.yaml`, `drivers/ethernet/eth_w6100.{c,h}`, `drivers/ethernet/Kconfig.w6100`).
- `app/CMakeLists.txt` — `ZEPHYR_EXTRA_MODULES` kiegészítve a modul útjával.
- `app/boards/w5500_evb_pico.overlay` — `&ethernet { compatible = "wiznet,w6100"; };` override.
- `app/prj.conf` — `CONFIG_ETH_W5500=n` + `CONFIG_ETH_W6100=y`.
- `tools/patches/apply.sh` — Patch 3/4/5 törölve (marad Patch 1 + 2).

**Verifikáció (EVB-Pico, 2026-04-19):**

```
<inf> eth_w6100: W6100 Initialized
<inf> eth_w6100: w5500@0 MAC set to 0c:2f:94:30:58:11
<inf> eth_w6100: w5500@0: Link up
<inf> eth_w6100: w5500@0: Link speed 10 Mb, half duplex
<inf> main: Ethernet link UP
<inf> main: Network: static IP 192.168.68.200
<inf> main: Searching for agent: 192.168.68.125:8888 ...
```

Build méret hatás: `zephyr.uf2` 863232 B → 864768 B (+1.5 KB), RAM 97.45% (változatlan).

**Follow-up bug (ERR-031) — end-to-end kiegészítés, 2026-04-19:**

Az első board-on-board teszt során kiderült, hogy a session handshake nem zárul le: agent látja a PEDAL UDP CREATE_CLIENT csomagokat, de a válasz STATUS reply nem ér vissza, és host→Pico ping is 100% loss. Az `ip neigh show 192.168.68.200` a chip *régi/default* MAC-jét mutatta (`00:00:00:01:02:03`), nem a hwinfo-alapú `0c:2f:94:30:58:11`-et.

Root cause: a W6100 **MACRAW módban** fut (socket 0), így a Zephyr L2 réteg építi a teljes Ethernet keretet szoftveresen, és a src MAC-et az `iface->link_addr`-ból olvassa. A `w6100_set_config(MAC_ADDRESS)` frissítette a chip SHAR-t és a `ctx->mac_addr`-t, **de nem hívta** `net_if_set_link_addr`-et — a kimenő keret src MAC-je ezért a `w6100_iface_init`-ben beállított kezdő értéken ragadt.

Javítás (`app/modules/w6100_driver/drivers/ethernet/eth_w6100.c`): a SHAR write után `net_if_set_link_addr(ctx->iface, ctx->mac_addr, 6, NET_LINK_ETHERNET)` hívás hozzáadva (részletes naplózás: ERRATA.md §ERR-031).

**End-to-end verifikáció (2026-04-19, ERR-031 javítás után):**
- `ip neigh show 192.168.68.200` → `lladdr 0c:2f:94:30:58:11 REACHABLE`
- `ping -c 3 192.168.68.200` → 0% loss, ~4.3 ms RTT
- Agent log: `client_key: 0x5496464D` (valid session)
- `ros2 node list` → `/robot/pedal` megjelent
- `ros2 topic hz /robot/heartbeat` → 1.00 Hz stabil (`std_msgs/msg/Bool`)
