#ifndef BRIDGE_CHANNEL_H
#define BRIDGE_CHANNEL_H

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/sys/atomic.h>

/* ------------------------------------------------------------------ */
/*  Message types supported by the channel system                     */
/* ------------------------------------------------------------------ */

typedef enum {
	MSG_BOOL    = 0,   /* std_msgs/Bool    */
	MSG_INT32   = 1,   /* std_msgs/Int32   */
	MSG_FLOAT32 = 2,   /* std_msgs/Float32 */
} msg_type_t;

/* ------------------------------------------------------------------ */
/*  Generic value container returned by read() and passed to write()  */
/* ------------------------------------------------------------------ */

typedef union {
	bool    b;     /* used for MSG_BOOL    */
	int32_t i32;   /* used for MSG_INT32   */
	float   f32;   /* used for MSG_FLOAT32 */
} channel_value_t;

/* ------------------------------------------------------------------ */
/*  Channel descriptor — const, stored in flash                       */
/*                                                                     */
/*  Connects a physical device to one or two ROS2 topics.             */
/*  Set topic_pub or topic_sub to NULL to disable that direction.     */
/* ------------------------------------------------------------------ */

typedef struct {
	const char  *name;        /* unique identifier (shown in logs)   */
	const char  *topic_pub;   /* publish topic, NULL = no publish    */
	const char  *topic_sub;   /* subscribe topic, NULL = no subscribe*/
	msg_type_t   msg_type;    /* message type for both directions    */
	uint32_t     period_ms;   /* default publish period (ms)         */
	bool         irq_capable; /* true = GPIO interrupt publish mode  */

	/* Hardware init — gpio/pwm/i2c/spi/adc setup etc.              */
	int  (*init)(void);

	/* Read sensor value / device status → publish to ROS2          */
	void (*read)(channel_value_t *out);

	/* Receive ROS2 subscribe message → write to actuator/setpoint  */
	void (*write)(const channel_value_t *in);
} channel_t;

/* ------------------------------------------------------------------ */
/*  Channel state — mutable, stored in RAM                            */
/*                                                                     */
/*  Modified at runtime by the parameter server and ISR.              */
/*  Separated from channel_t so descriptors can remain const.         */
/* ------------------------------------------------------------------ */

typedef struct {
	uint32_t  period_ms;    /* active period (overrides descriptor)  */
	bool      enabled;      /* channel active flag                   */
	bool      invert_logic; /* invert bool value on publish/receive  */
	atomic_t  irq_pending;  /* set by ISR, cleared by main loop      */
} channel_state_t;

#endif /* BRIDGE_CHANNEL_H */
