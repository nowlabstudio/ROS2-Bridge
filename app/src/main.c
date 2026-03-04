#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>

#include <rcl/rcl.h>
#include <rcl/error_handling.h>
#include <std_msgs/msg/int32.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>
#include <rmw_microros/rmw_microros.h>
#include <microros_transports.h>

#include <string.h>
#include "config/config.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

BUILD_ASSERT(DT_NODE_HAS_COMPAT(DT_CHOSEN(zephyr_console), zephyr_cdc_acm_uart),
	     "Console device is not CDC ACM UART");

/* ------------------------------------------------------------------ */
/*  Hálózati konfig alkalmazása (DHCP vagy statikus)                  */
/* ------------------------------------------------------------------ */

static K_SEM_DEFINE(dhcp_got_ip, 0, 1);
static struct net_mgmt_event_callback net_mgmt_cb;

static void net_event_handler(struct net_mgmt_event_callback *cb,
			       uint64_t event, struct net_if *iface)
{
	ARG_UNUSED(cb);
	ARG_UNUSED(iface);
	if (event == NET_EVENT_IPV4_DHCP_BOUND) {
		k_sem_give(&dhcp_got_ip);
	}
}

static void apply_network_config(void)
{
	struct net_if *iface = net_if_get_default();

	if (g_config.network.dhcp) {
		LOG_INF("Hálózat: DHCP indítva...");
		net_mgmt_init_event_callback(&net_mgmt_cb, net_event_handler,
					     NET_EVENT_IPV4_DHCP_BOUND);
		net_mgmt_add_event_callback(&net_mgmt_cb);
		net_dhcpv4_start(iface);
		if (k_sem_take(&dhcp_got_ip, K_SECONDS(15)) == 0) {
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
		k_sleep(K_MSEC(200));
	}
}

#define RCCHECK(fn) do { \
	rcl_ret_t rc = fn; \
	if (rc != RCL_RET_OK) { \
		LOG_ERR("RCL error %d (line %d)", (int)rc, __LINE__); \
		while (1) k_sleep(K_MSEC(1000)); \
	} \
} while (0)

static rcl_publisher_t publisher;
static std_msgs__msg__Int32 msg;

static void timer_callback(rcl_timer_t *timer, int64_t last_call_time)
{
	ARG_UNUSED(last_call_time);
	if (timer == NULL) {
		return;
	}
	rcl_ret_t rc = rcl_publish(&publisher, &msg, NULL);
	if (rc == RCL_RET_OK) {
		LOG_INF("publish: %d", msg.data);
	}
	msg.data++;
}

int main(void)
{
	const struct device *console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	uint32_t dtr = 0;

	/* USB CDC ACM init + DTR várás */
	if (usb_enable(NULL)) {
		return -1;
	}
	while (!dtr) {
		uart_line_ctrl_get(console, UART_LINE_CTRL_DTR, &dtr);
		k_sleep(K_MSEC(100));
	}

	/* ------------------------------------------------------------ */
	/*  Konfig betöltés LittleFS-ből (vagy alapértékek első induláskor) */
	/* ------------------------------------------------------------ */
	config_init();

	LOG_INF("W6100 EVB Pico - micro-ROS Bridge");
	LOG_INF("Node: %s%s", g_config.ros.namespace_, g_config.ros.node_name);
	LOG_INF("Agent: %s:%s", g_config.network.agent_ip, g_config.network.agent_port);

	/* Hálózati konfig alkalmazása (DHCP vagy statikus, config.json alapján) */
	apply_network_config();

	/* ------------------------------------------------------------ */
	/*  micro-ROS UDP transport — konfigból veszi az agent adatokat  */
	/* ------------------------------------------------------------ */
	memset(&default_params, 0, sizeof(default_params));
	strncpy(default_params.ip,   g_config.network.agent_ip,   sizeof(default_params.ip)   - 1);
	strncpy(default_params.port, g_config.network.agent_port, sizeof(default_params.port) - 1);

	rmw_uros_set_custom_transport(
		MICRO_ROS_FRAMING_REQUIRED,
		(void *)&default_params,
		zephyr_transport_open,
		zephyr_transport_close,
		zephyr_transport_write,
		zephyr_transport_read
	);

	/* ------------------------------------------------------------ */
	/*  micro-ROS init — node neve konfigból jön                    */
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

	RCCHECK(rclc_publisher_init_default(
		&publisher, &node,
		ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32),
		"counter"));

	rcl_timer_t timer;
	RCCHECK(rclc_timer_init_default(
		&timer, &support,
		RCL_MS_TO_NS(1000),
		timer_callback));

	rclc_executor_t executor;
	RCCHECK(rclc_executor_init(&executor, &support.context, 1, &allocator));
	RCCHECK(rclc_executor_add_timer(&executor, &timer));

	LOG_INF("micro-ROS node kész. Topic: %s%s/counter",
		g_config.ros.namespace_, g_config.ros.node_name);
	LOG_INF("Shell: 'bridge config show' a beállításokhoz");

	msg.data = 0;
	while (1) {
		rclc_executor_spin_some(&executor, RCL_MS_TO_NS(100));
		k_msleep(100);
	}
}
