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
 * @file aloe/sys_linux.h
 * @brief sys_linux
 */

#ifndef _H_ALOE_SYS_LINUX
#define _H_ALOE_SYS_LINUX

#ifndef _H_ALOE_SYS
#  error "Please included <aloe/sys.h> instead!"
#endif

#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <ctype.h>

#include "util.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup ALOE_LINUX
 * @{
 */

#define ALOE_TICK2MS(_ts) ((unsigned long)(_ts) / 1000ul)
#define ALOE_MS2TICK(_ms) ((unsigned long)(_ms) * 1000ul)

#define ALOE_TICK2US(_ts) (_ts)
#define ALOE_US2TICK(_us) (_us)

#define ALOE_SEM_NAME_SIZE 20
struct aloe_sem_rec {
	pthread_mutex_t mutex;
	int max, cnt;
	pthread_cond_t not_empty;
#if ALOE_SEM_NAME_SIZE
	char name[ALOE_SEM_NAME_SIZE];
#endif
};

/** Lock/unlock mutex.
 *
 * - Set -2ul to dur_sec or dur_us to **unlock** the mutex.
 * - Set -1ul to dur_sec or dur_us to **waiting until** lock the mutex.
 * - Set 0ul to dur_sec **and** dur_us to **try lock** the mutex.
 *
 * @param mutex
 * @param dur_sec
 * @param dur_us
 * @return
 */
int aloe_mutex_lock(pthread_mutex_t *mutex, unsigned long dur_sec,
		unsigned long dur_us);
#define aloe_mutex_unlock(_m) aloe_mutex_lock(_m, -2ul, -2ul)
#define aloe_mutex_lock_infinite(_m) aloe_mutex_lock(_m, -1ul, -1ul)

#define aloe_locker_decl(_func_modifier, _name) \
_func_modifier int _name ## _lock (unsigned long dur_sec, unsigned long dur_us) { \
	static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; \
	return aloe_mutex_lock(&mutex, dur_sec, dur_us); \
} \
_func_modifier int _name ## _unlock (void) { return _name ## _lock(-2ul, -2ul); } \
_func_modifier int _name ## _lock_infinite (void) { return _name ## _lock(-1ul, -1ul); }

/** @} ALOE_LINUX */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _H_ALOE_SYS_LINUX */
