/*
 * common/src/drivers/drv_pwm_in_pio.c — BL-020 Step 2 fix.
 *
 * RP2040 PIO-alapú, deterministic PWM input capture (HIGH-pulzus szélesség,
 * 1 µs felbontás). Egy csatorna = egy PIO state machine; a 6 RC csatornát
 * 4 SM a PIO0-on + 2 SM a PIO1-en méri. CPU ISR nem vesz részt a
 * mérésben, így a korábbi GPIO bank0 shared IRQ dispatch latency
 * (BL-020 diagnózis: +27.5 µs/HIGH-csatorna adagolódva a CH2 timestamp-ba)
 * megszűnik.
 *
 * A driver NEM Zephyr device driver (nem `DEVICE_DT_INST_DEFINE`); az
 * API-ja azonos az IRQ fallback driverrel (`drv_pwm_in.c`):
 *
 *     int rc_pwm_init(rc_pwm_channel_t *channels, int count);
 *     uint16_t rc_pwm_get(const rc_pwm_channel_t *ch);
 *     bool rc_pwm_valid(const rc_pwm_channel_t *ch);
 *
 * Ez tartja érintetlenül az `apps/rc/src/rc.c` és `apps/rc/src/user_channels.c`
 * modulokat — a fix csak ebben a fájlban él.
 *
 * Futásidő-adatáramlás:
 *
 *   1. DT compile-time: `DT_INST_FOREACH_STATUS_OKAY` a `nowlab,pwm-input-pio`
 *      node-okból épít egy `pwm_in_pio_slot` táblát (pin, parent piodev, pinctrl).
 *   2. `rc_pwm_init()`: minden hívó-oldali `rc_pwm_channel_t`-hez a
 *      `spec.pin` alapján megtalálja a slot-ot, allokál egy SM-et, betölti
 *      a `pwm_input` PIO programot (egyszer PIO-nként), pinctrl-t alkalmaz,
 *      beállítja a clkdiv-et, elindítja az SM-et, majd ütemez egy 50 Hz-es
 *      delayed work handler-t.
 *   3. Drain worker (`pwm_pio_drain_work_handler`): kiüríti mindegyik
 *      SM RX FIFO-ját. A FIFO-ban a PIO program (`mov isr, !x`) miatt már
 *      az `~x = elapsed count` érték van, ami a 2 MHz PIO órajel és a
 *      2-ciklusos count loop együtt pont 1 µs/count felbontást ad, így a
 *      FIFO-ból olvasott 32-bit érték KÖZVETLENÜL a mért µs-szélesség —
 *      a C oldalon nem kell még egyszer invertálni.
 *   4. `rc_pwm_get()` / `rc_pwm_valid()`: csak a cache-t olvassa, ISR-mentes.
 *
 * PIO program (8 utasítás, egyszer töltődik PIO blokkonként):
 *
 *     0: wait   0 pin, 0      ; szinkron: várj LOW-ra
 *     1: wait   1 pin, 0      ; várj a rising edge-re
 *     2: mov    x, !null      ; x = 0xFFFFFFFF (µs-szamlalo init)
 *     3: jmp    x--, 5        ; x != 0 → goto 5; x == 0 (overflow) → fall through
 *     4: jmp    0             ; overflow: 71 perces pulzus esetén restart
 *     5: jmp    pin, 3        ; pin HIGH → tovább számolunk
 *     6: mov    isr, !x       ; pin LOW: ISR = ~x = elapsed count
 *     7: push   noblock       ; FIFO-ba (régi minta drop, ha tele)
 *     (.wrap → 0)
 *
 * A count-loop 2 utasítás (3 és 5), vagyis 2 PIO-ciklus/lépés. clkdiv =
 * sys_clk / 2 MHz, így a loop 1 MHz-en fut → 1 count = 1 µs. Zephyr alatt
 * a sys_clk-et runtime-ban olvassuk (`clock_get_hz(clk_sys)`), hogy a
 * képlet ne legyen érzékeny a board vagy BSP változásaira (pl. ha valaki
 * a jövőben overclockolja a bridge-et).
 */

#define DT_DRV_COMPAT nowlab_pwm_input_pio

#include "drv_pwm_in.h"

#include <zephyr/device.h>
#include <zephyr/drivers/misc/pio_rpi_pico/pio_rpi_pico.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <hardware/clocks.h>
#include <hardware/pio.h>

