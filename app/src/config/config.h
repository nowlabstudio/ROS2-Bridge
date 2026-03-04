#ifndef BRIDGE_CONFIG_H
#define BRIDGE_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Configuration structures                                           */
/* ------------------------------------------------------------------ */

#define CFG_STR_LEN   48
#define CFG_FILE_PATH "/lfs/config.json"
#define CFG_MOUNT_PT  "/lfs"

typedef struct {
	bool dhcp;                     /* true = DHCP, false = static IP  */
	char ip[CFG_STR_LEN];          /* Static IP address (if dhcp=false) */
	char netmask[CFG_STR_LEN];     /* Subnet mask                     */
	char gateway[CFG_STR_LEN];     /* Default gateway                 */
	char agent_ip[CFG_STR_LEN];    /* micro-ROS agent IP address      */
	char agent_port[8];            /* micro-ROS agent port (string)   */
} cfg_network_t;

typedef struct {
	char node_name[CFG_STR_LEN];   /* ROS2 node name                  */
	char namespace_[CFG_STR_LEN];  /* ROS2 namespace                  */
} cfg_ros_t;

typedef struct {
	cfg_network_t network;
	cfg_ros_t     ros;
} bridge_config_t;

/* ------------------------------------------------------------------ */
/*  Global configuration — read by main.c and other modules           */
/* ------------------------------------------------------------------ */

extern bridge_config_t g_config;

/* ------------------------------------------------------------------ */
/*  API                                                                */
/* ------------------------------------------------------------------ */

/**
 * Initialize LittleFS and load config.json.
 * If the file does not exist, writes defaults and saves them.
 * Call this first in main().
 */
int config_init(void);

/**
 * Load config.json from flash into the g_config struct.
 * Returns: 0 on success, negative on error.
 */
int config_load(void);

/**
 * Save the g_config struct as config.json to flash.
 * Returns: 0 on success, negative on error.
 */
int config_save(void);

/**
 * Reset g_config to factory defaults (does not save).
 */
void config_reset_defaults(void);

/**
 * Set a value by dotted key name.
 * Examples:
 *   config_set("network.agent_ip", "192.168.1.100")
 *   config_set("network.agent_port", "8888")
 *   config_set("ros.node_name", "my_robot")
 *
 * Returns: 0 on success, -ENOENT for unknown key.
 */
int config_set(const char *key, const char *value);

/**
 * Print the full configuration to the log / shell.
 */
void config_print(void);

#endif /* BRIDGE_CONFIG_H */
