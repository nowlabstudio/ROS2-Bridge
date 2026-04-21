#include "okgo_btn.h"
#include "bridge/channel_manager.h"
#include "drivers/drv_gpio.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(okgo_btn, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/*  OK-Go nyomógomb — safety 2-pin AND                                */
/*                                                                     */
/*  Overlay (ACTIVE_LOW | PULL_UP):                                    */
/*    okgo-btn-a GP4                                                   */
/*    okgo-btn-b GP5                                                   */
/*                                                                     */
/*  A két pin ugyanannak a nyomógombnak két redundáns kontaktusa; a   */
/*  publikált érték csak akkor true, ha MINDKETTŐ aktív — ha az egyik */
/*  kontaktus megszakad, a safety AND nem húz be. Mindkét pinre IRQ  */
/*  edge-both regisztrálódik ugyanarra a channel_idx-re.               */
/* ------------------------------------------------------------------ */

static gpio_channel_cfg_t okgo_a_cfg = {
	.spec      = GPIO_DT_SPEC_GET(DT_ALIAS(okgo_btn_a), gpios),
	.dir       = GPIO_INPUT,
	.irq_flags = GPIO_INT_EDGE_BOTH,
};

static gpio_channel_cfg_t okgo_b_cfg = {
	.spec      = GPIO_DT_SPEC_GET(DT_ALIAS(okgo_btn_b), gpios),
	.dir       = GPIO_INPUT,
	.irq_flags = GPIO_INT_EDGE_BOTH,
};

static int okgo_btn_init(void)
{
	int idx = channel_manager_find_by_name("okgo_btn");

	if (idx < 0) {
		LOG_ERR("okgo_btn channel not found in registry");
		return -ENODEV;
	}

	int rc = drv_gpio_setup_irq(idx, &okgo_a_cfg);
	if (rc < 0) {
		return rc;
	}
	return drv_gpio_setup_irq(idx, &okgo_b_cfg);
}

static void okgo_btn_read(channel_value_t *val)
{
	/* ACTIVE_LOW + gpio_pin_get_dt inverzió: 1 = gomb lenyomva. */
	val->b = (drv_gpio_read(&okgo_a_cfg) == 1) &&
		 (drv_gpio_read(&okgo_b_cfg) == 1);
}

const channel_t okgo_btn_channel = {
	.name        = "okgo_btn",
	.topic_pub   = "okgo_btn",
	.topic_sub   = NULL,
	.msg_type    = MSG_BOOL,
	/*
	 * 100 ms fallback heartbeat — az állapotváltás IRQ-n keresztül
	 * <1 ms latency-vel érkezik, a periodikus publish csak stale
	 * detekcióhoz kell a subscriber oldalon.
	 */
	.period_ms   = 100,
	.irq_capable = true,
	.init        = okgo_btn_init,
	.read        = okgo_btn_read,
	.write       = NULL,
};
