#ifndef DRIVERS_DRV_GPIO_H
#define DRIVERS_DRV_GPIO_H

#include <zephyr/drivers/gpio.h>

/* ------------------------------------------------------------------ */
/*  GPIO channel configuration                                         */
/*                                                                     */
/*  Fill in a gpio_channel_cfg_t and call drv_gpio_setup_irq() from   */
/*  a channel's init() function to enable interrupt-driven publish.   */
/* ------------------------------------------------------------------ */

typedef struct {
	struct gpio_dt_spec  spec;         /* DT alias pin spec              */
	gpio_flags_t         dir;          /* GPIO_INPUT or GPIO_OUTPUT      */
	gpio_flags_t         irq_flags;    /* GPIO_INT_EDGE_BOTH etc.        */
	int                  channel_idx;  /* filled in by drv_gpio_setup_irq */
	struct gpio_callback cb_data;      /* Zephyr internal callback struct */
	int64_t              last_irq_ms;  /* debounce: last IRQ timestamp    */
} gpio_channel_cfg_t;

/**
 * Configure a GPIO pin for interrupt-driven channel publishing.
 *
 * Configures the pin as input, registers the ISR callback, and enables
 * the interrupt. The ISR sets an atomic flag via channel_manager_signal_irq();
 * the main loop handles the actual ROS2 publish.
 *
 * @param channel_idx  Channel index from channel_manager_find_by_name()
 * @param cfg          Pointer to a statically allocated gpio_channel_cfg_t
 * @return 0 on success, negative errno on error
 */
int drv_gpio_setup_irq(int channel_idx, gpio_channel_cfg_t *cfg);

/**
 * Configure a GPIO pin as output (no IRQ). For subscribe-driven channels
 * whose write() writes the pin via drv_gpio_write().
 *
 * @param cfg  Pointer to a configured gpio_channel_cfg_t (spec filled in)
 * @return 0 on success, negative errno on error
 */
int drv_gpio_setup_output(gpio_channel_cfg_t *cfg);

/**
 * Read current GPIO pin level.
 *
 * @param cfg  Pointer to a configured gpio_channel_cfg_t
 * @return 1 if high, 0 if low, negative errno on error
 */
int drv_gpio_read(const gpio_channel_cfg_t *cfg);

/**
 * Set GPIO output pin level.
 *
 * @param cfg    Pointer to a configured gpio_channel_cfg_t
 * @param value  0 = low, 1 = high
 * @return 0 on success, negative errno on error
 */
int drv_gpio_write(const gpio_channel_cfg_t *cfg, int value);

#endif /* DRIVERS_DRV_GPIO_H */
