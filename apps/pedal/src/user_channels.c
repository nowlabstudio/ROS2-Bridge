/*
 * apps/pedal/src/user_channels.c — PEDAL per-device csatorna-regiszter (BL-015 step 4).
 *
 * Jelenleg csak a heartbeat csatorna regisztrálódik — a PEDAL device a
 * BL-015 előtt is csak smoke/health pingre volt használva (test_heartbeat).
 * A tényleges pedál-hardware csatornák később, dedikált backlog item alapján
 * kerülnek be ide.
 */

#include "user/user_channels.h"
#include "bridge/channel_manager.h"
#include "config/config.h"
#include "pedal.h"

static void register_if_enabled(const channel_t *ch)
{
	if (config_channel_enabled(ch->name)) {
		channel_register(ch);
	}
}

void user_register_channels(void)
{
	register_if_enabled(&pedal_heartbeat_channel);
}
