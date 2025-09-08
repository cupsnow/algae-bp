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

#ifndef CLI_H_
#define CLI_H_

#include "priv.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void *cli_global;

void* cli1_init(void *evctx);
int cli1_cmd_add(void *_clictx, const char *str,
		int (*run)(void*, int, const char**), void *cbarg, const char *detail);

void cli1_test1(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CLI_H_ */
