/* $Id$
 *
 * This file is part of the project algae-bp
 *
 * Aug 24, 2026
 *
 * @author joelai
 *
 * @file /algae-bp/package/aloe/include/aloe/mini_udev.h
 * @brief mini_udev
 */

#ifndef MINI_UDEV_H_
#define MINI_UDEV_H_

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

int mini_udev_uevent_open(void);
void mini_udev_uevent_close(int fd);
const char* mini_udev_uevent_check(const char *buf, ssize_t len, const char *key);
static int mini_udev_uevent_is_hidraw_event(const char *buf, ssize_t len);

#ifdef __cplusplus
} /* extern "C" */
#endif




#endif /* MINI_UDEV_H_ */
