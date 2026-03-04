#ifndef USER_TEST_CHANNELS_H
#define USER_TEST_CHANNELS_H

#include "bridge/channel.h"

/* Publish-only: incrementing INT32 counter, 500ms period.
 * Topic: pico/counter
 * Verify with: ros2 topic echo /pico/counter */
extern const channel_t test_counter_channel;

/* Publish-only: toggling BOOL, 1000ms period.
 * Topic: pico/heartbeat
 * Verify with: ros2 topic echo /pico/heartbeat */
extern const channel_t test_heartbeat_channel;

/* Bidirectional INT32 echo, 1000ms period.
 * Receives on:  pico/echo_in   (ros2 topic pub /pico/echo_in ...)
 * Publishes on: pico/echo_out  (ros2 topic echo /pico/echo_out)
 * The last received value is echoed back on echo_out. */
extern const channel_t test_echo_channel;

#endif /* USER_TEST_CHANNELS_H */
