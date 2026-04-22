#include "drv_pwm_in.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(drv_pwm_in, LOG_LEVEL_INF);

static void pwm_isr(const struct device *dev,
		     struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(pins);

	rc_pwm_channel_t *ch = CONTAINER_OF(cb, rc_pwm_channel_t, cb_data);
	uint32_t now_cyc = k_cycle_get_32();

	if (gpio_pin_get_dt(&ch->spec)) {
		ch->rise_cycles = now_cyc;
	} else {
		uint32_t elapsed = now_cyc - ch->rise_cycles;
		uint32_t us = k_cyc_to_us_floor32(elapsed);

		if (us >= RC_PWM_MIN_US && us <= RC_PWM_MAX_US) {
			ch->pulse_us      = (uint16_t)us;
			ch->last_update_ms = k_uptime_get();
		}
	}
}

int rc_pwm_init(rc_pwm_channel_t *channels, int count)
{
	for (int i = 0; i < count; i++) {
		rc_pwm_channel_t *ch = &channels[i];

		/* BL-020 diag: skip CH3 IRQ regisztrációt — preemption-bias hipotézis
		 * megerősítésére. Ha a CH2 +27.5 µs bias eltűnik aktív CH3 mellett,
		 * a shared IO_IRQ_BANK0 dispatch latency a gyökérok. Patch ideiglenes,
		 * a megerősítés után törlendő (vagy átmegy PIO-alapú driverbe). */
		if (i == 2) {
			LOG_WRN("RC CH%d: IRQ regisztráció kihagyva (BL-020 diag)", i + 1);
			ch->pulse_us       = 1500;
			ch->last_update_ms = 0;
			continue;
		}

		if (!device_is_ready(ch->spec.port)) {
			LOG_ERR("RC CH%d: GPIO device not ready", i + 1);
			return -ENODEV;
		}

		int rc = gpio_pin_configure_dt(&ch->spec, GPIO_INPUT);
		if (rc < 0) {
			LOG_ERR("RC CH%d: pin configure error %d", i + 1, rc);
			return rc;
		}

		gpio_init_callback(&ch->cb_data, pwm_isr,
				   BIT(ch->spec.pin));

		rc = gpio_add_callback(ch->spec.port, &ch->cb_data);
		if (rc < 0) {
			LOG_ERR("RC CH%d: add callback error %d", i + 1, rc);
			return rc;
		}

		rc = gpio_pin_interrupt_configure_dt(&ch->spec,
						     GPIO_INT_EDGE_BOTH);
		if (rc < 0) {
			LOG_ERR("RC CH%d: IRQ configure error %d", i + 1, rc);
			return rc;
		}

		ch->pulse_us       = 1500;
		ch->last_update_ms = 0;

		LOG_INF("RC CH%d: pin %d ready", i + 1, ch->spec.pin);
	}
	return 0;
}

uint16_t rc_pwm_get(const rc_pwm_channel_t *ch)
{
	return ch->pulse_us;
}

bool rc_pwm_valid(const rc_pwm_channel_t *ch)
{
	return (k_uptime_get() - ch->last_update_ms) < RC_PWM_TIMEOUT_MS;
}
