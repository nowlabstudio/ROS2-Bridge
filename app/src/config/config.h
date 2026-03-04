#ifndef BRIDGE_CONFIG_H
#define BRIDGE_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Konfigurációs struktúrák                                           */
/* ------------------------------------------------------------------ */

#define CFG_STR_LEN   48
#define CFG_FILE_PATH "/lfs/config.json"
#define CFG_MOUNT_PT  "/lfs"

typedef struct {
	bool dhcp;                     /* true = DHCP, false = statikus  */
	char ip[CFG_STR_LEN];          /* Statikus IP (ha dhcp=false)    */
	char netmask[CFG_STR_LEN];     /* Alhálózati maszk               */
	char gateway[CFG_STR_LEN];     /* Alapértelmezett átjáró         */
	char agent_ip[CFG_STR_LEN];    /* micro-ROS agent IP-je          */
	char agent_port[8];            /* micro-ROS agent port (string)  */
} cfg_network_t;

typedef struct {
	char node_name[CFG_STR_LEN];   /* ROS2 node neve                 */
	char namespace_[CFG_STR_LEN];  /* ROS2 namespace                 */
} cfg_ros_t;

typedef struct {
	cfg_network_t network;
	cfg_ros_t     ros;
} bridge_config_t;

/* ------------------------------------------------------------------ */
/*  Globális konfiguráció — main.c és más modulok olvassák             */
/* ------------------------------------------------------------------ */

extern bridge_config_t g_config;

/* ------------------------------------------------------------------ */
/*  API                                                                */
/* ------------------------------------------------------------------ */

/**
 * Inicializálja a LittleFS-t és betölti a config.json-t.
 * Ha a fájl nem létezik, alapértékeket ír be és elmenti.
 * Hívd a legelső dolognként main()-ben.
 */
int config_init(void);

/**
 * Betölti a config.json-t a flash-ből a g_config struktúrába.
 * Return: 0 ha OK, negatív hiba esetén.
 */
int config_load(void);

/**
 * Elmenti a g_config struktúrát config.json-ként a flash-be.
 * Return: 0 ha OK, negatív hiba esetén.
 */
int config_save(void);

/**
 * Visszaállítja a g_config-ot gyári alapértékekre (nem menti).
 */
void config_reset_defaults(void);

/**
 * Beállít egy értéket pontozott kulcs alapján.
 * Példák:
 *   config_set("network.agent_ip", "192.168.1.100")
 *   config_set("network.agent_port", "8888")
 *   config_set("ros.node_name", "my_robot")
 *
 * Return: 0 ha OK, -ENOENT ha ismeretlen kulcs.
 */
int config_set(const char *key, const char *value);

/**
 * Kiírja a teljes konfigurációt a logba / shellbe.
 */
void config_print(void);

#endif /* BRIDGE_CONFIG_H */
