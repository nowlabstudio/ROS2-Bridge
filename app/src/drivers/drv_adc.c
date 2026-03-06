#include "drv_adc.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/adc.h>

LOG_MODULE_REGISTER(drv_adc, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/*  Hardware configuration                                             */
/*                                                                     */
/*  Voltage divider ratio: V_bat = V_adc * VDIV_RATIO                 */
/*  Calibrate VDIV_RATIO to match your actual resistor network.        */
/*                                                                     */
/*  Example: R1=100kΩ, R2=10kΩ → ratio = (100+10)/10 = 11.0          */
/*  Supports up to 3.3V * 11 = 36.3V battery voltage.                 */
/* ------------------------------------------------------------------ */

#define VDIV_RATIO   11.0f
#define ADC_REF_MV   3300       /* RP2040 ADC reference: 3.3V in mV  */
#define ADC_BITS     12         /* RP2040 ADC resolution              */

static const struct adc_dt_spec adc_spec =
	ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

static struct adc_sequence adc_seq;
static int16_t             adc_raw;

static int adc_battery_init(void)
{
	if (!adc_is_ready_dt(&adc_spec)) {
		LOG_ERR("ADC device not ready");
		return -ENODEV;
	}

	int rc = adc_channel_setup_dt(&adc_spec);

	if (rc < 0) {
		LOG_ERR("ADC channel setup error: %d", rc);
		return rc;
	}

	adc_sequence_init_dt(&adc_spec, &adc_seq);
	adc_seq.buffer      = &adc_raw;
	adc_seq.buffer_size = sizeof(adc_raw);

	LOG_INF("ADC battery channel ready (ratio=%.1f)", (double)VDIV_RATIO);
	return 0;
}

static void adc_battery_read(channel_value_t *val)
{
	int rc = adc_read_dt(&adc_spec, &adc_seq);

	if (rc < 0) {
		LOG_ERR("ADC read error: %d", rc);
		val->f32 = -1.0f;  /* sentinel: read error */
		return;
	}

	/* Convert raw to millivolts, then to battery voltage */
	int32_t mv = adc_raw;

	adc_raw_to_millivolts_dt(&adc_spec, &mv);

	val->f32 = ((float)mv / 1000.0f) * VDIV_RATIO;
}

/* ------------------------------------------------------------------ */
/*  Channel descriptor                                                 */
/* ------------------------------------------------------------------ */

const channel_t adc_battery_channel = {
	.name        = "adc_battery",
	.topic_pub   = "robot/battery_voltage",
	.topic_sub   = NULL,
	.msg_type    = MSG_FLOAT32,
	.period_ms   = 1000,
	.irq_capable = false,
	.init        = adc_battery_init,
	.read        = adc_battery_read,
	.write       = NULL,
};
