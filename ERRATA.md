# Errata — W6100 EVB Pico micro-ROS Bridge

**Utolsó frissítés:** 2026-03-07

Ez a dokumentum az összes ismert hibát tartalmazza.

---

## Tartalom

| ID | Rövid leírás | Súlyosság | Állapot |
|----|-------------|-----------|---------|
| [ERR-001](#err-001) | `param_server_init error: 11` | Közepes | Nyitott — heap stats logolás hozzáadva |
| [ERR-002](#err-002) | `/diagnostics` IP mező DHCP módban statikus IP-t mutat | Alacsony | **Javítva** — net stack-ből olvassa + MAC mező |
| [ERR-003](#err-003) | Router-en az eszköz neve "zephyr", nem a konfigurált node_name | Alacsony | **Javítva** — hostname = ROS\_Bridge + node\_name |
| [ERR-004](#err-004) | Több bridge ugyanazon a hálózaton: kapcsolatvesztés (+ 50 eszköz skálázás) | Kritikus | **Javítva** — ERR-005 root cause megoldva |
| [ERR-005](#err-005) | Hardcoded MAC cím — minden board azonos `00:00:00:01:02:03` | Kritikus | **Javítva** — hwinfo auto-gen + config.json override |

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

A hiba forrása a `param_server.c`-ben:
```c
rcl_ret_t rc = rclc_parameter_server_init_with_option(&param_server, node, &opts);
if (rc != RCL_RET_OK) {
    LOG_ERR("param_server_init error: %d", (int)rc);
    return -EIO;
}
```

A rendszer a hiba ellenére fut tovább. A param server nélkül:
- A JSON-ból betöltött paraméterek (`/lfs/ch_params.json`) érvényesülnek boot-kor
- A csatornák az elmentett / alapértelmezett értékekkel indulnak
- `ros2 param set/get/list` nem elérhető

#### Hibakód elemzés

`error: 11` = `RCL_RET_INVALID_ARGUMENT = 11` (közvetlen), VAGY
`RCL_RET_BAD_ALLOC (10) | RCL_RET_ERROR (1) = 11` (bitwise OR)

A `rclc_parameter_server_init_with_option` `ret |= ...` mintával OR-olja össze
a 6 service init visszatérési értékét + `init_parameter_server_memory_low` eredményét.

#### Kizárt okok

| # | Hipotézis | Eredmény |
|---|-----------|----------|
| 1 | `prj.conf` Kconfig idézőjel-csapda | **KIZÁRVA** — értékek helyesen idézőjelben vannak |
| 2 | `libmicroros.a` rossz limitekkel fordult | **KIZÁRVA** — `MAX_SERVICES=8` a binárisban |
| 3 | Executor handle count hiány | **KIZÁRVA** — `PARAM_SERVER_HANDLES = 6` benne van |
| 4 | `rcl_interfaces` service típusok hiányoznak | **KIZÁRVA** — mind a 6 service jelen van |
| 5 | Service névhossz overflow (50 byte buffer) | **KIZÁRVA** — leghosszabb = 37 char < 50 |
| 6 | Topic/service névhossz overflow az rmw rétegben (60 byte) | **KIZÁRVA** — leghosszabb = 47 char < 60 |
| 7 | Heap kimerülés | **VALÓSZÍNŰTLEN** — 96KB heap, becsülve ~15KB kellene; futásidejű mérés nélkül nem 100%-ig kizárható |

#### Legvalószínűbb területek

1. **XRCE session timeout** a service-ek DDS entitás-létrehozásakor
   (`run_xrce_session` → agent nem válaszol időben valamelyik service-re)
2. **Heap allokációs hiba** `rcl_resolve_name` → `rcutils_string_map_init` hívásánál
3. **`init_parameter_server_memory_low`** valamelyik allokációja meghiúsul

#### Következő lépések

- **A.) Futásidejű heap diagnostika:** `CONFIG_SYS_HEAP_RUNTIME_STATS=y` hozzáadása, heap állapot logolása a `rclc_parameter_server_init_with_option` hívás előtt/után
- **B.) Granulálisabb hibalog:** a 6 service init egyenkénti hívása és logolása
- **C.) XRCE timeout növelés:** `RMW_UXRCE_ENTITY_CREATION_TIMEOUT` megduplázása (1000ms → 2000ms) — könyvtárújrafordítás szükséges

#### Érintett fájlok

| Fájl | Megjegyzés |
|------|-----------|
| `app/src/bridge/param_server.c` | A hiba itt jelentkezik |
| `app/src/main.c` | Executor handle count — rendben |
| `app/prj.conf` | Kconfig értékek — rendben |
| `workspace/modules/lib/micro_ros_zephyr_module/modules/libmicroros/configured_colcon.meta` | Beégett limitek — rendben |

#### Mellékhatás

A param server hiba **nem fatális**. A rendszer fut:
- Csatornák normálisan publisholnak
- `/lfs/ch_params.json` betöltése boot-kor rendben zajlik
- `ros2 param set/get/list` nem elérhető amíg a hiba fennáll

---

## ERR-002

### `/diagnostics` IP mező DHCP módban a statikus IP-t mutat

**Dátum:** 2026-03-07
**Állapot:** Nyitott

#### A hiba

Ha a `config.json`-ban `"dhcp": true`, a bridge DHCP-vel kap IP címet a routertől.
A `/diagnostics` topicban viszont az `ip` mező továbbra is a `config.json`-ban
szereplő statikus IP címet adja vissza, nem a DHCP által ténylegesen kapottat.

Példa: a config-ban `"ip": "192.168.68.115"`, a DHCP kioszt mondjuk `192.168.68.42`-t,
de a `/diagnostics` az `ip` mezőben `192.168.68.115`-öt mutat.

#### Root cause

A `diagnostics_publish()` (`app/src/bridge/diagnostics.c`, 141. sor) a `g_config.network.ip`
értékét másolja be:

```c
strncpy(kv_val4, g_config.network.ip, sizeof(kv_val4) - 1);
```

Ez a config.json-ból betöltött érték, ami DHCP módban nem frissül a tényleges
DHCP-assigned címre.

#### Javítási irány

A `diagnostics_publish()`-ban a `g_config.network.ip` helyett a Zephyr net stack-ből
kellene lekérdezni az aktuális IP címet:

```c
struct net_if *iface = net_if_get_default();
struct net_if_config *cfg = net_if_get_config(iface);
if (cfg->ip.ipv4 && cfg->ip.ipv4->unicast[0].ipv4.is_used) {
    char addr_str[NET_IPV4_ADDR_LEN];
    net_addr_ntop(AF_INET,
                  &cfg->ip.ipv4->unicast[0].ipv4.address.in_addr,
                  addr_str, sizeof(addr_str));
    strncpy(kv_val4, addr_str, sizeof(kv_val4) - 1);
}
```

Alternatíva: az `apply_network_config()` DHCP callback-jében a `g_config.network.ip`
mezőt frissíteni a kapott címre — de ez összekeverheti a "konfigurált" és "aktuális"
értékeket.

#### Érintett fájl

| Fájl | Megjegyzés |
|------|-----------|
| `app/src/bridge/diagnostics.c` | `diagnostics_publish()` — IP forrás javítandó |

---

## ERR-003

### Router-en az eszköz neve "zephyr", nem a konfigurált node_name

**Dátum:** 2026-03-07
**Állapot:** Nyitott

#### A hiba

A hálózati routeren az eszköz "zephyr" (vagy "zephir") néven jelenik meg.
Ez a Zephyr RTOS alapértelmezett hostname-je. Nincs lehetőség megkülönböztetni
több bridge-et a router admin felületén.

#### Root cause

A projektben jelenleg nincs `CONFIG_NET_HOSTNAME_ENABLE` beállítva a `prj.conf`-ban,
így a Zephyr alapértelmezett hostname-jét használja, ami `"zephyr"`.

#### Javítási irány

**Preferált megoldás — config.json `node_name` alapján dinamikus hostname:**

A Zephyr `net_hostname_set_postfix()` API lehetővé teszi a hostname utólagos
módosítását futásidőben. A lépések:

1. `prj.conf`-ba:
   ```
   CONFIG_NET_HOSTNAME_ENABLE=y
   CONFIG_NET_HOSTNAME="ROS_Bridge"
   CONFIG_NET_HOSTNAME_UNIQUE=n
   CONFIG_NET_HOSTNAME_MAX_LEN=32
   ```

2. Az `apply_network_config()` elején (a DHCP indítás előtt) meghívni:
   ```c
   net_hostname_set_postfix((uint8_t *)g_config.ros.node_name,
                            strlen(g_config.ros.node_name));
   ```
   Így a hostname pl. `ROS_Bridge_E_STOP` lesz.

**Megjegyzés:** A `net_hostname_set_postfix` a `CONFIG_NET_HOSTNAME` értékéhez
fűzi hozzá az utótagot. Ha a `node_name` önmagában elég egyedi, egyszerűbb
megoldás a `CONFIG_NET_HOSTNAME` használata fallback névként, és futásidőben
csak a postfix beállítása.

**Egyszerű fallback megoldás — statikus hostname:**

Ha a dinamikus megoldás túl komplex, legalább a `prj.conf`-ban a hostname
lecserélhető:
```
CONFIG_NET_HOSTNAME_ENABLE=y
CONFIG_NET_HOSTNAME="ROS_Bridge"
```
Ez nem egyedi bridge-enként, de legalább azonosítja az eszköztípust.

#### Érintett fájlok

| Fájl | Megjegyzés |
|------|-----------|
| `app/prj.conf` | `CONFIG_NET_HOSTNAME_ENABLE` hozzáadandó |
| `app/src/main.c` | `apply_network_config()` — dinamikus hostname beállítás |

---

## ERR-004

### Több bridge ugyanazon a hálózaton: kapcsolatvesztés (+ 50 eszköz skálázás)

**Dátum:** 2026-03-07
**Állapot:** Nyitott — root cause valószínűleg **ERR-005 (azonos MAC cím)**

#### A hiba

Ha több W6100 bridge van ugyanazon a hálózaton és ugyanahhoz a micro-ROS
agent-hez csatlakozik, egy idő után az eszközök elvesztik a kapcsolatot.
A tünet:
- A **user LED kialszik** (= a bridge kilépett a "Phase 3: Run" ciklusból)
- A micro-ROS **agent továbbra is fut** a host oldalon
- A bridge a "Phase 1: Search for agent" állapotba kerül vissza
  (a `rmw_uros_ping_agent` sikertelen lett)
- A hiba nem azonnali: az eszközök egy ideig normálisan működnek, majd
  "egyszerre" esnek le

#### Root cause elemzés

A többeszközös működésnek **5 független egyediségi rétege** van. Jelenleg
mindegyik hibás vagy hiányzik:

| # | Réteg | Jelenlegi állapot | Hatás | Kapcsolódó errata |
|---|-------|-------------------|-------|-------------------|
| 1 | **MAC cím** | `00:00:00:01:02:03` — hardcoded a DTS-ben, **minden board azonos** | A switch/router nem tudja megkülönböztetni az eszközöket. ARP ütközés, csomagok rossz eszközhöz érkeznek. **Ez az elsődleges root cause.** | **ERR-005** |
| 2 | **Hostname** | `"zephyr"` — Zephyr default, minden board azonos | A router admin felületén nem különböztethetők meg. DHCP lease ütközés lehetséges. | **ERR-003** |
| 3 | **Node name** | config.json-ból jön, de a felhasználó felelőssége egyedivé tenni | DDS participant ID ütközés ha két bridge azonos `node_name` + `namespace` kombinációval regisztrál | — |
| 4 | **XRCE client key** | A micro-XRCE-DDS transport hash-ből generálja — azonos MAC + azonos IP esetén azonos lesz | Az agent összekeveri a session-öket, az egyik klienst eldobja | — |
| 5 | **UDP source port** | Zephyr automatikusan osztja ki — de azonos MAC → azonos IP → potenciális ütközés | NAT/agent nem tudja megkülönböztetni a forgalmat | — |

**Láncolat:** ERR-005 (azonos MAC) → azonos DHCP IP → azonos XRCE client key
→ session ütközés → agent eldobja az egyiket → LED kialszik → reconnect loop
→ a másik eszközt is eldobja → mindkét LED kialszik.

#### 50 eszközös forgatókönyv — teljes skálázási elemzés

A cél: **akár 50 W6100 bridge** működjön egyidejűleg ugyanazon a LAN-on,
ugyanahhoz a micro-ROS agent-hez csatlakozva.

##### L1 — Hálózati réteg (Ethernet / IP)

| Szempont | Limit | 50 eszközre | Teendő |
|----------|-------|-------------|--------|
| **MAC cím egyediség** | Kötelező, különben ARP káosz | **BLOKKOLÓ** — jelenleg mind azonos | ERR-005 javítása: config.json-ból vagy auto-generálás |
| **IP cím** | DHCP pool vagy statikus kiosztás | DHCP pool ≥ 50 szabad cím kell | Routeren pool méret ellenőrzés |
| **Hostname** | DHCP hostname ütközés | 50× "zephyr" → lease zavar | ERR-003 javítása: egyedi hostname |
| **Sávszélesség** | 100 Mbit/s half-duplex per eszköz | 50 eszköz × ~10 topic × 100 byte × 10 Hz ≈ **4 Mbit/s** — bőven elfér | Nem probléma |
| **ARP tábla méret** | Router/switch limit | Legtöbb SOHO router ≥ 128 entry | Ellenőrizni a konkrét router-en |

##### L2 — micro-XRCE-DDS réteg (agent oldal)

| Szempont | Limit | 50 eszközre | Teendő |
|----------|-------|-------------|--------|
| **Client key egyediség** | Kötelező, agent per-key session-t tart | Azonos MAC → azonos hash → **BLOKKOLÓ** | MAC javítás (ERR-005) automatikusan megoldja, VAGY explicit `rmw_uros_set_client_key()` |
| **Max kliens szám** | Agent alapértelmezetten nem korlátoz | 50 kliens → ~50 session, elfér | Agent `--discovery-timeout` és memória figyelés |
| **Entity limit per agent** | Nincs hard limit, de memóriafüggő | 50 × (20 pub + 16 sub + 8 srv) = **2200 entity** — az agent heap-jét figyelni | Agent-et dedikált gépen futtatni, ne RPi Zero-n |
| **Session timeout** | Alapértelmezett `SESSION_STATUS_TIMEOUT` | Sok kliensnél a heartbeat válasz lassulhat | Agent `-v6` logból monitorozni |

##### L3 — ROS 2 / DDS réteg

| Szempont | Limit | 50 eszközre | Teendő |
|----------|-------|-------------|--------|
| **Node name egyediség** | Kötelező a ROS graph-ban | 50 bridge → 50 egyedi `node_name` szükséges | Provisioning tool: `upload_config.py` egyedi config per eszköz |
| **Topic névtér** | Ütközés ha azonos topic nevek | 50× azonos topic → összeolvadnak | Namespace-t használni: `/robot_01/`, `/robot_02/`, stb. |
| **Discovery overhead** | DDS participant announcement | 50 participant → ~50 × 3 topic/sec broadcast | Kezelhető, de FastDDS discovery szerver ajánlott >20 eszköz felett |

##### L4 — Firmware / eszköz réteg

| Szempont | Limit | 50 eszközre | Teendő |
|----------|-------|-------------|--------|
| **Flash config** | Egyedi config.json per eszköz | Mind egyedileg flashelendő | `upload_config.py` batch script vagy provisioning séma |
| **RAM** | 264KB per eszköz, ~97% foglalt | Nem skálázási kérdés (per-eszköz) | — |
| **Watchdog** | 30s, per eszköz | Nem releváns | — |

##### Összefoglaló: blokkoló problémák 50 eszközhöz

```
┌──────────────────────────────────────────────────────────────────┐
│  BLOKKOLÓ (nem működik amíg nem javított):                       │
│                                                                  │
│  1. ERR-005: Azonos MAC cím — ARP ütközés, DHCP ütközés,        │
│     XRCE client key ütközés. MINDEN MÁS ERRE ÉPÜL.              │
│                                                                  │
│  2. Node name + namespace egyediség — provisioning kérdés,       │
│     de a felhasználó felelőssége.                                │
├──────────────────────────────────────────────────────────────────┤
│  AJÁNLOTT (működhet nélküle, de instabil/nehéz kezelni):         │
│                                                                  │
│  3. ERR-003: Hostname egyediség — DHCP lease problémák            │
│  4. /diagnostics aktuális IP (ERR-002) — debug nehézség          │
│  5. Explicit XRCE client key beállítás — defense in depth        │
│  6. Provisioning script a 50 config.json generálásához            │
├──────────────────────────────────────────────────────────────────┤
│  OPCIONÁLIS (>20 eszköz felett ajánlott):                        │
│                                                                  │
│  7. FastDDS Discovery Server az agent oldalon                     │
│  8. Agent memória/CPU monitorozás                                │
│  9. MAC cím + IP megjelenítése a /diagnostics-ban                │
└──────────────────────────────────────────────────────────────────┘
```

#### Diagnosztikai lépések (azonnali, javítás előtt)

- **A.) MAC cím ellenőrzés:** Routeren vagy Wireshark-kal megnézni, hogy a
  bridge-ek azonos MAC címmel rendelkeznek-e. **Valószínűleg igen** — lásd
  ERR-005.

- **B.) Agent verbose log:** `ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888 -v6`
  — megnézni, hogy a client_key-ek eltérőek-e.

