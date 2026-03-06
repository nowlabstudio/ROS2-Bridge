#ifndef BRIDGE_DIAGNOSTICS_H
#define BRIDGE_DIAGNOSTICS_H

#include <rcl/rcl.h>
#include <rclc/rclc.h>

/**
 * Reconnect counter — incremented by main.c on each reconnect cycle.
 * Read by diagnostics_publish() to report connection stability.
 */
extern int g_reconnect_count;

/**
 * Initialize the /diagnostics publisher.
 * Call BEFORE executor initialization (publisher only, no executor handles).
 *
 * @param node       Pointer to the active RCL node
 * @param allocator  Pointer to the RCL allocator
 * @return 0 on success, negative errno on error
 */
int  diagnostics_init(rcl_node_t *node, const rcl_allocator_t *allocator);

/**
 * Publish a DiagnosticArray message with current bridge status.
 * Call periodically from the main loop (e.g. every 5 seconds).
 * All allocations are static — no heap usage.
 */
void diagnostics_publish(void);

/**
 * Destroy the /diagnostics publisher.
 * Call BEFORE rcl_node_fini() during session cleanup.
 *
 * @param node  Pointer to the active RCL node
 */
void diagnostics_fini(rcl_node_t *node);

#endif /* BRIDGE_DIAGNOSTICS_H */
