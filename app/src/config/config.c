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

	for (int i = 0; i < RC_CH_COUNT; i++) {
		g_config.rc_trim.ch[i].min    = 1000;
		g_config.rc_trim.ch[i].center = 1500;
		g_config.rc_trim.ch[i].max    = 2000;
	}
	g_config.rc_trim.deadzone = 20;
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
	int pos = 0;

	pos += snprintf(buf + pos, buf_len - pos,
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
		"  }",
		g_config.network.mac,
		g_config.network.dhcp ? "true" : "false",
		g_config.network.ip,
		g_config.network.netmask,
		g_config.network.gateway,
		g_config.network.agent_ip,
		g_config.network.agent_port,
		g_config.ros.node_name,
		g_config.ros.namespace_);

	if (g_config.channel_count > 0) {
		pos += snprintf(buf + pos, buf_len - pos, ",\n  \"channels\": {");
		for (int i = 0; i < g_config.channel_count; i++) {
			const cfg_channel_entry_t *ce = &g_config.channels[i];

			if (ce->topic[0]) {
				pos += snprintf(buf + pos, buf_len - pos,
					"%s\n    \"%s\": { \"enabled\": %s, \"topic\": \"%s\" }",
					i > 0 ? "," : "",
					ce->name,
					ce->enabled ? "true" : "false",
					ce->topic);
			} else {
				pos += snprintf(buf + pos, buf_len - pos,
					"%s\n    \"%s\": %s",
					i > 0 ? "," : "",
					ce->name,
					ce->enabled ? "true" : "false");
			}
		}
		pos += snprintf(buf + pos, buf_len - pos, "\n  }");
	}

	pos += snprintf(buf + pos, buf_len - pos,
		",\n  \"rc_trim\": {");
	for (int i = 0; i < RC_CH_COUNT; i++) {
		const cfg_rc_trim_ch_t *t = &g_config.rc_trim.ch[i];

		pos += snprintf(buf + pos, buf_len - pos,
			"%s\n    \"ch%d_min\": %u, \"ch%d_center\": %u, \"ch%d_max\": %u",
			i > 0 ? "," : "",
			i + 1, t->min, i + 1, t->center, i + 1, t->max);
	}
	pos += snprintf(buf + pos, buf_len - pos,
		",\n    \"deadzone\": %u\n  }",
		g_config.rc_trim.deadzone);

	snprintf(buf + pos, buf_len - pos, "\n}\n");
}

/* ------------------------------------------------------------------ */
/*  Parse "channels": { ... } block                                    */
/*                                                                     */
/*  Supports two value formats per entry:                              */
/*    "name": true/false            — simple bool                      */
/*    "name": { "enabled": ..., "topic": "..." }  — extended           */
/* ------------------------------------------------------------------ */

static void parse_channel_object(const char *start, const char *obj_end,
				 cfg_channel_entry_t *e)
{
	e->enabled = true;
	e->topic[0] = '\0';

	bool val;

	if (json_get_bool(start, "enabled", &val) == 0) {
		e->enabled = val;
	}

	char topic_buf[CFG_CH_NAME_LEN];

	if (json_get_str(start, "topic", topic_buf, sizeof(topic_buf)) == 0) {
		SAFE_STRCPY(e->topic, topic_buf);
	}
}

