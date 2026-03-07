#ifndef DRIVERS_DRV_PWM_IN_H
#define DRIVERS_DRV_PWM_IN_H

#include <zephyr/drivers/gpio.h>
#include <stdint.h>
#include <stdbool.h>

#define RC_PWM_MIN_US   800
#define RC_PWM_MAX_US   2200
#define RC_PWM_TIMEOUT_MS 500

typedef struct {
	struct gpio_dt_spec  spec;
	struct gpio_callback cb_data;
	volatile uint32_t    rise_cycles;
	volatile uint16_t    pulse_us;
	volatile int64_t     last_update_ms;
} rc_pwm_channel_t;

/**
 * Initialize an array of RC PWM input channels.
 * Configures GPIO interrupts on both edges for pulse width measurement.
 */
int rc_pwm_init(rc_pwm_channel_t *channels, int count);

/** Get the latest pulse width in microseconds (1000-2000 typical). */
uint16_t rc_pwm_get(const rc_pwm_channel_t *ch);

/** Check if the channel has received a valid pulse recently. */
bool rc_pwm_valid(const rc_pwm_channel_t *ch);

#endif /* DRIVERS_DRV_PWM_IN_H */
