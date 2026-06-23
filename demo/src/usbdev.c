/*
 * Copyright (c) 2026 Antmicro <www.antmicro.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_msc.h>
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include <stdint.h>

LOG_MODULE_REGISTER(usbdev);

#define DEMO_VID 0x2fe3
#define DEMO_PID 0x0008

USBD_DEVICE_DEFINE(demo_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), DEMO_VID, DEMO_PID);

USBD_DESC_LANG_DEFINE(demo_lang);
USBD_DESC_MANUFACTURER_DEFINE(demo_mfr, "Antmicro");
USBD_DESC_PRODUCT_DEFINE(demo_product, "STM32H7 Renode Reference Platform");
USBD_DESC_SERIAL_NUMBER_DEFINE(demo_sn);

USBD_DESC_CONFIG_DEFINE(fs_cfg_desc, "FS Configuration");

static const uint8_t attributes = USB_SCD_SELF_POWERED;

USBD_CONFIGURATION_DEFINE(demo_fs_config, attributes, CONFIG_DEMO_USB_MAX_POWER, &fs_cfg_desc);

USBD_DEFINE_MSC_LUN(ram, "RAM", "Zephyr", "RAMDisk", "0.00");

static int usbdev_cmd_on(const struct shell *sh, size_t argc, char *argv[]);
static int usbdev_cmd_off(const struct shell *sh, size_t argc, char *argv[]);

static int usbd_setup_device(void)
{
	int err;

	err = usbd_add_descriptor(&demo_usbd, &demo_lang);
	if (err) {
		LOG_ERR("Failed to initialize language descriptor (err=%d)", err);
		return err;
	}

	err = usbd_add_descriptor(&demo_usbd, &demo_mfr);
	if (err) {
		LOG_ERR("Failed to initialize manufacturer descriptor (err=%d)", err);
		return err;
	}

	err = usbd_add_descriptor(&demo_usbd, &demo_product);
	if (err) {
		LOG_ERR("Failed to initialize product descriptor (err=%d)", err);
		return err;
	}

	err = usbd_add_descriptor(&demo_usbd, &demo_sn);
	if (err) {
		LOG_ERR("Failed to initialize SN descriptor (err=%d)", err);
		return err;
	}

	err = usbd_add_configuration(&demo_usbd, USBD_SPEED_FS, &demo_fs_config);
	if (err) {
		LOG_ERR("Failed to add Full-Speed configuration (err=%d)", err);
		return err;
	}

	err = usbd_register_all_classes(&demo_usbd, USBD_SPEED_FS, 1, NULL);
	if (err) {
		LOG_ERR("Failed to add register classes (err=%d)", err);
		return err;
	}

	usbd_self_powered(&demo_usbd, attributes & USB_SCD_SELF_POWERED);

	err = usbd_init(&demo_usbd);
	if (err) {
		LOG_ERR("Failed to initialize USB stack (err=%d)", err);
		return err;
	}

	return 0;
}

SYS_INIT(usbd_setup_device, APPLICATION, 0);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_usbdev_cmds,
			       SHELL_CMD_ARG(on, NULL,
					     SHELL_HELP("Enable USB mass-storage device", ""),
					     usbdev_cmd_on, 1, 0),
			       SHELL_CMD_ARG(off, NULL,
					     SHELL_HELP("Disable USB mass-storage device", ""),
					     usbdev_cmd_off, 1, 0),
			       SHELL_SUBCMD_SET_END);

static int usbdev_cmd_on(const struct shell *sh, size_t argc, char *argv[])
{
	int err;

	err = usbd_enable(&demo_usbd);
	if (err) {
		shell_error(sh, "Failed to enable USB device stack");
		return err;
	}

	shell_info(sh, "Enabled USB mass-storage device");

	return 0;
}

static int usbdev_cmd_off(const struct shell *sh, size_t argc, char *argv[])
{
	int err;

	err = usbd_disable(&demo_usbd);
	if (err) {
		shell_error(sh, "Failed to disable USB device stack");
		return err;
	}

	shell_info(sh, "Disabled USB mass-storage device");

	return 0;
}

SHELL_CMD_REGISTER(usbdev, &sub_usbdev_cmds, "USB mass-storage device commands", NULL);
