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

/* ------------------------------------------------------------------ */
/*  Channel parameter persistence — /lfs/ch_params.json               */
/* ------------------------------------------------------------------ */

/**
 * One entry per channel — passed to channel_params_save().
 */
typedef struct {
	const char *name;      /* channel name (e.g. "limit_sw_1")      */
	uint32_t    period_ms; /* active publish period                  */
	bool        enabled;   /* channel enabled flag                   */
	bool        invert;    /* invert_logic flag                      */
} channel_param_entry_t;

/**
 * Save channel parameters to /lfs/ch_params.json.
 * Invalidates the read cache so next load re-reads from flash.
 *
 * @param entries  Array of channel_param_entry_t
 * @param count    Number of entries
 * @return 0 on success, negative errno on error
 */
int channel_params_save(const channel_param_entry_t *entries, int count);

/**
 * Load one channel's parameters from /lfs/ch_params.json.
 * The file is read once and cached; subsequent calls use the cache.
 * Missing keys in the file leave the caller's output values unchanged,
 * so initialize them to the channel's default before calling.
 *
 * @param ch_name   Channel name to look up
 * @param period_ms Output: publish period in ms (unchanged if not found)
 * @param enabled   Output: enabled flag (unchanged if not found)
 * @param invert    Output: invert_logic flag (unchanged if not found)
 * @return 0 on success, -ENOENT if file not found, negative on error
 */
int channel_params_load(const char *ch_name,
			uint32_t *period_ms, bool *enabled, bool *invert);

#endif /* BRIDGE_CONFIG_H */
