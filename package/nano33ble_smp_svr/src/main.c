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

#define STORAGE_PARTITION_LABEL	storage_partition
#define STORAGE_PARTITION_ID	PARTITION_ID(STORAGE_PARTITION_LABEL)

/* Define an example stats group; approximates seconds since boot. */
STATS_SECT_START(smp_svr_stats)
STATS_SECT_ENTRY(ticks)
STATS_SECT_END;

/* Assign a name to the `ticks` stat. */
STATS_NAME_START(smp_svr_stats)
STATS_NAME(smp_svr_stats, ticks)
STATS_NAME_END(smp_svr_stats);

/* Define an instance of the stats group. */
STATS_SECT_DECL(smp_svr_stats) smp_svr_stats;

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

	int rc = STATS_INIT_AND_REG(smp_svr_stats, STATS_SIZE_32,
				    "smp_svr_stats");

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
			STATS_INC(smp_svr_stats, ticks);
			blink_ticks = 0;
		}
	}
	return 0;
}
