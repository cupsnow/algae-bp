/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 * Copyright (c) 2020 Prevas A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/stats/stats.h>
#include <zephyr/usb/usb_device.h>

#ifdef CONFIG_MCUMGR_GRP_FS
#include <zephyr/device.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#endif
#ifdef CONFIG_MCUMGR_GRP_STAT
#include <zephyr/mgmt/mcumgr/grp/stat_mgmt/stat_mgmt.h>
#endif
#ifdef CONFIG_MCUMGR_TRANSPORT_UDP_DTLS
#include <zephyr/mgmt/mcumgr/transport/smp_udp.h>
#endif

#define LOG_LEVEL LOG_LEVEL_DBG
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(smp_sample);

#include "common.h"

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>

#include <stdio.h>
#include <stdlib.h>

typedef struct {
	int lvl, lno;
	const char *fn;
	const struct shell *sh;
} logging_t;

/* Serialize complete log_m messages. Call only from thread context. */
K_MUTEX_DEFINE(log_m_mutex);

static int log_m(logging_t *logging, const char *fmt, ...) {
	char fn_buf[20], ts_buf[20], lvl_buf[8], lno_buf[8];
	int ret = 0, len = 0;
	va_list va;
	uint64_t us, seconds;

	if ((ret = k_mutex_lock(&log_m_mutex, K_FOREVER)) != 0) return ret;

	// function name or []
	if (logging->fn) {
		len = snprintf(fn_buf, sizeof(fn_buf), "[%s]", 
				(logging->fn ? logging->fn : ""));
		if (len >= sizeof(fn_buf)) fn_buf[sizeof(fn_buf) - 1] = '\0';
	} else {
		fn_buf[0] = '\0';
	}

	// [HH:MM:SS.mmm,uuu]
	us = k_ticks_to_us_floor64(k_uptime_ticks());
	seconds = us / 1000000U;
	len = snprintf(ts_buf, sizeof(ts_buf), "[%02u:%02u:%02u.%03u,%03u]",
			(unsigned)(seconds / 3600u), (unsigned)((seconds / 60u) % 60u),
			(unsigned)(seconds % 60u), 
			(unsigned)((us / 1000u) % 1000u), (unsigned)(us % 1000u));
	if (len >= sizeof(ts_buf)) ts_buf[sizeof(ts_buf) - 1] = '\0';

	// severity or none
	len = snprintf(lvl_buf, sizeof(lvl_buf), "%s",
			((logging->lvl & 0xf) == 3 ? "[DBG]" :
			(logging->lvl & 0xf) == 2 ? "[INFO]" :
			(logging->lvl & 0xf) == 1 ? "[ERR]" :
			""));
	if (len >= sizeof(lvl_buf)) lvl_buf[sizeof(lvl_buf) - 1] = '\0';

	// line number or none
	if (logging->lno > 0) {
		len = snprintf(lno_buf, sizeof(lno_buf), "[#%d]", logging->lno);
		if (len >= sizeof(lno_buf)) lno_buf[sizeof(lno_buf) - 1] = '\0';
	} else {
		lno_buf[0] = '\0';
	}

	shell_fprintf_normal(logging->sh, "%s%s%s%s", ts_buf, lvl_buf, fn_buf, 
			lno_buf); 

	va_start(va, fmt);
	shell_vfprintf(logging->sh, SHELL_VT100_COLOR_DEFAULT, fmt, va);
	va_end(va);

	k_mutex_unlock(&log_m_mutex);
	return ret;
}

#define log_n(_fmt, _args...) do { \
	log_m(&(logging_t){0, 0, NULL, sh}, _fmt, ## _args); \
} while(0)

#define log_d(_fmt, _args...) do { \
	log_m(&(logging_t){0x3, __LINE__, __func__, sh}, _fmt, ## _args); \
} while(0)

#define log_e(_fmt, _args...) do { \
	log_m(&(logging_t){0x1, __LINE__, __func__, sh}, _fmt, ## _args); \
} while(0)