- **C.) Node name audit:** Minden eszköz config.json-jában a `node_name` és
  `namespace` egyedinek kell lennie.

#### Érintett fájlok

| Fájl | Megjegyzés |
|------|-----------|
| `app/src/main.c` | `bridge_run()` — LED kialszik, reconnect ciklusba kerül |
| `app/config.json` | `node_name` / `namespace` egyediség szükséges |
| `workspace/zephyr/boards/wiznet/w5500_evb_pico/w5500_evb_pico.dts` | Hardcoded MAC — **ERR-005 root cause** |

---

## ERR-005

### Hardcoded MAC cím — minden board azonos `00:00:00:01:02:03`

**Dátum:** 2026-03-07
**Állapot:** Nyitott — **megerősített root cause az ERR-004-hez**

#### A hiba

A board device tree (`w5500_evb_pico.dts`, 157. sor) tartalmaz egy
hardcoded MAC címet:

```dts
ethernet: w5500@0 {
    compatible = "wiznet,w5500";
    ...
    local-mac-address = [00 00 00 01 02 03];
};
```

Az alkalmazás overlay (`app/boards/w5500_evb_pico.overlay`) ezt **nem írja felül**
— csak a `compatible` property-t változtatja `"wiznet,w6100"`-ra.

Következmény: **minden egyes W6100 EVB Pico board ugyanazt a
`00:00:00:01:02:03` MAC címet kapja.** Ráadásul ez a cím:
- Nem érvényes OUI (a `00:00:00` prefix nem regisztrált gyártói cím)
- Nem locally administered (a LAA bit nincs beállítva)
- Egyértelműen placeholder / development érték

