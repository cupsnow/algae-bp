/* $Id$
 *
 * This file is part of the project algae-bp
 *
 * @author joelai
 *
 * @file /algae-bp/package/tester1/bt.h
 * @brief bluetooth
 */

#ifndef BT_H_
#define BT_H_

#ifdef __cplusplus
extern "C" {
#endif

extern void *bt_global;

void* bt_init(void *evctx);
void bt_destroy(void *_bt2);

int cli_cmd_btrfcomm(void*, int argc, const char **argv);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* BT_H_ */
