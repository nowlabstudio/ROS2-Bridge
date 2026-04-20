#include "pedal.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pedal, LOG_LEVEL_INF);

static bool g_heartbeat = false;

static void heartbeat_read(channel_value_t *val)
{
	g_heartbeat = !g_heartbeat;
	val->b = g_heartbeat;
}

const channel_t pedal_heartbeat_channel = {
	.name      = "pedal_heartbeat",
	.topic_pub = "heartbeat",
	.topic_sub = NULL,
	.msg_type  = MSG_BOOL,
	.period_ms = 1000,
	.init      = NULL,
	.read      = heartbeat_read,
	.write     = NULL,
};
