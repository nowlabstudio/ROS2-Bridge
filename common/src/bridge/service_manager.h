#ifndef BRIDGE_SERVICE_MANAGER_H
#define BRIDGE_SERVICE_MANAGER_H

#include <rcl/rcl.h>
#include <rclc/executor.h>

/* ------------------------------------------------------------------ */
/*  Limits                                                             */
/* ------------------------------------------------------------------ */

#define SERVICE_MAX 8

/* ------------------------------------------------------------------ */
/*  Handler function pointer types                                     */
/* ------------------------------------------------------------------ */

/**
 * Handler for std_srvs/SetBool services.
 * Called from the executor context (NOT from ISR).
 *
 * @param request     The boolean request value
 * @param out_success Set to true if the request was fulfilled
 * @param out_msg     Short status message (pointer to static string)
 */
typedef void (*set_bool_handler_t)(bool request,
				   bool *out_success,
				   const char **out_msg);

/**
 * Handler for std_srvs/Trigger services.
 * Called from the executor context (NOT from ISR).
 *
 * @param out_success Set to true if the trigger was executed
 * @param out_msg     Short status message (pointer to static string)
 */
typedef void (*trigger_handler_t)(bool *out_success,
				  const char **out_msg);

/* ------------------------------------------------------------------ */
/*  Registration — call BEFORE service_manager_init()                 */
/* ------------------------------------------------------------------ */

/**
 * Register a std_srvs/SetBool service.
 *
 * @param srv_name  ROS2 service name (e.g. "robot/brake_release")
 * @param handler   Callback invoked when a request arrives
 * @return 0 on success, -ENOMEM if SERVICE_MAX reached
 */
int service_register_set_bool(const char *srv_name,
			      set_bool_handler_t handler);

/**
 * Register a std_srvs/Trigger service.
 *
 * @param srv_name  ROS2 service name (e.g. "robot/emergency_stop")
 * @param handler   Callback invoked when a request arrives
 * @return 0 on success, -ENOMEM if SERVICE_MAX reached
 */
int service_register_trigger(const char *srv_name,
			     trigger_handler_t handler);

/* ------------------------------------------------------------------ */
/*  Lifecycle — called from ros_session_init/fini                     */
/* ------------------------------------------------------------------ */

/**
 * Create ROS2 service entities and add them to the executor.
 * Call AFTER executor initialization.
 *
 * @param node      Pointer to the active RCL node
 * @param executor  Pointer to the initialized executor
 * @return 0 on success, negative errno on error
 */
int service_manager_init(rcl_node_t *node, rclc_executor_t *executor);

/**
 * Destroy all ROS2 service entities.
 * Call BEFORE rcl_node_fini() during session cleanup.
 *
 * @param node  Pointer to the active RCL node
 */
void service_manager_fini(rcl_node_t *node);

/**
 * Return the number of registered services.
 * Used to calculate executor handle count.
 */
int service_count(void);

#endif /* BRIDGE_SERVICE_MANAGER_H */
