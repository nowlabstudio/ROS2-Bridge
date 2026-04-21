/*
 * apps/rc/src/user_channels.c — RC per-device csatorna-regiszter (BL-015 step 3).
 *
 * RC-specifikus csatornák (estop/mode/okgo/relay/battery NEM ide tartozik):
 *   - rc_ch1..rc_ch6 — PWM input publisher csatornák
 *   - gp8..gp11      — generic Bool output + pin-readback state csatornák
 *
 * A register_if_enabled pattern megőrzi a config_channel_enabled() szemantikát:
 * a runtime config (devices/RC/config.json) eldöntheti, hogy egy adott csatorna
 * aktív legyen-e.
 */

#include "user/user_channels.h"
#include "bridge/channel_manager.h"
#include "config/config.h"
#include "rc.h"
#include "gpio_out.h"

static void register_if_enabled(const channel_t *ch)
{
	if (config_channel_enabled(ch->name)) {
		channel_register(ch);
	}
}

void user_register_channels(void)
{
	register_if_enabled(&rc_ch1_channel);
	register_if_enabled(&rc_ch2_channel);
	register_if_enabled(&rc_ch3_channel);
	register_if_enabled(&rc_ch4_channel);
	register_if_enabled(&rc_ch5_channel);
	register_if_enabled(&rc_ch6_channel);
	register_if_enabled(&gp8_channel);
	register_if_enabled(&gp9_channel);
	register_if_enabled(&gp10_channel);
	register_if_enabled(&gp11_channel);
}
