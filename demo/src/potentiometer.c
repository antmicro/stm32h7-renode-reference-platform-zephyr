/*
 * Copyright (c) 2026 Antmicro <www.antmicro.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/toolchain.h>
#include <zephyr/shell/shell.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pot);

#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || !DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "Missing potentiometer channel definition"
#endif

static const struct adc_dt_spec pot_channel = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);

K_EVENT_DEFINE(pot_dump_evt);
#define POT_DUMP_ENABLE BIT(0)

struct pot_value {
	int32_t raw;
	int32_t mv;
};

static int pot_cmd_read(const struct shell *sh, size_t argc, char *argv[]);
static int pot_cmd_dump(const struct shell *sh, size_t argc, char *argv[]);

static int pot_read(struct pot_value *reading)
{
	static uint32_t pot_buffer;
	static const struct adc_sequence pot_sequence = {
		.buffer = &pot_buffer,
		.buffer_size = sizeof(pot_buffer),
		.channels = BIT(pot_channel.channel_id),
		.resolution = pot_channel.resolution,
	};
	int err = 0;

	if (!adc_is_ready_dt(&pot_channel)) {
		LOG_ERR("ADC device %s not ready", pot_channel.dev->name);
		return -ENODEV;
	}

	err = adc_read_dt(&pot_channel, &pot_sequence);
	if (err) {
		LOG_ERR("ADC read failed (err=%d)", err);
		return err;
	};

	reading->raw = pot_buffer;
	reading->mv = pot_buffer;

	err = adc_raw_to_millivolts_dt(&pot_channel, &reading->mv);
	if (err) {
		LOG_WRN_ONCE("Unable to convert ADC raw value to millivolts (err=%d)", err);
		reading->mv = 0;
	}

	return 0;
}

static void pot_dump_worker(void *a1, void *a2, void *a3)
{
	ARG_UNUSED(a1);
	ARG_UNUSED(a2);
	ARG_UNUSED(a3);

	struct pot_value reading;

	for (;;) {
		k_event_wait(&pot_dump_evt, POT_DUMP_ENABLE, false, K_FOREVER);

		if (!pot_read(&reading)) {
			LOG_INF("raw = %" PRId32 ", mV = %" PRId32, reading.raw, reading.mv);
		}

		k_sleep(K_SECONDS(1));
	}
}

K_THREAD_DEFINE(pot_read_worker, 512, pot_dump_worker, NULL, NULL, NULL, 5, 0, 0);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_pot_cmds,
			       SHELL_CMD_ARG(read, NULL, SHELL_HELP("Read current value", ""),
					     pot_cmd_read, 1, 0),
			       SHELL_CMD_ARG(dump, NULL,
					     SHELL_HELP("Enable value dumping", "<on|off>"),
					     pot_cmd_dump, 2, 0),
			       SHELL_SUBCMD_SET_END);

static int pot_cmd_read(const struct shell *sh, size_t argc, char *argv[])
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int err;
	struct pot_value reading;

	err = pot_read(&reading);
	if (err) {
		shell_error(sh, "Reading potentiometer value failed (err=%d)", err);
		return err;
	}

	shell_info(sh, "Potentiometer (%s) value: %" PRId32 "mv (raw %" PRId32 ")",
		   pot_channel.dev->name, reading.mv, reading.raw);

	return 0;
}

static int pot_cmd_dump(const struct shell *sh, size_t argc, char *argv[])
{
	bool enable;
	int err = 0;

	enable = shell_strtobool(argv[1], 0, &err);
	if (err) {
		shell_error(sh, "Invalid argument: %s", argv[1]);
		return err;
	}

	if (enable) {
		shell_info(sh, "Potentiometer value dumping enabled");
		k_event_set(&pot_dump_evt, POT_DUMP_ENABLE);
	} else {
		shell_info(sh, "Potentiometer value dumping disabled");
		k_event_clear(&pot_dump_evt, POT_DUMP_ENABLE);
	}

	return 0;
}

SHELL_CMD_REGISTER(pot, &sub_pot_cmds, "Potentiometer commands", NULL);
