# policy.md — Fejlesztési keretek és alapelvek

> Utolsó frissítés: 2026-04-19
> Státusz: aktív, minden munkamenet előtt és alatt kötelező betartani.

Ez a dokumentum rögzíti a projektgazda által előírt fejlesztési szabályokat és
a bevezető promptot, hogy **egyetlen** új munkamenet se induljon el kontextus
nélkül. A `memory.md` és a `docs/backlog.md` ennek az alapelvnek alárendeltek.

---

## 1. Projekt kontextus (a projektgazda szavaival)

A projekt egy komplex robot ROS2 architektúrájának része. A **micro-ROS
bridge** a roboton kezeli a külső perifériákat (lásd a `devices/` mappát):
E-Stop, RC vevő, pedál, stb. Az eszközök **működő tesztpéldányok**, jelenleg
használatban vannak. Egy **központi micro-ROS firmware** fordul, ami a WIZnet
**W6100 EVB Pico** panelen fut; a `devices/<BOARD>/config.json` a board-specifikus
viselkedést (MAC, hostname, topic-ok, csatornák) adja meg.

### A megoldandó probléma

A fordítás elromlott — nem tudjuk újrafordítani és a micro-ROS eszközöket frissíteni.
A gyökérok feltehetően kompatibilitási probléma a **Zephyr RTOS**, a
**WIZnet W6100-EVB-Pico** és a **micro-ROS Jazzy** között. A projekt korábban
**macOS-en, Docker alatt** lefordult; egy újrafordítás során a környezet elveszett.

Most Linuxon (Ubuntu, ROS2-Bridge repo a `~/Dev/ROS2-Bridge` alatt) kell újra
működőképessé tenni, úgy hogy **a későbbi adatvesztés és fordítási hibák
elkerülhetők legyenek**.

### Megközelítés

A robot már **production-ready** folyamat; ehhez illesztjük a micro-ROS bridge-et,
amely **működő, de prototípus** állapotban van. Tankönyvi tisztaságú architektúrát
és dokumentációt építünk — a cél nem csak a működés, hanem a **megértés** és a
követhetőség is.

---

## 2. Alapelvek (kötelező)

### 0. ELSŐ LÉPÉS — dokumentáció és kódbázis átnézése (nincs kivétel)

Minden munkamenet és minden érdemi feladat előtt **először** át kell nézni:

- a teljes dokumentációt (`*.md` a repo gyökerében és `docs/` alatt, ha van),
- a releváns kódbázist (`app/src/`, `host_ws/src/`, `devices/`, `docker/`, `tools/`),
- a konfigurációs fájlokat (`prj.conf`, `west.yml`, overlay, `CMakeLists.txt`,
  `docker-compose.yml`, `Makefile`, `config.json`-ok),
- szükség esetén a launch/URDF fájlokat.

**Soha nem dolgozunk emlékezetből** — mindig a tényleges fájlokból.

### 1. Kódminőség

- Hibátlan, részletesen dokumentált kód; tankönyvi szintű rendszerezés.
- Ez egy **tanulási célú ROS2 robot projekt** — a tisztaság és követhetőség
  ugyanolyan fontos, mint a működés.
- Komment csak akkor, ha egy nem triviális **miértet** rögzít
  (kényszer, meglepő invariáns, konkrét bug workaroundja). Triviális komment
  nem kerül be.

### 2. Backlog

A TODO-k **nem szétszórva élnek**, hanem a `docs/backlog.md`-ben. Minden
bejegyzés tartalmazza:

- a **kontextust** (mi a helyzet, miért merült fel),
- az **okot** (miért érdemes / szükséges foglalkozni vele),
- az **érintett fájlokat**.

Lezárás után a bejegyzés vagy törölhető, vagy „lezárt” szekcióba mozgatható a
tanulság megőrzése érdekében.

### 3. Feladat lezárás (checklist)

Minden érdemi feladat **csak akkor számít késznek**, ha:

1. Az állapot dokumentálva van (`memory.md` és/vagy `CHANGELOG_DEV.md` +
   érintett doksik frissítve).
2. `git commit` megfelelő, beszélő üzenettel (magyarázza a **miértet** is).
3. `git push` a távoli repoba.

Ha bármelyik lépés elmarad, a feladat **nyitott**.

### 4. Munkamenet eleje

Minden új munkamenet **első parancsa**: `git pull`. Így biztos, hogy a friss
állapotban dolgozunk.

### 5. Memory persistence — `memory.md`

A beszélgetés során szerzett **technikai adatok, mérési eredmények és fontos
döntések** azonnal a `memory.md`-be kerülnek. Ez az elsődleges referenciánk.
A `memory.md` szekcionált, időrendi és tematikus nézetet is kínál.

### 6. Context management

Csak az **aktuálisan szerkesztett** fájlokat tartjuk a kontextusban. Ha egy
modullal végeztünk, kiesik a munkakontextusból (a kredit spórolása érdekében).
Ha később visszatérünk hozzá, a `memory.md` alapján rekonstruálható a
legfontosabb tudás.