#### Hatás

| Szcenárió | Eredmény |
|-----------|----------|
| 1 bridge a hálózaton | Működik — nincs ütközés |
| 2+ bridge a hálózaton | ARP tábla ütközés: a switch/router ugyanazt a MAC-et látja több portról → csomagok random eszközhöz érkeznek → DHCP lease zavar → XRCE session ütközés → **ERR-004 tünetei** |
| 50 bridge a hálózaton | Teljes hálózati káosz — egyik bridge sem tud stabilan kommunikálni |

#### Javítási irány

##### Opció A: MAC cím a `config.json`-ból (preferált)

A `config.json` `network` szekciójába új mező:

```json
{
  "network": {
    "mac": "02:00:00:00:00:01",
    "ip": "192.168.68.115",
    ...
  }
}
```

A `config.h` struktúra bővítése:

```c
typedef struct {
    bool dhcp;
    char mac[20];              /* "02:XX:XX:XX:XX:XX" */
    char ip[CFG_STR_LEN];
    ...
} cfg_network_t;
```

Az `apply_network_config()` elején a MAC cím felülírása a Zephyr net stack-en:

```c
struct net_if *iface = net_if_get_default();

if (strlen(g_config.network.mac) > 0) {
    struct net_eth_addr mac;
    /* parse "02:AA:BB:CC:DD:EE" → 6 bytes */
    if (parse_mac(g_config.network.mac, &mac) == 0) {
        net_if_set_link_addr(iface, mac.addr, sizeof(mac.addr),
                             NET_LINK_ETHERNET);
    }
}
```

