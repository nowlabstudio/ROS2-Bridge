/*
 * apps/estop/src/user_channels.c — E_STOP per-device csatorna-regiszter (BL-015 step 2).
 *
 * A régi közös app/src/user/user_channels.c minden device csatornáját
 * felsorolta (test_*, estop, rc_ch1..6) és config-flag alapján döntött a
 * regisztrációról. Itt csak a ténylegesen E_STOP-hoz tartozó csatornák
 * jelennek meg — a binárisban más device kódja (rc.c, test_channels.c)
 * fizikailag nincs jelen.
 *
 * A "register_if_enabled" pattern megmarad: a runtime config (devices/E_STOP/
 * config.json) eldöntheti, hogy az adott csatornát aktív legyen-e — ezzel a
 * meglévő config_channel_enabled() szemantika változatlan.
 *
 * BL-014 Fázis 2 új csatornái (mode/okgo/okgo_led) ide kerülnek be, miután
 * BL-015 lezárult.
 */

#include "user/user_channels.h"
#include "bridge/channel_manager.h"
#include "config/config.h"
#include "estop.h"

static void register_if_enabled(const channel_t *ch)
{
	if (config_channel_enabled(ch->name)) {
		channel_register(ch);
	}
}

void user_register_channels(void)
{
	register_if_enabled(&estop_channel);
}
