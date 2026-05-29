/* $Id$
 *
 * Copyright (c) 2026, joelai
 * All Rights Reserved
 *
 * SPDX-License-Identifier: MIT
 *
 * @file priv.c
 * @brief noname
 */

#include "priv.h"

extern "C"
evconn_t* evconn_list_foreach(evconn_list_t *q, evconn_t *p) {
	if (!p) {
		p = TAILQ_FIRST(q);
	} else {
		p = TAILQ_NEXT(p, qent);
	}
	return p;
}
