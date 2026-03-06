# W6100 Bridge v2.0 — Átnézési kérdések és döntési pontok

> Dátum: 2026-03-06
> Alap: BRIDGE_V2_IMPLEMENTATION_PLAN.md átnézése a kódbázis alapján
> Állapot: LEZÁRVA — minden döntés megvan, implementáció megkezdhető

---

## 1. RAM budget kritikus hiba — LEZÁRVA

**Probléma:**
A terv azt állítja: v2.0 után ~227 KB RAM, ~37 KB szabad.
A `CONFIG_HEAP_MEM_POOL_SIZE=131072` (128 KB) BSS-ben foglal — benne van a jelenlegi 220 KB-ban.
A tervezett heap növelés (+32 KB) hozzáadódik ehhez:

```
220 KB  (jelenlegi, tartalmazza a 128 KB heap rezervációt)
+ 32 KB  (heap: 128 KB → 160 KB)
+  7 KB  (új statikus allokációk: state[], diagnostics, services...)
────────
≈ 259 KB  ← valós szám
264 KB   (RP2040 teljes RAM)
~5 KB szabad — nem 37 KB!
```

**Döntés: Heap növelés ELMARAD. CONFIG_HEAP_MEM_POOL_SIZE=131072 (128 KB) marad.**
- Statikus allokációs stratégia (diagnostics, channel_state_t) miatt a extra heap szükségtelen.
- A ~37 KB szabad RAM kritikus a Zephyr hálózati réteg, IRQ stack-ek, LittleFS számára.
- Ha a build mapsize mégis heap hiányt jelez, akkor és csak akkor módosítjuk.

---

## 2. `sys_heap_runtime_stats_get` a diagnostics-ban — LEZÁRVA

**Probléma:**
```c
sys_heap_runtime_stats_get(NULL, &heap_stats);  // NULL = crash/fordítási hiba
```
Belső, verziófüggő Zephyr szimbólumra (z_malloc_heap) hivatkozás ipari szoftverben nem megengedett.

**Döntés: C) opció — Heap monitoring KIHAGYVA a diagnostics-ból.**
- A `/diagnostics` topik publikálja: uptime, reconnect_cycles, channels, firmware.
- Heap érték helyett csatorna I2C busz hibák és státuszok kerülnek be majd.
- Zephyr frissítés nem törheti el a kódot.

---

## 3. Reconnect lifecycle sorrendiség — LEZÁRVA

**Döntés: Az alábbi sorrend elfogadott.**

`ros_session_fini()`:
```
1. diagnostics_fini(&node)
2. param_server_fini(&node)
3. service_manager_fini(&node)
4. channel_manager_destroy_entities(...)
5. rclc_executor_fini(...)
6. rcl_node_fini(...)
7. rclc_support_fini(...)
```

`ros_session_init()`:
```
1. rclc_support_init / node_init
2. channel_manager_create_entities
3. diagnostics_init(&node, ...)          <- executor init ELOTT (csak publisher)
4. executor init:
     handle_count = sub_count + PARAM_SERVER_HANDLES + service_count()
5. channel_manager_add_subs_to_executor
6. param_server_init(&node, &executor)   <- executor init UTAN
7. service_manager_init(&node, &executor)
8. param_server_load_from_config()       <- flash ertekek visszatoltese
```

Megjegyzés: `param_server_load_from_config()` az init után azonnal fut, hogy a csatornák
már a perzisztens értékekkel (period_ms, enabled, invert_logic) működjenek.

---

## 4. GPIO DT overlay — LEZÁRVA

**Döntés: Azonnal indulunk placeholder app.overlay-jel.**
- Teszt pinek: GP14 = `relay-brake`, GP15 = `estop-btn`
- A teljes 2. fázis (Interrupt kód) megírható a végleges kábelezés ismerete nélkül.
- Amikor a fizikai pin-kiosztás véglegesedik: csak az overlay GP számai változnak,
  egyetlen sor C kód sem módosul.

---

## 5. Publish kódduplikáció — LEZÁRVA

**Döntés: Kiszervezés a 2. fázisban kötelező.**

```c
static void perform_channel_publish(int i, channel_value_t *val);
```

Ezt hívja `channel_manager_publish()` és `channel_manager_handle_irq_pending()` egyaránt.
Előkészíti a 6. fázis komplex szenzortípusait (sensor_msgs/Imu stb.).

---

## Implementációs sorrend — döntések után frissítve

| Commit | Fázis | Tartalom | Változás a tervhez képest |
|--------|-------|----------|--------------------------|
| 1 | 0. Fázis | prj.conf + libmicroros.mk | Heap 128 KB marad (nem 163840) |
| 2 | 1. Fázis | channel.h + channel_manager | + perform_channel_publish() helper |
| 3 | 2. Fázis | drv_gpio + ISR + 1ms loop + overlay | + app.overlay placeholder |
| 4 | 3. Fázis | diagnostics.c | Heap stat kihagyva, 4 KV marad |
| 5 | 4. Fázis | param_server + config kiterjesztés | + load_from_config reconnect |
| 6 | 5. Fázis | service_manager | Változatlan |
| 7 | 6. Fázis | drv_adc + drv_imu keretrendszer | Változatlan |
