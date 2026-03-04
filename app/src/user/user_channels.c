#include "user_channels.h"
#include "bridge/channel_manager.h"
#include "test_channels.h"

/* ------------------------------------------------------------------ */
/*  Add your channel headers here:                                     */
/*                                                                     */
/*  #include "motor_left.h"                                            */
/*  #include "distance_sensor.h"                                       */
/*  #include "imu.h"                                                   */
/*                                                                     */
/* ------------------------------------------------------------------ */

void user_register_channels(void)
{
	/* Test channels — no hardware required, remove when not needed  */
	channel_register(&test_counter_channel);
	channel_register(&test_heartbeat_channel);
	channel_register(&test_echo_channel);
}
