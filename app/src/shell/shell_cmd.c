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

	shell_print(sh, "--- Bridge configuration ---");
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
	shell_print(sh, "To save: bridge config save");
	shell_print(sh, "To activate: bridge reboot");
	return 0;
}

/* ------------------------------------------------------------------ */
/*  bridge config set <key> <value>                                    */
/* ------------------------------------------------------------------ */

static int cmd_config_set(const struct shell *sh, size_t argc, char **argv)
{
	if (argc != 3) {
		shell_error(sh, "Usage: bridge config set <key> <value>");
		shell_error(sh, "Examples:");
		shell_error(sh, "  bridge config set network.agent_ip 192.168.1.100");
		shell_error(sh, "  bridge config set network.agent_port 8888");
		shell_error(sh, "  bridge config set ros.node_name my_robot");
		return -EINVAL;
	}

	int rc = config_set(argv[1], argv[2]);
	if (rc == -ENOENT) {
		shell_error(sh, "Unknown key: %s", argv[1]);
		shell_error(sh, "Valid keys:");
		shell_error(sh, "  network.dhcp (true/false)");
		shell_error(sh, "  network.ip, network.netmask, network.gateway");
		shell_error(sh, "  network.agent_ip, network.agent_port");
		shell_error(sh, "  ros.node_name, ros.namespace");
		return rc;
	}

	shell_print(sh, "OK: %s = %s", argv[1], argv[2]);
	shell_print(sh, "To save: bridge config save");
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
		shell_error(sh, "Save error: %d", rc);
		return rc;
	}
	shell_print(sh, "Config saved. To activate: bridge reboot");
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
		shell_error(sh, "Load error: %d", rc);
		return rc;
	}
	shell_print(sh, "Config reloaded.");
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
	shell_print(sh, "Factory defaults restored (not yet saved).");
	shell_print(sh, "To save: bridge config save");
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

	shell_print(sh, "Rebooting...");
	k_sleep(K_MSEC(500));
	sys_reboot(SYS_REBOOT_COLD);
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Shell command tree registration                                    */
/* ------------------------------------------------------------------ */

SHELL_STATIC_SUBCMD_SET_CREATE(sub_config,
	SHELL_CMD(show,  NULL, "Show configuration",              cmd_config_show),
	SHELL_CMD(set,   NULL, "Set value: set <key> <value>",    cmd_config_set),
	SHELL_CMD(save,  NULL, "Save to flash",                   cmd_config_save),
	SHELL_CMD(load,  NULL, "Load from flash",                 cmd_config_load),
	SHELL_CMD(reset, NULL, "Restore factory defaults",        cmd_config_reset),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_bridge,
	SHELL_CMD(config, &sub_config, "Configuration management", NULL),
	SHELL_CMD(reboot, NULL,        "Reboot device",            cmd_reboot),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(bridge, &sub_bridge, "Bridge commands", NULL);
