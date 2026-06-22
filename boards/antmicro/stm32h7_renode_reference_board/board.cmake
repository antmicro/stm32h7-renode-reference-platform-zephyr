# Copyright (c) 2026 Antmicro <www.antmicro.com>
# SPDX-License-Identifier: Apache-2.0

board_runner_args(openocd "--config=${BOARD_DIR}/support/stm32h7xx_over_ft4232h-jtag.cfg")

include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
