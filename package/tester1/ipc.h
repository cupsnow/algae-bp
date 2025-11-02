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

#ifndef _H_ALGAE_IPC
#define _H_ALGAE_IPC

#include "priv.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void *ipc_global;

typedef enum {
	ipc1_type_null = 0,

	ipc1_type_callback1,

	ipc1_type_max
} ipc1_type_t;

void* ipc1_init(void *evctx);
int ipc1_write(void *ipc, int type, int *seq, const void *data, size_t data_sz);

typedef struct {
	void (*func)(void*);
	void *cbarg;
} ipc1_type_callback_t;

int ipc1_register_callback(void *ipc, int *seq, void (*func)(void*), void *cbarg);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _H_ALGAE_IPC */
