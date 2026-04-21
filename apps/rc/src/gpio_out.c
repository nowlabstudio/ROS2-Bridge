#include "gpio_out.h"
#include "drivers/drv_gpio.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(gpio_out, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/*  Generic GPIO output csatornák — GP8..GP11                         */
/*                                                                     */
/*  Mindegyik pin két ROS2 topicon jelenik meg:                        */
/*    - gpX         (std_msgs/Bool, subscribe) — parancs               */
/*    - gpX_state   (std_msgs/Bool, publish 5 Hz) — tényleges szint    */
/*                                                                     */
/*  A write() a pinre teszi a parancsot (ACTIVE_HIGH miatt true → 1). */
/*  A state publisher a legutóbb kiadott parancsot adja vissza — RP2040 */
/*  Zephyr GPIO driveren a pin-readback csak INPUT bufferrel mûködik,  */
/*  amit a drv_gpio_setup_output nem kapcsol be. Tiszta output pinnél  */
/*  külső hatás nincs, így a write-echo semantikailag azonos a tényle- */
/*  ges pinszinttel.                                                    */
/*                                                                     */
/*  GP8 fizikailag a világítás-relét hajtja (BL-016), a többi GPIO    */
/*  aktuálisan szabad — bármilyen ACTIVE_HIGH relé/LED bedrótozható.  */
/* ------------------------------------------------------------------ */

#define STATE_PERIOD_MS 200  /* 5 Hz feedback rate */

static gpio_channel_cfg_t gp8_cfg = {
	.spec = GPIO_DT_SPEC_GET(DT_ALIAS(gp8_out), gpios),
	.dir  = GPIO_OUTPUT,
};

static gpio_channel_cfg_t gp9_cfg = {
	.spec = GPIO_DT_SPEC_GET(DT_ALIAS(gp9_out), gpios),
	.dir  = GPIO_OUTPUT,
};

static gpio_channel_cfg_t gp10_cfg = {
	.spec = GPIO_DT_SPEC_GET(DT_ALIAS(gp10_out), gpios),
	.dir  = GPIO_OUTPUT,
};

static gpio_channel_cfg_t gp11_cfg = {
	.spec = GPIO_DT_SPEC_GET(DT_ALIAS(gp11_out), gpios),
	.dir  = GPIO_OUTPUT,
};

/* Last commanded state per pin — read() publishes this back */
static bool gp8_state_cache;
static bool gp9_state_cache;
static bool gp10_state_cache;
static bool gp11_state_cache;

static void gpio_write_common(gpio_channel_cfg_t *cfg, bool *cache,
                              const channel_value_t *val)
{
	LOG_DBG("write: pin=%d val=%d", cfg->spec.pin, val->b ? 1 : 0);
	drv_gpio_write(cfg, val->b ? 1 : 0);
	*cache = val->b;
}

/* ---- GP8 ---- */
static int  gp8_init(void)                       { return drv_gpio_setup_output(&gp8_cfg); }
static void gp8_read(channel_value_t *out)       { out->b = gp8_state_cache; }
static void gp8_write(const channel_value_t *v)  { gpio_write_common(&gp8_cfg, &gp8_state_cache, v); }

const channel_t gp8_channel = {
	.name        = "gp8",
	.topic_pub   = "gp8_state",
	.topic_sub   = "gp8",
	.msg_type    = MSG_BOOL,
	.period_ms   = STATE_PERIOD_MS,
	.irq_capable = false,
	.init        = gp8_init,
	.read        = gp8_read,
	.write       = gp8_write,
};

/* ---- GP9 ---- */
static int  gp9_init(void)                       { return drv_gpio_setup_output(&gp9_cfg); }
static void gp9_read(channel_value_t *out)       { out->b = gp9_state_cache; }
static void gp9_write(const channel_value_t *v)  { gpio_write_common(&gp9_cfg, &gp9_state_cache, v); }

const channel_t gp9_channel = {
	.name        = "gp9",
	.topic_pub   = "gp9_state",
	.topic_sub   = "gp9",
	.msg_type    = MSG_BOOL,
	.period_ms   = STATE_PERIOD_MS,
	.irq_capable = false,
	.init        = gp9_init,
	.read        = gp9_read,
	.write       = gp9_write,
};

/* ---- GP10 ---- */
static int  gp10_init(void)                      { return drv_gpio_setup_output(&gp10_cfg); }
static void gp10_read(channel_value_t *out)      { out->b = gp10_state_cache; }
static void gp10_write(const channel_value_t *v) { gpio_write_common(&gp10_cfg, &gp10_state_cache, v); }

const channel_t gp10_channel = {
	.name        = "gp10",
	.topic_pub   = "gp10_state",
	.topic_sub   = "gp10",
	.msg_type    = MSG_BOOL,
	.period_ms   = STATE_PERIOD_MS,
	.irq_capable = false,
	.init        = gp10_init,
	.read        = gp10_read,
	.write       = gp10_write,
};

/* ---- GP11 ---- */
static int  gp11_init(void)                      { return drv_gpio_setup_output(&gp11_cfg); }
static void gp11_read(channel_value_t *out)      { out->b = gp11_state_cache; }
static void gp11_write(const channel_value_t *v) { gpio_write_common(&gp11_cfg, &gp11_state_cache, v); }

const channel_t gp11_channel = {
	.name        = "gp11",
	.topic_pub   = "gp11_state",
	.topic_sub   = "gp11",
	.msg_type    = MSG_BOOL,
	.period_ms   = STATE_PERIOD_MS,
	.irq_capable = false,
	.init        = gp11_init,
	.read        = gp11_read,
	.write       = gp11_write,
};
