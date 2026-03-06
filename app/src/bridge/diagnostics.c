#include "diagnostics.h"
#include "channel_manager.h"
#include "config/config.h"

#include <diagnostic_msgs/msg/diagnostic_array.h>
#include <diagnostic_msgs/msg/diagnostic_status.h>
#include <diagnostic_msgs/msg/key_value.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(diagnostics, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/*  Reconnect counter — incremented by main.c                         */
/* ------------------------------------------------------------------ */

int g_reconnect_count;

/* ------------------------------------------------------------------ */
/*  Static storage — zero heap usage                                  */
/* ------------------------------------------------------------------ */

#define DIAG_KV_COUNT 5

/* Key buffers — fixed strings */
static char kv_key0[] = "uptime_s";
static char kv_key1[] = "channels";
static char kv_key2[] = "reconnects";
static char kv_key3[] = "firmware";
static char kv_key4[] = "ip";

/* Value buffers — written at publish time */
static char kv_val0[16];   /* uptime_s      */
static char kv_val1[4];    /* channels      */
static char kv_val2[8];    /* reconnects    */
static char kv_val3[] = "v2.0-W6100";  /* firmware (fixed) */
static char kv_val4[16];   /* ip            */

static char hw_id_buf[] = "w6100_evb_pico";

static diagnostic_msgs__msg__KeyValue        kv_items[DIAG_KV_COUNT];
static diagnostic_msgs__msg__DiagnosticStatus status_msg;
static diagnostic_msgs__msg__DiagnosticArray  diag_array;

static rcl_publisher_t diag_pub;
static bool            diag_ready;

/* ------------------------------------------------------------------ */
/*  Helper macro: bind a static char array to a rosidl String struct  */
/* ------------------------------------------------------------------ */

#define BIND_STR_STATIC(s, buf) \
	do { \
		(s).data     = (buf); \
		(s).size     = strlen(buf); \
		(s).capacity = sizeof(buf); \
	} while (0)

#define BIND_STR_BUF(s, buf) \
	do { \
		(s).data     = (buf); \
		(s).size     = 0; \
		(s).capacity = sizeof(buf); \
	} while (0)

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

int diagnostics_init(rcl_node_t *node, const rcl_allocator_t *allocator)
{
	ARG_UNUSED(allocator);

	/* Bind key strings (fixed) */
	BIND_STR_STATIC(kv_items[0].key, kv_key0);
	BIND_STR_STATIC(kv_items[1].key, kv_key1);
	BIND_STR_STATIC(kv_items[2].key, kv_key2);
	BIND_STR_STATIC(kv_items[3].key, kv_key3);
	BIND_STR_STATIC(kv_items[4].key, kv_key4);

	/* Bind value buffers (filled at publish time) */
	BIND_STR_BUF(kv_items[0].value, kv_val0);
	BIND_STR_BUF(kv_items[1].value, kv_val1);
	BIND_STR_BUF(kv_items[2].value, kv_val2);
	BIND_STR_STATIC(kv_items[3].value, kv_val3);  /* firmware: fixed */
	BIND_STR_BUF(kv_items[4].value, kv_val4);

	/* Status message */
	BIND_STR_STATIC(status_msg.name,        g_config.ros.node_name);
	BIND_STR_STATIC(status_msg.hardware_id, hw_id_buf);
	status_msg.level           = diagnostic_msgs__msg__DiagnosticStatus__OK;
	status_msg.values.data     = kv_items;
	status_msg.values.size     = DIAG_KV_COUNT;
	status_msg.values.capacity = DIAG_KV_COUNT;

	/* DiagnosticArray wraps the single status entry */
	diag_array.status.data     = &status_msg;
	diag_array.status.size     = 1;
	diag_array.status.capacity = 1;

	rcl_ret_t rc = rclc_publisher_init_default(
		&diag_pub, node,
		ROSIDL_GET_MSG_TYPE_SUPPORT(diagnostic_msgs, msg, DiagnosticArray),
		"/diagnostics");

	if (rc != RCL_RET_OK) {
		LOG_ERR("diagnostics publisher init error: %d", (int)rc);
		return -EIO;
	}

	diag_ready = true;
	LOG_INF("Diagnostics publisher ready (/diagnostics)");
	return 0;
}

void diagnostics_publish(void)
{
	if (!diag_ready) {
		return;
	}

	/* uptime_s */
	snprintf(kv_val0, sizeof(kv_val0), "%lld", k_uptime_get() / 1000);
	kv_items[0].value.size = strlen(kv_val0);

	/* channels */
	snprintf(kv_val1, sizeof(kv_val1), "%d", channel_manager_count());
	kv_items[1].value.size = strlen(kv_val1);

	/* reconnects */
	snprintf(kv_val2, sizeof(kv_val2), "%d", g_reconnect_count);
	kv_items[2].value.size = strlen(kv_val2);

	/* firmware: fixed, size already set by BIND_STR_STATIC */

	/* ip */
	strncpy(kv_val4, g_config.network.ip, sizeof(kv_val4) - 1);
	kv_val4[sizeof(kv_val4) - 1] = '\0';
	kv_items[4].value.size = strlen(kv_val4);

	status_msg.level = diagnostic_msgs__msg__DiagnosticStatus__OK;

	(void)rcl_publish(&diag_pub, &diag_array, NULL);
}

void diagnostics_fini(rcl_node_t *node)
{
	if (!diag_ready) {
		return;
	}
	rcl_publisher_fini(&diag_pub, node);
	diag_ready = false;
	LOG_INF("Diagnostics publisher destroyed");
}
