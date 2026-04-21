#include "okgo_led.h"
#include "drivers/drv_gpio.h"

/* ------------------------------------------------------------------ */
/*  OK-Go visszajelző LED — subscribe-only output csatorna             */
/*                                                                     */
/*  Overlay (ACTIVE_HIGH, output):                                     */
/*    okgo-led GP22                                                    */
/*                                                                     */
/*  Írható std_msgs/Bool (topic default: okgo_led). A write() a       */
/*  logikai értéket közvetlenül a pinre teszi — ACTIVE_HIGH miatt     */
/*  true → pin magas, false → pin alacsony.                            */
/* ------------------------------------------------------------------ */

static gpio_channel_cfg_t okgo_led_cfg = {
	.spec = GPIO_DT_SPEC_GET(DT_ALIAS(okgo_led), gpios),
	.dir  = GPIO_OUTPUT,
};

static int okgo_led_init(void)
{
	return drv_gpio_setup_output(&okgo_led_cfg);
}

static void okgo_led_write(const channel_value_t *val)
{
	drv_gpio_write(&okgo_led_cfg, val->b ? 1 : 0);
}

const channel_t okgo_led_channel = {
	.name        = "okgo_led",
	.topic_pub   = NULL,
	.topic_sub   = "okgo_led",
	.msg_type    = MSG_BOOL,
	.period_ms   = 0,
	.irq_capable = false,
	.init        = okgo_led_init,
	.read        = NULL,
	.write       = okgo_led_write,
};
