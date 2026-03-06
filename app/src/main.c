#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/sys/reboot.h>

#include <rcl/rcl.h>
#include <rcl/error_handling.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>
#include <rmw_microros/rmw_microros.h>
#include <microros_transports.h>

#include <string.h>
#include "config/config.h"
#include "bridge/channel_manager.h"
#include "bridge/diagnostics.h"
#include "bridge/param_server.h"
#include "bridge/service_manager.h"
#include "user/user_channels.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

BUILD_ASSERT(DT_NODE_HAS_COMPAT(DT_CHOSEN(zephyr_console), zephyr_cdc_acm_uart),
	     "Console device is not CDC ACM UART");

/* ------------------------------------------------------------------ */
/*  Timing constants                                                   */
/* ------------------------------------------------------------------ */

#define DTR_TIMEOUT_MS       3000   /* max wait for USB monitor (autonomous mode) */
#define WDT_TIMEOUT_MS       30000  /* hardware watchdog timeout                  */
#define AGENT_PING_TIMEOUT   200    /* agent ping timeout in ms                   */
#define AGENT_PING_ATTEMPTS  1      /* number of ping attempts per cycle          */
#define AGENT_WAIT_DELAY_MS  2000   /* delay between ping retries                 */
#define NET_LINK_TIMEOUT_S   15     /* Ethernet link UP wait timeout              */
#define DHCP_TIMEOUT_S       20     /* DHCP lease acquisition timeout             */

/* ------------------------------------------------------------------ */
/*  Hardware watchdog                                                  */
/* ------------------------------------------------------------------ */

static const struct device *wdt;
static int wdt_channel = -1;

static void watchdog_init(void)
{
	wdt = DEVICE_DT_GET(DT_NODELABEL(wdt0));
	if (!device_is_ready(wdt)) {
		LOG_WRN("Watchdog not available");
		wdt = NULL;
		return;
	}

	struct wdt_timeout_cfg wdt_cfg = {
		.window.min = 0,
		.window.max = WDT_TIMEOUT_MS,
		.callback   = NULL,
		.flags      = WDT_FLAG_RESET_SOC,
	};

	wdt_channel = wdt_install_timeout(wdt, &wdt_cfg);
	if (wdt_channel < 0) {
		LOG_WRN("Watchdog timeout install error: %d", wdt_channel);
		wdt = NULL;
		return;
	}

	if (wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG) < 0) {
		LOG_WRN("Watchdog setup error");
		wdt = NULL;
		return;
	}

	LOG_INF("Watchdog active (%d ms timeout)", WDT_TIMEOUT_MS);
}

static inline void watchdog_feed(void)
{
	if (wdt && wdt_channel >= 0) {
		wdt_feed(wdt, wdt_channel);
	}
}

/* ------------------------------------------------------------------ */
/*  Status LED — GP25 (built-in LED)                                  */
/* ------------------------------------------------------------------ */

static const struct gpio_dt_spec status_led =
	GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static void led_set(bool on)
{
	gpio_pin_set_dt(&status_led, on ? 1 : 0);
}

/* ------------------------------------------------------------------ */
/*  Apply network configuration (DHCP or static IP)                  */
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

	/* Wait for Ethernet link UP */
	if (!net_if_is_up(iface)) {
		LOG_INF("Waiting for Ethernet link (max %ds)...", NET_LINK_TIMEOUT_S);
		net_mgmt_init_event_callback(&net_mgmt_cb, net_event_handler,
					     NET_EVENT_IF_UP);
		net_mgmt_add_event_callback(&net_mgmt_cb);
		if (k_sem_take(&net_event_sem, K_SECONDS(NET_LINK_TIMEOUT_S)) == 0) {
			LOG_INF("Ethernet link UP");
		} else {
			LOG_WRN("Ethernet link timeout — continuing without cable");
		}
		net_mgmt_del_event_callback(&net_mgmt_cb);
	}

	if (g_config.network.dhcp) {
		LOG_INF("Network: DHCP starting...");
		net_mgmt_init_event_callback(&net_mgmt_cb, net_event_handler,
					     NET_EVENT_IPV4_DHCP_BOUND);
		net_mgmt_add_event_callback(&net_mgmt_cb);
		net_dhcpv4_start(iface);
		if (k_sem_take(&net_event_sem, K_SECONDS(DHCP_TIMEOUT_S)) == 0) {
			LOG_INF("DHCP: IP address assigned");
		} else {
			LOG_WRN("DHCP timeout — continuing without IP address");
		}
		net_mgmt_del_event_callback(&net_mgmt_cb);
	} else {
		struct in_addr addr, mask, gw;

		if (net_addr_pton(AF_INET, g_config.network.ip,      &addr) < 0 ||
		    net_addr_pton(AF_INET, g_config.network.netmask, &mask) < 0 ||
		    net_addr_pton(AF_INET, g_config.network.gateway, &gw)   < 0) {
			LOG_ERR("Network address format error — check config.json");
			return;
		}

		net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0);
		net_if_ipv4_set_netmask_by_addr(iface, &addr, &mask);
		net_if_ipv4_router_add(iface, &gw, true, 0);

		LOG_INF("Network: static IP %s", g_config.network.ip);
		k_sleep(K_MSEC(500));
	}
}

