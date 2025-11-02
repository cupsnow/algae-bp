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

#ifndef _H_ALGAE_PRIV_EV
#define _H_ALGAE_PRIV_EV

#include "priv.h"
#include <aloe/ev.h>
#include <aloe/compat/openbsd/sys/queue.h>
#include <aloe/compat/openbsd/sys/tree.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct evconn_rec {
	int fd;
	void *ev_ctx, *ev;
	TAILQ_ENTRY(evconn_rec) qent;
} evconn_t;
typedef TAILQ_HEAD(evconn_list_rec, evconn_rec) evconn_list_t;

#define evconn_list_add(_q, _e) TAILQ_INSERT_TAIL(_q, _e, qent)
#define evconn_list_rm(_q, _e) TAILQ_REMOVE(_q, _e, qent)

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _H_ALGAE_PRIV_EV */
