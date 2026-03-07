#include "user_channels.h"
#include "bridge/channel_manager.h"
#include "config/config.h"
#include "test_channels.h"
#include "estop.h"

/* ------------------------------------------------------------------ */
/*  Add your channel headers here:                                     */
/*                                                                     */
/*  #include "motor_left.h"                                            */
/*  #include "distance_sensor.h"                                       */
/*  #include "imu.h"                                                   */
/*                                                                     */
/* ------------------------------------------------------------------ */

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
}
