/* $Id$
 *
 * Copyright (c) 2025, joelai
 * All Rights Reserved
 *
 * SPDX-License-Identifier: MIT
 *
 * @file noname
 * @brief noname
 */

#ifndef IPC_H_
#define IPC_H_

#include "priv.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void *ipc_global;

int ipc1_write(void *ipc, int type, int *seq, const void *data, size_t data_sz);
void* ipc1_init(void *evctx);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* IPC_H_ */