static void parse_channels(const char *json)
{
	g_config.channel_count = 0;

	const char *sec = strstr(json, "\"channels\"");
	if (!sec) {
		return;
	}

	const char *brace = strchr(sec, '{');
	if (!brace) {
		return;
	}
	brace++;

	/* Find matching closing brace (skip nested {}) */
	int depth = 1;
	const char *end = brace;

	while (*end && depth > 0) {
		if (*end == '{') {
			depth++;
		} else if (*end == '}') {
			depth--;
		}
		if (depth > 0) {
			end++;
		}
	}
	if (depth != 0) {
		return;
	}

	const char *pos = brace;

	while (pos < end && g_config.channel_count < CFG_MAX_CHANNELS) {
		const char *q1 = memchr(pos, '"', end - pos);
		if (!q1) {
			break;
		}
		q1++;

		const char *q2 = memchr(q1, '"', end - q1);
		if (!q2) {
			break;
		}

		size_t len = q2 - q1;
		if (len == 0 || len >= CFG_CH_NAME_LEN) {
			pos = q2 + 1;
			continue;
		}

		cfg_channel_entry_t *e =
			&g_config.channels[g_config.channel_count];
		memcpy(e->name, q1, len);
		e->name[len] = '\0';
		e->topic[0] = '\0';

		pos = q2 + 1;
		while (pos < end && (*pos == ' ' || *pos == ':' ||
		       *pos == '\t' || *pos == '\n' || *pos == '\r')) {
			pos++;
		}

		if (*pos == '{') {
			/* Extended format: { "enabled": ..., "topic": "..." } */
			const char *obj_start = pos;
			const char *obj_end = strchr(pos + 1, '}');

			if (!obj_end || obj_end > end) {
				break;
			}
			parse_channel_object(obj_start, obj_end, e);
			g_config.channel_count++;
			pos = obj_end + 1;
		} else if (pos + 4 <= end && strncmp(pos, "true", 4) == 0) {
			e->enabled = true;
			g_config.channel_count++;
			pos += 4;
		} else if (pos + 5 <= end && strncmp(pos, "false", 5) == 0) {
			e->enabled = false;
			g_config.channel_count++;
			pos += 5;
		}
	}
}

/* ------------------------------------------------------------------ */
/*  Parse "rc_trim": { ... } block                                     */
/* ------------------------------------------------------------------ */

static void parse_rc_trim(const char *json)
{
	const char *sec = strstr(json, "\"rc_trim\"");
	if (!sec) {
		return;
	}

	const char *brace = strchr(sec, '{');
	if (!brace) {
		return;
	}

	const char *obj_end = strchr(brace + 1, '}');
	if (!obj_end) {
		return;
	}

	/* Temporarily null-terminate the section for scoped parsing */
	size_t slen = obj_end - brace + 1;
	char scope_buf[512];

	if (slen >= sizeof(scope_buf)) {
		return;
	}
	memcpy(scope_buf, brace, slen);
	scope_buf[slen] = '\0';

	int32_t ival;

	for (int i = 0; i < RC_CH_COUNT; i++) {
		char key[16];

		snprintf(key, sizeof(key), "ch%d_min", i + 1);
		if (json_get_int(scope_buf, key, &ival) == 0) {
			g_config.rc_trim.ch[i].min = (uint16_t)ival;
		}
		snprintf(key, sizeof(key), "ch%d_center", i + 1);
		if (json_get_int(scope_buf, key, &ival) == 0) {
			g_config.rc_trim.ch[i].center = (uint16_t)ival;
		}
		snprintf(key, sizeof(key), "ch%d_max", i + 1);
		if (json_get_int(scope_buf, key, &ival) == 0) {
			g_config.rc_trim.ch[i].max = (uint16_t)ival;
		}
	}

	if (json_get_int(scope_buf, "deadzone", &ival) == 0) {
		g_config.rc_trim.deadzone = (uint16_t)ival;
	}
}

/* ------------------------------------------------------------------ */
/*  Load from flash                                                    */
/* ------------------------------------------------------------------ */

