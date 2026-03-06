#include "channel_manager.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <rcl/rcl.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>

#include <std_msgs/msg/bool.h>
#include <std_msgs/msg/int32.h>
#include <std_msgs/msg/float32.h>

#include <string.h>

LOG_MODULE_REGISTER(channel_manager, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/*  Internal state                                                     */
/* ------------------------------------------------------------------ */

static const channel_t  *channels[CHANNEL_MAX];
static channel_state_t   states[CHANNEL_MAX];
static int               channel_count;

/* ROS2 entities per channel */
static rcl_publisher_t    pub[CHANNEL_MAX];
static rcl_subscription_t sub[CHANNEL_MAX];
static bool               pub_active[CHANNEL_MAX];
static bool               sub_active[CHANNEL_MAX];

/* Publish message storage */
static std_msgs__msg__Bool    msg_pub_bool[CHANNEL_MAX];
static std_msgs__msg__Int32   msg_pub_int32[CHANNEL_MAX];
static std_msgs__msg__Float32 msg_pub_float32[CHANNEL_MAX];

/* Subscribe message storage */
static std_msgs__msg__Bool    msg_sub_bool[CHANNEL_MAX];
static std_msgs__msg__Int32   msg_sub_int32[CHANNEL_MAX];
static std_msgs__msg__Float32 msg_sub_float32[CHANNEL_MAX];

/* Publish timers */
static int64_t last_publish_ms[CHANNEL_MAX];

/* ------------------------------------------------------------------ */
/*  Helper functions — type-based dispatch                            */
/* ------------------------------------------------------------------ */

static const rosidl_message_type_support_t *get_type_support(msg_type_t t)
{
	switch (t) {
	case MSG_BOOL:    return ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Bool);
	case MSG_INT32:   return ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32);
	case MSG_FLOAT32: return ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Float32);
	default:          return ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Float32);
	}
}

static void *get_sub_msg(int idx)
{
	if (idx < 0 || idx >= CHANNEL_MAX || !channels[idx]) {
		return &msg_sub_float32[0];
	}
	switch (channels[idx]->msg_type) {
	case MSG_BOOL:    return &msg_sub_bool[idx];
	case MSG_INT32:   return &msg_sub_int32[idx];
	case MSG_FLOAT32: return &msg_sub_float32[idx];
	default:          return &msg_sub_float32[idx];
	}
}

/* ------------------------------------------------------------------ */
/*  Core publish helper — shared by periodic and IRQ paths            */
/* ------------------------------------------------------------------ */

static void perform_channel_publish(int i)
{
	const channel_t *ch = channels[i];

	if (!ch || !ch->read) {
		return;
	}

	channel_value_t val = {0};
	ch->read(&val);

	if (states[i].invert_logic && ch->msg_type == MSG_BOOL) {
		val.b = !val.b;
	}

	switch (ch->msg_type) {
	case MSG_BOOL:
		msg_pub_bool[i].data = val.b;
		(void)rcl_publish(&pub[i], &msg_pub_bool[i], NULL);
		break;
	case MSG_INT32:
		msg_pub_int32[i].data = val.i32;
		(void)rcl_publish(&pub[i], &msg_pub_int32[i], NULL);
		break;
	case MSG_FLOAT32:
		msg_pub_float32[i].data = val.f32;
		(void)rcl_publish(&pub[i], &msg_pub_float32[i], NULL);
		break;
	}
}

/* ------------------------------------------------------------------ */
/*  Subscribe callback — generic, shared by all channels              */
/* ------------------------------------------------------------------ */