LOG_MODULE_REGISTER(drv_pwm_in_pio, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/*  PIO program (hand-encoded — nincs pioasm-függőség a build-ben)    */
/* ------------------------------------------------------------------ */

RPI_PICO_PIO_DEFINE_PROGRAM(pwm_input, 0, 7,
	0x2020, /*  0: wait   0 pin, 0           */
	0x20a0, /*  1: wait   1 pin, 0           */
	0xa02b, /*  2: mov    x, !null           */
	0x0045, /*  3: count: jmp    x--, 5      */
	0x0000, /*  4:        jmp    0           */
	0x00c3, /*  5: dec:   jmp    pin, 3      */
	0xa0c9, /*  6:        mov    isr, !x     */
	0x8000  /*  7:        push   noblock     */
);

/* ------------------------------------------------------------------ */
/*  DT compile-time slot table                                        */
/* ------------------------------------------------------------------ */

struct pwm_in_pio_slot {
	uint8_t                           pin;
	const struct device              *piodev;
	const struct pinctrl_dev_config  *pin_cfg;
};

#define SLOT_DEFINE(inst)                                                    \
	PINCTRL_DT_INST_DEFINE(inst);                                        \
	static const struct pwm_in_pio_slot slot_##inst = {                  \
		.pin     = DT_INST_PROP(inst, pin),                          \
		.piodev  = DEVICE_DT_GET(DT_INST_PARENT(inst)),              \
		.pin_cfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),             \
	};

DT_INST_FOREACH_STATUS_OKAY(SLOT_DEFINE)

#define SLOT_PTR(inst) &slot_##inst,
static const struct pwm_in_pio_slot *const pwm_pio_slots[] = {
	DT_INST_FOREACH_STATUS_OKAY(SLOT_PTR)
};
#define PWM_PIO_SLOT_COUNT ARRAY_SIZE(pwm_pio_slots)

BUILD_ASSERT(PWM_PIO_SLOT_COUNT > 0,
	     "drv_pwm_in_pio: no nowlab,pwm-input-pio nodes found in DT — "
	     "check that the board overlay enables &pio0/&pio1 and the child nodes.");

/* ------------------------------------------------------------------ */
/*  PIO-blokkonként egy program-példány                                */
/* ------------------------------------------------------------------ */

#define PWM_PIO_BLOCK_MAX  2

struct pwm_pio_block {
	PIO pio;
	int offset;
};

static struct pwm_pio_block pwm_pio_blocks[PWM_PIO_BLOCK_MAX] = {
	{ .pio = NULL, .offset = -1 },
	{ .pio = NULL, .offset = -1 },
};

static int pwm_pio_load_program_once(PIO pio, int *out_offset)
{
	for (size_t i = 0; i < ARRAY_SIZE(pwm_pio_blocks); i++) {
		if (pwm_pio_blocks[i].pio == pio) {
			*out_offset = pwm_pio_blocks[i].offset;
			return 0;
		}
	}
	for (size_t i = 0; i < ARRAY_SIZE(pwm_pio_blocks); i++) {
		if (pwm_pio_blocks[i].pio == NULL) {
			if (!pio_can_add_program(pio,
				RPI_PICO_PIO_GET_PROGRAM(pwm_input))) {
				LOG_ERR("PIO block out of instruction memory");
				return -EBUSY;
			}
			int off = pio_add_program(pio,
				RPI_PICO_PIO_GET_PROGRAM(pwm_input));
			pwm_pio_blocks[i].pio    = pio;
			pwm_pio_blocks[i].offset = off;
			*out_offset              = off;
			return 0;
		}
	}
	return -ENOMEM;
}

/* ------------------------------------------------------------------ */
/*  50 Hz drain worker (system workqueue)                              */
/* ------------------------------------------------------------------ */

#define PWM_PIO_DRAIN_PERIOD_MS  20  /* 50 Hz — egy RC frame per drain */

static rc_pwm_channel_t *pwm_pio_channels;
static int               pwm_pio_channel_count;

static struct k_work_delayable pwm_pio_drain_work;

