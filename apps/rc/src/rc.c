#include "rc.h"
#include "bridge/channel_manager.h"
#include "config/config.h"
#include "drivers/drv_pwm_in.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(rc, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/*  Hardware — 6 PWM input channels on GP2..GP7                       */
/* ------------------------------------------------------------------ */

static rc_pwm_channel_t rc_hw[RC_CH_COUNT] = {
	[0] = { .spec = GPIO_DT_SPEC_GET(DT_ALIAS(rc_ch1), gpios) },
	[1] = { .spec = GPIO_DT_SPEC_GET(DT_ALIAS(rc_ch2), gpios) },
	[2] = { .spec = GPIO_DT_SPEC_GET(DT_ALIAS(rc_ch3), gpios) },
	[3] = { .spec = GPIO_DT_SPEC_GET(DT_ALIAS(rc_ch4), gpios) },
	[4] = { .spec = GPIO_DT_SPEC_GET(DT_ALIAS(rc_ch5), gpios) },
	[5] = { .spec = GPIO_DT_SPEC_GET(DT_ALIAS(rc_ch6), gpios) },
};

static int rc_init(void)
{
	return rc_pwm_init(rc_hw, RC_CH_COUNT);
}

/* ------------------------------------------------------------------ */
/*  EMA filter state (per channel)                                     */
/* ------------------------------------------------------------------ */

static float ema_state[RC_CH_COUNT];
static bool  ema_initialized[RC_CH_COUNT];

/* ------------------------------------------------------------------ */
/*  Normalize raw PWM (us) to -1.0 .. +1.0 using trim from config    */
/* ------------------------------------------------------------------ */

static float rc_normalize(int hw_idx)
{
	if (!rc_pwm_valid(&rc_hw[hw_idx])) {
		ema_initialized[hw_idx] = false;
		return 0.0f;
	}

	uint16_t raw = rc_pwm_get(&rc_hw[hw_idx]);
	const cfg_rc_trim_ch_t *t = &g_config.rc_trim.ch[hw_idx];
	uint16_t dz = g_config.rc_trim.deadzone;

	int16_t centered = (int16_t)raw - (int16_t)t->center;

	if (abs(centered) < (int16_t)dz) {
		centered = 0;
	}

	float norm;

	if (centered > 0) {
		uint16_t range = t->max - t->center;

		norm = (range > 0) ? (float)centered / (float)range : 0.0f;
	} else if (centered < 0) {
		uint16_t range = t->center - t->min;

		norm = (range > 0) ? (float)centered / (float)range : 0.0f;
	} else {
		norm = 0.0f;
	}

	if (norm > 1.0f) {
		norm = 1.0f;
	} else if (norm < -1.0f) {
		norm = -1.0f;
	}

	float alpha = g_config.rc_trim.ema_alpha;

	if (alpha >= 1.0f) {
		return norm;
	}

	if (!ema_initialized[hw_idx]) {
		ema_state[hw_idx] = norm;
		ema_initialized[hw_idx] = true;
		return norm;
	}

	ema_state[hw_idx] = alpha * norm + (1.0f - alpha) * ema_state[hw_idx];
	return ema_state[hw_idx];
}

/* ------------------------------------------------------------------ */
/*  Read functions — one per channel                                   */
/* ------------------------------------------------------------------ */

static void rc_ch1_read(channel_value_t *val) { val->f32 = rc_normalize(0); }
static void rc_ch2_read(channel_value_t *val) { val->f32 = rc_normalize(1); }
static void rc_ch3_read(channel_value_t *val) { val->f32 = rc_normalize(2); }
static void rc_ch4_read(channel_value_t *val) { val->f32 = rc_normalize(3); }
static void rc_ch5_read(channel_value_t *val) { val->f32 = rc_normalize(4); }
static void rc_ch6_read(channel_value_t *val) { val->f32 = rc_normalize(5); }

/* ------------------------------------------------------------------ */
/*  Channel descriptors                                                */
/* ------------------------------------------------------------------ */

#define RC_STICK_PERIOD_MS   20    /* 50Hz — matches RC frame rate */
#define RC_SWITCH_PERIOD_MS  50

const channel_t rc_ch1_channel = {
	.name      = "rc_ch1",
	.topic_pub = "rc_ch1",
	.msg_type  = MSG_FLOAT32,
	.period_ms = RC_STICK_PERIOD_MS,
	.init      = rc_init,
	.read      = rc_ch1_read,
};

const channel_t rc_ch2_channel = {
	.name      = "rc_ch2",
	.topic_pub = "rc_ch2",
	.msg_type  = MSG_FLOAT32,
	.period_ms = RC_STICK_PERIOD_MS,
	.read      = rc_ch2_read,
};

const channel_t rc_ch3_channel = {
	.name      = "rc_ch3",
	.topic_pub = "rc_ch3",
	.msg_type  = MSG_FLOAT32,
	.period_ms = RC_STICK_PERIOD_MS,
	.read      = rc_ch3_read,
};

const channel_t rc_ch4_channel = {
	.name      = "rc_ch4",
	.topic_pub = "rc_ch4",
	.msg_type  = MSG_FLOAT32,
	.period_ms = RC_STICK_PERIOD_MS,
	.read      = rc_ch4_read,
};

const channel_t rc_ch5_channel = {
	.name      = "rc_ch5",
	.topic_pub = "rc_ch5",
	.msg_type  = MSG_FLOAT32,
	.period_ms = RC_SWITCH_PERIOD_MS,
	.read      = rc_ch5_read,
};

const channel_t rc_ch6_channel = {
	.name      = "rc_ch6",
	.topic_pub = "rc_ch6",
	.msg_type  = MSG_FLOAT32,
	.period_ms = RC_SWITCH_PERIOD_MS,
	.read      = rc_ch6_read,
};