**Megjegyzés:** A MAC-nak `02:xx:xx:xx:xx:xx` formátumúnak kell lennie
(LAA bit beállítva a legfelső oktetben), mert nem regisztrált OUI-t használunk.

##### Opció B: Auto-generált MAC az RP2040 unique ID-ből

Az RP2040-nek van egy 64 bites egyedi flash ID-je, ami kiolvasható:

```c
#include <hardware/flash.h>

uint8_t uid[8];
flash_get_unique_id(uid);

struct net_eth_addr mac = {
    .addr = { 0x02, uid[3], uid[4], uid[5], uid[6], uid[7] }
};
```

Előnyök:
- Nincs szükség kézi konfigurációra
- Garantáltan egyedi minden board-on
- A `0x02` prefix jelzi, hogy locally administered

Hátrányok:
- Az RP2040 flash unique ID API (`flash_get_unique_id`) Zephyr alatt
  közvetlenül nem mindig elérhető — a Pico SDK-ból kellhet hívni
- Ha az `pico_unique_board_id` Zephyr-ben wrappelve van, az egyszerűbb

##### Opció C: Overlay-ben `zephyr,random-mac-address`

Az `app/boards/w5500_evb_pico.overlay`-be:

```dts
&ethernet {
    compatible = "wiznet,w6100";
    zephyr,random-mac-address;
    /delete-property/ local-mac-address;
};
```

