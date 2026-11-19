# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
if(NOT DEFINED EXTRA_CONF_FILE)
  set(EXTRA_CONF_FILE "bt.conf;uart.conf")
endif()
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nano33ble_test2_ncs)
target_sources(app PRIVATE src/main.c src/bluetooth.c)
