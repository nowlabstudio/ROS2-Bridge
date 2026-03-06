#include "service_manager.h"

#include <std_srvs/srv/set_bool.h>
#include <std_srvs/srv/trigger.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <string.h>

LOG_MODULE_REGISTER(service_manager, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/*  Service type enum                                                  */
/* ------------------------------------------------------------------ */

typedef enum {
	SRV_SET_BOOL = 0,
	SRV_TRIGGER  = 1,
} srv_type_t;

/* ------------------------------------------------------------------ */
/*  Internal service descriptor                                        */
/* ------------------------------------------------------------------ */

typedef struct {
	const char  *name;
	srv_type_t   type;
	union {
		set_bool_handler_t set_bool;
		trigger_handler_t  trigger;
	} handler;
} srv_descriptor_t;

/* ------------------------------------------------------------------ */
/*  Internal state                                                     */
/* ------------------------------------------------------------------ */

static srv_descriptor_t descriptors[SERVICE_MAX];
static int              srv_count;

/* ROS2 service entities */
static rcl_service_t srv_entities[SERVICE_MAX];
static bool          srv_active[SERVICE_MAX];

/* Static request/response message storage — one pair per slot */
static std_srvs__srv__SetBool_Request  sb_req[SERVICE_MAX];
static std_srvs__srv__SetBool_Response sb_res[SERVICE_MAX];
static std_srvs__srv__Trigger_Request  tr_req[SERVICE_MAX];
static std_srvs__srv__Trigger_Response tr_res[SERVICE_MAX];

/* Response message string buffers */
static char res_msg_buf[SERVICE_MAX][64];

/* ------------------------------------------------------------------ */
/*  Service callbacks                                                  */
/* ------------------------------------------------------------------ */

static void set_bool_callback(const void *req_in, void *res_out,
			       rmw_request_id_t *req_id, void *context)
{
	ARG_UNUSED(req_id);
	int idx = (int)(intptr_t)context;

	if (idx < 0 || idx >= srv_count) {
		return;
	}

	const std_srvs__srv__SetBool_Request  *req = req_in;
	std_srvs__srv__SetBool_Response       *res = res_out;

	bool        success = false;
	const char *msg     = "";

	descriptors[idx].handler.set_bool(req->data, &success, &msg);

	res->success = success;

	/* Bind static string buffer to response */
	strncpy(res_msg_buf[idx], msg, sizeof(res_msg_buf[idx]) - 1);
	res_msg_buf[idx][sizeof(res_msg_buf[idx]) - 1] = '\0';
	res->message.data     = res_msg_buf[idx];
	res->message.size     = strlen(res_msg_buf[idx]);
	res->message.capacity = sizeof(res_msg_buf[idx]);

	LOG_INF("SetBool %s: req=%d success=%d msg=%s",
		descriptors[idx].name, req->data, success, msg);
}

static void trigger_callback(const void *req_in, void *res_out,
			      rmw_request_id_t *req_id, void *context)
{
	ARG_UNUSED(req_in);
	ARG_UNUSED(req_id);
	int idx = (int)(intptr_t)context;

	if (idx < 0 || idx >= srv_count) {
		return;
	}

	std_srvs__srv__Trigger_Response *res = res_out;

	bool        success = false;
	const char *msg     = "";

	descriptors[idx].handler.trigger(&success, &msg);

	res->success = success;

	strncpy(res_msg_buf[idx], msg, sizeof(res_msg_buf[idx]) - 1);
	res_msg_buf[idx][sizeof(res_msg_buf[idx]) - 1] = '\0';
	res->message.data     = res_msg_buf[idx];
	res->message.size     = strlen(res_msg_buf[idx]);
	res->message.capacity = sizeof(res_msg_buf[idx]);

	LOG_INF("Trigger %s: success=%d msg=%s",
		descriptors[idx].name, success, msg);
}

/* ------------------------------------------------------------------ */
/*  Public API — registration                                          */
/* ------------------------------------------------------------------ */

int service_register_set_bool(const char *srv_name,
			      set_bool_handler_t handler)
{
	if (srv_count >= SERVICE_MAX) {
		LOG_ERR("service_register: max %d services", SERVICE_MAX);
		return -ENOMEM;
	}
	if (!srv_name || !handler) {
		return -EINVAL;
	}

	descriptors[srv_count].name              = srv_name;
	descriptors[srv_count].type              = SRV_SET_BOOL;
	descriptors[srv_count].handler.set_bool  = handler;
	srv_count++;

	LOG_INF("Service registered (SetBool): %s", srv_name);
	return 0;
}

int service_register_trigger(const char *srv_name,
			     trigger_handler_t handler)
{
	if (srv_count >= SERVICE_MAX) {
		LOG_ERR("service_register: max %d services", SERVICE_MAX);
		return -ENOMEM;
	}
	if (!srv_name || !handler) {
		return -EINVAL;
	}

	descriptors[srv_count].name             = srv_name;
	descriptors[srv_count].type             = SRV_TRIGGER;
	descriptors[srv_count].handler.trigger  = handler;
	srv_count++;

	LOG_INF("Service registered (Trigger): %s", srv_name);
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Public API — lifecycle                                             */
/* ------------------------------------------------------------------ */

int service_count(void)
{
	return srv_count;
}

int service_manager_init(rcl_node_t *node, rclc_executor_t *executor)
{
	if (!node || !executor) {
		return -EINVAL;
	}

	for (int i = 0; i < srv_count; i++) {
		const srv_descriptor_t *d = &descriptors[i];
		rcl_ret_t rc;

		if (d->type == SRV_SET_BOOL) {
			rc = rclc_service_init_default(
				&srv_entities[i], node,
				ROSIDL_GET_SRV_TYPE_SUPPORT(
					std_srvs, srv, SetBool),
				d->name);
		} else {
			rc = rclc_service_init_default(
				&srv_entities[i], node,
				ROSIDL_GET_SRV_TYPE_SUPPORT(
					std_srvs, srv, Trigger),
				d->name);
		}

		if (rc != RCL_RET_OK) {
			LOG_ERR("%s service init error: %d", d->name, (int)rc);
			continue;
		}
		srv_active[i] = true;

		void *req_msg = (d->type == SRV_SET_BOOL)
				? (void *)&sb_req[i]
				: (void *)&tr_req[i];
		void *res_msg = (d->type == SRV_SET_BOOL)
				? (void *)&sb_res[i]
				: (void *)&tr_res[i];

		rclc_service_callback_t cb = (d->type == SRV_SET_BOOL)
					     ? set_bool_callback
					     : trigger_callback;

		rc = rclc_executor_add_service_with_context(
			executor,
			&srv_entities[i],
			req_msg,
			res_msg,
			cb,
			(void *)(intptr_t)i);

		if (rc != RCL_RET_OK) {
			LOG_ERR("%s executor add error: %d", d->name, (int)rc);
			rcl_service_fini(&srv_entities[i], node);
			srv_active[i] = false;
		} else {
			LOG_INF("Service ready: %s", d->name);
		}
	}

	return 0;
}

void service_manager_fini(rcl_node_t *node)
{
	for (int i = 0; i < srv_count; i++) {
		if (srv_active[i]) {
			rcl_service_fini(&srv_entities[i], node);
			srv_active[i] = false;
		}
	}
	LOG_INF("Services destroyed");
}
