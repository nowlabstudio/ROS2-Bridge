#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>

#include <rcl/rcl.h>
#include <rcl/error_handling.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>
#include <rmw_microros/rmw_microros.h>
#include <microros_transports.h>

#include <string.h>
#include "config/config.h"
#include "bridge/channel_manager.h"
#include "user/user_channels.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/*  Státusz LED — GP25 (beépített LED)                                 */
/* ------------------------------------------------------------------ */

static const struct gpio_dt_spec status_led =
	GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static void led_set(bool on)
{
	gpio_pin_set_dt(&status_led, on ? 1 : 0);
}

BUILD_ASSERT(DT_NODE_HAS_COMPAT(DT_CHOSEN(zephyr_console), zephyr_cdc_acm_uart),
	     "Console device is not CDC ACM UART");

#define RCCHECK(fn) do { \
	rcl_ret_t rc = fn; \
	if (rc != RCL_RET_OK) { \
		LOG_ERR("RCL error %d (line %d)", (int)rc, __LINE__); \
		while (1) k_sleep(K_MSEC(1000)); \
	} \
} while (0)

/* ------------------------------------------------------------------ */
/*  Hálózati konfig alkalmazása (DHCP vagy statikus)                  */
/* ------------------------------------------------------------------ */

static K_SEM_DEFINE(net_event_sem, 0, 1);
static struct net_mgmt_event_callback net_mgmt_cb;

static void net_event_handler(struct net_mgmt_event_callback *cb,
			       uint64_t event, struct net_if *iface)
{
	ARG_UNUSED(cb);
	ARG_UNUSED(iface);
	if (event == NET_EVENT_IF_UP ||
	    event == NET_EVENT_IPV4_DHCP_BOUND) {
		k_sem_give(&net_event_sem);
	}
}

