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
 * @file aloe/util.h
 * @brief util
 *
 */

#ifndef _H_ALOE_UTIL
#define _H_ALOE_UTIL

/** @mainpage
 *
 * -  Overview
 *    -  Meet the subsystem. `CPPFLAGS+=-DALOE_SYS_LINUX=1`
 *    -  `#include <aloe/sys.h>`
 *    -  include another aloe header.
 *
 * -  Development
 *    -  Project layout
 *    -  API docs
 *
 * @defgroup ALOE aloe
 * @brief Function without specified section.
 *
 * @defgroup ALOE_SYS System
 * @brief System specified function.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup ALOE
 * @{
 */

#define _aloe_stringify(_s) # _s
#define aloe_stringify(_s) _aloe_stringify(_s)

#define aloe_offsetof(_type, _member) ((_type *)NULL)->_member
#define aloe_containerof(_obj, _type, _member) \
	((_type *)((_obj) ? ((char*)(_obj) - offsetof(_type, _member)) : NULL))
#define aloe_offsetafter(_type, _member) (offsetof(_type, _member) + sizeof(aloe_member_of(_type, _member)))

#define _aloe_concat2(_s1, _s2) _s1 ## _s2
#define aloe_concat2(_s1, _s2) _aloe_concat2(_s1, _s2)
#define _aloe_concat3(_s1, _s2, _s3) _s1 ## _s2 ## _s3
#define aloe_concat3(_s1, _s2, _s3) _aloe_concat3(_s1, _s2, _s3)

#define aloe_arraysize(_a) (sizeof(_a) / sizeof((_a)[0]))

#define aloe_trex "🦖"
#define aloe_sauropod "🦕"
#define aloe_lizard "🦎"
#define aloe_endl_msw "\r\n"
#define aloe_endl_unix "\n"

/** Version number. */
#define ALOE_VERSION_MAJOR 0
#define ALOE_VERSION_MINOR 1
#define ALOE_VERSION_BUILD 1

/** Return version. */
const char* aloe_version(int *ver, size_t cnt);

/** Generic buffer holder. */
typedef struct aloe_buf_rec {
	void *data; /**< Memory pointer. */
	size_t cap; /**< Memory capacity. */
	size_t lmt; /**< Data size. */
	size_t pos; /**< Data start. */
} aloe_buf_t;

#define _aloe_buf_clear(_buf) do {(_buf)->lmt = (_buf)->cap; (_buf)->pos = 0;} while (0)
#define _aloe_buf_flip(_buf) do {(_buf)->lmt = (_buf)->pos; (_buf)->pos = 0;} while (0)

aloe_buf_t* aloe_buf_clear(aloe_buf_t *buf);
aloe_buf_t* aloe_buf_flip(aloe_buf_t *buf);
aloe_buf_t* aloe_buf_rewind(aloe_buf_t *buf);

#define ALOE_TIMESEC_NORM(_sec, _ss, _scale) \
	if ((_ss) >= _scale) { \
		(_sec) += (_ss) / _scale; \
		(_ss) %= (_scale); \
	}

/** A - B . */
#define ALOE_TIMESEC_CMP(_a_sec, _a_ss, _b_sec, _b_ss) ( \
	((_a_sec) > (_b_sec)) ? 1 : \
	((_a_sec) < (_b_sec)) ? -1 : \
	((_a_ss) > (_b_ss)) ? 1 : \
	((_a_ss) < (_b_ss)) ? -1 : \
	0)

/** C = A - B .
 *
 * Expect A >= B
 */
#define ALOE_TIMESEC_SUB(_a_sec, _a_ss, _b_sec, _b_ss, _c_sec, _c_ss, _scale) \
	if ((_a_ss) < (_b_ss)) { \
		(_c_sec) = (_a_sec) - (_b_sec) - 1; \
		(_c_ss) = (_scale) + (_a_ss) - (_b_ss); \
	} else { \
		(_c_sec) = (_a_sec) - (_b_sec); \
		(_c_ss) = (_a_ss) - (_b_ss); \
	}

/** C = A + B .
 *
 * Expect normalized A and B.
 * Output will be normalized.
 */
#define ALOE_TIMESEC_ADD(_a_sec, _a_ss, _b_sec, _b_ss, _c_sec, _c_ss, _scale) \
	do { \
		(_c_sec) = (_a_sec) + (_b_sec); \
		(_c_ss) = (_a_ss) + (_b_ss); \
		ALOE_TIMESEC_NORM(_c_sec, _c_ss, _scale); \
	} while(0)

/** @} ALOE */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _H_ALOE_UTIL */
