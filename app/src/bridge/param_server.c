#include "param_server.h"
#include "channel_manager.h"
#include "config/config.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <string.h>
#include <stdio.h>

LOG_MODULE_REGISTER(param_server, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/*  Internal state                                                     */
/* ------------------------------------------------------------------ */

static rclc_parameter_server_t param_server;
static bool                    param_server_ready;

/* ------------------------------------------------------------------ */
/*  Parameter change callback                                          */
/* ------------------------------------------------------------------ */

static bool on_param_changed(const Parameter *old_p,
			      const Parameter *new_p,
			      void *context)
{
	ARG_UNUSED(old_p);
	ARG_UNUSED(context);

	if (!new_p || !new_p->name.data) {
		return true;
	}

	const char *name = new_p->name.data;
	LOG_INF("Param change: %s", name);

	/* Expected format: "ch.<channel_name>.<param>" */
	if (strncmp(name, "ch.", 3) != 0) {
		return true;
	}

	const char *rest = name + 3;
	const char *dot  = strrchr(rest, '.');

	if (!dot || dot == rest) {
		return false;
	}

	/* Extract channel name and param name */
	char ch_name[48] = {0};
	size_t ch_len = (size_t)(dot - rest);

	if (ch_len >= sizeof(ch_name)) {
		return false;
	}
	strncpy(ch_name, rest, ch_len);
	const char *param = dot + 1;

	int idx = channel_manager_find_by_name(ch_name);

	if (idx < 0) {
		LOG_WRN("Unknown channel: %s", ch_name);
		return false;
	}

	/* Apply the change to channel state */
	if (strcmp(param, "period_ms") == 0 &&
	    new_p->value.type == RCLC_PARAMETER_INT) {
		int64_t v = new_p->value.integer_value;

		if (v <= 0 || v > 60000) {
			LOG_WRN("period_ms out of range: %lld", v);
			return false;
		}
		channel_state_set_period(idx, (uint32_t)v);
		LOG_INF("  %s.period_ms = %u ms", ch_name, (uint32_t)v);

	} else if (strcmp(param, "enabled") == 0 &&
		   new_p->value.type == RCLC_PARAMETER_BOOL) {
		channel_state_set_enabled(idx, new_p->value.bool_value);
		LOG_INF("  %s.enabled = %s", ch_name,
			new_p->value.bool_value ? "true" : "false");

	} else if (strcmp(param, "invert_logic") == 0 &&
		   new_p->value.type == RCLC_PARAMETER_BOOL) {
		channel_state_set_invert(idx, new_p->value.bool_value);
		LOG_INF("  %s.invert_logic = %s", ch_name,
			new_p->value.bool_value ? "true" : "false");

	} else {
		LOG_WRN("Unknown param: %s", param);
		return false;
	}

	/* Auto-save to flash */
	param_server_save_to_config();
	return true;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

int param_server_init(rcl_node_t *node, rclc_executor_t *executor)
{
	rclc_parameter_options_t opts = {
		.notify_changed_over_dds     = false,
		.max_params                  = 48,   /* 16 channels x 3 params */
		.allow_undeclared_parameters = false,
		.low_mem_mode                = true, /* required for RP2040    */
	};

	rcl_ret_t rc = rclc_parameter_server_init_with_option(
		&param_server, node, &opts);

	if (rc != RCL_RET_OK) {
		LOG_ERR("param_server_init error: %d", (int)rc);
		return -EIO;
	}

	/* Declare 3 parameters per channel with descriptor defaults */
	char param_name[64];
	int  count = channel_manager_count();

	for (int i = 0; i < count; i++) {
		const char *ch = channel_manager_name(i);

		if (!ch) {
			continue;
		}

		snprintf(param_name, sizeof(param_name),
			 "ch.%s.period_ms", ch);
		rclc_add_parameter(&param_server, param_name,
				   RCLC_PARAMETER_INT);
		rclc_parameter_set_int(&param_server, param_name,
				       (int64_t)channel_state_get_period(i));

		snprintf(param_name, sizeof(param_name),
			 "ch.%s.enabled", ch);
		rclc_add_parameter(&param_server, param_name,
				   RCLC_PARAMETER_BOOL);
		rclc_parameter_set_bool(&param_server, param_name,
					channel_state_get_enabled(i));

		snprintf(param_name, sizeof(param_name),
			 "ch.%s.invert_logic", ch);
		rclc_add_parameter(&param_server, param_name,
				   RCLC_PARAMETER_BOOL);
		rclc_parameter_set_bool(&param_server, param_name,
					channel_state_get_invert(i));
	}

	rc = rclc_executor_add_parameter_server_with_context(
		executor, &param_server, on_param_changed, NULL);

	if (rc != RCL_RET_OK) {
		LOG_ERR("executor add param_server error: %d", (int)rc);
		rclc_parameter_server_fini(&param_server, node);
		return -EIO;
	}

	param_server_ready = true;
	LOG_INF("Parameter server ready (%d params)", 3 * count);
	return 0;
}

void param_server_fini(rcl_node_t *node)
{
	if (!param_server_ready) {
		return;
	}
	rclc_parameter_server_fini(&param_server, node);
	param_server_ready = false;
	LOG_INF("Parameter server destroyed");
}

int param_server_load_from_config(void)
{
	int count  = channel_manager_count();
	int loaded = 0;

	for (int i = 0; i < count; i++) {
		const char *ch_name = channel_manager_name(i);

		if (!ch_name) {
			continue;
		}

		/* Start from current state defaults */
		uint32_t period  = channel_state_get_period(i);
		bool     enabled = channel_state_get_enabled(i);
		bool     invert  = channel_state_get_invert(i);

		int rc = channel_params_load(ch_name, &period, &enabled, &invert);

		if (rc == -ENOENT) {
			/* File not found — use defaults, stop trying */
			break;
		}

		/* Apply loaded values to channel_manager state */
		channel_state_set_period(i,  period);
		channel_state_set_enabled(i, enabled);
		channel_state_set_invert(i,  invert);

		/* Sync to live parameter server so ros2 param list is accurate */
		if (param_server_ready) {
			char pname[64];

			snprintf(pname, sizeof(pname),
				 "ch.%s.period_ms", ch_name);
			rclc_parameter_set_int(&param_server, pname,
					       (int64_t)period);

			snprintf(pname, sizeof(pname),
				 "ch.%s.enabled", ch_name);
			rclc_parameter_set_bool(&param_server, pname, enabled);

			snprintf(pname, sizeof(pname),
				 "ch.%s.invert_logic", ch_name);
			rclc_parameter_set_bool(&param_server, pname, invert);
		}

		loaded++;
	}

	if (loaded > 0) {
		LOG_INF("Channel params restored: %d channels", loaded);
	}
	return (loaded > 0) ? 0 : -ENOENT;
}

int param_server_save_to_config(void)
{
	int count = channel_manager_count();

	/* Stack allocation safe: CHANNEL_MAX = 16, struct is small */
	channel_param_entry_t entries[CHANNEL_MAX];

	for (int i = 0; i < count; i++) {
		entries[i].name      = channel_manager_name(i);
		entries[i].period_ms = channel_state_get_period(i);
		entries[i].enabled   = channel_state_get_enabled(i);
		entries[i].invert    = channel_state_get_invert(i);
	}

	return channel_params_save(entries, count);
}
