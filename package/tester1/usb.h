/* $Id$
 *
 * This file is part of the project algae-bp
 *
 * Aug 25, 2026
 *
 * @author joelai
 *
 * @file /algae-bp/package/tester1/usb.h
 * @brief usb
 */

#ifndef USB_H_
#define USB_H_

#ifdef __cplusplus
extern "C" {
#endif

extern void *usb_global;

void* usb_init(void *evctx);
void usb_destroy(void *_usb2);

int cli_cmd_usb(void*, int argc, const char **argv);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* USB_H_ */
