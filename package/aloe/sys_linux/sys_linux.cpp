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
 * @file /algae-bp/package/aloe/compat_linux/sys_linux.cpp
 * @brief sys_linux
 */

#include <aloe/sys.h>
#include "../log.h"

extern "C"
int aloe_mutex_lock(pthread_mutex_t *mutex, unsigned long dur_sec,
		unsigned long dur_us) {
	struct timespec tv;

	if (dur_sec == -2ul || dur_us == -2ul) return pthread_mutex_unlock(mutex);
	if (dur_sec == -1ul || dur_us == -1ul) return pthread_mutex_lock(mutex);
	if (dur_sec == 0ul && dur_us == 0ul) return pthread_mutex_trylock(mutex);

	if (clock_gettime(CLOCK_REALTIME, &tv) != 0) return errno;
	tv.tv_sec += dur_sec;

	tv.tv_nsec += dur_us * 1000ul;
	if ((tv.tv_nsec += dur_us * 1000ul) >= 1000000000ul) {
		tv.tv_sec += tv.tv_nsec / 1000000000ul;
		tv.tv_nsec %= 1000000000ul;
	}
	return pthread_mutex_timedlock(mutex, &tv);
}

extern "C"
unsigned long aloe_ticks(void) {
	struct timespec tv;

	clock_gettime(CLOCK_MONOTONIC, &tv);

	return tv.tv_sec * 1000000ul +
			(tv.tv_nsec + 500ul) / 1000ul;
}

extern "C"
aloe_sem_t* aloe_sem_create(int max, int cnt, const char *name) {

}

extern "C"
int aloe_sem_init(aloe_sem_t *sem, int max, int cnt, const char *name) {

}

extern "C"
void aloe_sem_post(aloe_sem_t *sem, char broadcast) {

}

extern "C"
int aloe_sem_wait(aloe_sem_t *sem, unsigned long dur_sec, unsigned long dur_us) {

}

extern "C"
void aloe_sem_destroy(aloe_sem_t *sem) {

}
