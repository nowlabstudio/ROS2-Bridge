#include "estop.h"
#include "bridge/channel_manager.h"
#include "drivers/drv_gpio.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(estop, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/*  Hardware config                                                    */
/*                                                                     */
/*  NC gomb GP27 és GND között, pull-up engedélyezve (overlay-ben).   */
/*  GPIO_ACTIVE_LOW miatt gpio_pin_get logikai értékei:                */
/*    1 = gomb ZÁRVA  (normál állapot, rendszer OK)                    */
/*    0 = gomb NYITVA (E-Stop aktiválva, veszély)                      */
/*  A csatorna true-t publikál, ha az E-Stop aktív.                   */
/* ------------------------------------------------------------------ */

static gpio_channel_cfg_t estop_cfg = {
	.spec      = GPIO_DT_SPEC_GET(DT_ALIAS(estop_btn), gpios),
	.dir       = GPIO_INPUT,
	.irq_flags = GPIO_INT_EDGE_BOTH,
};

static int estop_init(void)
{
	int idx = channel_manager_find_by_name("estop");

	if (idx < 0) {
		LOG_ERR("estop channel not found in registry");
		return -ENODEV;
	}

	return drv_gpio_setup_irq(idx, &estop_cfg);
}

static void estop_read(channel_value_t *val)
{
	/* gpio returns 0 when E-Stop is triggered (button open, pin pulled high,
	 * but ACTIVE_LOW inverts: high → logical 0).
	 * Publish true = E-Stop ACTIVE.
	 */
	val->b = (drv_gpio_read(&estop_cfg) == 0);
}

const channel_t estop_channel = {
	.name        = "estop",
	.topic_pub   = "robot/estop",
	.topic_sub   = NULL,
	.msg_type    = MSG_BOOL,
	.period_ms   = 500,       /* periodic fallback, even without edge */
	.irq_capable = true,      /* immediate publish on GPIO edge       */
	.init        = estop_init,
	.read        = estop_read,
	.write       = NULL,
};
