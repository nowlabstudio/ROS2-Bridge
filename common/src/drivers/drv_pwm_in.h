#ifndef DRIVERS_DRV_PWM_IN_H
#define DRIVERS_DRV_PWM_IN_H

#include <zephyr/drivers/gpio.h>
#include <stdint.h>
#include <stdbool.h>

#define RC_PWM_MIN_US   800
#define RC_PWM_MAX_US   2200
#define RC_PWM_TIMEOUT_MS 500

/*
 * Két driver-implementáció osztja ezt a struct-ot (Kconfig-kapcsolóval):
 *
 *   - `drv_pwm_in.c`      — GPIO IRQ (EDGE_BOTH) driver. `cb_data` + `rise_cycles`
 *                           a callback-ban használatos; `pulse_us`-t az ISR frissíti.
 *   - `drv_pwm_in_pio.c`  — RP2040 PIO alapú deterministic driver (BL-020 Step 2).
 *                           `pio_instance` + `pio_sm` azonosítja az allokált SM-et;
 *                           a `pulse_us` cache-t egy 50 Hz delayed work handler
 *                           frissíti: a FIFO-ban a PIO program (`mov isr, !x`)
 *                           miatt eleve az elapsed µs érték van, tehát a
 *                           drain közvetlenül használja (nincs második invert).
 *
 * A PIO-mezők void* pointerként vannak deklarálva, hogy ne kelljen
 * a pico-sdk <hardware/pio.h>-ját behúzni minden apps/<device> forrásba — az IRQ
 * driverben ezek a mezők mindig NULL / 0, és a kód nem hivatkozik rájuk.
 * PIO buildben a driver `(PIO)pio_instance` cast-tal használja.
 */
typedef struct {
	struct gpio_dt_spec  spec;
	struct gpio_callback cb_data;
	volatile uint32_t    rise_cycles;
	volatile uint16_t    pulse_us;
	volatile int64_t     last_update_ms;
	void                *pio_instance;   /* PIO0/PIO1, NULL IRQ-buildben */
	uint8_t              pio_sm;         /* csak a PIO driverben érvényes */
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
