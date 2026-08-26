/* $Id$
 *
 * Copyright 2026, Dexatek Technology Ltd.
 * This is proprietary information of Dexatek Technology Ltd.
 * All Rights Reserved. Reproduction of this documentation or the
 * accompanying programs in any manner whatsoever without the written
 * permission of Dexatek Technology Ltd. is strictly forbidden.
 *
 * @author joelai
 *
 * @file /esh-ws/package/esh-tester/usbhid_mini.h
 * @brief usbhid_mini
 */

#ifndef _H_ALGAE_USBHID_MINI
#define _H_ALGAE_USBHID_MINI

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Find a hidraw device matching VID/PID.
 *
 * Returns:
 *   fd >= 0 : opened matching device
 *   -1      : not found
 */
int mini_hid_open_hidraw(const char *devdir, uint16_t vid, uint16_t pid,
		char *found_path, size_t found_path_size);
int mini_hid_open(const char *path);
void mini_hid_close(int fd);
int mini_hid_read(int fd, void *buf, size_t len, int timeout_ms);
int mini_hid_write(int fd, const void *buf, size_t len);

struct mini_hid_info {
	uint32_t bustype;
	uint16_t vendor;
	uint16_t product;
};
int mini_hid_get_info(int fd, struct mini_hid_info *info);
int mini_hid_get_name(int fd, char *buf, size_t size);
int mini_hid_get_phys(int fd, char *buf, size_t size);
int mini_hid_get_report_descriptor(int fd, uint8_t *buf, size_t bufsize);


int mini_hid_get_serial( int fd, char *buf, size_t size);

/* buf[0] must contain Report ID */
int mini_hid_get_feature( int fd, unsigned char *buf, size_t size);

/* buf[0] must contain Report ID */
int mini_hid_set_feature( int fd, unsigned char *buf, size_t size);

/* buf[0] must contain Report ID */
int mini_hid_get_input( int fd, unsigned char *buf, size_t size);

/* buf[0] must contain Report ID */
int mini_hid_set_output( int fd, unsigned char *buf, size_t size);

/** Parse and print a HID report descriptor (short items). */
typedef int (*mini_hid_printf_t)(void*, const char*, ...);
void mini_hid_parse_report_descriptor(const uint8_t *desc, int len,
		mini_hid_printf_t outf, void *outf_args);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _H_ALGAE_USBHID_MINI */
