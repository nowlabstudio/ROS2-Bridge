#include "drv_gpio.h"
#include "bridge/channel_manager.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(drv_gpio, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/*  ISR handler — only sets atomic flag, nothing else                 */
/* ------------------------------------------------------------------ */

#define DEBOUNCE_MS 50

static void gpio_isr_handler(const struct device *dev,
			      struct gpio_callback *cb,
			      uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(pins);

	gpio_channel_cfg_t *cfg = CONTAINER_OF(cb, gpio_channel_cfg_t, cb_data);

	int64_t now = k_uptime_get();

	if ((now - cfg->last_irq_ms) < DEBOUNCE_MS) {
		return;
	}
	cfg->last_irq_ms = now;

	channel_manager_signal_irq(cfg->channel_idx);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

int drv_gpio_setup_irq(int channel_idx, gpio_channel_cfg_t *cfg)
{
	if (!cfg || channel_idx < 0) {
		return -EINVAL;
	}

	if (!device_is_ready(cfg->spec.port)) {
		LOG_ERR("GPIO device not ready: %s", cfg->spec.port->name);
		return -ENODEV;
	}

	int rc = gpio_pin_configure_dt(&cfg->spec, GPIO_INPUT);
	if (rc < 0) {
		LOG_ERR("GPIO pin configure error: %d", rc);
		return rc;
	}

	cfg->channel_idx = channel_idx;

	gpio_init_callback(&cfg->cb_data, gpio_isr_handler,
			   BIT(cfg->spec.pin));

	rc = gpio_add_callback(cfg->spec.port, &cfg->cb_data);
	if (rc < 0) {
		LOG_ERR("GPIO add callback error: %d", rc);
		return rc;
	}

	rc = gpio_pin_interrupt_configure_dt(&cfg->spec, cfg->irq_flags);
	if (rc < 0) {
		LOG_ERR("GPIO interrupt configure error: %d", rc);
		gpio_remove_callback(cfg->spec.port, &cfg->cb_data);
		return rc;
	}

	LOG_INF("GPIO IRQ configured: pin %d, channel_idx %d",
		cfg->spec.pin, channel_idx);
	return 0;
}

int drv_gpio_setup_output(gpio_channel_cfg_t *cfg)
{
	if (!cfg) {
		return -EINVAL;
	}

	if (!device_is_ready(cfg->spec.port)) {
		LOG_ERR("GPIO device not ready: %s", cfg->spec.port->name);
		return -ENODEV;
	}

	int rc = gpio_pin_configure_dt(&cfg->spec, GPIO_OUTPUT_INACTIVE);
	if (rc < 0) {
		LOG_ERR("GPIO output configure error: %d", rc);
		return rc;
	}

	LOG_INF("GPIO OUT configured: pin %d", cfg->spec.pin);
	return 0;
}

int drv_gpio_read(const gpio_channel_cfg_t *cfg)
{
	if (!cfg) {
		return -EINVAL;
	}
	return gpio_pin_get_dt(&cfg->spec);
}

int drv_gpio_write(const gpio_channel_cfg_t *cfg, int value)
{
	if (!cfg) {
		return -EINVAL;
	}
	return gpio_pin_set_dt(&cfg->spec, value);
}
