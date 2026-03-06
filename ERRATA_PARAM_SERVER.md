# Errata: param_server_init error: 11

**Dátum:** 2026-03-06
**Állapot:** Nyitott — root cause ismeretlen, folytatás szükséges

---

## A hiba

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

---

## Hibakód elemzés

`error: 11` = `RCL_RET_INVALID_ARGUMENT = 11` (közvetlen), VAGY
`RCL_RET_BAD_ALLOC (10) | RCL_RET_ERROR (1) = 11` (bitwise OR)

A `rclc_parameter_server_init_with_option` `ret |= ...` mintával OR-olja össze
a 6 service init visszatérési értékét + `init_parameter_server_memory_low` eredményét.

---

## Kizárt okok

### 1. prj.conf Kconfig idézőjel-csapda
**Hipotézis:** A string típusú micro-ROS Kconfig változók idézőjelek nélkül vannak,
a Zephyr csendben figyelmen kívül hagyja, alapértelmezett (=0) értékekkel fordít.

**Vizsgálat:** `app/prj.conf` és `workspace/build/zephyr/.config` ellenőrzése.

**Eredmény: KIZÁRVA.** Az értékek helyesen, idézőjelben vannak:
```
CONFIG_MICROROS_NODES="1"
CONFIG_MICROROS_PUBLISHERS="20"
CONFIG_MICROROS_SUBSCRIBERS="16"
CONFIG_MICROROS_CLIENTS="0"
CONFIG_MICROROS_SERVERS="8"
```

---

### 2. libmicroros.a rossz limitekkel fordult
**Hipotézis:** A könyvtár a régi / alapértelmezett értékekkel (`MAX_SERVICES=0`) fordult,
annak ellenére, hogy a meta fájl helyes.

**Vizsgálat:**
- `libmicroros/include/rmw_microxrcedds_c/config.h` (beégett értékek)
- Build log: `micro_ros_src/log/build_2026-03-06_17-36-55/logger_all.log`

**Eredmény: KIZÁRVA.** A lefordított könyvtárban:
```c
#define RMW_UXRCE_MAX_NODES 1
#define RMW_UXRCE_MAX_PUBLISHERS 20
#define RMW_UXRCE_MAX_SUBSCRIPTIONS 16
#define RMW_UXRCE_MAX_SERVICES 8    // ← helyes
```
A build log megerősíti: mindkét build (`17:36` és `18:01`) `MAX_SERVICES=8`-cal futott.

---

### 3. Executor handle count hiány
**Hipotézis:** A `rclc_executor_init` harmadik paramétere túl kicsi,
nem fér el a param server 6 handle-je.

**Vizsgálat:** `main.c` és `param_server.h`.

**Eredmény: KIZÁRVA.**
```c
// main.c:
int handle_count = sub_count + PARAM_SERVER_HANDLES + service_count();
// PARAM_SERVER_HANDLES = RCLC_EXECUTOR_PARAMETER_SERVER_HANDLES = 6
```
A handle count helyesen tartalmazza a param server igényét.
(Megjegyzés: a hiba a `rclc_parameter_server_init_with_option`-ból jön,
nem a `rclc_executor_add_parameter_server_with_context`-ból.)

---

### 4. rcl_interfaces service típusok hiányoznak
**Hipotézis:** A `ROSIDL_GET_SRV_TYPE_SUPPORT(...)` NULL-t ad vissza valamelyik
service-re, mert nincs lefordítva.

**Vizsgálat:** `micro_ros_src/src/rcl_interfaces/rcl_interfaces/srv/` és `nm libmicroros.a`.

**Eredmény: KIZÁRVA.** Mind a 6 szükséges service jelen van:
- `GetParameters`, `GetParameterTypes`, `SetParameters`
- `SetParametersAtomically`, `ListParameters`, `DescribeParameters`

A `libmicroros.a`-ban megtalálhatók a megfelelő type support objektumok.
(Csak `test_msgs` van COLCON_IGNORE-olva, az rcl_interfaces maga nem.)

---

### 5. Service névhossz overflow
**Hipotézis:** A `rclc_parameter_server_init_service` 50 byte-os statikus buffere
(`RCLC_PARAMETER_MAX_STRING_LENGTH`) megtelik.

**Vizsgálat:** Kézzel kiszámolt névhosszak.

**Eredmény: KIZÁRVA.**
```
node_name:    "pico_bridge"                         = 11 char
leghosszabb:  "/set_parameters_atomically"           = 26 char
összesen:     "pico_bridge/set_parameters_atomically" = 37 char < 50
```

