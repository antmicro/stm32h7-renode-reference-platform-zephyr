# Copyright (c) 2026 Antmicro <www.antmicro.com>
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(renode_demo LANGUAGES C)

target_sources(app PRIVATE src/potentiometer.c src/flash_store.c)

target_sources_ifdef(CONFIG_DEMO_USB app PRIVATE src/usbdev.c)
