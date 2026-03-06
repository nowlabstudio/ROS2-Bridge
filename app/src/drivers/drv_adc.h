#ifndef DRIVERS_DRV_ADC_H
#define DRIVERS_DRV_ADC_H

#include "bridge/channel.h"

/**
 * ADC battery voltage channel — pre-built channel_t descriptor.
 *
 * Reads RP2040 ADC input and applies a voltage divider ratio to compute
 * the real battery voltage. Publishes as std_msgs/Float32 on
 * "robot/battery_voltage".
 *
 * Hardware:
 *   - ADC pin: GP26 (ADC0) — adjust DTS if different
 *   - Voltage divider: VDIV_RATIO in drv_adc.c (default 11.0)
 *     Calibrate for your resistor network before use.
 *
 * Register with:
 *   channel_register(&adc_battery_channel);
 */
extern const channel_t adc_battery_channel;

#endif /* DRIVERS_DRV_ADC_H */
