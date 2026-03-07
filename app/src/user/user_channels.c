#include "user_channels.h"
#include "bridge/channel_manager.h"
#include "config/config.h"
#include "test_channels.h"
#include "estop.h"
#include "rc.h"

static void register_if_enabled(const channel_t *ch)
{
	if (config_channel_enabled(ch->name)) {
		channel_register(ch);
	}
}

void user_register_channels(void)
{
	register_if_enabled(&test_counter_channel);
	register_if_enabled(&test_heartbeat_channel);
	register_if_enabled(&test_echo_channel);
	register_if_enabled(&estop_channel);

	register_if_enabled(&rc_ch1_channel);
	register_if_enabled(&rc_ch2_channel);
	register_if_enabled(&rc_ch3_channel);
	register_if_enabled(&rc_ch4_channel);
	register_if_enabled(&rc_ch5_channel);
	register_if_enabled(&rc_ch6_channel);
}
