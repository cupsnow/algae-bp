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

#ifndef _H_ALGAE_CLI
#define _H_ALGAE_CLI

/**
 * @defgroup DUMMY_CLI cli
 * @ingroup DUMMY
 * @brief Command line.
 */

#include "priv.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup DUMMY_CLI
 * @{
 */
extern void *cli_global;

void* cli1_init(void *evctx);
int cli1_cmd_add(void *_clictx, const char *str,
		int (*run)(void*, int, const char**), void *cbarg, const char *detail);

void cli1_test1(void);


/** @} DUMMY_CLI */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _H_ALGAE_CLI */