/* ------------------------------------------------------------------ */
/*  micro-ROS session init / cleanup                                  */
/* ------------------------------------------------------------------ */

static rcl_allocator_t allocator;
static rclc_support_t  support;
static rcl_node_t      node;
static rclc_executor_t executor;
static bool            session_active;

static bool ros_session_init(void)
{
	allocator = rcl_get_default_allocator();

	if (rclc_support_init(&support, 0, NULL, &allocator) != RCL_RET_OK) {
		LOG_ERR("rclc_support_init error");
		return false;
	}

	if (rclc_node_init_default(&node,
				   g_config.ros.node_name,
				   g_config.ros.namespace_,
				   &support) != RCL_RET_OK) {
		LOG_ERR("rclc_node_init error");
		rclc_support_fini(&support);
		return false;
	}

	if (channel_manager_create_entities(&node, &allocator) < 0) {
		LOG_ERR("channel_manager_create_entities error");
		rcl_node_fini(&node);
		rclc_support_fini(&support);
		return false;
	}

	/* Diagnostics publisher — before executor (no executor handle needed) */
	if (diagnostics_init(&node, &allocator) < 0) {
		LOG_WRN("Diagnostics init failed — continuing without /diagnostics");
	}

	int sub_count    = channel_manager_sub_count();
	int handle_count = sub_count + PARAM_SERVER_HANDLES + service_count();

	if (handle_count < 1) {
		handle_count = 1;
	}

	if (rclc_executor_init(&executor, &support.context,
			       handle_count, &allocator) != RCL_RET_OK) {
		LOG_ERR("rclc_executor_init error");
		diagnostics_fini(&node);
		channel_manager_destroy_entities(&node, &allocator);
		rcl_node_fini(&node);
		rclc_support_fini(&support);
		return false;
	}

	channel_manager_add_subs_to_executor(&executor);

	/* Parameter server — after executor init, adds itself to executor */
	if (param_server_init(&node, &executor) < 0) {
		LOG_WRN("Param server init failed — continuing without params");
	}

	/* Services — after executor init, each adds itself to executor */
	if (service_manager_init(&node, &executor) < 0) {
		LOG_WRN("Service manager init failed");
	}

	/* Restore persisted channel params from flash */
	param_server_load_from_config();

	session_active = true;
	LOG_INF("micro-ROS session active. %d channels, %d subscribers.",
		channel_manager_count(), sub_count);
	return true;
}

static void ros_session_fini(void)
{
	if (!session_active) {
		return;
	}
	/* Destroy in reverse init order — children before parent node */
	service_manager_fini(&node);
	param_server_fini(&node);
	diagnostics_fini(&node);
	channel_manager_destroy_entities(&node, &allocator);
	rclc_executor_fini(&executor);
	rcl_node_fini(&node);
	rclc_support_fini(&support);
	session_active = false;
	LOG_INF("micro-ROS session closed");
}

/* ------------------------------------------------------------------ */
/*  Reconnection loop                                                  */
/* ------------------------------------------------------------------ */