static void sub_callback(const void *msg_in, void *context)
{
	int idx = (int)(intptr_t)context;

	if (idx < 0 || idx >= channel_count) {
		return;
	}

	const channel_t *ch = channels[idx];

	if (!ch || !ch->write) {
		return;
	}

	channel_value_t val = {0};

	switch (ch->msg_type) {
	case MSG_BOOL:
		val.b = ((std_msgs__msg__Bool *)msg_in)->data;
		if (states[idx].invert_logic) {
			val.b = !val.b;
		}
		break;
	case MSG_INT32:
		val.i32 = ((std_msgs__msg__Int32 *)msg_in)->data;
		break;
	case MSG_FLOAT32:
		val.f32 = ((std_msgs__msg__Float32 *)msg_in)->data;
		break;
	}

	ch->write(&val);
}

/* ------------------------------------------------------------------ */
/*  Public API — registration & lifecycle                             */
/* ------------------------------------------------------------------ */

int channel_register(const channel_t *ch)
{
	if (channel_count >= CHANNEL_MAX) {
		LOG_ERR("channel_register: max %d channels allowed", CHANNEL_MAX);
		return -ENOMEM;
	}
	if (!ch || !ch->name) {
		return -EINVAL;
	}

	int i = channel_count;
	channels[i] = ch;

	/* Initialize mutable state from descriptor defaults */
	states[i].period_ms    = ch->period_ms;
	states[i].enabled      = true;
	states[i].invert_logic = false;
	atomic_set(&states[i].irq_pending, 0);

	channel_count++;
	LOG_INF("Channel registered: %s", ch->name);
	return 0;
}

void channel_manager_init_channels(void)
{
	for (int i = 0; i < channel_count; i++) {
		if (!channels[i]) {
			continue;
		}
		if (channels[i]->init) {
			int rc = channels[i]->init();
			if (rc < 0) {
				LOG_ERR("%s init error: %d", channels[i]->name, rc);
			} else {
				LOG_INF("%s init OK", channels[i]->name);
			}
		}
	}
}

int channel_manager_create_entities(rcl_node_t *node,
				    const rcl_allocator_t *allocator)
{
	if (!node || !allocator) {
		return -EINVAL;
	}

	for (int i = 0; i < channel_count; i++) {
		const channel_t *ch = channels[i];

		if (!ch) {
			continue;
		}

		const rosidl_message_type_support_t *ts = get_type_support(ch->msg_type);

		/* Publisher */
		if (ch->topic_pub && ch->read) {
			rcl_ret_t rc = rclc_publisher_init_default(
				&pub[i], node, ts, ch->topic_pub);
			if (rc == RCL_RET_OK) {
				pub_active[i] = true;
				last_publish_ms[i] = k_uptime_get();
				LOG_INF("%s publisher: %s", ch->name, ch->topic_pub);
			} else {
				if (rcl_error_is_set()) {
					LOG_ERR("%s publisher error: %d — %s",
						ch->name, (int)rc,
						rcl_get_error_string().str);
					rcl_reset_error();
				} else {
					LOG_ERR("%s publisher error: %d",
						ch->name, (int)rc);
				}
			}
		}

		/* Subscriber */
		if (ch->topic_sub && ch->write) {
			rcl_ret_t rc = rclc_subscription_init_default(
				&sub[i], node, ts, ch->topic_sub);
			if (rc == RCL_RET_OK) {
				sub_active[i] = true;
				LOG_INF("%s subscriber: %s", ch->name, ch->topic_sub);
			} else {
				LOG_ERR("%s subscriber error: %d", ch->name, (int)rc);
			}
		}
	}

	return 0;
}

void channel_manager_destroy_entities(rcl_node_t *node,
				      const rcl_allocator_t *allocator)
{
	ARG_UNUSED(allocator);

	for (int i = 0; i < channel_count; i++) {
		if (pub_active[i]) {
			rcl_publisher_fini(&pub[i], node);
			pub_active[i] = false;
		}
		if (sub_active[i]) {
			rcl_subscription_fini(&sub[i], node);
			sub_active[i] = false;
		}
	}
	LOG_INF("Channel entities destroyed");
}

/* ------------------------------------------------------------------ */
/*  Executor integration                                               */
/* ------------------------------------------------------------------ */

