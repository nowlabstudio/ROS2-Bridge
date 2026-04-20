#ifndef DRIVERS_DRV_IMU_H
#define DRIVERS_DRV_IMU_H

/*
 * IMU driver framework — placeholder for future sensor_msgs/Imu publishing.
 *
 * The channel system currently supports MSG_BOOL / MSG_INT32 / MSG_FLOAT32.
 * sensor_msgs/Imu requires a dedicated publish path (9 floats + header).
 * This file reserves the integration point; the implementation will be
 * completed in a future iteration when:
 *   1. A concrete IMU (e.g. MPU-6050, BNO085) is selected
 *   2. MSG_SENSOR_IMU is added to msg_type_t
 *   3. channel_manager_publish() gains an IMU-specific branch
 *
 * Static allocation pattern (ZERO heap):
 *   imu_msg.header.frame_id.data     = frame_id_buf;    // static char[]
 *   imu_msg.header.frame_id.size     = sizeof(frame_id_buf) - 1;
 *   imu_msg.header.frame_id.capacity = sizeof(frame_id_buf);
 *
 *   // Covariance[0] = -1.0 = "unknown" per Nav2 / robot_localization std.
 *   imu_msg.orientation_covariance[0]         = -1.0f;
 *   imu_msg.angular_velocity_covariance[0]    = -1.0f;
 *   imu_msg.linear_acceleration_covariance[0] = -1.0f;
 *
 * Frame ID: "bridge_imu_link" (configurable via parameter server)
 * Topic:    "robot/imu"
 */

#endif /* DRIVERS_DRV_IMU_H */
