/*
 * Copyright (c) 2026 Antmicro <www.antmicro.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/shell/shell.h>
#include <zephyr/toolchain.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(flash_store);

#define NVS_PARTITION storage_partition

static struct nvs_fs fs = {
	.flash_device = FIXED_PARTITION_DEVICE(NVS_PARTITION),
	.offset = FIXED_PARTITION_OFFSET(NVS_PARTITION),
	.sector_count = 3U,
};

#define DEMO_BOOT_CNT_KEY 1
#define DEMO_MESSAGE_KEY  2

static const char demo_message_default[] = "All your base are belong to us";
static uint8_t buffer[CONFIG_DEMO_FLASH_STORE_MSG_BUF_SIZE] = {0};

static int flash_store_bootcnt_cmd_get(const struct shell *sh, size_t argc, char *argv[]);
static int flash_store_bootcnt_cmd_reset(const struct shell *sh, size_t argc, char *argv[]);
static int flash_store_msg_cmd_read(const struct shell *sh, size_t argc, char *argv[]);
static int flash_store_msg_cmd_write(const struct shell *sh, size_t argc, char *argv[]);

static int bootcnt_write(uint32_t cnt)
{
	int rc;

	LOG_DBG("Setting boot count to %d", cnt);
	rc = nvs_write(&fs, DEMO_BOOT_CNT_KEY, (void *)&cnt, sizeof(cnt));
	if (rc < 0) {
		return rc;
	}

	return 0;
}

static int message_write(const char *str)
{
	int rc;
	size_t msg_len = strlen(str);

	/* Truncate string to fit in buffer */
	msg_len = MIN(msg_len, sizeof(buffer) - 1);
	memcpy(buffer, str, msg_len);
	buffer[msg_len++] = 0;

	LOG_DBG("Writing %d bytes long message to flash", msg_len);

	rc = nvs_write(&fs, DEMO_MESSAGE_KEY, (void *)buffer, msg_len);
	if (rc < 0) {
	}
	return rc;
}

static int demo_flash_init(void)
{
	int rc;
	uint32_t boot_cnt;
	struct flash_pages_info info;

	if (!device_is_ready(fs.flash_device)) {
		LOG_ERR("Flash device %s is not ready", fs.flash_device->name);
		return -ENODEV;
	}

	LOG_DBG("Using flash device '%s' at offset %ld", fs.flash_device->name, fs.offset);

	rc = flash_get_page_info_by_offs(fs.flash_device, fs.offset, &info);
	if (rc) {
		LOG_ERR("Unable to get page info (err=%d)", rc);
		return rc;
	}
	fs.sector_size = info.size;

	LOG_DBG("Page Info - start offset: %ld, size: %zu, index: %d", info.start_offset, info.size,
		info.index);

	rc = nvs_mount(&fs);
	if (rc) {
		LOG_ERR("Failed to mount flash storage (err=%d)", rc);
		return rc;
	}

	/* Check if first boot and set default values if so */
	rc = nvs_read(&fs, DEMO_BOOT_CNT_KEY, (void *)&boot_cnt, sizeof(boot_cnt));
	if (rc > 0) {
		LOG_DBG("Boot counter at %d, incrementing to %d", boot_cnt, boot_cnt + 1);
		++boot_cnt;
	} else {
		LOG_DBG("Boot counter not found, setting to default value");
		boot_cnt = 1;
	}
	rc = bootcnt_write(boot_cnt);
	if (rc < 0) {
		LOG_ERR("Failed to write boot count to flash (err=%d)", rc);
		return rc;
	}

	rc = nvs_read(&fs, DEMO_MESSAGE_KEY, (void *)buffer, sizeof(buffer));
	if (rc <= 0) {
		LOG_DBG("Custom message not found, setting to default value");
		rc = message_write(demo_message_default);
		if (rc < 0) {
			LOG_ERR("Failed to write message to flash (err=%d)", rc);
			return rc;
		}
	}

	return 0;
}

SYS_INIT(demo_flash_init, APPLICATION, 0);

SHELL_STATIC_SUBCMD_SET_CREATE(flash_store_bootcnt_cmds,
			       SHELL_CMD_ARG(get, NULL, SHELL_HELP("Get boot counter.", ""),
					     flash_store_bootcnt_cmd_get, 1, 0),
			       SHELL_CMD_ARG(reset, NULL, SHELL_HELP("Reset boot counter.", ""),
					     flash_store_bootcnt_cmd_reset, 1, 0),
			       SHELL_SUBCMD_SET_END);

static int flash_store_bootcnt_cmd_get(const struct shell *sh, size_t argc, char *argv[])
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int rc;
	uint32_t boot_cnt;

	rc = nvs_read(&fs, DEMO_BOOT_CNT_KEY, (void *)&boot_cnt, sizeof(boot_cnt));
	if (rc < 0) {
		shell_error(sh, "Reading boot counter failed (err=%d)", rc);
		return rc;
	}

	shell_print(sh, "Boot counter = %d", boot_cnt);
	return 0;
}

static int flash_store_bootcnt_cmd_reset(const struct shell *sh, size_t argc, char *argv[])
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int rc;

	rc = bootcnt_write(0);
	if (rc < 0) {
		shell_error(sh, "Failed to reset boot counter (err=%d)", rc);
		return rc;
	}
	shell_info(sh, "Boot counter reset");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(flash_store_message_cmds,
			       SHELL_CMD_ARG(read, NULL, SHELL_HELP("Read saved message.", ""),
					     flash_store_msg_cmd_read, 1, 0),
			       SHELL_CMD_ARG(write, NULL,
					     SHELL_HELP("Write new message.", "MESSAGE"),
					     flash_store_msg_cmd_write, 2, SHELL_OPT_ARG_RAW),
			       SHELL_SUBCMD_SET_END);

static int flash_store_msg_cmd_read(const struct shell *sh, size_t argc, char *argv[])
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int rc;

	rc = nvs_read(&fs, DEMO_MESSAGE_KEY, (void *)buffer, sizeof(buffer));
	if (rc < 0) {
		shell_error(sh, "Reading saved message failed (err=%d)", rc);
		return rc;
	}

	shell_print(sh, "Saved message is:\n\"%s\"", buffer);
	return 0;
}

static int flash_store_msg_cmd_write(const struct shell *sh, size_t argc, char *argv[])
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int rc;

	rc = message_write(argv[1]);
	if (rc < 0) {
		shell_error(sh, "Writing new message failed (err=%d)", rc);
		return rc;
	}

	shell_info(sh, "New message saved to flash");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(flash_store_cmds,
			       SHELL_CMD_ARG(boot_counter, &flash_store_bootcnt_cmds,
					     "Boot counter operations", NULL, 2, 0),
			       SHELL_CMD_ARG(message, &flash_store_message_cmds,
					     "Saved message operations", NULL, 2, 0),
			       SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(flash_store, &flash_store_cmds, "Flash storage commands", NULL);