### 7. Óvatos, reverzibilitást figyelő műveletek

- Destruktív műveletek (`rm -rf`, `git reset --hard`, force push, workspace
  törlés) előtt **jóváhagyás kötelező**.
- Nem kerülünk meg hibát (`--no-verify`, checksum kikapcsolás, stb.) — a
  gyökérokot keressük.
- Ismeretlen fájl / ág / állapot → **vizsgálat, nem törlés**.

### 8. Pin mindent, ami driftelhet

A projekt újrafordíthatóságának feltétele, hogy **külső függőségek pinelve**
legyenek (git sha, verzió tag, Docker image tag). Ha nem pinelt, az új
bejegyzés a backlogba.

### 9. Dokumentáció és naplózás szinkronban

A `.cursor/rules/errata-changelog.mdc` szabály továbbra is él:

- `CHANGELOG_DEV.md` — session végén új bejegyzés.
- `ERRATA.md` — új hiba → új `ERR-XXX`; javított hiba → állapot frissítés.
- `ONBOARDING.md` — rendszerszintű változás (új service, új parancs, új
  előfeltétel) esetén frissítés.
- `memory.md` — folyamatosan (minden releváns tény mentése).

---

## 3. Eredeti prompt (változatlan formában, referenciaként)

> Nézd át a teljes projektet. Hozz létre egy memória filet és folyamatosan
> tartsd frissen.
>
> A projekt leirata és a megoldandó probléma:
> A projekt egy komplex robot ROS2 architektúra része és elromlott, nem tudom
> leforditani es frissíteni a micro ros eszközöket. A probléma gyökere az, hogy
> a zephyr, a wisnet w6100-evb-pico között kompatibilitási problémák léptek fel.
> Ezt a projektet korábban egy mac-en fordítottam docker alól, ott működött,
> majd később egy ujraforditasnal elveszett a környezet.
>
> A feladatok:
> - elsődleges feladatunk a projekt sikeres fordítása.
> - a microros bridge a roboton kezeli a külső perifériákat, ahogy azt a devices
>   mappában látod. Ezek még csak teszt, de működő eszközök. Jelenleg használva
>   vannak. Egy központi microros eszköz fordul, ami fut a w6100-evb-pico -n és
>   ez megkapja az egyedi konfig filet, ami egyedivé teszi az eszközt.
> - most eddig szeretnénk eljutni olyan módon, hogy később elkerüljük a
>   fordítási hibákat és az adatvesztést.
>
> Megközelítésünk:
> A robot egy tankönyvi példája a robotfejlesztésnek. Már most production ready
> a folyamat és az eszköz. Ehhez illesztjük hozzá a microros eszközünket ami
> működő de még prototípus állapotban van.
>
> Hozz létre egy policy.md filet, amiben mented ezt a promptot, és a fejlesztés
> kereteit.
>
> Alapelvek
> 0. ELSŐ LÉPÉS — mindig, kivétel nélkül: Mielőtt bármit csinálsz, átnézed a
>    teljes dokumentációt (docs/*.md) és a releváns kódbázist (config fájlok,
>    launch fájlok, URDF, forráskód). E nélkül egy lépést sem teszel. Olvasd el
>    a forrásfájlokat, ne emlékezetből dolgozz.
>
> Kódminőség: Hibátlanul és részletesen dokumentált kód, tankönyvi tisztaságú
> rendszerezéssel és dokumentációval. Ez egy tanulási célú ROS2 robot projekt —
> a cél nem csak a működő robot, hanem a megértés és a tiszta, követhető
> architektúra.
>
> Backlog: A TODO-kat a docs/backlog.md-ben gyűjtjük, magyarázva, érthetően.
> Minden bejegyzés tartalmazza a kontextust, az okot és az érintett fájlokat.
>
> Feladat lezárás: Minden feladat végén:
> - Állapot dokumentálása (progress.md vagy releváns docs frissítése)
> - git commit megfelelő üzenettel
> - git push
>
> Munkamenet eleje: Minden munkamenet elején git pull a friss állapotért.
>
> Memory Persistence: A beszélgetés során szerzett minden technikai adatot,
> mérési eredményt és fontos döntést írj be a memory.md fájlba. Ez az elsődleges
> referenciánk.
>
> Context Management: Csak az aktuálisan szerkesztett fájlokat tartsd a
> kontextusban. Ha végeztünk egy modullal, dobd ki a memóriából a kreditek
> spórolása érdekében.

---

## 4. Ellenőrző lista munkamenet végén

- [ ] `memory.md` frissítve (új tények, mérések, döntések).
- [ ] `docs/backlog.md` frissítve (új TODO vagy lezárt bejegyzés).
- [ ] `CHANGELOG_DEV.md` új session-bejegyzés.
- [ ] `ERRATA.md` új vagy módosított `ERR-XXX` szekció, ha volt hiba.
- [ ] `ONBOARDING.md` csak akkor, ha rendszerszintű változás történt.
- [ ] `git status` tiszta.
- [ ] `git push` lefutott.
