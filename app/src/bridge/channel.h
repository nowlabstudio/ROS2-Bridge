#ifndef BRIDGE_CHANNEL_H
#define BRIDGE_CHANNEL_H

#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Üzenettípusok (ROS2 std_msgs)                                      */
/* ------------------------------------------------------------------ */

typedef enum {
	MSG_BOOL    = 0,
	MSG_INT32   = 1,
	MSG_FLOAT32 = 2,
} msg_type_t;

/* ------------------------------------------------------------------ */
/*  Csatorna érték — amit a read() visszaad és a write() megkap        */
/* ------------------------------------------------------------------ */

typedef union {
	bool    b;
	int32_t i32;
	float   f32;
} channel_value_t;

/* ------------------------------------------------------------------ */
/*  Csatorna leíró struktúra                                           */
/* ------------------------------------------------------------------ */

typedef struct {
	const char  *name;        /* egyedi azonosító (loghoz, debug)    */
	const char  *topic_pub;   /* publish topic, NULL = nem publikál  */
	const char  *topic_sub;   /* subscribe topic, NULL = nem figyel  */
	msg_type_t   msg_type;    /* üzenet típusa mindkét irányban      */
	uint32_t     period_ms;   /* publish periódus milliszekundumban  */

	/* Inicializálás — hardware setup, gpio/pwm/i2c/spi init stb.   */
	int  (*init)(void);

	/* Szenzor olvasás / státusz lekérdezés → ROS2 publish           */
	void (*read)(channel_value_t *out);

	/* ROS2 subscribe üzenet fogadása → aktuátor / setpoint írás     */
	void (*write)(const channel_value_t *in);
} channel_t;

#endif /* BRIDGE_CHANNEL_H */
