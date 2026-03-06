#ifndef BRIDGE_CHANNEL_MANAGER_H
#define BRIDGE_CHANNEL_MANAGER_H

#include "channel.h"

#include <rcl/rcl.h>
#include <rclc/executor.h>

/* ------------------------------------------------------------------ */
/*  Limits                                                             */
/* ------------------------------------------------------------------ */

#define CHANNEL_MAX 16   /* maximum number of registerable channels */

/* ------------------------------------------------------------------ */
/*  Registration & lifecycle                                           */
/* ------------------------------------------------------------------ */

/**
 * Register a channel. Call from user_register_channels(),
 * BEFORE micro-ROS initialization.
 */
int channel_register(const channel_t *ch);

/**
 * Call the init() function of all registered channels.
 * Call BEFORE micro-ROS initialization.
 */
void channel_manager_init_channels(void);

/**
 * Create ROS2 publisher and subscriber entities.
 * Call AFTER node creation.
 */
int channel_manager_create_entities(rcl_node_t *node,
				    const rcl_allocator_t *allocator);

/**
 * Destroy all ROS2 publisher and subscriber entities.
 * Call before reconnection (session cleanup).
 */
void channel_manager_destroy_entities(rcl_node_t *node,
				      const rcl_allocator_t *allocator);

/* ------------------------------------------------------------------ */
/*  Executor integration                                               */
/* ------------------------------------------------------------------ */

/**
 * Return the number of active subscriber channels.
 * Needed for executor initialization (handle count).
 */
int channel_manager_sub_count(void);

/**
 * Add all subscriptions to the executor.
 * Call AFTER executor initialization.
 */
int channel_manager_add_subs_to_executor(rclc_executor_t *executor);

/* ------------------------------------------------------------------ */
/*  Main loop publish                                                  */
/* ------------------------------------------------------------------ */

/**
 * Periodic publish — checks timers and calls read() on active channels.
 * Call from the main loop every 1ms.
 */
void channel_manager_publish(void);

/**
 * IRQ pending check — immediately publishes channels with a pending
 * interrupt flag. Call FIRST in the main loop, before executor spin.
 * ISR-safe: reads atomic flags set by channel_manager_signal_irq().
 */
void channel_manager_handle_irq_pending(void);

/* ------------------------------------------------------------------ */
/*  ISR interface                                                      */
/* ------------------------------------------------------------------ */

/**
 * Signal an IRQ event for a channel. Safe to call from ISR context.
 * Sets an atomic flag; the main loop handles the actual publish.
 */
void channel_manager_signal_irq(int channel_idx);

/* ------------------------------------------------------------------ */
/*  State API — used by parameter server                              */
/* ------------------------------------------------------------------ */

void     channel_state_set_period(int idx, uint32_t ms);
void     channel_state_set_enabled(int idx, bool en);
void     channel_state_set_invert(int idx, bool inv);
uint32_t channel_state_get_period(int idx);
bool     channel_state_get_enabled(int idx);
bool     channel_state_get_invert(int idx);

/* ------------------------------------------------------------------ */
/*  Lookup & info                                                      */
/* ------------------------------------------------------------------ */

/** Return total number of registered channels. */
int channel_manager_count(void);

/** Find channel index by name. Returns -1 if not found. */
int channel_manager_find_by_name(const char *name);

/** Return channel name by index. Returns NULL if out of range. */
const char *channel_manager_name(int idx);

#endif /* BRIDGE_CHANNEL_MANAGER_H */