static int cmd_nano_info(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	log_d("enter %d\n", 8766);
	log_n("message %d\n", 8766);
	shell_print(sh, "Nano33 BLE test2, build " __DATE__ " " __TIME__);
	shell_print(sh, "Uptime: %lld ms", (long long)k_uptime_get());
	return 0;
}

static int cmd_nano_echo(const struct shell *sh, size_t argc, char **argv)
{
	for (size_t i = 1; i < argc; i++) {
		shell_fprintf(sh, SHELL_NORMAL, "%s%s", i == 1 ? "" : " ", argv[i]);
	}
	shell_print(sh, "");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(nano_commands,
	SHELL_CMD_ARG(info, NULL, "Show build and uptime", cmd_nano_info, 1, 0),
	SHELL_CMD_ARG(echo, NULL, "Echo arguments", cmd_nano_echo, 2, 15),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(nano, &nano_commands, "Nano33 example commands", NULL);
#else
#define log_d(...)
#define log_e(...)
#endif

#define STORAGE_PARTITION_LABEL	storage_partition
#define STORAGE_PARTITION_ID	PARTITION_ID(STORAGE_PARTITION_LABEL)

/* Define an example stats group; approximates seconds since boot. */
STATS_SECT_START(nano33ble_test2_stats)
STATS_SECT_ENTRY(ticks)
STATS_SECT_END;

/* Assign a name to the `ticks` stat. */
STATS_NAME_START(nano33ble_test2_stats)
STATS_NAME(nano33ble_test2_stats, ticks)
STATS_NAME_END(nano33ble_test2_stats);

/* Define an instance of the stats group. */
STATS_SECT_DECL(nano33ble_test2_stats) nano33ble_test2_stats;

#ifdef CONFIG_MCUMGR_GRP_FS
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(cstorage);
static struct fs_mount_t littlefs_mnt = {
	.type = FS_LITTLEFS,
	.fs_data = &cstorage,
	.storage_dev = (void *)STORAGE_PARTITION_ID,
	.mnt_point = "/lfs1"
};
#endif

int main(void)
{
	const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
	bool led_ready = gpio_is_ready_dt(&led);
	unsigned int blink_ticks = 0;

	if (led_ready) {
		int err = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

		if (err < 0) {
			LOG_ERR("Error configuring LED [%d]", err);
			led_ready = false;
		}
	} else {
		LOG_ERR("LED GPIO device is not ready");
	}

	int rc = STATS_INIT_AND_REG(nano33ble_test2_stats, STATS_SIZE_32,
				    "nano33ble_test2_stats");

	if (rc < 0) {
		LOG_ERR("Error initializing stats system [%d]", rc);
	}

	/* Register the built-in mcumgr command handlers. */
#ifdef CONFIG_MCUMGR_GRP_FS
	rc = fs_mount(&littlefs_mnt);
	if (rc < 0) {
		LOG_ERR("Error mounting littlefs [%d]", rc);
	}
#endif

#ifdef CONFIG_MCUMGR_TRANSPORT_UDP_DTLS
	rc = setup_udp_dtls();

	if (rc == 0) {
		rc = smp_udp_open();

		if (rc != 0) {
			LOG_ERR("UDP transport open failed: %d", rc);
		}
	} else {
		LOG_ERR("TLS init failed, cannot start UDP transport");
	}
#endif

#ifdef CONFIG_MCUMGR_TRANSPORT_BT
	start_smp_bluetooth_adverts();
#endif

	/* using __TIME__ ensure that a new binary will be built on every
	 * compile which is convenient when testing firmware upgrade.
	 */
	LOG_INF("build time: " __DATE__ " " __TIME__);

	/* Toggle every 250 ms for a 2 Hz blink. MCUmgr runs on the work queue. */
	while (1) {
		if (led_ready) {
			rc = gpio_pin_toggle_dt(&led);
			if (rc < 0) {
				LOG_ERR("Error toggling LED [%d]", rc);
				led_ready = false;
			}
		}
		k_sleep(K_MSEC(1000 / 2 / 2));
		if (++blink_ticks == 4) {
			STATS_INC(nano33ble_test2_stats, ticks);
			blink_ticks = 0;
		}
	}
	return 0;
}
