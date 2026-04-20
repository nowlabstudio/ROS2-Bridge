#ifndef PEDAL_H
#define PEDAL_H

#include "bridge/channel.h"

/* PEDAL heartbeat — publish-only BOOL toggle, 1 Hz.
 * Topic: heartbeat (ROS namespace /robot → /robot/heartbeat).
 * Verify with: ros2 topic echo /robot/heartbeat
 *
 * BL-015 step 4: a régi test_channels.c heartbeat-jének PEDAL-os átnevezése.
 * A struct .name = "pedal_heartbeat" (config.json key); a .topic_pub = "heartbeat"
 * marad a memory-rögzített BL-010+ERR-031 zöld topic kompatibilitásáért. */
extern const channel_t pedal_heartbeat_channel;

#endif /* PEDAL_H */
