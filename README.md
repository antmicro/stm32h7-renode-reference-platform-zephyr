# STM32H7 Renode Reference Platform

Copyright (c) 2026 [Antmicro](https://www.antmicro.com)

This repository contains files for Antmicro's
[Renode Reference Platform Board](https://openhardware.antmicro.com/boards/stm32h7-renode-reference-platform/?tab=features)
configured for [Zephyr](https://github.com/zephyrproject-rtos/zephyr) as an external module, as well as
a demo application showcasing board features.

# Quick start

## Setting up workspace

Configure a virtual environment and install `west` with `pip`:
```sh
pip install west
```

Then, initialize the workspace using `west`:
```sh
west init -l .
west update
west packages pip --install
west sdk install
```

## Building the demo

```sh
west build -b stm32h7_renode_reference_board demo
```

## Flashing the board

```sh
west flash
```

## Running simulation in Renode

```sh
west simulate
```

# Demo

```sh
Available commands:
  can      : CAN controller commands
  clear    : Clear screen.
  date     : Date commands
  device   : Device commands
  devmem   : Read/write physical memory
             Usage:
             Read memory at address with optional width:
             devmem <address> [<width>]
             Write memory at address with mandatory width and value:
             devmem <address> <width> <value>
  gpio     : GPIO commands
  help     : Prints the help message.
  history  : Command history.
  hwinfo   : HWINFO commands
  i2c      : I2C commands
  input    : Input commands
  kernel   : Kernel commands
  led      : LED commands
  net      : Networking commands
  pot      : Potentiometer commands
  rem      : Ignore lines beginning with 'rem '
  resize   : Console gets terminal screen size or assumes default in case the
             readout fails. It must be executed after each terminal width change
             to ensure correct text display.
  retval   : Print return value of most recent command
  sensor   : Sensor commands
  shell    : Useful, not Unix-like shell commands.
```
