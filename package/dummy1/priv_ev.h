/* $Id$
 *
 * Copyright 2025, Dexatek Technology Ltd.
 * This is proprietary information of Dexatek Technology Ltd.
 * All Rights Reserved. Reproduction of this documentation or the
 * accompanying programs in any manner whatsoever without the written
 * permission of Dexatek Technology Ltd. is strictly forbidden.
 *
 * @author joelai
 *
 * @file /algae-bp/package/dummy1/priv_ev.h
 * @brief priv_ev
 */

#ifndef PRIV_EV_H_
#define PRIV_EV_H_

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

#endif /* PRIV_EV_H_ */