---

### 6. Topic/service névhossz overflow az rmw rétegben
**Hipotézis:** A `RMW_UXRCE_TOPIC_NAME_MAX_LENGTH = 60` byte-os buffer megtelik
az expanded service névvel + prefix/suffix kombinációban.

**Vizsgálat:** `rmw_service.c` + `utils.c` + névhossz számítás.

**Eredmény: KIZÁRVA.**
```
Expanded name:  "/pico_bridge/set_parameters_atomically" = 38 char < 60
Request topic:  "rq/pico_bridge/set_parameters_atomicallyRequest" = 47 char < 60
Reply topic:    "rr/pico_bridge/set_parameters_atomicallyReply"   = 45 char < 60
```

---

### 7. Heap kimerülés
**Hipotézis:** A 96KB heap szűk, a param server inicializálása előtt már
kimerül, az allokációk meghiúsulnak.

**Vizsgálat:** ELF szekció elemzés, heap allokáció becslés.

**Eredmény: VALÓSZÍNŰTLEN, de nem teljesen kizárva.**

Statikus RAM helyzet:
```
Összes RAM:  270,336 bytes (264 KB)
Foglalt:     262,344 bytes (97%)
Szabad:        7,992 bytes (~8 KB) — ez statikus foglalás!
```

A heap pool maga 96KB (`CONFIG_HEAP_MEM_POOL_SIZE=98304`) és a `noinit`
szekció részeként statikusan van lefoglalva. A param_server_init előtt
becsülhetően max ~3-5 KB heap van felhasználva (rcl impl struktúrák,
topic névstringek). A 6 service + parameter memory összesen ~10-15 KB
lenne — elvileg bőven elfér a 96KB-os heap-ben.

**Megjegyzés:** Futásidejű mérés nélkül nem 100%-ig kizárható.

---

## Jelenlegi állapot / következő lépések

A root cause sztatikus analízissel nem azonosítható be. A legvalószínűbb
területek, ahol a hiba keletkezhet:

1. **XRCE session timeout** a service-ek DDS entitás-létrehozásakor
   (`run_xrce_session` → agent nem válaszol időben valamelyik service-re)

2. **Heap allokációs hiba** `rcl_resolve_name` → `rcutils_string_map_init`
   hívásánál (bár valószínűtlen)

3. **init_parameter_server_memory_low** valamelyik allokációja meghiúsul

### A folytatáshoz szükséges lépések

**A.) Futásidejű heap diagnostika**

`app/prj.conf`-ba hozzáadni ideiglenesen:
```
CONFIG_SYS_HEAP_RUNTIME_STATS=y
```

`app/src/bridge/param_server.c`-be a `rclc_parameter_server_init_with_option`
hívás elé:
```c
#include <zephyr/sys/heap_listener.h>
// vagy sys_heap_runtime_stats_get() API-val
```

**B.) Granulálisabb hibalog**

Ha a heap rendben van, a param_server.c-t módosítani kell úgy, hogy
egyenként hívja meg a 6 service initet és logolja melyik hiúsul meg.
Ehhez a `rclc_parameter_server_init_with_option` logikáját kellene
leutánozni helyi kóddal.

**C.) XRCE timeout növelés**

A `RMW_UXRCE_ENTITY_CREATION_TIMEOUT = 1000` ms megduplázása (`2000`)
— ha az oka timing, ez megoldhatja. Ehhez könyvtárújrafordítás szükséges
(`configured_colcon.meta` módosítása).

---

## Érintett fájlok

| Fájl | Megjegyzés |
|------|-----------|
| `app/prj.conf` | Konfigurációs értékek — rendben |
| `app/src/bridge/param_server.c` | A hiba itt jelentkezik |
| `app/src/main.c` | Executor handle count — rendben |
| `workspace/modules/lib/micro_ros_zephyr_module/modules/libmicroros/configured_colcon.meta` | Beégett limitek — rendben |
| `workspace/modules/lib/micro_ros_zephyr_module/modules/libmicroros/include/rmw_microxrcedds_c/config.h` | MAX_SERVICES=8 — rendben |

---

## Mellékhatás / jelenlegi működés

A param server hiba **nem fatális**. A rendszer fut:
- Csatornák (estop, counter, heartbeat, echo) normálisan publisholnak
- `/lfs/ch_params.json` betöltése boot-kor rendben zajlik
- `ros2 param set/get/list` nem elérhető amíg a hiba fennáll
