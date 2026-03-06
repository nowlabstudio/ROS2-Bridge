# W6100 Bridge v2.0 — Implementációs Masterplan

> Dátum: 2026-03-06
> Állapot: Tervezet — átnézésre
> Alapja: v1.5 (stress test passed, 17/17 auto test OK)

---

## Tartalom

1. [Kritikus infrastruktúra-probléma](#0-fázis--infrastruktúra-alap)
2. [Architektúra áttekintés](#architektúra-változások-áttekintése)
3. [0. Fázis — Infrastruktúra alap (Library rebuild)](#0-fázis--infrastruktúra-alap-library-rebuild)
4. [1. Fázis — channel.h + channel_manager refactor](#1-fázis--channelh-és-channel_manager-kiterjesztése)
5. [2. Fázis — F5: Interrupt + 1ms loop](#2-fázis--f5-interrupt-alapú-publikálás--1ms-loop)
6. [3. Fázis — F4: Diagnostics](#3-fázis--f4-diagnostics)
7. [4. Fázis — F1: Parameter Server](#4-fázis--f1-parameter-server)
8. [5. Fázis — F3: Services (E-Stop + Relay)](#5-fázis--f3-services-e-stop--relay)
9. [6. Fázis — F2: Sensor típusok](#6-fázis--f2-sensor-típusok)
10. [Végleges fájlstruktúra](#végleges-fájlstruktúra-és-módosítási-összefoglaló)
11. [RAM Budget](#ram-budget--becsült-allokáció-v20-után)
12. [Implementációs sorrend](#implementációs-sorrend-commit-onként)

---

## Előzetes lelet: Kritikus infrastruktúra-probléma

Mielőtt egyetlen sort kódolunk, van egy **blokkírozó probléma**, amelyet fel kell oldani.
A `colcon.meta` és a `libmicroros.mk` vizsgálata alapján:

```makefile
# libmicroros.mk, line 102 — std_srvs LETILTVA:
touch src/common_interfaces/std_srvs/COLCON_IGNORE;
```

```json
// colcon.meta alapértelmezések (felülírandók prj.conf-ból):
"RMW_UXRCE_MAX_PUBLISHERS": "1",
"RMW_UXRCE_MAX_SUBSCRIPTIONS": "0",
"RMW_UXRCE_MAX_SERVICES": "0"
```

A `configure_colcon_meta` step a `prj.conf`-ból olvassa a tényleges értékeket —
de a jelenlegi `prj.conf`-ban **nincsenek explicit MICROROS entity limitek**.
Emiatt a Kconfig defaultok érvényesek, amelyek nem fedik a valós igényt.

**Ez azt jelenti: a teljes libmicroros.a újrafordítás az összes feature előfeltétele.**

---

## Architektúra-változások áttekintése

```
app/src/
├── main.c                    <- MÓDOSUL: 1ms loop, param/service/diag lifecycle
├── config/
│   ├── config.h/.c           <- MÓDOSUL: channel_params mentés/betöltés
├── bridge/
│   ├── channel.h             <- MÓDOSUL: channel_state_t különválik, irq_capable
│   ├── channel_manager.h/.c  <- MÓDOSUL: state management, irq pending
│   ├── param_server.h/.c     <- UJ: rclc_parameter_server_t wrapper
│   ├── service_manager.h/.c  <- UJ: std_srvs/SetBool + Trigger keretrendszer
│   └── diagnostics.h/.c      <- UJ: diagnostic_msgs/DiagnosticArray publisher
└── drivers/                  <- UJ könyvtár
    ├── drv_gpio.h/.c         <- UJ: GPIO + interrupt driver
    └── drv_adc.h/.c          <- UJ: ADC driver (akkumulátor feszültség)
```

### Feature — Fázis mapping

| Feature | Fázis | Library rebuild | Prioritás |
|---------|-------|----------------|-----------|
| F5 — Interrupt + 1ms loop | 2 | NEM | Kritikus (AGV biztonság) |
| F4 — /diagnostics | 3 | NEM | Magas |
| F1 — Parameter Server | 4 | IGEN (0. fázis) | Magas |
| F3 — Services (E-Stop) | 5 | IGEN (0. fázis) | Magas |
| F2 — sensor_msgs/Temp,Range | 6 | NEM | Közepes |
| F2 — sensor_msgs/Imu | 6 | NEM | Alacsony (keretrendszer) |
| F6 — Dynamic JSON config | — | IGEN | Utolsó (Step 3 után) |

---

## 0. Fázis — Infrastruktúra alap (Library rebuild)

Egyszeri, ~45-60 perc Docker-ben. Utána csak az alkalmazáskód változik.

### 0.1 — `app/prj.conf` kiegészítések

```kconfig
# --- micro-ROS entity limits ---
# Publishers: 16 csatorna + 1 param event pub + 1 diagnostics + spare = 20
# Subscribers: 16 csatorna (legrosszabb eset)
# Servers: 6 param server + 2 user service (E-Stop, relay) = 8
CONFIG_MICROROS_PUBLISHERS=20
CONFIG_MICROROS_SUBSCRIBERS=16
CONFIG_MICROROS_SERVERS=8
CONFIG_MICROROS_CLIENTS=0
CONFIG_MICROROS_NODES=1

# --- GPIO interrupt ---
CONFIG_ATOMIC_OPERATIONS_BUILTIN=y

# --- Heap növelés (diagnostics + param server) ---
# Jelenlegi: 131072 (128 KB) -> 160 KB
CONFIG_HEAP_MEM_POOL_SIZE=163840

# --- Heap monitoring (diagnostics szabad heap méréshez) ---
CONFIG_SYS_HEAP_RUNTIME_STATS=y
```

### 0.2 — `libmicroros.mk` módosítás

```diff
# Törlendő sor (~102. sor):
-   touch src/common_interfaces/std_srvs/COLCON_IGNORE;
```

Ez engedélyezi: `std_srvs/SetBool`, `std_srvs/Trigger`, `std_srvs/Empty`.

### 0.3 — Build

```bash
make build
# Elvárt log:
# [micro-ROS] RMW_UXRCE_MAX_PUBLISHERS=20
# [micro-ROS] RMW_UXRCE_MAX_SUBSCRIPTIONS=16
# [micro-ROS] RMW_UXRCE_MAX_SERVICES=8
```

---

## 1. Fázis — `channel.h` és `channel_manager` kiterjesztése

Library rebuild NEM szükséges. Ez az alap, amire minden más épül.

### 1.1 — `channel.h` — Állapot szétválasztása

A jelenlegi `channel_t` `const` struktúra ezért nem módosítható futásidőben.
Megoldás: **statikus descriptor** (flash) + **dinamikus state** (RAM).

```c
/* channel.h */

typedef enum {
    MSG_BOOL    = 0,   /* std_msgs/Bool    */
    MSG_INT32   = 1,   /* std_msgs/Int32   */
    MSG_FLOAT32 = 2,   /* std_msgs/Float32 */
    /* Jövőbeli bővítés (F2, 6. fázis):   */
    /* MSG_SENSOR_TEMP  = 3,              */
    /* MSG_SENSOR_RANGE = 4,              */
    /* MSG_SENSOR_IMU   = 5,              */
} msg_type_t;

typedef union {
    bool    b;
    int32_t i32;
    float   f32;
} channel_value_t;

/* Statikus descriptor — const marad, flash-ben tárolódik */
typedef struct {
    const char  *name;
    const char  *topic_pub;
    const char  *topic_sub;
    msg_type_t   msg_type;
    uint32_t     period_ms;     /* alap periódus (param server felülírhatja) */
    bool         irq_capable;   /* true = GPIO interrupt mód engedélyezhető */
    int  (*init)(void);
    void (*read)(channel_value_t *out);
    void (*write)(const channel_value_t *in);
} channel_t;

/* Dinamikus állapot — RAM-ban, param server és ISR módosítja */
typedef struct {
    uint32_t  period_ms;     /* aktív periódus (felülírt érték) */
    bool      enabled;       /* csatorna aktív-e */
    bool      invert_logic;  /* bool érték invertálása */
    atomic_t  irq_pending;   /* ISR-ből beállítva, főciklus olvassa */
} channel_state_t;
```

### 1.2 — `channel_manager.c` — State management

Új belső állapot:
```c
static channel_state_t states[CHANNEL_MAX];
```

`channel_register()` kiegészítés — állapot inicializálás a descriptorból:
```c
states[channel_count].period_ms    = ch->period_ms;
states[channel_count].enabled      = true;
states[channel_count].invert_logic = false;
atomic_set(&states[channel_count].irq_pending, 0);
```

Új publikus API:
```c
/* Param server számára */
void     channel_state_set_period(int idx, uint32_t ms);
void     channel_state_set_enabled(int idx, bool en);
void     channel_state_set_invert(int idx, bool inv);
uint32_t channel_state_get_period(int idx);
bool     channel_state_get_enabled(int idx);
bool     channel_state_get_invert(int idx);

/* ISR-ből hívható (atomic, ISR-safe) */
void channel_manager_signal_irq(int channel_idx);

/* Főciklusból hívva — IRQ pending esetén azonnali publish */
void channel_manager_handle_irq_pending(void);

/* Csatorna keresés név alapján */
int channel_manager_find_by_name(const char *name);

/* Csatorna nevének lekérése index alapján */
const char *channel_manager_name(int idx);
```

`channel_manager_publish()` módosítás:
```c
void channel_manager_publish(void)
{
    int64_t now = k_uptime_get();
    for (int i = 0; i < channel_count; i++) {
        if (!pub_active[i] || !states[i].enabled) {  /* enabled check */
            continue;
        }
        if ((now - last_publish_ms[i]) < (int64_t)states[i].period_ms) {
            continue;
        }
        last_publish_ms[i] = now;
        channel_value_t val = {0};
        channels[i]->read(&val);

        /* Logic inversion for bool channels */
        if (states[i].invert_logic && channels[i]->msg_type == MSG_BOOL) {
            val.b = !val.b;
        }
        /* ... publish (meglévő switch) ... */
    }
}
```

---

## 2. Fázis — F5: Interrupt-alapú publikálás + 1ms loop

Library rebuild NEM szükséges. Legkritikusabb biztonsági fejlesztés.

### Motiváció

AGV 14 km/h = 3.88 m/s sebesség esetén:
- Jelenlegi 10ms loop jitter → ~4 cm elmozdulás érzékelésig
- Cél: sub-millisecond fizikai válaszidő limitkapcsolónál / E-Stop-nál

### 2.1 — `drivers/drv_gpio.h`

```c
#ifndef DRV_GPIO_H
#define DRV_GPIO_H

#include "bridge/channel.h"
#include <zephyr/drivers/gpio.h>

typedef struct {
    struct gpio_dt_spec  spec;
    gpio_flags_t         dir;          /* GPIO_INPUT vagy GPIO_OUTPUT */
    bool                 irq_mode;     /* true = interrupt alapú olvasás */
    gpio_flags_t         irq_flags;    /* GPIO_INT_EDGE_BOTH stb. */
    int                  channel_idx;  /* visszautalás channel_manager indexre */
    struct gpio_callback cb_data;      /* Zephyr belső callback struct */
} gpio_channel_cfg_t;

int drv_gpio_setup_irq(int channel_idx, gpio_channel_cfg_t *cfg);

#endif
```

### 2.2 — `drivers/drv_gpio.c` — ISR handler

```c
/* ISR — CSAK atomic flag, semmi más! */
static void gpio_isr_handler(const struct device *dev,
                              struct gpio_callback *cb,
                              uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(pins);
    gpio_channel_cfg_t *cfg = CONTAINER_OF(cb, gpio_channel_cfg_t, cb_data);
    channel_manager_signal_irq(cfg->channel_idx);  /* atomic set, ISR-safe */
}

int drv_gpio_setup_irq(int channel_idx, gpio_channel_cfg_t *cfg)
{
    gpio_pin_configure_dt(&cfg->spec, GPIO_INPUT);
    gpio_init_callback(&cfg->cb_data, gpio_isr_handler,
                       BIT(cfg->spec.pin));
    gpio_add_callback(cfg->spec.port, &cfg->cb_data);
    gpio_pin_interrupt_configure_dt(&cfg->spec, cfg->irq_flags);
    cfg->channel_idx = channel_idx;
    return 0;
}
```

### 2.3 — `channel_manager.c` — IRQ pending kezelés

```c
/* ISR-safe — atomic write */
void channel_manager_signal_irq(int channel_idx)
{
    if (channel_idx >= 0 && channel_idx < channel_count) {
        atomic_set_bit(&states[channel_idx].irq_pending, 0);
    }
}

/* Főciklusból hívva — IRQ pending esetén azonnali publish */
void channel_manager_handle_irq_pending(void)
{
    int64_t now = k_uptime_get();
    for (int i = 0; i < channel_count; i++) {
        if (!pub_active[i] || !channels[i]->irq_capable) {
            continue;
        }
        if (atomic_test_and_clear_bit(&states[i].irq_pending, 0)) {
            last_publish_ms[i] = now;  /* timer reset */
            channel_value_t val = {0};
            channels[i]->read(&val);
            if (states[i].invert_logic && channels[i]->msg_type == MSG_BOOL) {
                val.b = !val.b;
            }
            /* ... publish (meglévő switch) ... */
            LOG_DBG("IRQ publish: %s", channels[i]->name);
        }
    }
}
```

### 2.4 — `main.c` — 1ms spin loop

```c
/* bridge_run() — Phase 3: Run */
int64_t last_ping_ms = k_uptime_get();
int64_t last_diag_ms = k_uptime_get();

while (true) {
    /* F5: IRQ pending check — ELSO, mielott barmi mas futna */
    channel_manager_handle_irq_pending();

    /* Executor: 1ms timeout (volt: 10ms) */
    rclc_executor_spin_some(&executor, RCL_MS_TO_NS(1));

    /* Normál időzített publikálás */
    channel_manager_publish();

    watchdog_feed();

    int64_t now = k_uptime_get();

    /* Ping: 1 masodpercenkent */
    if ((now - last_ping_ms) >= 1000) {
        last_ping_ms = now;
        if (rmw_uros_ping_agent(AGENT_PING_TIMEOUT,
                                AGENT_PING_ATTEMPTS) != RMW_RET_OK) {
            LOG_WRN("Agent connection lost");
            break;
        }
    }

    /* F4: Diagnostics: 5 masodpercenkent */
    if ((now - last_diag_ms) >= 5000) {
        last_diag_ms = now;
        diagnostics_publish();
    }

    k_msleep(1);  /* volt: 10ms */
}
```

**CPU terhelés:** 1ms loop, RP2040 @ 133 MHz. Egy iteráció becsült futásideje < 50 μs.
A `k_msleep(1)` thread sleepbe teszi a CPU-t, nem busy-wait. Energiafogyasztás nem növekszik érdemben.

### 2.5 — Felhasználói GPIO csatorna példa (irq_capable)

```c
/* user_channels.c / drv_gpio.c */
static gpio_channel_cfg_t limit_sw_cfg = {
    .spec      = GPIO_DT_SPEC_GET(DT_ALIAS(limit_sw_1), gpios),
    .dir       = GPIO_INPUT,
    .irq_mode  = true,
    .irq_flags = GPIO_INT_EDGE_BOTH,
};

static int limit_sw_init(void)
{
    int idx = channel_manager_find_by_name("limit_sw_1");
    return drv_gpio_setup_irq(idx, &limit_sw_cfg);
}

static void limit_sw_read(channel_value_t *val)
{
    val->b = gpio_pin_get_dt(&limit_sw_cfg.spec);
}

const channel_t gpio_limit_sw_channel = {
    .name        = "limit_sw_1",
    .topic_pub   = "robot/limit_switch_1",
    .topic_sub   = NULL,
    .msg_type    = MSG_BOOL,
    .period_ms   = 100,      /* fallback polling, ha az IRQ kimarad */
    .irq_capable = true,     /* azonnali publish IRQ-ra */
    .init        = limit_sw_init,
    .read        = limit_sw_read,
    .write       = NULL,
};
```

---

## 3. Fázis — F4: Diagnostics

Library rebuild NEM szükséges. `diagnostic_msgs` megvan a könyvtárban.

### 3.1 — Statikus allokáció stratégia

A `DiagnosticArray` dinamikus szekvenciákat tartalmaz (String fields).
Megoldás: **statikus char tömbök + rosidl String struct kézi bekötése** — NULLA heap-igény.

### 3.2 — `bridge/diagnostics.h`

```c
#ifndef BRIDGE_DIAGNOSTICS_H
#define BRIDGE_DIAGNOSTICS_H

#include <rcl/rcl.h>
#include <rclc/rclc.h>

/* Globális reconnect számláló — main.c inkrementálja fázis 4-ben */
extern int g_reconnect_count;

int  diagnostics_init(rcl_node_t *node, const rcl_allocator_t *allocator);
void diagnostics_publish(void);
void diagnostics_fini(rcl_node_t *node);

#endif
```

### 3.3 — `bridge/diagnostics.c`

```c
#include "diagnostics.h"
#include "channel_manager.h"
#include <diagnostic_msgs/msg/diagnostic_array.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/kernel.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(diagnostics, LOG_LEVEL_INF);

#define DIAG_KV_COUNT 5

int g_reconnect_count = 0;

/* Statikus string bufferek */
static char kv_key0[] = "uptime_s";
static char kv_key1[] = "free_heap_bytes";
static char kv_key2[] = "channels";
static char kv_key3[] = "reconnects";
static char kv_key4[] = "firmware";
static char kv_val0[16];
static char kv_val1[16];
static char kv_val2[4];
static char kv_val3[8];
static char kv_val4[] = "v2.0-W6100";
static char hw_id_buf[]   = "w6100_evb_pico";
static char node_name_buf[] = "pico_bridge";

static diagnostic_msgs__msg__KeyValue        kv_items[DIAG_KV_COUNT];
static diagnostic_msgs__msg__DiagnosticStatus status_msg;
static diagnostic_msgs__msg__DiagnosticArray  diag_array;
static rcl_publisher_t diag_pub;
static bool diag_ready = false;

/* String struct kézi bekötése statikus bufferhez */
#define BIND_STR(s, buf) \
    (s).data = (buf); (s).size = strlen(buf); (s).capacity = sizeof(buf)

int diagnostics_init(rcl_node_t *node, const rcl_allocator_t *allocator)
{
    ARG_UNUSED(allocator);

    /* KeyValue bekötések */
    BIND_STR(kv_items[0].key, kv_key0); kv_items[0].value.data = kv_val0; kv_items[0].value.capacity = sizeof(kv_val0);
    BIND_STR(kv_items[1].key, kv_key1); kv_items[1].value.data = kv_val1; kv_items[1].value.capacity = sizeof(kv_val1);
    BIND_STR(kv_items[2].key, kv_key2); kv_items[2].value.data = kv_val2; kv_items[2].value.capacity = sizeof(kv_val2);
    BIND_STR(kv_items[3].key, kv_key3); kv_items[3].value.data = kv_val3; kv_items[3].value.capacity = sizeof(kv_val3);
    BIND_STR(kv_items[4].key, kv_key4); BIND_STR(kv_items[4].value, kv_val4);

    BIND_STR(status_msg.name,        node_name_buf);
    BIND_STR(status_msg.hardware_id, hw_id_buf);
    status_msg.level             = diagnostic_msgs__msg__DiagnosticStatus__OK;
    status_msg.values.data       = kv_items;
    status_msg.values.size       = DIAG_KV_COUNT;
    status_msg.values.capacity   = DIAG_KV_COUNT;

    diag_array.status.data     = &status_msg;
    diag_array.status.size     = 1;
    diag_array.status.capacity = 1;

    rcl_ret_t rc = rclc_publisher_init_default(
        &diag_pub, node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(diagnostic_msgs, msg, DiagnosticArray),
        "/diagnostics");
    if (rc != RCL_RET_OK) {
        LOG_ERR("diagnostics publisher error: %d", (int)rc);
        return -EIO;
    }

    diag_ready = true;
    LOG_INF("Diagnostics publisher ready");
    return 0;
}

void diagnostics_publish(void)
{
    if (!diag_ready) return;

    /* Uptime */
    snprintf(kv_val0, sizeof(kv_val0), "%lld", k_uptime_get() / 1000);
    kv_items[0].value.size = strlen(kv_val0);

    /* Szabad heap */
    struct sys_memory_stats heap_stats;
    sys_heap_runtime_stats_get(NULL, &heap_stats);
    snprintf(kv_val1, sizeof(kv_val1), "%zu", heap_stats.free_bytes);
    kv_items[1].value.size = strlen(kv_val1);

    /* Csatornák */
    snprintf(kv_val2, sizeof(kv_val2), "%d", channel_manager_count());
    kv_items[2].value.size = strlen(kv_val2);

    /* Reconnect szamlalo */
    snprintf(kv_val3, sizeof(kv_val3), "%d", g_reconnect_count);
    kv_items[3].value.size = strlen(kv_val3);

    /* Firmware verzio — fix */
    kv_items[4].value.size = strlen(kv_val4);

    /* Level: OK > 8KB, WARN 4-8KB, ERROR < 4KB szabad heap */
    size_t free_b = heap_stats.free_bytes;
    status_msg.level =
        (free_b > 8192) ? diagnostic_msgs__msg__DiagnosticStatus__OK :
        (free_b > 4096) ? diagnostic_msgs__msg__DiagnosticStatus__WARN :
                          diagnostic_msgs__msg__DiagnosticStatus__ERROR;

    (void)rcl_publish(&diag_pub, &diag_array, NULL);
}

void diagnostics_fini(rcl_node_t *node)
{
    if (!diag_ready) return;
    rcl_publisher_fini(&diag_pub, node);
    diag_ready = false;
}
```

### 3.4 — Ellenőrzés ROS 2-n

```bash
ros2 topic echo /diagnostics
# Várható output:
# header: {stamp: {sec: 3600, nanosec: 0}, frame_id: ''}
# status:
# - level: 0  # OK
#   name: pico_bridge
#   hardware_id: w6100_evb_pico
#   values:
#   - {key: uptime_s,       value: '3600'}
#   - {key: free_heap_bytes, value: '35000'}
#   - {key: channels,       value: '3'}
#   - {key: reconnects,     value: '0'}
#   - {key: firmware,       value: 'v2.0-W6100'}

# rqt_robot_monitor kompatibilis (natív DiagnosticArray)
```

---

## 4. Fázis — F1: Parameter Server

Library rebuild SZÜKSÉGES (0. fázis elvégzése után).

### 4.1 — `bridge/param_server.h`

```c
#ifndef BRIDGE_PARAM_SERVER_H
#define BRIDGE_PARAM_SERVER_H

#include <rclc_parameter/rclc_parameter.h>
#include <rclc/executor.h>
#include <rcl/node.h>

/* Ennyi executor handle-t foglal le */
#define PARAM_SERVER_HANDLES  RCLC_EXECUTOR_PARAMETER_SERVER_HANDLES  /* = 6 */

int  param_server_init(rcl_node_t *node, rclc_executor_t *executor);
void param_server_fini(rcl_node_t *node);
int  param_server_load_from_config(void);
int  param_server_save_to_config(void);

#endif
```

### 4.2 — `bridge/param_server.c`

```c
#include "param_server.h"
#include "channel_manager.h"
#include "config/config.h"
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(param_server, LOG_LEVEL_INF);

static rclc_parameter_server_t param_server;
static bool param_server_ready = false;

/* Paraméter változás callback */
static bool on_param_changed(const Parameter *old_p,
                              const Parameter *new_p,
                              void *context)
{
    ARG_UNUSED(old_p);
    ARG_UNUSED(context);
    if (!new_p) return true;  /* törlési kérés */

    const char *name = new_p->name.data;
    LOG_INF("Param change: %s", name);

    /* Névformátum: "ch.<channel_name>.<param>" */
    if (strncmp(name, "ch.", 3) != 0) return true;

    const char *rest = name + 3;
    const char *dot  = strrchr(rest, '.');
    if (!dot) return false;

    char ch_name[48] = {0};
    strncpy(ch_name, rest, MIN((size_t)(dot - rest), sizeof(ch_name) - 1));
    const char *param = dot + 1;

    int idx = channel_manager_find_by_name(ch_name);
    if (idx < 0) {
        LOG_WRN("Unknown channel: %s", ch_name);
        return false;
    }

    if (strcmp(param, "period_ms") == 0 &&
        new_p->value.type == RCLC_PARAMETER_INT) {
        int64_t v = new_p->value.integer_value;
        if (v < 0 || v > 60000) return false;
        channel_state_set_period(idx, (uint32_t)v);

    } else if (strcmp(param, "enabled") == 0 &&
               new_p->value.type == RCLC_PARAMETER_BOOL) {
        channel_state_set_enabled(idx, new_p->value.bool_value);

    } else if (strcmp(param, "invert_logic") == 0 &&
               new_p->value.type == RCLC_PARAMETER_BOOL) {
        channel_state_set_invert(idx, new_p->value.bool_value);

    } else {
        return false;
    }

    /* Auto-mentés flash-re */
    param_server_save_to_config();
    return true;
}

int param_server_init(rcl_node_t *node, rclc_executor_t *executor)
{
    rclc_parameter_options_t opts = {
        .notify_changed_over_dds      = false,
        .max_params                   = 48,   /* 16 csatorna x 3 param */
        .allow_undeclared_parameters  = false,
        .low_mem_mode                 = true, /* RP2040: kotelezo! */
    };

    rcl_ret_t rc = rclc_parameter_server_init_with_option(
        &param_server, node, &opts);
    if (rc != RCL_RET_OK) {
        LOG_ERR("param_server_init error: %d", (int)rc);
        return -EIO;
    }

    /* Minden csatornához 3 paraméter deklarálás */
    char param_name[64];
    for (int i = 0; i < channel_manager_count(); i++) {
        const char *ch = channel_manager_name(i);

        snprintf(param_name, sizeof(param_name), "ch.%s.period_ms", ch);
        rclc_add_parameter(&param_server, param_name, RCLC_PARAMETER_INT);
        rclc_parameter_set_int(&param_server, param_name,
                               (int64_t)channel_state_get_period(i));

        snprintf(param_name, sizeof(param_name), "ch.%s.enabled", ch);
        rclc_add_parameter(&param_server, param_name, RCLC_PARAMETER_BOOL);
        rclc_parameter_set_bool(&param_server, param_name,
                                channel_state_get_enabled(i));

        snprintf(param_name, sizeof(param_name), "ch.%s.invert_logic", ch);
        rclc_add_parameter(&param_server, param_name, RCLC_PARAMETER_BOOL);
        rclc_parameter_set_bool(&param_server, param_name,
                                channel_state_get_invert(i));
    }

    rc = rclc_executor_add_parameter_server_with_context(
        executor, &param_server, on_param_changed, NULL);
    if (rc != RCL_RET_OK) {
        LOG_ERR("executor add param_server error: %d", (int)rc);
        return -EIO;
    }

    param_server_ready = true;
    LOG_INF("Parameter server ready (%d params)", 3 * channel_manager_count());
    return 0;
}

void param_server_fini(rcl_node_t *node)
{
    if (!param_server_ready) return;
    rclc_parameter_server_fini(&param_server, node);
    param_server_ready = false;
}
```

### 4.3 — Paraméter perzisztencia — `/lfs/ch_params.json`

Flat JSON kulcsok, kompatibilis a meglévő JSON parser-rel:

```json
{
  "ch.limit_sw_1.period_ms": 100,
  "ch.limit_sw_1.enabled": true,
  "ch.limit_sw_1.invert": false,
  "ch.adc_battery.period_ms": 1000,
  "ch.adc_battery.enabled": true,
  "ch.adc_battery.invert": false
}
```

Mentés `config.c` kiterjesztéssel — `channel_params_save()` / `channel_params_load()`.

### 4.4 — `main.c` executor handle count módosítás

```c
int sub_count    = channel_manager_sub_count();
int handle_count = sub_count
                 + PARAM_SERVER_HANDLES   /* F1: 6 */
                 + service_count();       /* F3: user service-ek */
if (handle_count == 0) handle_count = 1;
```

### 4.5 — Használat ROS 2-n

```bash
# Csatorna mintavételezési ideje futás közben
ros2 param set /pico_bridge ch.adc_battery.period_ms 500

# Csatorna ideiglenes lekapcsolása
ros2 param set /pico_bridge ch.limit_sw_1.enabled false

# Limitkapcsoló logika invertálása (pl. normally-closed switch)
ros2 param set /pico_bridge ch.limit_sw_1.invert_logic true

# Összes paraméter listázása
ros2 param list /pico_bridge
```

---

## 5. Fázis — F3: Services (E-Stop + Relay)

Library rebuild SZÜKSÉGES (0. fázis: std_srvs engedélyezve).

### 5.1 — `bridge/service_manager.h`

```c
#ifndef BRIDGE_SERVICE_MANAGER_H
#define BRIDGE_SERVICE_MANAGER_H

#include <rcl/rcl.h>
#include <rclc/executor.h>

#define SERVICE_MAX 8

/* SetBool service: request=bool, response=success+message */
typedef void (*set_bool_handler_t)(bool request,
                                   bool *out_success,
                                   const char **out_msg);

/* Trigger service: request=ures, response=success+message */
typedef void (*trigger_handler_t)(bool *out_success,
                                  const char **out_msg);

int service_register_set_bool(const char *srv_name,
                               set_bool_handler_t handler);
int service_register_trigger(const char *srv_name,
                              trigger_handler_t handler);

int  service_manager_init(rcl_node_t *node, rclc_executor_t *executor);
void service_manager_fini(rcl_node_t *node);
int  service_count(void);

#endif
```

### 5.2 — Felhasználói service regisztráció példa

```c
/* user_channels.c */

/* E-Stop — Trigger service */
static void estop_trigger(bool *ok, const char **msg)
{
    gpio_pin_set(estop_dev, ESTOP_PIN, 1);
    *ok  = true;
    *msg = "Emergency stop activated";
    LOG_ERR("!!! E-STOP ACTIVATED !!!");
}

/* Fékkioldás — SetBool service */
static void brake_set_bool(bool request, bool *ok, const char **msg)
{
    gpio_pin_set(brake_dev, BRAKE_PIN, request ? 1 : 0);
    *ok  = true;
    *msg = request ? "Brake released" : "Brake engaged";
}

void user_register_channels(void)
{
    /* Csatornák */
    channel_register(&gpio_limit_sw_channel);
    channel_register(&adc_battery_channel);

    /* Services */
    service_register_trigger("robot/emergency_stop",  estop_trigger);
    service_register_set_bool("robot/brake_release",  brake_set_bool);
}
```

### 5.3 — Használat ROS 2-n

```bash
# E-Stop aktiválás
ros2 service call /robot/emergency_stop std_srvs/srv/Trigger

# Fék kioldás
ros2 service call /robot/brake_release std_srvs/srv/SetBool "data: true"

# Fék behúzás
ros2 service call /robot/brake_release std_srvs/srv/SetBool "data: false"
```

---

## 6. Fázis — F2: Sensor típusok

Library rebuild NEM szükséges — sensor_msgs és diagnostic_msgs megvan a könyvtárban.

### 6.1 — ADC Battery Channel (MSG_FLOAT32 — már működik)

```c
/* drivers/drv_adc.c */

/* RP2040 ADC: 12-bit, 3.3V referencia
   Feszültségoszto: V_bat = V_adc * (R1+R2)/R2 */
#define VDIV_RATIO   4.0f   /* pl. 3:1 osztó → 12V max akkumulátor */
#define ADC_REF_V    3.3f
#define ADC_MAX      4096.0f

static void adc_battery_read(channel_value_t *val)
{
    int16_t raw = 0;
    /* ... adc_read() ... */
    float v_adc = ((float)raw / ADC_MAX) * ADC_REF_V;
    val->f32 = v_adc * VDIV_RATIO;
}

const channel_t adc_battery_channel = {
    .name        = "adc_battery",
    .topic_pub   = "robot/battery_voltage",
    .topic_sub   = NULL,
    .msg_type    = MSG_FLOAT32,
    .period_ms   = 1000,
    .irq_capable = false,
    .init        = adc_battery_init,
    .read        = adc_battery_read,
    .write       = NULL,
};
```

### 6.2 — sensor_msgs/Imu keretrendszer — statikus allokáció

```c
/* drivers/drv_imu.c */
#include <sensor_msgs/msg/imu.h>

static sensor_msgs__msg__Imu imu_msg;
static char frame_id_buf[] = "bridge_imu_link";

static int imu_init(void)
{
    /* Statikus string bekötés — NINCS rosidl allokátor hívás, NULLA heap */
    imu_msg.header.frame_id.data     = frame_id_buf;
    imu_msg.header.frame_id.size     = sizeof(frame_id_buf) - 1;
    imu_msg.header.frame_id.capacity = sizeof(frame_id_buf);

    /* Kovariancia[0] = -1.0: "ismeretlen adat" — ROS 2 Nav2 szabvány.
       A robot_localization és Nav2 EKF ezt figyelembe veszi és
       nem számol fals adatokkal a szűrőben. */
    imu_msg.orientation_covariance[0]         = -1.0;
    imu_msg.angular_velocity_covariance[0]    = -1.0;
    imu_msg.linear_acceleration_covariance[0] = -1.0;

    /* TODO: konkrét I2C IMU (pl. MPU-6050, BNO055) inicializálás */
    return 0;
}
```

**Megjegyzés:** Az IMU `read()` callback paraméterlistája kiterjesztést igényel
(a `channel_value_t` union nem elegendő komplex IMU adathoz).
Megoldás: `void *pub_msg_override` mező a `channel_t`-ben, vagy dedikált IMU
publish path a `channel_manager_publish()`-ban MSG_SENSOR_IMU ágban.
**Ez a channel_manager következő iterációjának feladata.**

---

## Végleges fájlstruktúra és módosítási összefoglaló

```
MÓDOSÍTOTT fájlok:
  app/prj.conf
    + CONFIG_MICROROS_PUBLISHERS=20
    + CONFIG_MICROROS_SUBSCRIBERS=16
    + CONFIG_MICROROS_SERVERS=8
    + CONFIG_MICROROS_CLIENTS=0
    + CONFIG_MICROROS_NODES=1
    + CONFIG_ATOMIC_OPERATIONS_BUILTIN=y
    + CONFIG_HEAP_MEM_POOL_SIZE=163840
    + CONFIG_SYS_HEAP_RUNTIME_STATS=y

  workspace/modules/lib/micro_ros_zephyr_module/modules/libmicroros/libmicroros.mk
    - touch src/common_interfaces/std_srvs/COLCON_IGNORE;

  app/src/main.c
    + 1ms spin loop (volt: 10ms)
    + channel_manager_handle_irq_pending() hívás
    + diagnostics_publish() hívás (5s)
    + g_reconnect_count++ a reconnect logban
    + param_server, service_manager, diagnostics lifecycle

  app/src/bridge/channel.h
    + channel_state_t struct
    + irq_capable mező a channel_t-ben

  app/src/bridge/channel_manager.h/.c
    + states[CHANNEL_MAX] tömb
    + channel_state_set/get API
    + channel_manager_signal_irq()
    + channel_manager_handle_irq_pending()
    + channel_manager_find_by_name()
    + channel_manager_name()
    + enabled + invert_logic check a publish()-ban

  app/src/config/config.c/.h
    + channel_params_save() -> /lfs/ch_params.json
    + channel_params_load() <- /lfs/ch_params.json

UJ fájlok:
  app/src/bridge/param_server.h/.c     (F1 — Parameter Server)
  app/src/bridge/service_manager.h/.c  (F3 — Services)
  app/src/bridge/diagnostics.h/.c      (F4 — Diagnostics)
  app/src/drivers/drv_gpio.h/.c        (F5 + hardver driver)
  app/src/drivers/drv_adc.h/.c         (F2 — ADC Battery)
  app/src/drivers/drv_imu.h/.c         (F2 — IMU keretrendszer)

app/CMakeLists.txt kiegészítés:
  target_sources(app PRIVATE
      ...
      src/bridge/param_server.c
      src/bridge/service_manager.c
      src/bridge/diagnostics.c
      src/drivers/drv_gpio.c
      src/drivers/drv_adc.c
      src/drivers/drv_imu.c
  )
```

---

## RAM Budget — Becsült allokáció v2.0 után

| Komponens | Becsült RAM |
|-----------|------------|
| Jelenlegi használat (v1.5) | 220 KB |
| `channel_state_t[16]` | +384 B |
| `rclc_parameter_server_t` | +~3 KB |
| 48 paraméter lista (low_mem_mode) | +~2 KB |
| `diagnostics.c` statikus bufferek | +~512 B |
| Service request/response struct-ok (×8) | +~1 KB |
| `sensor_msgs__msg__Imu` (1 példány) | +~400 B |
| **Összesen** | **~227 KB** |
| **Szabad marad** | **~37 KB** |

A 37 KB szabad RAM elegendő normál Zephyr stack és heap működéshez.

**Figyelmeztetés:** Ha a heap (CONFIG_HEAP_MEM_POOL_SIZE) 128 KB-ról 160 KB-ra nő,
akkor a teljes RAM felhasználás meg kell vizsgálni újrafordítás után a build mapsize outputból.

---

## Implementációs sorrend (commit-onként)

```
Commit 1  [0. Fázis]  prj.conf + libmicroros.mk + rebuild
          Fajl:       app/prj.conf, libmicroros.mk
          Tag:        v2.0-infra
          Build idő:  ~60 perc (Docker, egyszeri)

Commit 2  [1. Fázis]  channel.h + channel_manager refactor
          Fajl:       channel.h, channel_manager.h/.c
          Tag:        v2.0-state
          Build idő:  ~2 perc

Commit 3  [2. Fázis]  drv_gpio.c + ISR + 1ms loop
          Fajl:       drivers/drv_gpio.h/.c, main.c
          Tag:        v2.0-irq
          Teszt:      limitkapcsoló IRQ latencia mérés

Commit 4  [3. Fázis]  diagnostics.c/.h
          Fajl:       bridge/diagnostics.h/.c, main.c
          Tag:        v2.0-diag
          Teszt:      ros2 topic echo /diagnostics, rqt_robot_monitor

Commit 5  [4. Fázis]  param_server.c/.h + config kiterjesztés
          Fajl:       bridge/param_server.h/.c, config.c/.h
          Tag:        v2.0-params
          Teszt:      ros2 param set /pico_bridge ch.*.period_ms

Commit 6  [5. Fázis]  service_manager.c/.h
          Fajl:       bridge/service_manager.h/.c
          Tag:        v2.0-services
          Teszt:      ros2 service call /robot/emergency_stop

Commit 7  [6. Fázis]  drv_adc.c + drv_imu.c (keretrendszer)
          Fajl:       drivers/drv_adc.h/.c, drivers/drv_imu.h/.c
          Tag:        v2.0-sensors
          Teszt:      ros2 topic echo /robot/battery_voltage
```

---

## Nyitott kérdések / döntések az implementáció során

1. **drv_gpio pin konfigurálása:** A pin-ek DT alias-ból (`.overlay` fájl) vagy
   `GPIO_DT_SPEC_GET_BY_IDX`-szel? Az overlay megközelítés tisztább, de minden
   egyes GPIO csatornánál DTS módosítás kell. Alternatíva: runtime pin config
   struct-ból (later: F6 előkészítés).

2. **E-Stop GPIO hardver kapcsolat:** Milyen elektromos logikával? Normally-open
   vagy normally-closed relé? Ez az `invert_logic` paraméter default értékét
   befolyásolja.

3. **Akkumulátor ADC feszültségosztó arány:** A `VDIV_RATIO = 4.0f` placeholder.
   A tényleges hardver szerint kell kalibrálni.

4. **IMU I2C cím és típus:** Mikor kerül konkrét IMU a hardverre, a `drv_imu.c`
   placeholder feltölthető a Zephyr sensor API hívásaival.

5. **`sys_heap_runtime_stats_get(NULL, ...)` API:** Ellenőrizni kell, hogy a
   Zephyr main heap lekérdezése pontosan ez az API-e az adott Zephyr verzióban
   (v4.3.99). Alternatíva: `k_mem_slab_num_free_get()` vagy `sys_heap_max_size()`.

---

*Verzió: 1.0 | Létrehozva: 2026-03-06 | Tervező: Claude (claude-sonnet-4-6)*
