/* $Id$
 *
 * Copyright 2025, joelai
 * This is proprietary information of joelai
 * All Rights Reserved. Reproduction of this documentation or the
 * accompanying programs in any manner whatsoever without the written
 * permission of joelai is strictly forbidden.
 *
 * @author joelai
 *
 * @file aloe/sys_linux.h
 * @brief sys_linux
 *
 */

#ifndef _H_ALOE_SYS_LINUX
#define _H_ALOE_SYS_LINUX

/**
 * @defgroup ALOE_LINUX Linux
 * @ingroup ALOE_SYS
 * @brief Linux specified function.
 *
 */

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
#include <string.h>

#include "util.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup ALOE_LINUX
 * @{
 */

/** Convert ticks to milliseconds. */
#define ALOE_TICK2MS(_ts) ((unsigned long)(_ts) / 1000ul)

/** Convert milliseconds to ticks. */
#define ALOE_MS2TICK(_ms) ((unsigned long)(_ms) * 1000ul)

/** Convert ticks to microseconds. */
#define ALOE_TICK2US(_ts) (_ts)

/** Convert microseconds to ticks. */
#define ALOE_US2TICK(_us) (_us)

/** Identify length. */
#define ALOE_SEM_NAME_SIZE 20

/** Handle implementation. */
struct aloe_sem_rec {
	pthread_mutex_t mutex; /**< pthread mutex handle. */
	int max; /**< Max count. */
	int cnt; /**< Current free count. */
	pthread_cond_t not_empty; /**< Notify. */
#if ALOE_SEM_NAME_SIZE
	char name[ALOE_SEM_NAME_SIZE]; /**< Identify. */
#endif
};

/** Lock/unlock mutex.
 *
 * - Set -2ul to dur_sec or dur_us to **unlock** the mutex.
 * - Set -1ul to dur_sec or dur_us to **waiting until** lock the mutex.
 * - Set 0ul to dur_sec **and** dur_us to **try lock** the mutex.
 */
int aloe_mutex_lock(pthread_mutex_t *mutex, unsigned long dur_sec,
		unsigned long dur_us);

/** Unlock mutex. */
#define aloe_mutex_unlock(_m) aloe_mutex_lock(_m, -2ul, -2ul)

/** Wait until lock mutex. */
#define aloe_mutex_lock_infinite(_m) aloe_mutex_lock(_m, -1ul, -1ul)

/** Declare function static mutex. */
#define aloe_locker_decl(_func_modifier, _name) \
_func_modifier int _name ## _lock (unsigned long dur_sec, unsigned long dur_us) { \
	static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; \
	return aloe_mutex_lock(&mutex, dur_sec, dur_us); \
} \
_func_modifier int _name ## _unlock (void) { return _name ## _lock(-2ul, -2ul); } \
_func_modifier int _name ## _lock_infinite (void) { return _name ## _lock(-1ul, -1ul); }

/** Condition wait. */
int aloe_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex,
		unsigned long dur_sec, unsigned long dur_us);

/** Identify length. */
#define ALOE_THREAD_NAME_SIZE 20

/** Handle implementation. */
struct aloe_thread_rec {
	pthread_t thread; /**< pthread thread handle. */
	void (*run)(struct aloe_thread_rec*); /**< User runnable. */
#if ALOE_THREAD_NAME_SIZE
	char name[ALOE_THREAD_NAME_SIZE]; /**< Identify. */
#endif
};

int aloe_file_nonblock(int fd, int en);
int aloe_so_reuseaddr(int fd);
int aloe_so_keepalive(int fd);

typedef struct {
	void *fmem;
	size_t fmem_sz;
} aloe_mmap_t;

aloe_mmap_t* aloe_mmap_reset(aloe_mmap_t *mm);
int aloe_mmap_file(const char *fn, aloe_mmap_t *mm);
void aloe_munmap(aloe_mmap_t *mm);

/** @} ALOE_LINUX */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _H_ALOE_SYS_LINUX */