Ez minden boot-nál új random MAC-et generál. Előnye: egyszerű. Hátránya:
a DHCP szerver minden rebootnál új lease-t ad → IP cím változik boot-onként.
50 eszköz esetén ez az IP management-et nehezíti.

##### Ajánlott kombináció (50 eszközhöz)

| Prioritás | Megoldás | Indok |
|-----------|----------|-------|
| **1. (kötelező)** | Opció B — auto-generált MAC a flash UID-ből | Nincs kézi konfig, garantáltan egyedi, determinisztikus (nem változik reboot-onként) |
| **2. (kiegészítő)** | Opció A — `config.json` felülírás | Ha a felhasználó explicit MAC-et akar (pl. vállalati MAC range), az override-olja az auto-generáltat |
| **3. (fallback)** | Default: `02:` + flash UID utolsó 5 byte | Ha nincs config.json-ban MAC, ez a default |

A boot-szekvencia:

```
1. DTS-ből indulna a 00:00:00:01:02:03 (de ezt felülírjuk)
2. apply_network_config() eleje:
   a. Ha config.json-ban van "mac" → azt használjuk
   b. Ha nincs → RP2040 flash UID-ből generálunk 02:xx:xx:xx:xx:xx
3. net_if_set_link_addr() beállítja az új MAC-et
4. Ezután indul a DHCP / statikus IP konfiguráció
```