static void pwm_pio_drain_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	for (int i = 0; i < pwm_pio_channel_count; i++) {
		rc_pwm_channel_t *ch = &pwm_pio_channels[i];

		if (ch->pio_instance == NULL) {
			continue;
		}

		PIO    pio = (PIO)ch->pio_instance;
		size_t sm  = ch->pio_sm;

		bool     got_sample = false;
		uint32_t x_raw      = 0;

		while (!pio_sm_is_rx_fifo_empty(pio, sm)) {
			x_raw      = pio_sm_get(pio, sm);
			got_sample = true;
		}

		if (got_sample && x_raw >= RC_PWM_MIN_US && x_raw <= RC_PWM_MAX_US) {
			ch->pulse_us       = (uint16_t)x_raw;
			ch->last_update_ms = k_uptime_get();
		}
	}

	k_work_schedule(&pwm_pio_drain_work, K_MSEC(PWM_PIO_DRAIN_PERIOD_MS));
}

/* ------------------------------------------------------------------ */
/*  SM setup (pinctrl + program + clkdiv + enable)                    */
/* ------------------------------------------------------------------ */

static int pwm_pio_sm_setup(const struct pwm_in_pio_slot *slot,
			    rc_pwm_channel_t *ch)
{
	if (!device_is_ready(slot->piodev)) {
		LOG_ERR("GP%u: PIO parent device not ready", slot->pin);
		return -ENODEV;
	}

	PIO pio = pio_rpi_pico_get_pio(slot->piodev);

	int offset;
	int ret = pwm_pio_load_program_once(pio, &offset);

	if (ret < 0) {
		return ret;
	}

	size_t sm;

	ret = pio_rpi_pico_allocate_sm(slot->piodev, &sm);
	if (ret < 0) {
		LOG_ERR("GP%u: PIO SM allocation failed: %d", slot->pin, ret);
		return ret;
	}

	ret = pinctrl_apply_state(slot->pin_cfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		LOG_ERR("GP%u: pinctrl apply failed: %d", slot->pin, ret);
		return ret;
	}

	pio_sm_config c = pio_get_default_sm_config();

	sm_config_set_wrap(&c,
		offset + RPI_PICO_PIO_GET_WRAP_TARGET(pwm_input),
		offset + RPI_PICO_PIO_GET_WRAP(pwm_input));
	sm_config_set_in_pins(&c, slot->pin);
	sm_config_set_jmp_pin(&c, slot->pin);
	sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);

	/* 2-ciklusos count loop → 1 µs/count-hoz PIO órajel = 2 MHz kell. */
	float div = (float)clock_get_hz(clk_sys) / 2.0f / 1000000.0f;

	sm_config_set_clkdiv(&c, div);

	pio_sm_set_consecutive_pindirs(pio, sm, slot->pin, 1, false);
	pio_sm_init(pio, sm, offset, &c);
	pio_sm_set_enabled(pio, sm, true);

	ch->pio_instance = (void *)pio;
	ch->pio_sm       = (uint8_t)sm;
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

int rc_pwm_init(rc_pwm_channel_t *channels, int count)
{
	pwm_pio_channels      = channels;
	pwm_pio_channel_count = count;

	for (int i = 0; i < count; i++) {
		uint8_t                        pin  = channels[i].spec.pin;
		const struct pwm_in_pio_slot  *slot = NULL;

		for (size_t j = 0; j < PWM_PIO_SLOT_COUNT; j++) {
			if (pwm_pio_slots[j]->pin == pin) {
				slot = pwm_pio_slots[j];
				break;
			}
		}

		if (slot == NULL) {
			LOG_ERR("RC CH%d: no PIO slot for GP%u — "
				"ellenőrizd az overlay `nowlab,pwm-input-pio` node-jait",
				i + 1, pin);
			return -ENODEV;
		}

		int ret = pwm_pio_sm_setup(slot, &channels[i]);

		if (ret < 0) {
			LOG_ERR("RC CH%d: PIO SM setup failed: %d", i + 1, ret);
			return ret;
		}

		channels[i].pulse_us       = 1500;
		channels[i].last_update_ms = 0;

		LOG_INF("RC CH%d: PIO on GP%u (sm=%u)",
			i + 1, pin, channels[i].pio_sm);
	}

	k_work_init_delayable(&pwm_pio_drain_work, pwm_pio_drain_work_handler);
	k_work_schedule(&pwm_pio_drain_work, K_MSEC(PWM_PIO_DRAIN_PERIOD_MS));
	return 0;
}

uint16_t rc_pwm_get(const rc_pwm_channel_t *ch)
{
	return ch->pulse_us;
}

bool rc_pwm_valid(const rc_pwm_channel_t *ch)
{
	return (k_uptime_get() - ch->last_update_ms) < RC_PWM_TIMEOUT_MS;
}