static void bridge_run(void)
{
	LOG_INF("Starting bridge main loop");

	while (true) {
		watchdog_feed();

		/* ---------------------------------------------------- */
		/*  Phase 1: Search for agent (blocking, with WDT feed) */
		/* ---------------------------------------------------- */
		if (rmw_uros_ping_agent(AGENT_PING_TIMEOUT, AGENT_PING_ATTEMPTS)
		    != RMW_RET_OK) {
			led_set(false);
			LOG_INF("Searching for agent: %s:%s ...",
				g_config.network.agent_ip,
				g_config.network.agent_port);

			while (rmw_uros_ping_agent(AGENT_PING_TIMEOUT,
						   AGENT_PING_ATTEMPTS)
			       != RMW_RET_OK) {
				watchdog_feed();
				k_sleep(K_MSEC(AGENT_WAIT_DELAY_MS));
			}

			LOG_INF("Agent found — initializing session");
		}

		/* ---------------------------------------------------- */
		/*  Phase 2: Session initialization                     */
		/* ---------------------------------------------------- */
		if (!ros_session_init()) {
			LOG_WRN("Session init failed, retrying...");
			k_sleep(K_MSEC(AGENT_WAIT_DELAY_MS));
			continue;
		}

		led_set(true);

		/* ---------------------------------------------------- */
		/*  Phase 3: Run — while agent is reachable            */
		/* ---------------------------------------------------- */
		int64_t last_ping_ms = k_uptime_get();
		int64_t last_diag_ms = k_uptime_get();

		while (true) {
			/* IRQ pending check — first, before anything else   */
			channel_manager_handle_irq_pending();

			/* Executor: 1ms timeout */
			rclc_executor_spin_some(&executor, RCL_MS_TO_NS(1));

			/* Periodic publish */
			channel_manager_publish();

			watchdog_feed();

			int64_t now = k_uptime_get();

			if ((now - last_ping_ms) >= 1000) {
				last_ping_ms = now;

				if (rmw_uros_ping_agent(AGENT_PING_TIMEOUT,
							AGENT_PING_ATTEMPTS)
				    != RMW_RET_OK) {
					LOG_WRN("Agent connection lost");
					break;
				}
			}

			if ((now - last_diag_ms) >= 5000) {
				last_diag_ms = now;
				diagnostics_publish();
			}

			k_msleep(1);
		}

		/* ---------------------------------------------------- */
		/*  Phase 4: Cleanup, back to agent search             */
		/* ---------------------------------------------------- */
		led_set(false);
		ros_session_fini();
		g_reconnect_count++;
		LOG_INF("Reconnecting... (attempt %d)", g_reconnect_count);
		k_sleep(K_MSEC(AGENT_WAIT_DELAY_MS));
	}
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
	/* LED init — first, so it can signal during boot */
	gpio_pin_configure_dt(&status_led, GPIO_OUTPUT_INACTIVE);
	led_set(false);

	/* Hardware watchdog */
	watchdog_init();

	/* USB CDC ACM init */
	usb_enable(NULL);

	/* Wait for DTR — max DTR_TIMEOUT_MS, then continue without monitor */
	{
		const struct device *console =
			DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
		uint32_t dtr      = 0;
		int64_t  deadline = k_uptime_get() + DTR_TIMEOUT_MS;

		while (!dtr && k_uptime_get() < deadline) {
			uart_line_ctrl_get(console, UART_LINE_CTRL_DTR, &dtr);
			watchdog_feed();
			k_sleep(K_MSEC(100));
		}

		if (dtr) {
			LOG_INF("USB console connected");
		} else {
			LOG_INF("No USB console — autonomous mode");
		}
	}

	/* ------------------------------------------------------------ */
	/*  Load configuration                                          */
	/* ------------------------------------------------------------ */
	config_init();
	watchdog_feed();

	LOG_INF("W6100 EVB Pico - micro-ROS Bridge");
	LOG_INF("Node: %s%s", g_config.ros.namespace_, g_config.ros.node_name);
	LOG_INF("Agent: %s:%s", g_config.network.agent_ip,
		g_config.network.agent_port);

	/* ------------------------------------------------------------ */
	/*  Register channels and initialize hardware                  */
	/* ------------------------------------------------------------ */
	user_register_channels();
	channel_manager_init_channels();
	watchdog_feed();

	/* ------------------------------------------------------------ */
	/*  Apply network configuration                                */
	/* ------------------------------------------------------------ */
	apply_network_config();
	watchdog_feed();

	/* ------------------------------------------------------------ */
	/*  Configure micro-ROS UDP transport                          */
	/* ------------------------------------------------------------ */
	memset(&default_params, 0, sizeof(default_params));
	strncpy(default_params.ip,   g_config.network.agent_ip,
		sizeof(default_params.ip)   - 1);
	strncpy(default_params.port, g_config.network.agent_port,
		sizeof(default_params.port) - 1);
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

	LOG_INF("Shell: 'bridge config show'");

	/* ------------------------------------------------------------ */
	/*  Reconnection loop — never returns                          */
	/* ------------------------------------------------------------ */
	bridge_run();

	/* Should never reach here */
	sys_reboot(SYS_REBOOT_COLD);
	return 0;
}