#### Érintett fájlok

| Fájl | Megjegyzés |
|------|-----------|
| `workspace/zephyr/boards/wiznet/w5500_evb_pico/w5500_evb_pico.dts:157` | `local-mac-address = [00 00 00 01 02 03]` — a probléma forrása |
| `app/boards/w5500_evb_pico.overlay` | Nem írja felül a MAC-et — bővítendő |
| `app/src/config/config.h` | `cfg_network_t` — `mac` mező hozzáadandó |
| `app/src/config/config.c` | `config_load/save/set` — MAC parse/save logika |
| `app/src/main.c` | `apply_network_config()` — MAC beállítás a net stack-en |
| `app/config.json` | Új `"mac"` mező (opcionális) |

---

## Megjegyzések

- **ERR-005 (azonos MAC) az ERR-004 (multi-bridge kapcsolatvesztés) megerősített
  root cause-a.** Az ERR-005 javítása nélkül a többeszközös működés nem lehetséges.
- Az ERR-003 (hostname) az ERR-005-höz kapcsolódik: egyedi MAC → egyedi DHCP lease,
  de egyedi hostname is szükséges a könnyű azonosításhoz.
- Az ERR-001 (param server) és ERR-004 egymástól függetlenek, de mindkettő a
  micro-XRCE-DDS réteggel kapcsolatos.
- A hibák javítási prioritása: **ERR-005 > ERR-004 > ERR-003 > ERR-001 > ERR-002**
  (az ERR-005 javítása az ERR-004-et is megoldja)

### Provisioning forgatókönyv 50 eszközhöz

Ha az ERR-005 (MAC) és ERR-003 (hostname) javítva van, a 50 eszköz telepítéséhez
a következő adatoknak kell egyedinek lenniük eszközönként:

| Paraméter | Forrás | Automatikus? |
|-----------|--------|-------------|
| MAC cím | Flash UID → auto-generált | **Igen** |
| IP cím | DHCP | **Igen** |
| Hostname | config.json `node_name` → auto | **Igen** (ha ERR-003 javítva) |
| ROS node_name | config.json | **Nem** — felhasználó állítja |
| ROS namespace | config.json | **Nem** — felhasználó állítja |

Minimálisan a felhasználónak eszközönként csak a `node_name`-t és opcionálisan
a `namespace`-t kell beállítania. A `upload_config.py` batch módja:

```bash
for i in $(seq 1 50); do
    python3 tools/upload_config.py --port /dev/ttyACMx \
        --set ros.node_name="bridge_$(printf '%02d' $i)" \
        --set ros.namespace="/fleet"
done
```