int config_load(void)
{
	struct fs_file_t f;
	/* static: allocated in BSS, not on stack */
	static char buf[2048];
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

	parse_channels(buf);
	parse_rc_trim(buf);

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
	static char buf[2048];

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
	} else if (strncmp(key, "channels.", 9) == 0) {
		const char *rest = key + 9;
		const char *dot = strchr(rest, '.');

		if (dot) {
			/* channels.<name>.topic */
			size_t nlen = dot - rest;
			char ch_name[CFG_CH_NAME_LEN];

			if (nlen >= sizeof(ch_name)) {
				return -EINVAL;
			}
			memcpy(ch_name, rest, nlen);
			ch_name[nlen] = '\0';

			if (strcmp(dot + 1, "topic") == 0) {
				for (int i = 0; i < g_config.channel_count; i++) {
					if (strcmp(g_config.channels[i].name, ch_name) == 0) {
						SAFE_STRCPY(g_config.channels[i].topic, value);
						return 0;
					}
				}
				if (g_config.channel_count >= CFG_MAX_CHANNELS) {
					return -ENOMEM;
				}
				cfg_channel_entry_t *e =
					&g_config.channels[g_config.channel_count++];
				SAFE_STRCPY(e->name, ch_name);
				e->enabled = true;
				SAFE_STRCPY(e->topic, value);
			} else {
				return -ENOENT;
			}
		} else {
			/* channels.<name> = true/false */
			bool en = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);

			for (int i = 0; i < g_config.channel_count; i++) {
				if (strcmp(g_config.channels[i].name, rest) == 0) {
					g_config.channels[i].enabled = en;
					return 0;
				}
			}
			if (g_config.channel_count >= CFG_MAX_CHANNELS) {
				return -ENOMEM;
			}
			cfg_channel_entry_t *e =
				&g_config.channels[g_config.channel_count++];
			SAFE_STRCPY(e->name, rest);
			e->enabled = en;
			e->topic[0] = '\0';
		}
	} else if (strncmp(key, "rc_trim.", 8) == 0) {
		const char *field = key + 8;
		int32_t ival = (int32_t)strtol(value, NULL, 10);

		if (strcmp(field, "deadzone") == 0) {
			g_config.rc_trim.deadzone = (uint16_t)ival;
		} else if (strlen(field) >= 6 && field[0] == 'c' && field[1] == 'h' &&
			   field[2] >= '1' && field[2] <= '6' && field[3] == '_') {
			int ch_idx = field[2] - '1';
			const char *sub = field + 4;

			if (strcmp(sub, "min") == 0) {
				g_config.rc_trim.ch[ch_idx].min = (uint16_t)ival;
			} else if (strcmp(sub, "center") == 0) {
				g_config.rc_trim.ch[ch_idx].center = (uint16_t)ival;
			} else if (strcmp(sub, "max") == 0) {
				g_config.rc_trim.ch[ch_idx].max = (uint16_t)ival;
			} else {
				return -ENOENT;
			}
		} else {
			return -ENOENT;
		}
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
	if (g_config.channel_count > 0) {
		LOG_INF("[channels]");
		for (int i = 0; i < g_config.channel_count; i++) {
			const cfg_channel_entry_t *ce = &g_config.channels[i];

			if (ce->topic[0]) {
				LOG_INF("  %s: %s (topic: %s)", ce->name,
					ce->enabled ? "true" : "false",
					ce->topic);
			} else {
				LOG_INF("  %s: %s", ce->name,
					ce->enabled ? "true" : "false");
			}
		}
	}
	LOG_INF("[rc_trim]");
	for (int i = 0; i < RC_CH_COUNT; i++) {
		const cfg_rc_trim_ch_t *t = &g_config.rc_trim.ch[i];

		LOG_INF("  ch%d: %u/%u/%u", i + 1, t->min, t->center, t->max);
	}
	LOG_INF("  deadzone: %u", g_config.rc_trim.deadzone);
}

/* ------------------------------------------------------------------ */
/*  Channel enabled lookup                                             */
/* ------------------------------------------------------------------ */

bool config_channel_enabled(const char *name)
{
	if (!name) {
		return false;
	}
	for (int i = 0; i < g_config.channel_count; i++) {
		if (strcmp(g_config.channels[i].name, name) == 0) {
			return g_config.channels[i].enabled;
		}
	}
	return true;
}

const char *config_channel_topic(const char *name)
{
	if (!name) {
		return NULL;
	}
	for (int i = 0; i < g_config.channel_count; i++) {
		if (strcmp(g_config.channels[i].name, name) == 0 &&
		    g_config.channels[i].topic[0]) {
			return g_config.channels[i].topic;
		}
	}
	return NULL;
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
