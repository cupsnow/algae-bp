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
 * @file aloe/sys.h
 *
 */

#ifndef _H_ALOE_SYS
#define _H_ALOE_SYS

/**
 * @defgroup ALOE_SEM Semaphore
 * @ingroup ALOE_SYS
 *
 * @defgroup ALOE_THREAD Thread
 * @ingroup ALOE_SYS
 *
 */

#if defined(ALOE_SYS_AMEBA_DX)
#  include "sys_ameba_dx.h"
#elif defined(ALOE_SYS_AMEBA_LP)
#  include "sys_ameba_lp.h"
#elif defined(ALOE_SYS_LINUX)
#  include "sys_linux.h"
#endif

#include "util.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup ALOE_SYS
 *
 * Include `<aloe/sys.h>` instead of system implementation `sys_xxx.h`
 *
 * System should implement the macros.
 *
 * -  ALOE_TICK2MS(), ALOE_MS2TICK()
 * -  ALOE_TICK2US(), ALOE_US2TICK()
 *
 * @{
 */

#ifndef aloe_endl
#  define aloe_endl aloe_endl_msw
#endif

/** Get system timestamp.
 *
 * The timestamp unit depends on the system.
 *
 * See also ALOE_TICK2MS(), ALOE_MS2TICK(), ALOE_TICK2US(), ALOE_US2TICK()
 */
unsigned long aloe_ticks(void);

typedef struct __attribute__((packed)) aloe_mem_rec {
	const int *sig;
	int id;
	size_t sz;
	// char buf[sz];
	// struct aloe_mem_rec *rec;
} aloe_mem_t;

void aloe_mem_init(aloe_mem_t *mm, int id, size_t sz);
int aloe_mem_check(aloe_mem_t *mm);

extern const int aloe_mem_sig;

/** Allocate memory. */
void *aloe_malloc(size_t sz);

/** Allocate zero filled memory. */
void *aloe_calloc(size_t cnt, size_t sz);

/** Release memory. */
int aloe_free(void *p);

/** @addtogroup ALOE_SEM
 * @{
 */

/** Handle. */
typedef struct aloe_sem_rec aloe_sem_t;

/** Create semaphore. */
aloe_sem_t* aloe_sem_create(int max, int cnt, const char *name);

/** Initialized handle. */
int aloe_sem_init(aloe_sem_t *sem, int max, int cnt, const char *name);

/** Unlock a semaphore. */
void aloe_sem_post(aloe_sem_t *sem, char broadcast, char from_isr);

/** Lock a semaphore.
 *
 * Duration -1ul or -2ul are invalid.
 */
int aloe_sem_wait(aloe_sem_t *sem, unsigned long dur_sec, unsigned long dur_us,
		char from_isr);

/** Destroy created semaphore. */
void aloe_sem_destroy(aloe_sem_t *sem);

/** @} ALOE_SEM */

/** @addtogroup ALOE_THREAD
 * @{
 */

/** Handle. */
typedef struct aloe_thread_rec aloe_thread_t;

/** Start thread. */
int aloe_thread_run(aloe_thread_t*, void(*)(aloe_thread_t*), size_t stack,
		int prio, const char *name);

#ifndef ALOE_THREAD_SLEEP
/** Hold current thread execution. */
#  define ALOE_THREAD_SLEEP(_ms) usleep(_ms * 1000)
#endif

/** @} ALOE_THREAD */

/** @} ALOE_SYS */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _H_ALOE_SYS */
