/*
 * apps/rc/src/user_channels.c — RC per-device csatorna-regiszter (BL-015 step 3).
 *
 * Csak az RC csatornák (CH1..CH6) regisztrálódnak — estop, mode, okgo, relay,
 * battery NEM kerül ide. A register_if_enabled pattern megőrzi a meglévő
 * config_channel_enabled() szemantikát: a runtime config (devices/RC/config.json)
 * eldöntheti, hogy az adott csatorna aktív legyen-e.
 */

#include "user/user_channels.h"
#include "bridge/channel_manager.h"
#include "config/config.h"
#include "rc.h"

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
}
