/*
 * Copyright (c) 2026 Antmicro <www.antmicro.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include "multinode.h"

#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/devicetree/can.h>
#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/util_macro.h>
#include <zephyr/toolchain.h>

LOG_MODULE_REGISTER(multinode_can);

BUILD_ASSERT(CONFIG_DEMO_MULTINODE_CAN_REQ_MSG_ID <= CAN_STD_ID_MASK, "CAN filter ID out of range");
BUILD_ASSERT(CONFIG_DEMO_MULTINODE_CAN_RESP_MSG_ID <= CAN_STD_ID_MASK,
	     "CAN filter ID out of range");

#define CAN_DEVICE DT_ALIAS(multinode_can)

const static struct device *dev = DEVICE_DT_GET(CAN_DEVICE);

const static uint32_t max_bitrate = DT_CAN_TRANSCEIVER_MAX_BITRATE(CAN_DEVICE, 1000000);

const static struct can_filter req_filter = {
	.id = CONFIG_DEMO_MULTINODE_CAN_REQ_MSG_ID,
	.mask = CAN_STD_ID_MASK,
	.flags = 0,
};
static int req_filter_id = -1;
CAN_MSGQ_DEFINE(req_msgq, CONFIG_DEMO_MULTINODE_CAN_RX_QUEUE_SIZE);

const static struct can_filter resp_filter = {
	.id = CONFIG_DEMO_MULTINODE_CAN_RESP_MSG_ID,
	.mask = CAN_STD_ID_MASK,
	.flags = 0,
};
static int resp_filter_id = -1;
CAN_MSGQ_DEFINE(resp_msgq, CONFIG_DEMO_MULTINODE_CAN_RX_QUEUE_SIZE);

#define CAN_CONFIGURED  0
#define CAN_LISTENER_ON 1
#define CAN_SENDER_ON   2

// NOTE: This value is only changed inside shell command handlers (shell thread) so we don't
// have to worry about races.
static int can_state = 0;

static int can_setup(const struct shell *sh)
{
	int rc;

	if (IS_BIT_SET(can_state, CAN_CONFIGURED)) {
		return -EALREADY;
	}

	if (!device_is_ready(dev)) {
		shell_error(sh, "Can device %s not ready", dev->name);
		return -ENODEV;
	}

	LOG_DBG("Setting %s bitrate to %ul", dev->name, max_bitrate);
	rc = can_set_bitrate(dev, max_bitrate);
	if (rc < 0) {
		shell_error(sh, "Failed to set %s device bitrate (err %d)", dev->name, rc);
		return rc;
	}

	LOG_DBG("Setting %s mode to normal", dev->name);
	rc = can_set_mode(dev, CAN_MODE_NORMAL);
	if (rc < 0) {
		shell_error(sh, "Failed to set %s device mode (err %d)", dev->name, rc);
		return rc;
	}

	rc = can_start(dev);
	if (rc < 0) {
		shell_error(sh, "Failed to start %s interface (err %d)", dev->name, rc);
		return rc;
	}

	WRITE_BIT(can_state, CAN_CONFIGURED, 1);
	shell_print(sh, "Configured %s interface", dev->name);

	return rc;
}

static int can_shutdown(const struct shell *sh)
{
	if (!IS_BIT_SET(can_state, CAN_CONFIGURED)) {
		return -EALREADY;
	}
	if (FIELD_GET(GENMASK(CAN_SENDER_ON, CAN_LISTENER_ON), can_state)) {
		return 0;
	}

	can_stop(dev);
	can_state = 0;

	shell_print(sh, "Stopped %s interface", dev->name);

	return 0;
}

static void listener_worker(void *a1, void *a2, void *a3)
{
	ARG_UNUSED(a1);
	ARG_UNUSED(a2);
	ARG_UNUSED(a3);

	struct can_frame rx_frame, tx_frame = {.id = CONFIG_DEMO_MULTINODE_CAN_RESP_MSG_ID,
					       .dlc = can_bytes_to_dlc(sizeof(uint64_t)),
					       .flags = 0};
	int rc;
	for (;;) {
		k_msgq_get(&req_msgq, &rx_frame, K_FOREVER);
		if (can_dlc_to_bytes(rx_frame.dlc) != 8) {
			LOG_ERR("malformed message from %d", rx_frame.id);
			continue;
		}

		LOG_INF("incoming 'Ping%" PRIu64 "'", (uint64_t)(*rx_frame.data));

		memcpy(tx_frame.data, rx_frame.data, 8);
		rc = can_send(dev, &tx_frame, K_MSEC(CONFIG_DEMO_MULTINODE_CAN_TIMEOUT), nullptr,
			      nullptr);

		LOG_INF("response 'Pong%" PRIu64 "'", (uint64_t)(*tx_frame.data));
	}
}
K_THREAD_DEFINE(listener_thrd, CONFIG_DEMO_MULTINODE_CAN_LISTEN_THRD_STACK_SIZE, listener_worker,
		nullptr, nullptr, nullptr, CONFIG_DEMO_MULTINODE_CAN_LISTEN_THRD_PRIO, 0, 0);

static void send_callback(const struct device *dev, int error, void *data)
{
	ARG_UNUSED(dev);

	if (error) {
		return;
	}
	k_sem_give(data);
}

int multinode_can_listen_start(const struct shell *sh, size_t argc, char **argv)
{
	int rc;

	if (IS_BIT_SET(can_state, CAN_LISTENER_ON)) {
		shell_error(sh, "Listener on %s already enabled", dev->name);
		return -EALREADY;
	}

	rc = can_setup(sh);
	if (rc && rc != -EALREADY) {
		return rc;
	}

	LOG_DBG("Enabling request filter");
	req_filter_id = can_add_rx_filter_msgq(dev, &req_msgq, &req_filter);
	if (req_filter_id < 0) {
		shell_error(sh, "Failed to enable request message filter (%d)", req_filter_id);
		return req_filter_id;
	}

	WRITE_BIT(can_state, CAN_LISTENER_ON, 1);
	shell_print(sh, "CAN listener started on %s", dev->name);

	return rc;
}

int multinode_can_listen_stop(const struct shell *sh, size_t argc, char **argv)
{
	int rc;

	if (!IS_BIT_SET(can_state, CAN_LISTENER_ON)) {
		shell_error(sh, "Listener on %s already disabled", dev->name);
		return -EALREADY;
	}

	LOG_DBG("Removing request filter");
	can_remove_rx_filter(dev, req_filter_id);
	req_filter_id = -1;

	WRITE_BIT(can_state, CAN_LISTENER_ON, 0);
	shell_print(sh, "CAN listener stopped on %s", dev->name);

	rc = can_shutdown(sh);
	if (rc && rc != -EALREADY) {
		shell_error(sh, "Interface %s already shutdown", dev->name);
		return rc;
	}

	return rc;
}

int multinode_can_send(const struct shell *sh, size_t argc, char **argv)
{
	int rc;
	int64_t now;
	struct can_frame rx_frame, tx_frame = {.id = CONFIG_DEMO_MULTINODE_CAN_REQ_MSG_ID,
					       .dlc = can_bytes_to_dlc(sizeof(uint64_t)),
					       .flags = 0};
	struct k_sem tx_sem;

	rc = can_setup(sh);

	LOG_DBG("Enabling response filter");
	resp_filter_id = can_add_rx_filter_msgq(dev, &resp_msgq, &resp_filter);
	if (resp_filter_id < 0) {
		shell_error(sh, "Failed to enable response message filter (%d)", resp_filter_id);
		return resp_filter_id;
	}
	WRITE_BIT(can_state, CAN_SENDER_ON, 1);

	k_sem_init(&tx_sem, 0, 1);

	for (uint64_t i = 0; i < 4; ++i) {
		memcpy(tx_frame.data, &i, 8);

		shell_print(sh, "send 'Ping%" PRIu64 "'", i);

		now = k_uptime_get();
		rc = can_send(dev, &tx_frame, K_MSEC(CONFIG_DEMO_MULTINODE_CAN_TIMEOUT),
			      send_callback, &tx_sem);
		if (rc < 0 || (rc = k_sem_take(&tx_sem, K_MSEC(CONFIG_DEMO_MULTINODE_CAN_TIMEOUT -
							       (k_uptime_get() - now))))) {
			shell_error(sh, "Sending message failed (err %d)", rc);
			break;
		}

		rc = k_msgq_get(&resp_msgq, &rx_frame,
				K_MSEC(CONFIG_DEMO_MULTINODE_CAN_TIMEOUT - (k_uptime_get() - now)));
		if (rc < 0) {
			shell_error(sh, "Getting response failed (err=%d)", rc);
			break;
		}

		shell_print(sh, "resp 'Pong%" PRIu64 "' (elapsed %" PRIi64 "ms)",
			    (uint64_t)(*rx_frame.data), k_uptime_delta(&now));

		k_sleep(K_SECONDS(1));
	}

	LOG_DBG("Removing response filter");
	can_remove_rx_filter(dev, resp_filter_id);
	resp_filter_id = -1;

	WRITE_BIT(can_state, CAN_SENDER_ON, 0);
	rc |= can_shutdown(sh);

	return rc;
}
