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
/*  API                                                                */
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
 * Return the total number of registered channels.
 */
int channel_manager_count(void);

/**
 * Return the number of subscriber channels.
 * Needed for executor initialization (handle count).
 */
int channel_manager_sub_count(void);

/**
 * Add all subscriptions to the executor.
 * Call AFTER executor initialization.
 */
int channel_manager_add_subs_to_executor(rclc_executor_t *executor);

/**
 * Destroy all ROS2 publisher and subscriber entities.
 * Call before reconnection (session cleanup).
 */
void channel_manager_destroy_entities(rcl_node_t *node,
				      const rcl_allocator_t *allocator);

/**
 * Periodic publish — checks timers and calls read() on active channels.
 * Call from the main loop, approximately every 10ms.
 */
void channel_manager_publish(void);

#endif /* BRIDGE_CHANNEL_MANAGER_H */
