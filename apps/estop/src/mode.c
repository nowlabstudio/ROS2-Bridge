#include "mode.h"
#include "bridge/channel_manager.h"
#include "drivers/drv_gpio.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(mode, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/*  3-state rotary switch — 2 GPIO pin, közös csatorna                */
/*                                                                     */
/*  Overlay (ACTIVE_LOW | PULL_UP):                                    */
/*    mode-auto   GP2 — aktív → publish 2                             */
/*    mode-follow GP3 — aktív → publish 1                             */
/*    egyik sem aktív (közép állás)  → publish 0 (learn)              */
/*                                                                     */
/*  Mindkét pin edge-both IRQ-ot regisztrál ugyanarra a channel_idx-re,*/
/*  így bármelyik állapotváltás azonnali publish-t trigger-el.        */
/* ------------------------------------------------------------------ */

enum {
	MODE_LEARN  = 0,
	MODE_FOLLOW = 1,
	MODE_AUTO   = 2,
};

static gpio_channel_cfg_t mode_auto_cfg = {
	.spec      = GPIO_DT_SPEC_GET(DT_ALIAS(mode_auto), gpios),
	.dir       = GPIO_INPUT,
	.irq_flags = GPIO_INT_EDGE_BOTH,
};

static gpio_channel_cfg_t mode_follow_cfg = {
	.spec      = GPIO_DT_SPEC_GET(DT_ALIAS(mode_follow), gpios),
	.dir       = GPIO_INPUT,
	.irq_flags = GPIO_INT_EDGE_BOTH,
};

static int mode_init(void)
{
	int idx = channel_manager_find_by_name("mode");

	if (idx < 0) {
		LOG_ERR("mode channel not found in registry");
		return -ENODEV;
	}

	int rc = drv_gpio_setup_irq(idx, &mode_auto_cfg);
	if (rc < 0) {
		return rc;
	}
	return drv_gpio_setup_irq(idx, &mode_follow_cfg);
}

static void mode_read(channel_value_t *val)
{
	/* ACTIVE_LOW overlay + gpio_pin_get_dt inverzió: 1 = aktív. */
	bool auto_on   = (drv_gpio_read(&mode_auto_cfg)   == 1);
	bool follow_on = (drv_gpio_read(&mode_follow_cfg) == 1);

	if (auto_on) {
		val->i32 = MODE_AUTO;
	} else if (follow_on) {
		val->i32 = MODE_FOLLOW;
	} else {
		val->i32 = MODE_LEARN;
	}
}

const channel_t mode_channel = {
	.name        = "mode",
	.topic_pub   = "mode",
	.topic_sub   = NULL,
	.msg_type    = MSG_INT32,
	/*
	 * 100 ms fallback heartbeat — az állapotváltás IRQ-n keresztül
	 * <1 ms latency-vel érkezik, a periodikus publish csak stale
	 * detekcióhoz kell a subscriber oldalon.
	 */
	.period_ms   = 100,
	.irq_capable = true,
	.init        = mode_init,
	.read        = mode_read,
	.write       = NULL,
};
