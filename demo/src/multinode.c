/*
 * Copyright (c) 2026 Antmicro <www.antmicro.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include "multinode.h"

#include <zephyr/shell/shell.h>

#ifdef CONFIG_DEMO_MULTINODE_ETH
SHELL_STATIC_SUBCMD_SET_CREATE(sub_multinode_eth_server_cmds,
			       SHELL_CMD_ARG(start, nullptr, "Start multinode server",
					     multinode_eth_server_start, 1, 0),
			       SHELL_CMD_ARG(stop, nullptr, "Stop multinode server",
					     multinode_eth_server_stop, 1, 0),
			       SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_multinode_eth_cmds,
			       SHELL_CMD_ARG(server, &sub_multinode_eth_server_cmds, "Server mode",
					     nullptr, 2, 0),
			       SHELL_CMD_ARG(client, nullptr, "Client mode", multinode_eth_client,
					     1, 0),
			       SHELL_SUBCMD_SET_END);
#endif // CONFIG_DEMO_MULTINODE_ETH

SHELL_STATIC_SUBCMD_SET_CREATE(sub_multinode_cmds,
#ifdef CONFIG_DEMO_MULTINODE_ETH
			       SHELL_CMD_ARG(eth, &sub_multinode_eth_cmds,
					     "Multinode demo over ethernet", nullptr, 1, 0),
#endif // CONFIG_DEMO_MULTINODE_ETH
			       SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(multinode, &sub_multinode_cmds, "Multinode demo commands", NULL);
