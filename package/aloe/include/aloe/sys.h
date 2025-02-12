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
 * @brief sys
 */

#ifndef _H_ALOE_SYS
#define _H_ALOE_SYS

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
 * @{
 */

#ifndef aloe_endl
#  define aloe_endl aloe_endl_msw
#endif

/** Get system timestamp.
 *
 * The timestamp unit depends on the system.
 *
 * System may implement the macros convert unit between timestamp and realtime.
 *
 * ALOE_TICK2MS()
 * ALOE_MS2TICK()
 *
 * ALOE_TICK2US()
 * ALOE_US2TICK()
 */
unsigned long aloe_ticks(void);

typedef struct aloe_sem_rec aloe_sem_t;

aloe_sem_t* aloe_sem_create(int max, int cnt, const char *name);
int aloe_sem_init(aloe_sem_t *sem, int max, int cnt, const char *name);

/** Unlock a semaphore. */
void aloe_sem_post(aloe_sem_t *sem, char broadcast);

/** Lock a semaphore.
 *
 * Duration -1ul or -2ul are invalid.
 */
int aloe_sem_wait(aloe_sem_t *sem, unsigned long dur_sec, unsigned long dur_us);
void aloe_sem_destroy(aloe_sem_t *sem);

/** @} ALOE_SYS */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _H_ALOE_SYS */
