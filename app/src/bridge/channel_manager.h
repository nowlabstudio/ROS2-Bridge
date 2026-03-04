#ifndef BRIDGE_CHANNEL_MANAGER_H
#define BRIDGE_CHANNEL_MANAGER_H

#include "channel.h"

#include <rcl/rcl.h>
#include <rclc/executor.h>

/* ------------------------------------------------------------------ */
/*  Korlátok                                                           */
/* ------------------------------------------------------------------ */

#define CHANNEL_MAX 16   /* max regisztrálható csatorna */

/* ------------------------------------------------------------------ */
/*  API                                                                */
/* ------------------------------------------------------------------ */

/**
 * Regisztrál egy csatornát. Hívd user_register_channels()-ből,
 * a micro-ROS init ELŐTT.
 */
int channel_register(const channel_t *ch);

/**
 * Meghívja az összes regisztrált csatorna init() függvényét.
 * Hívd a micro-ROS init ELŐTT.
 */
void channel_manager_init_channels(void);

/**
 * Létrehozza a ROS2 publisher és subscriber entitásokat.
 * Hívd a node létrehozása UTÁN.
 */
int channel_manager_create_entities(rcl_node_t *node,
				    const rcl_allocator_t *allocator);

/**
 * Adja a subscribe-olható csatornák számát.
 * Az executor init-hez kell (handle count).
 */
int channel_manager_sub_count(void);

/**
 * Hozzáadja az összes subscription-t az executorhoz.
 * Hívd az executor init UTÁN.
 */
int channel_manager_add_subs_to_executor(rclc_executor_t *executor);

/**
 * Periódikus publish — ellenőrzi a timer-eket és meghívja
 * az aktív csatornák read() függvényét.
 * Hívd a fő loop-ban, ~10ms-enként.
 */
void channel_manager_publish(void);

#endif /* BRIDGE_CHANNEL_MANAGER_H */