int channel_manager_sub_count(void)
{
	int count = 0;

	for (int i = 0; i < channel_count; i++) {
		if (sub_active[i]) {
			count++;
		}
	}
	return count;
}

int channel_manager_add_subs_to_executor(rclc_executor_t *executor)
{
	if (!executor) {
		return -EINVAL;
	}

	for (int i = 0; i < channel_count; i++) {
		if (!sub_active[i]) {
			continue;
		}

		rcl_ret_t rc = rclc_executor_add_subscription_with_context(
			executor,
			&sub[i],
			get_sub_msg(i),
			sub_callback,
			(void *)(intptr_t)i,
			ON_NEW_DATA);

		if (rc != RCL_RET_OK) {
			LOG_ERR("%s executor sub error: %d",
				channels[i]->name, (int)rc);
			return -EIO;
		}
	}

	return 0;
}

/* ------------------------------------------------------------------ */
/*  Main loop publish                                                  */
/* ------------------------------------------------------------------ */

void channel_manager_publish(void)
{
	int64_t now = k_uptime_get();

	for (int i = 0; i < channel_count; i++) {
		if (!pub_active[i] || !states[i].enabled) {
			continue;
		}

		if ((now - last_publish_ms[i]) < (int64_t)states[i].period_ms) {
			continue;
		}

		last_publish_ms[i] = now;
		perform_channel_publish(i);
	}
}

void channel_manager_handle_irq_pending(void)
{
	int64_t now = k_uptime_get();

	for (int i = 0; i < channel_count; i++) {
		if (!pub_active[i] || !channels[i]->irq_capable) {
			continue;
		}

		if (atomic_test_and_clear_bit(&states[i].irq_pending, 0)) {
			last_publish_ms[i] = now;  /* reset periodic timer */
			perform_channel_publish(i);
			LOG_DBG("IRQ publish: %s", channels[i]->name);
		}
	}
}

/* ------------------------------------------------------------------ */
/*  ISR interface                                                      */
/* ------------------------------------------------------------------ */

void channel_manager_signal_irq(int channel_idx)
{
	if (channel_idx >= 0 && channel_idx < channel_count) {
		atomic_set_bit(&states[channel_idx].irq_pending, 0);
	}
}

/* ------------------------------------------------------------------ */
/*  State API                                                          */
/* ------------------------------------------------------------------ */

void channel_state_set_period(int idx, uint32_t ms)
{
	if (idx >= 0 && idx < channel_count) {
		states[idx].period_ms = ms;
	}
}

void channel_state_set_enabled(int idx, bool en)
{
	if (idx >= 0 && idx < channel_count) {
		states[idx].enabled = en;
	}
}

void channel_state_set_invert(int idx, bool inv)
{
	if (idx >= 0 && idx < channel_count) {
		states[idx].invert_logic = inv;
	}
}

uint32_t channel_state_get_period(int idx)
{
	if (idx >= 0 && idx < channel_count) {
		return states[idx].period_ms;
	}
	return 0;
}

bool channel_state_get_enabled(int idx)
{
	if (idx >= 0 && idx < channel_count) {
		return states[idx].enabled;
	}
	return false;
}

bool channel_state_get_invert(int idx)
{
	if (idx >= 0 && idx < channel_count) {
		return states[idx].invert_logic;
	}
	return false;
}

/* ------------------------------------------------------------------ */
/*  Lookup & info                                                      */
/* ------------------------------------------------------------------ */

int channel_manager_count(void)
{
	return channel_count;
}

int channel_manager_find_by_name(const char *name)
{
	if (!name) {
		return -1;
	}
	for (int i = 0; i < channel_count; i++) {
		if (channels[i] && strcmp(channels[i]->name, name) == 0) {
			return i;
		}
	}
	return -1;
}

const char *channel_manager_name(int idx)
{
	if (idx >= 0 && idx < channel_count && channels[idx]) {
		return channels[idx]->name;
	}
	return NULL;
}