static void apply_network_config(void)
{
	struct net_if *iface = net_if_get_default();

	/* Ethernet link UP várás (max 10 másodperc) */
	if (!net_if_is_up(iface)) {
		LOG_INF("Ethernet link várás...");
		net_mgmt_init_event_callback(&net_mgmt_cb, net_event_handler,
					     NET_EVENT_IF_UP);
		net_mgmt_add_event_callback(&net_mgmt_cb);
		if (k_sem_take(&net_event_sem, K_SECONDS(10)) == 0) {
			LOG_INF("Ethernet link UP");
		} else {
			LOG_WRN("Ethernet link timeout, folytatás...");
		}
		net_mgmt_del_event_callback(&net_mgmt_cb);
	}

	if (g_config.network.dhcp) {
		LOG_INF("Hálózat: DHCP indítva...");
		net_mgmt_init_event_callback(&net_mgmt_cb, net_event_handler,
					     NET_EVENT_IPV4_DHCP_BOUND);
		net_mgmt_add_event_callback(&net_mgmt_cb);
		net_dhcpv4_start(iface);
		if (k_sem_take(&net_event_sem, K_SECONDS(15)) == 0) {
			LOG_INF("DHCP: IP kiosztva");
		} else {
			LOG_WRN("DHCP: timeout, folytatás IP nélkül");
		}
		net_mgmt_del_event_callback(&net_mgmt_cb);
	} else {
		struct in_addr addr, mask, gw;

		if (net_addr_pton(AF_INET, g_config.network.ip, &addr) < 0 ||
		    net_addr_pton(AF_INET, g_config.network.netmask, &mask) < 0 ||
		    net_addr_pton(AF_INET, g_config.network.gateway, &gw) < 0) {
			LOG_ERR("Hálózati cím formátum hiba");
			return;
		}

		net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0);
		net_if_ipv4_set_netmask_by_addr(iface, &addr, &mask);
		net_if_ipv4_router_add(iface, &gw, true, 0);

		LOG_INF("Hálózat: statikus IP %s", g_config.network.ip);
		k_sleep(K_MSEC(500));
	}
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
	const struct device *console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	uint32_t dtr = 0;

	/* LED init */
	gpio_pin_configure_dt(&status_led, GPIO_OUTPUT_INACTIVE);
	led_set(false);

	/* USB CDC ACM init + DTR várás */
	if (usb_enable(NULL)) {
		return -1;
	}
	while (!dtr) {
		uart_line_ctrl_get(console, UART_LINE_CTRL_DTR, &dtr);
		k_sleep(K_MSEC(100));
	}

	/* ------------------------------------------------------------ */
	/*  Konfig betöltés                                              */
	/* ------------------------------------------------------------ */
	config_init();

	LOG_INF("W6100 EVB Pico - micro-ROS Bridge");
	LOG_INF("Node: %s%s", g_config.ros.namespace_, g_config.ros.node_name);
	LOG_INF("Agent: %s:%s", g_config.network.agent_ip, g_config.network.agent_port);

	/* ------------------------------------------------------------ */
	/*  Csatornák regisztrálása és hardware init                    */
	/* ------------------------------------------------------------ */
	user_register_channels();
	channel_manager_init_channels();

	/* ------------------------------------------------------------ */
	/*  Hálózati konfig alkalmazása                                 */
	/* ------------------------------------------------------------ */
	apply_network_config();

	/* ------------------------------------------------------------ */
	/*  micro-ROS UDP transport                                     */
	/* ------------------------------------------------------------ */
	memset(&default_params, 0, sizeof(default_params));
	strncpy(default_params.ip,   g_config.network.agent_ip,   sizeof(default_params.ip)   - 1);
	strncpy(default_params.port, g_config.network.agent_port, sizeof(default_params.port) - 1);
	default_params.ip[sizeof(default_params.ip)   - 1] = '\0';
	default_params.port[sizeof(default_params.port) - 1] = '\0';

	rmw_uros_set_custom_transport(
		MICRO_ROS_FRAMING_REQUIRED,
		(void *)&default_params,
		zephyr_transport_open,
		zephyr_transport_close,
		zephyr_transport_write,
		zephyr_transport_read
	);

	/* ------------------------------------------------------------ */
	/*  micro-ROS init                                              */
	/* ------------------------------------------------------------ */
	rcl_allocator_t allocator = rcl_get_default_allocator();
	rclc_support_t support;
	RCCHECK(rclc_support_init(&support, 0, NULL, &allocator));

	rcl_node_t node;
	RCCHECK(rclc_node_init_default(
		&node,
		g_config.ros.node_name,
		g_config.ros.namespace_,
		&support));

	/* ------------------------------------------------------------ */
	/*  ROS2 publisher / subscriber entitások létrehozása           */
	/* ------------------------------------------------------------ */
	if (channel_manager_create_entities(&node, &allocator) < 0) {
		LOG_ERR("Channel entitás létrehozási hiba");
	}

	/* ------------------------------------------------------------ */
	/*  Executor — handle count = subscriber csatornák száma        */
	/* ------------------------------------------------------------ */
	int sub_count = channel_manager_sub_count();
	int handle_count = (sub_count > 0) ? sub_count : 1;

	rclc_executor_t executor;
	RCCHECK(rclc_executor_init(&executor, &support.context,
				   handle_count, &allocator));

	channel_manager_add_subs_to_executor(&executor);

	LOG_INF("Bridge kész. %d csatorna regisztrálva, %d subscriber.",
		channel_manager_count(), sub_count);
	LOG_INF("Shell: 'bridge config show'");

	/* ------------------------------------------------------------ */
	/*  Fő loop                                                     */
	/* ------------------------------------------------------------ */
	int64_t last_ping_ms = 0;
	bool    agent_ok     = false;

	while (1) {
		rclc_executor_spin_some(&executor, RCL_MS_TO_NS(10));
		channel_manager_publish();

		/* Agent ping — 1 másodpercenként */
		int64_t now = k_uptime_get();

		if ((now - last_ping_ms) >= 1000) {
			last_ping_ms = now;
			bool connected = (rmw_uros_ping_agent(100, 1) == RMW_RET_OK);

			if (connected != agent_ok) {
				agent_ok = connected;
				led_set(agent_ok);
				LOG_INF("ROS2 agent: %s",
					agent_ok ? "KAPCSOLÓDVA" : "LEKAPCSOLÓDVA");
			}
		}

		k_msleep(10);
	}
}
