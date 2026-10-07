/*
 * Copyright (c) 2026 Antmicro <www.antmicro.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _MULTINODE_H
#define _MULTINODE_H

#include <zephyr/shell/shell.h>

#ifdef CONFIG_DEMO_MULTINODE_ETH
int multinode_eth_server_start(const struct shell *sh, size_t argc, char *argv[]);
int multinode_eth_server_stop(const struct shell *sh, size_t argc, char *argv[]);
int multinode_eth_client(const struct shell *sh, size_t argc, char *argv[]);
#endif // CONFIG_DEMO_MULTINODE_ETH

#ifdef CONFIG_DEMO_MULTINODE_CAN
int multinode_can_listen_start(const struct shell *sh, size_t argc, char *argv[]);
int multinode_can_listen_stop(const struct shell *sh, size_t argc, char *argv[]);
int multinode_can_send(const struct shell *sh, size_t argc, char *argv[]);
#endif // CONFIG_DEMO_MULTINODE_CAN

#endif // _MULTINODE_H
