#ifndef BRIDGE_PARAM_SERVER_H
#define BRIDGE_PARAM_SERVER_H

#include <rclc_parameter/rclc_parameter.h>
#include <rclc/executor.h>
#include <rcl/node.h>

/* ------------------------------------------------------------------ */
/*  Executor handle count required by the parameter server            */
/*                                                                     */
/*  Add this to the executor handle_count in ros_session_init().      */
/* ------------------------------------------------------------------ */

#define PARAM_SERVER_HANDLES  RCLC_EXECUTOR_PARAMETER_SERVER_HANDLES  /* = 6 */

/* ------------------------------------------------------------------ */
/*  API                                                                */
/* ------------------------------------------------------------------ */

/**
 * Initialize the parameter server and add it to the executor.
 * Call AFTER executor initialization.
 * Declares 3 parameters per registered channel:
 *   ch.<name>.period_ms  (INT)
 *   ch.<name>.enabled    (BOOL)
 *   ch.<name>.invert_logic (BOOL)
 *
 * @param node      Pointer to the active RCL node
 * @param executor  Pointer to the initialized executor
 * @return 0 on success, negative errno on error
 */
int param_server_init(rcl_node_t *node, rclc_executor_t *executor);

/**
 * Destroy the parameter server.
 * Call BEFORE rcl_node_fini() during session cleanup.
 *
 * @param node  Pointer to the active RCL node
 */
void param_server_fini(rcl_node_t *node);

/**
 * Load channel parameters from /lfs/ch_params.json and apply them
 * to the live parameter server and channel_manager state.
 * Call AFTER param_server_init() during session startup.
 *
 * @return 0 on success, -ENOENT if no saved params (uses defaults)
 */
int param_server_load_from_config(void);

/**
 * Save all current channel parameter values to /lfs/ch_params.json.
 * Called automatically from the parameter change callback.
 * Can also be called manually.
 *
 * @return 0 on success, negative errno on error
 */
int param_server_save_to_config(void);

#endif /* BRIDGE_PARAM_SERVER_H */
