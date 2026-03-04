#include "config.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>

#include <string.h>
#include <stdio.h>
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
/*  Globális konfig példány                                            */
/* ------------------------------------------------------------------ */

bridge_config_t g_config;

/* ------------------------------------------------------------------ */
/*  Segédmakró: biztonságos string másolás null terminálással          */
/* ------------------------------------------------------------------ */

#define SAFE_STRCPY(dst, src) \
	do { \
		strncpy((dst), (src), sizeof(dst) - 1); \
		(dst)[sizeof(dst) - 1] = '\0'; \
	} while (0)

/* ------------------------------------------------------------------ */
/*  Alapértékek                                                        */
/* ------------------------------------------------------------------ */

void config_reset_defaults(void)
{
	memset(&g_config, 0, sizeof(g_config));
	g_config.network.dhcp = false;
	SAFE_STRCPY(g_config.network.ip,         "192.168.68.114");
	SAFE_STRCPY(g_config.network.netmask,    "255.255.255.0");
	SAFE_STRCPY(g_config.network.gateway,    "192.168.68.1");
	SAFE_STRCPY(g_config.network.agent_ip,   "192.168.68.125");
	SAFE_STRCPY(g_config.network.agent_port, "8888");
	SAFE_STRCPY(g_config.ros.node_name,      "pico_bridge");
	SAFE_STRCPY(g_config.ros.namespace_,     "/");
}

/* ------------------------------------------------------------------ */
/*  Egyszerű JSON érték kinyerők                                       */
/* ------------------------------------------------------------------ */

/* Maximális keresőpuffer méret: idézőjelek + kulcs + kettőspont + null */
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

	/* Whitespace és kettőspont átugrása */
	while (*pos == ' ' || *pos == ':' || *pos == '\t' ||
	       *pos == '\r' || *pos == '\n') {
		pos++;
	}

	if (*pos != '"') {
		return -EINVAL;
	}
	pos++; /* nyitó idézőjel átugrása */

	size_t i = 0;
	while (*pos && *pos != '"' && i < out_len - 1) {
		out[i++] = *pos++;
	}
	out[i] = '\0';

	/* Ellenőrzés: záró idézőjel megvolt? */
	if (*pos != '"') {
		out[0] = '\0';
		return -EINVAL;
	}

	return 0;
}

/* ------------------------------------------------------------------ */
/*  JSON generátor                                                     */
/* ------------------------------------------------------------------ */

static void config_to_json(char *buf, size_t buf_len)
{
	snprintf(buf, buf_len,
		"{\n"
		"  \"network\": {\n"
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
/*  Betöltés flash-ből                                                 */
/* ------------------------------------------------------------------ */

int config_load(void)
{
	struct fs_file_t f;
	/* static: nem a stacken foglal helyet */
	static char buf[1024];
	ssize_t bytes_read;

	fs_file_t_init(&f);

	int rc = fs_open(&f, CFG_FILE_PATH, FS_O_READ);
	if (rc < 0) {
		LOG_WRN("config.json nem található, alapértékek betöltése");
		config_reset_defaults();
		return rc;
	}

	bytes_read = fs_read(&f, buf, sizeof(buf) - 1);
	fs_close(&f);

	if (bytes_read < 0) {
		LOG_ERR("Olvasási hiba: %d", (int)bytes_read);
		config_reset_defaults();
		return (int)bytes_read;
	}
	buf[bytes_read] = '\0';

	/* JSON mezők kinyerése — hibák logolva, alapérték megmarad */
	json_get_bool(buf, "dhcp",      &g_config.network.dhcp);
	json_get_str(buf,  "ip",        g_config.network.ip,         CFG_STR_LEN);
	json_get_str(buf,  "netmask",   g_config.network.netmask,    CFG_STR_LEN);
	json_get_str(buf,  "gateway",   g_config.network.gateway,    CFG_STR_LEN);
	json_get_str(buf,  "agent_ip",  g_config.network.agent_ip,   CFG_STR_LEN);
	json_get_str(buf,  "agent_port",g_config.network.agent_port, sizeof(g_config.network.agent_port));
	json_get_str(buf,  "node_name", g_config.ros.node_name,      CFG_STR_LEN);
	json_get_str(buf,  "namespace", g_config.ros.namespace_,     CFG_STR_LEN);

	LOG_INF("Konfig betöltve: %s", CFG_FILE_PATH);
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Mentés flash-be                                                    */
/* ------------------------------------------------------------------ */

int config_save(void)
{
	struct fs_file_t f;
	/* static: nem a stacken foglal helyet */
	static char buf[512];

	config_to_json(buf, sizeof(buf));

	fs_file_t_init(&f);
	int rc = fs_open(&f, CFG_FILE_PATH, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (rc < 0) {
		LOG_ERR("Fájl megnyitási hiba: %d", rc);
		return rc;
	}

	ssize_t written = fs_write(&f, buf, strlen(buf));
	fs_close(&f);

	if (written < 0) {
		LOG_ERR("Írási hiba: %d", (int)written);
		return (int)written;
	}

	LOG_INF("Konfig elmentve (%d bájt)", (int)written);
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Kulcs=érték beállítás (shell-ből hívva)                            */
/* ------------------------------------------------------------------ */

int config_set(const char *key, const char *value)
{
	if (!key || !value) {
		return -EINVAL;
	}

	if (strcmp(key, "network.dhcp") == 0) {
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
/*  Kiírás logba                                                       */
/* ------------------------------------------------------------------ */

void config_print(void)
{
	LOG_INF("--- Bridge konfiguráció ---");
	LOG_INF("[network]");
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
/*  Inicializálás (LittleFS mount + config betöltés)                   */
/* ------------------------------------------------------------------ */

int config_init(void)
{
	int rc;

	/* LittleFS mount */
	rc = fs_mount(&bridge_lfs_mount);
	if (rc < 0) {
		LOG_ERR("LittleFS mount hiba: %d", rc);
		LOG_WRN("Alapértékekkel folytatás");
		config_reset_defaults();
		return rc;
	}
	LOG_INF("LittleFS mountolva: %s", CFG_MOUNT_PT);

	/* Config betöltés */
	rc = config_load();
	if (rc < 0) {
		/* Első indulás: mentsük el az alapértékeket */
		LOG_INF("Első indulás — alapértékek mentése");
		config_save();
	}

	config_print();
	return 0;
}
