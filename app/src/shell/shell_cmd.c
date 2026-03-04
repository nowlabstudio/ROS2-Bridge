#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#include "config/config.h"

LOG_MODULE_REGISTER(shell_cmd, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/*  bridge config show                                                 */
/* ------------------------------------------------------------------ */

static int cmd_config_show(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "--- Bridge konfiguráció ---");
	shell_print(sh, "[network]");
	shell_print(sh, "  dhcp:       %s", g_config.network.dhcp ? "true" : "false");
	shell_print(sh, "  ip:         %s", g_config.network.ip);
	shell_print(sh, "  netmask:    %s", g_config.network.netmask);
	shell_print(sh, "  gateway:    %s", g_config.network.gateway);
	shell_print(sh, "  agent_ip:   %s", g_config.network.agent_ip);
	shell_print(sh, "  agent_port: %s", g_config.network.agent_port);
	shell_print(sh, "[ros]");
	shell_print(sh, "  node_name:  %s", g_config.ros.node_name);
	shell_print(sh, "  namespace:  %s", g_config.ros.namespace_);
	shell_print(sh, "");
	shell_print(sh, "Menteshez: bridge config save");
	shell_print(sh, "Aktiváláshoz: reboot");
	return 0;
}

/* ------------------------------------------------------------------ */
/*  bridge config set <kulcs> <érték>                                  */
/* ------------------------------------------------------------------ */

static int cmd_config_set(const struct shell *sh, size_t argc, char **argv)
{
	if (argc != 3) {
		shell_error(sh, "Használat: bridge config set <kulcs> <érték>");
		shell_error(sh, "Példák:");
		shell_error(sh, "  bridge config set network.agent_ip 192.168.1.100");
		shell_error(sh, "  bridge config set network.agent_port 8888");
		shell_error(sh, "  bridge config set ros.node_name my_robot");
		return -EINVAL;
	}

	int rc = config_set(argv[1], argv[2]);
	if (rc == -ENOENT) {
		shell_error(sh, "Ismeretlen kulcs: %s", argv[1]);
		shell_error(sh, "Érvényes kulcsok:");
		shell_error(sh, "  network.dhcp (true/false)");
		shell_error(sh, "  network.ip, network.netmask, network.gateway");
		shell_error(sh, "  network.agent_ip, network.agent_port");
		shell_error(sh, "  ros.node_name, ros.namespace");
		return rc;
	}

	shell_print(sh, "OK: %s = %s", argv[1], argv[2]);
	shell_print(sh, "Menteshez: bridge config save");
	return 0;
}

/* ------------------------------------------------------------------ */
/*  bridge config save                                                 */
/* ------------------------------------------------------------------ */

static int cmd_config_save(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int rc = config_save();
	if (rc < 0) {
		shell_error(sh, "Mentési hiba: %d", rc);
		return rc;
	}
	shell_print(sh, "Konfig elmentve. Aktiváláshoz: reboot");
	return 0;
}

/* ------------------------------------------------------------------ */
/*  bridge config load                                                 */
/* ------------------------------------------------------------------ */

static int cmd_config_load(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int rc = config_load();
	if (rc < 0) {
		shell_error(sh, "Betöltési hiba: %d", rc);
		return rc;
	}
	shell_print(sh, "Konfig újratöltve.");
	cmd_config_show(sh, 0, NULL);
	return 0;
}

/* ------------------------------------------------------------------ */
/*  bridge config reset                                                */
/* ------------------------------------------------------------------ */

static int cmd_config_reset(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	config_reset_defaults();
	shell_print(sh, "Alapértékek visszaállítva (még nincs mentve).");
	shell_print(sh, "Menteshez: bridge config save");
	cmd_config_show(sh, 0, NULL);
	return 0;
}

/* ------------------------------------------------------------------ */
/*  bridge reboot                                                      */
/* ------------------------------------------------------------------ */

static int cmd_reboot(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "Újraindítás...");
	k_sleep(K_MSEC(500));
	sys_reboot(SYS_REBOOT_COLD);
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Shell parancs fa regisztrálása                                     */
/* ------------------------------------------------------------------ */

SHELL_STATIC_SUBCMD_SET_CREATE(sub_config,
	SHELL_CMD(show,  NULL, "Konfig megjelenítése",        cmd_config_show),
	SHELL_CMD(set,   NULL, "Érték beállítása: set <kulcs> <érték>", cmd_config_set),
	SHELL_CMD(save,  NULL, "Mentés flash-be",             cmd_config_save),
	SHELL_CMD(load,  NULL, "Betöltés flash-ből",          cmd_config_load),
	SHELL_CMD(reset, NULL, "Gyári alapértékek visszaállítása", cmd_config_reset),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_bridge,
	SHELL_CMD(config, &sub_config, "Konfiguráció kezelés", NULL),
	SHELL_CMD(reboot, NULL,        "Újraindítás",          cmd_reboot),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(bridge, &sub_bridge, "Bridge parancsok", NULL);
