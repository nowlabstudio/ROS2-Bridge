#include "config.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

LOG_MODULE_REGISTER(config, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/*  LittleFS mount                                                     */
/* ------------------------------------------------------------------ */

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(lfs_cfg);

static struct fs_mount_t bridge_lfs_mount = {
	.type        = FS_LITTLEFS,
	.fs_data     = &lfs_cfg,
	.storage_dev = (void *)FIXED_PARTITION_ID(storage_partition),
	.mnt_point   = CFG_MOUNT_PT,
};

/* ------------------------------------------------------------------ */
/*  Global config instance                                             */
/* ------------------------------------------------------------------ */

bridge_config_t g_config;

/* ------------------------------------------------------------------ */
/*  Helper macro: safe string copy with explicit null termination      */
/* ------------------------------------------------------------------ */

#define SAFE_STRCPY(dst, src) \
	do { \
		strncpy((dst), (src), sizeof(dst) - 1); \
		(dst)[sizeof(dst) - 1] = '\0'; \
	} while (0)

/* ------------------------------------------------------------------ */
/*  Factory defaults                                                   */
/* ------------------------------------------------------------------ */

void config_reset_defaults(void)
{
	memset(&g_config, 0, sizeof(g_config));
	g_config.network.dhcp = false;
	g_config.network.mac[0] = '\0';
	SAFE_STRCPY(g_config.network.ip,         "192.168.68.114");
	SAFE_STRCPY(g_config.network.netmask,    "255.255.255.0");
	SAFE_STRCPY(g_config.network.gateway,    "192.168.68.1");
	SAFE_STRCPY(g_config.network.agent_ip,   "192.168.68.125");
	SAFE_STRCPY(g_config.network.agent_port, "8888");
	SAFE_STRCPY(g_config.ros.node_name,      "pico_bridge");
	SAFE_STRCPY(g_config.ros.namespace_,     "/");
}

/* ------------------------------------------------------------------ */
/*  Simple JSON value extractors                                       */
/* ------------------------------------------------------------------ */

/* Search buffer size: quotes + key + colon + null terminator */
#define JSON_SEARCH_BUF (CFG_STR_LEN + 4)

static int json_get_bool(const char *json, const char *key, bool *out)
{
	char search[JSON_SEARCH_BUF];

	if (strlen(key) >= CFG_STR_LEN) {
		return -EINVAL;
	}
	snprintf(search, sizeof(search), "\"%s\"", key);

	const char *pos = strstr(json, search);
	if (!pos) {
		return -ENOENT;
	}
	pos += strlen(search);
	while (*pos == ' ' || *pos == ':' || *pos == '\t' ||
	       *pos == '\r' || *pos == '\n') {
		pos++;
	}
	if (strncmp(pos, "true", 4) == 0) {
		*out = true;
		return 0;
	} else if (strncmp(pos, "false", 5) == 0) {
		*out = false;
		return 0;
	}
	return -EINVAL;
}

static int json_get_str(const char *json, const char *key,
			char *out, size_t out_len)
{
	char search[JSON_SEARCH_BUF];

	if (strlen(key) >= CFG_STR_LEN || out_len == 0) {
		return -EINVAL;
	}
	snprintf(search, sizeof(search), "\"%s\"", key);

	const char *pos = strstr(json, search);
	if (!pos) {
		return -ENOENT;
	}

	pos += strlen(search);

	/* Skip whitespace and colon */
	while (*pos == ' ' || *pos == ':' || *pos == '\t' ||
	       *pos == '\r' || *pos == '\n') {
		pos++;
	}

	if (*pos != '"') {
		return -EINVAL;
	}
	pos++; /* skip opening quote */

	size_t i = 0;
	while (*pos && *pos != '"' && i < out_len - 1) {
		out[i++] = *pos++;
	}
	out[i] = '\0';

	/* Verify: closing quote found? */
	if (*pos != '"') {
		out[0] = '\0';
		return -EINVAL;
	}

	return 0;
}

static int json_get_int(const char *json, const char *key, int32_t *out)
{
	char search[JSON_SEARCH_BUF];

	if (strlen(key) >= CFG_STR_LEN) {
		return -EINVAL;
	}
	snprintf(search, sizeof(search), "\"%s\"", key);

	const char *pos = strstr(json, search);
	if (!pos) {
		return -ENOENT;
	}
	pos += strlen(search);
	while (*pos == ' ' || *pos == ':' || *pos == '\t' ||
	       *pos == '\r' || *pos == '\n') {
		pos++;
	}
	if (*pos == '-' || (*pos >= '0' && *pos <= '9')) {
		*out = (int32_t)strtol(pos, NULL, 10);
		return 0;
	}
	return -EINVAL;
}

/* ------------------------------------------------------------------ */
/*  JSON generator                                                     */
/* ------------------------------------------------------------------ */

static void config_to_json(char *buf, size_t buf_len)
{
	snprintf(buf, buf_len,
		"{\n"
		"  \"network\": {\n"
		"    \"mac\": \"%s\",\n"
		"    \"dhcp\": %s,\n"
		"    \"ip\": \"%s\",\n"
		"    \"netmask\": \"%s\",\n"
		"    \"gateway\": \"%s\",\n"
		"    \"agent_ip\": \"%s\",\n"
		"    \"agent_port\": \"%s\"\n"
		"  },\n"
		"  \"ros\": {\n"
		"    \"node_name\": \"%s\",\n"
		"    \"namespace\": \"%s\"\n"
		"  }\n"
		"}\n",
		g_config.network.mac,
		g_config.network.dhcp ? "true" : "false",
		g_config.network.ip,
		g_config.network.netmask,
		g_config.network.gateway,
		g_config.network.agent_ip,
		g_config.network.agent_port,
		g_config.ros.node_name,
		g_config.ros.namespace_);
}

/* ------------------------------------------------------------------ */
/*  Load from flash                                                    */
/* ------------------------------------------------------------------ */

int config_load(void)
{
	struct fs_file_t f;
	/* static: allocated in BSS, not on stack */
	static char buf[1024];
	ssize_t bytes_read;

	fs_file_t_init(&f);

	int rc = fs_open(&f, CFG_FILE_PATH, FS_O_READ);
	if (rc < 0) {
		LOG_WRN("config.json not found, loading defaults");
		config_reset_defaults();
		return rc;
	}

	bytes_read = fs_read(&f, buf, sizeof(buf) - 1);
	fs_close(&f);

	if (bytes_read < 0) {
		LOG_ERR("Read error: %d", (int)bytes_read);
		config_reset_defaults();
		return (int)bytes_read;
	}
	buf[bytes_read] = '\0';

	/* Extract JSON fields — errors logged, defaults preserved */
	json_get_str(buf,  "mac",       g_config.network.mac,        sizeof(g_config.network.mac));
	json_get_bool(buf, "dhcp",      &g_config.network.dhcp);
	json_get_str(buf,  "ip",        g_config.network.ip,         CFG_STR_LEN);
	json_get_str(buf,  "netmask",   g_config.network.netmask,    CFG_STR_LEN);
	json_get_str(buf,  "gateway",   g_config.network.gateway,    CFG_STR_LEN);
	json_get_str(buf,  "agent_ip",  g_config.network.agent_ip,   CFG_STR_LEN);
	json_get_str(buf,  "agent_port",g_config.network.agent_port, sizeof(g_config.network.agent_port));
	json_get_str(buf,  "node_name", g_config.ros.node_name,      CFG_STR_LEN);
	json_get_str(buf,  "namespace", g_config.ros.namespace_,     CFG_STR_LEN);

	LOG_INF("Config loaded: %s", CFG_FILE_PATH);
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Save to flash                                                      */
/* ------------------------------------------------------------------ */

int config_save(void)
{
	struct fs_file_t f;
	/* static: allocated in BSS, not on stack */
	static char buf[512];

	config_to_json(buf, sizeof(buf));

	fs_file_t_init(&f);
	int rc = fs_open(&f, CFG_FILE_PATH, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (rc < 0) {
		LOG_ERR("File open error: %d", rc);
		return rc;
	}

	ssize_t written = fs_write(&f, buf, strlen(buf));
	fs_close(&f);

	if (written < 0) {
		LOG_ERR("Write error: %d", (int)written);
		return (int)written;
	}

	LOG_INF("Config saved (%d bytes)", (int)written);
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Key=value setter (called from shell)                               */
/* ------------------------------------------------------------------ */

int config_set(const char *key, const char *value)
{
	if (!key || !value) {
		return -EINVAL;
	}

	if (strlen(value) >= CFG_STR_LEN) {
		return -ENAMETOOLONG;
	}

	if (strcmp(key, "network.mac") == 0) {
		SAFE_STRCPY(g_config.network.mac, value);
	} else if (strcmp(key, "network.dhcp") == 0) {
		g_config.network.dhcp = (strcmp(value, "true") == 0 ||
					 strcmp(value, "1") == 0);
	} else if (strcmp(key, "network.ip") == 0) {
		SAFE_STRCPY(g_config.network.ip, value);
	} else if (strcmp(key, "network.netmask") == 0) {
		SAFE_STRCPY(g_config.network.netmask, value);
	} else if (strcmp(key, "network.gateway") == 0) {
		SAFE_STRCPY(g_config.network.gateway, value);
	} else if (strcmp(key, "network.agent_ip") == 0) {
		SAFE_STRCPY(g_config.network.agent_ip, value);
	} else if (strcmp(key, "network.agent_port") == 0) {
		SAFE_STRCPY(g_config.network.agent_port, value);
	} else if (strcmp(key, "ros.node_name") == 0) {
		SAFE_STRCPY(g_config.ros.node_name, value);
	} else if (strcmp(key, "ros.namespace") == 0) {
		SAFE_STRCPY(g_config.ros.namespace_, value);
	} else {
		return -ENOENT;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Print to log                                                       */
/* ------------------------------------------------------------------ */

void config_print(void)
{
	LOG_INF("--- Bridge configuration ---");
	LOG_INF("[network]");
	LOG_INF("  mac:        %s", g_config.network.mac[0] ? g_config.network.mac : "(auto)");
	LOG_INF("  dhcp:       %s", g_config.network.dhcp ? "true" : "false");
	LOG_INF("  ip:         %s", g_config.network.ip);
	LOG_INF("  netmask:    %s", g_config.network.netmask);
	LOG_INF("  gateway:    %s", g_config.network.gateway);
	LOG_INF("  agent_ip:   %s", g_config.network.agent_ip);
	LOG_INF("  agent_port: %s", g_config.network.agent_port);
	LOG_INF("[ros]");
	LOG_INF("  node_name:  %s", g_config.ros.node_name);
	LOG_INF("  namespace:  %s", g_config.ros.namespace_);
}

/* ------------------------------------------------------------------ */
/*  Initialization (LittleFS mount + config load)                     */
/* ------------------------------------------------------------------ */

int config_init(void)
{
	int rc;

	/* Mount LittleFS */
	rc = fs_mount(&bridge_lfs_mount);
	if (rc < 0) {
		LOG_ERR("LittleFS mount error: %d", rc);
		LOG_WRN("Continuing with defaults");
		config_reset_defaults();
		return rc;
	}
	LOG_INF("LittleFS mounted: %s", CFG_MOUNT_PT);

	/* Load config */
	rc = config_load();
	if (rc < 0) {
		/* First boot: save defaults to flash */
		LOG_INF("First boot — saving defaults");
		config_save();
	}

	config_print();
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Channel parameter persistence — /lfs/ch_params.json               */
/*                                                                     */
/*  Format: flat JSON with dotted keys, e.g.:                         */
/*    { "ch.limit_sw_1.period_ms": 100,                               */
/*      "ch.limit_sw_1.enabled": true,                                */
/*      "ch.limit_sw_1.invert": false }                               */
/* ------------------------------------------------------------------ */

#define CH_PARAMS_FILE  "/lfs/ch_params.json"
#define CH_PARAMS_BUF   2048

/* Module-level cache — one file read shared across all channel lookups */
static char ch_params_buf[CH_PARAMS_BUF];
static bool ch_params_loaded;

int channel_params_save(const channel_param_entry_t *entries, int count)
{
	if (!entries || count <= 0) {
		return -EINVAL;
	}

	static char wbuf[CH_PARAMS_BUF];
	int pos = 0;

	pos += snprintf(wbuf + pos, sizeof(wbuf) - pos, "{\n");

	for (int i = 0; i < count; i++) {
		const channel_param_entry_t *e = &entries[i];
		const char *comma = (i < count - 1) ? "," : "";

		pos += snprintf(wbuf + pos, sizeof(wbuf) - pos,
			"  \"ch.%s.period_ms\": %u,\n"
			"  \"ch.%s.enabled\": %s,\n"
			"  \"ch.%s.invert\": %s%s\n",
			e->name, e->period_ms,
			e->name, e->enabled ? "true" : "false",
			e->name, e->invert  ? "true" : "false",
			comma);

		if (pos >= (int)sizeof(wbuf) - 1) {
			LOG_ERR("ch_params buffer overflow");
			return -ENOMEM;
		}
	}

	pos += snprintf(wbuf + pos, sizeof(wbuf) - pos, "}\n");

	struct fs_file_t f;
	fs_file_t_init(&f);

	int rc = fs_open(&f, CH_PARAMS_FILE,
			 FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (rc < 0) {
		LOG_ERR("ch_params open error: %d", rc);
		return rc;
	}

	ssize_t written = fs_write(&f, wbuf, strlen(wbuf));
	fs_close(&f);

	if (written < 0) {
		LOG_ERR("ch_params write error: %d", (int)written);
		return (int)written;
	}

	/* Invalidate cache so next load re-reads from flash */
	ch_params_loaded = false;

	LOG_INF("ch_params saved: %d channels, %d bytes", count, (int)written);
	return 0;
}

int channel_params_load(const char *ch_name,
			uint32_t *period_ms, bool *enabled, bool *invert)
{
	if (!ch_name) {
		return -EINVAL;
	}

	/* Load file once per session into module-level cache */
	if (!ch_params_loaded) {
		struct fs_file_t f;
		fs_file_t_init(&f);

		int rc = fs_open(&f, CH_PARAMS_FILE, FS_O_READ);
		if (rc < 0) {
			LOG_WRN("ch_params not found, using channel defaults");
			return rc;
		}

		ssize_t bytes = fs_read(&f, ch_params_buf,
					sizeof(ch_params_buf) - 1);
		fs_close(&f);

		if (bytes < 0) {
			LOG_ERR("ch_params read error: %d", (int)bytes);
			return (int)bytes;
		}
		ch_params_buf[bytes] = '\0';
		ch_params_loaded = true;
		LOG_INF("ch_params loaded (%d bytes)", (int)bytes);
	}

	/* Extract per-channel values — missing keys leave caller's value unchanged */
	char key[CFG_STR_LEN + 20];
	int32_t ival = 0;
	bool bval    = false;

	if (period_ms) {
		snprintf(key, sizeof(key), "ch.%s.period_ms", ch_name);
		if (json_get_int(ch_params_buf, key, &ival) == 0 && ival > 0) {
			*period_ms = (uint32_t)ival;
		}
	}
	if (enabled) {
		snprintf(key, sizeof(key), "ch.%s.enabled", ch_name);
		if (json_get_bool(ch_params_buf, key, &bval) == 0) {
			*enabled = bval;
		}
	}
	if (invert) {
		snprintf(key, sizeof(key), "ch.%s.invert", ch_name);
		if (json_get_bool(ch_params_buf, key, &bval) == 0) {
			*invert = bval;
		}
	}

	return 0;
}
