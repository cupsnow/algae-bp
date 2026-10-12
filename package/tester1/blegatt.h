/* $Id$
 *
 * This file is part of the project algae-bp
 *
 * @author joelai
 *
 * @file /algae-bp/package/tester1/blegatt.h
 * @brief BLE GATT peripheral
 */

#ifndef BLEGATT_H_
#define BLEGATT_H_

#ifdef __cplusplus
extern "C" {
#endif

extern void *blegatt_global;

void* blegatt_init(void *evctx);
void blegatt_destroy(void *_ble);

int cli_cmd_blegatt(void*, int argc, const char **argv);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* BLEGATT_H_ */
