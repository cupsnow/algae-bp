/* $Id$
 *
 * Copyright 2024, Dexatek Technology Ltd.
 * This is proprietary information of Dexatek Technology Ltd.
 * All Rights Reserved. Reproduction of this documentation or the
 * accompanying programs in any manner whatsoever without the written
 * permission of Dexatek Technology Ltd. is strictly forbidden.
 *
 * @author joelai
 *
 * @file /algae-bp/package/aloe/util.cpp
 * @brief util
 */

#include <aloe/sys.h>
#include "log.h"

extern "C"
const char* aloe_version(int *ver, size_t cnt) {
	static char ver_str[] = "aloe "
			"v" aloe_stringify(ALOE_VERSION_MAJOR)
			"." aloe_stringify(ALOE_VERSION_MINOR)
			"." aloe_stringify(ALOE_VERSION_BUILD);
	if (ver) {
		if (cnt > 0) ver[0] = ALOE_VERSION_MAJOR;
		if (cnt > 1) ver[1] = ALOE_VERSION_MINOR;
		if (cnt > 2) ver[2] = ALOE_VERSION_BUILD;
		if (cnt > 3) memset(&ver[3], 0, (cnt - 3) * sizeof(ver[0]));
	}
	return ver_str;
}

extern "C"
aloe_buf_t* aloe_buf_clear(aloe_buf_t *buf) {
	_aloe_buf_clear(buf);
	return buf;
}

extern "C"
aloe_buf_t* aloe_buf_flip(aloe_buf_t *buf) {
	_aloe_buf_flip(buf);
	return buf;
}

extern "C"
aloe_buf_t* aloe_buf_rewind(aloe_buf_t *buf) {
	size_t sz;

	if (buf->pos <= 0) return buf;
	if (buf->pos < buf->lmt) {
		sz = buf->lmt - buf->pos;
		if (sz > 0) memmove(buf->data, (char*)buf->data + buf->pos, sz);
	} else {
		if (buf->lmt > buf->cap || buf->pos > buf->lmt) {
			aloe_log_e("Sanity check invalid %lu <= %lu <= %lu\n",
					(unsigned long)buf->pos, (unsigned long)buf->lmt,
					(unsigned long)buf->cap);
		}
		sz = 0;
	}
	buf->pos = 0;
	buf->lmt = sz;
	return buf;
}

