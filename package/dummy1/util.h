/* $Id$
 *
 * Copyright 2023, Dexatek Technology Ltd.
 * This is proprietary information of Dexatek Technology Ltd.
 * All Rights Reserved. Reproduction of this documentation or the
 * accompanying programs in any manner whatsoever without the written
 * permission of Dexatek Technology Ltd. is strictly forbidden.
 *
 * @author joelai
 */

#ifndef UTIL_H_
#define UTIL_H_

#ifdef __cplusplus
#  include <cstdio>
#  include <cstdlib>
#  include <cstdarg>
#else
#  include <stdio.h>
#  include <stdlib.h>
#  include <stdarg.h>
#endif

#include <time.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	unsigned major, minor, release, code;
} algae_version_t;

const char* algae_version(algae_version_t *ver);

#ifdef __cplusplus
} /* extern "C" */
#endif




#endif /* UTIL_H_ */
