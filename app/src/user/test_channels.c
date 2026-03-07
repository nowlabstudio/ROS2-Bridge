#include "test_channels.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_channels, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/*  Counter — publish-only, counts up from 0                          */
/* ------------------------------------------------------------------ */

static int32_t g_counter = 0;

static void counter_read(channel_value_t *val)
{
	val->i32 = g_counter++;
}

const channel_t test_counter_channel = {
	.name      = "test_counter",
	.topic_pub = "counter",
	.topic_sub = NULL,
	.msg_type  = MSG_INT32,
	.period_ms = 500,
	.init      = NULL,
	.read      = counter_read,
	.write     = NULL,
};

/* ------------------------------------------------------------------ */
/*  Heartbeat — publish-only, toggles true/false every second         */
/* ------------------------------------------------------------------ */

static bool g_heartbeat = false;

static void heartbeat_read(channel_value_t *val)
{
	g_heartbeat = !g_heartbeat;
	val->b = g_heartbeat;
}

const channel_t test_heartbeat_channel = {
	.name      = "test_heartbeat",
	.topic_pub = "heartbeat",
	.topic_sub = NULL,
	.msg_type  = MSG_BOOL,
	.period_ms = 1000,
	.init      = NULL,
	.read      = heartbeat_read,
	.write     = NULL,
};

/* ------------------------------------------------------------------ */
/*  Echo — bidirectional INT32                                         */
/*  Subscribes on pico/echo_in, publishes the last received value     */
/*  back on pico/echo_out.                                             */
/* ------------------------------------------------------------------ */

static int32_t g_echo_value = 0;

static void echo_read(channel_value_t *val)
{
	val->i32 = g_echo_value;
}

static void echo_write(const channel_value_t *val)
{
	g_echo_value = val->i32;
	LOG_INF("echo_in received: %d", g_echo_value);
}

const channel_t test_echo_channel = {
	.name      = "test_echo",
	.topic_pub = "echo_out",
	.topic_sub = "echo_in",
	.msg_type  = MSG_INT32,
	.period_ms = 1000,
	.init      = NULL,
	.read      = echo_read,
	.write     = echo_write,
};
