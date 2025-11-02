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
 * @file /algae-bp/package/aloe/compat_linux/sys_linux.cpp
 * @brief sys_linux
 */

#include <aloe/sys.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "../log.h"

#  define log_m(_lvl, _msg, _args...) do { \
	struct timespec ts; \
	struct tm tm; \
	clock_gettime(CLOCK_REALTIME, &ts); \
	localtime_r(&ts.tv_sec, &tm); \
	fprintf(stdout, "[%02ld:%02ld:%02ld.%06ld][%s][%s][#%d]" _msg, \
			(long)tm.tm_hour, (long)tm.tm_min, (long)tm.tm_sec, \
			(long)ts.tv_nsec / 1000, \
			_lvl, __func__, __LINE__, ##_args); \
	fflush(stdout); \
} while(0)
#  define log_d(...) log_m("Debug", __VA_ARGS__)
#  define log_e(...) log_m("ERROR", __VA_ARGS__)

static int snstrncpy(char *buf, size_t buf_sz, const char *name, size_t len) {
	if (len <= 0) len = name ? strlen(name) : 0;
	if (len > 0) {
		if (len >= buf_sz) len = buf_sz - 1;
		memcpy(buf, name, len);
	}
	buf[len] = '\0';
	return len;
}

#define snstrcpy(_p, _s, _n) snstrncpy(_p, _s, _n, -1)

#define aloe_mem_id_stdc 0

extern "C"
void *aloe_malloc(size_t sz) {
	aloe_mem_t *mm = NULL;
	size_t msz = sizeof(*mm) + sz + sizeof(mm);

	if ((mm = (aloe_mem_t*)malloc(msz)) == NULL) return NULL;
	aloe_mem_init(mm, aloe_mem_id_stdc, sz);
	return (void*)(mm + 1);
}

extern "C"
void *aloe_calloc(size_t cnt, size_t sz) {
	void *v = NULL;

	sz *= cnt;
	if ((v = aloe_malloc(sz)) == NULL) {
		return NULL;
	}
	memset(v, 0, sz);
	return v;
}

extern "C"
int aloe_free(void *p) {
	aloe_mem_t *mm;
	int ret = 0;

	if (!p) return 0;
	mm = (aloe_mem_t*)p - 1;
	if ((ret = aloe_mem_check(mm)) < 0) return -1;
	mm->sig = NULL;
	if (mm->id == aloe_mem_id_stdc) {
		free(mm);
	}
	return ret;
}

/** Return microseconds. */
extern "C"
uint64_t aloe_ticks(void) {
	struct timespec tv;

	clock_gettime(CLOCK_MONOTONIC_RAW, &tv);

	return tv.tv_sec * 1000000ul +
			(tv.tv_nsec + 500ul) / 1000ul;
}

extern "C"
int aloe_mutex_lock(pthread_mutex_t *mutex, unsigned long dur_sec,
		unsigned long dur_us) {
	struct timespec tv;

	if (dur_sec == -2ul || dur_us == -2ul) return pthread_mutex_unlock(mutex);
	if (dur_sec == -1ul || dur_us == -1ul) return pthread_mutex_lock(mutex);
	if (dur_sec == 0ul && dur_us == 0ul) return pthread_mutex_trylock(mutex);

	if (clock_gettime(CLOCK_REALTIME, &tv) != 0) return errno;

	dur_us *= 1000ul;
	ALOE_TIMESEC_ADD(tv.tv_sec, tv.tv_nsec, dur_sec, dur_us,
			tv.tv_sec, tv.tv_nsec, 1000000000ul);
	return pthread_mutex_timedlock(mutex, &tv);
}

extern "C"
int aloe_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex,
		unsigned long dur_sec, unsigned long dur_us) {
	struct timespec tv;

	if (dur_sec == -1ul || dur_us == -1ul) return pthread_cond_wait(cond, mutex);

	if (clock_gettime(CLOCK_REALTIME, &tv) != 0) return errno;
	if (dur_sec != 0ul || dur_us != 0ul) {
		dur_us *= 1000ul;
		ALOE_TIMESEC_ADD(tv.tv_sec, tv.tv_nsec, dur_sec, dur_us,
				tv.tv_sec, tv.tv_nsec, 1000000000ul);
	}
	return pthread_cond_timedwait(cond, mutex, &tv);
}

extern "C"
aloe_sem_t* aloe_sem_create(int max, int cnt, const char *name) {
	aloe_sem_t *sem = NULL;

	if ((sem = (aloe_sem_t*)aloe_calloc(1, sizeof(*sem))) == NULL) {
		return NULL;
	}
	if (aloe_sem_init(sem, max, cnt, name) != 0) {
		aloe_free(sem);
		return NULL;
	}
	return sem;
}

extern "C"
int aloe_sem_init(aloe_sem_t *sem, int max, int cnt, const char *name) {
	int r;

	if ((r = pthread_mutex_init(&sem->mutex, NULL)) != 0) {
		return r;
	}
	if ((r = pthread_cond_init(&sem->not_empty, NULL)) != 0) {
		pthread_mutex_destroy(&sem->mutex);
		return r;
	}
	sem->max = max;
	sem->cnt = cnt;
#if defined(ALOE_SEM_NAME_SIZE) && ALOE_SEM_NAME_SIZE > 0
	if (name != sem->name) {
		snstrcpy(sem->name, ALOE_SEM_NAME_SIZE, name);
	}
#endif
	return 0;
}

extern "C"
void aloe_sem_post(aloe_sem_t *sem, char broadcast, char from_isr) {

	(void)from_isr;

	aloe_mutex_lock_infinite(&sem->mutex);
	if ((sem->cnt < sem->max) && ((++(sem->cnt)) == 1)) {
		if (broadcast) pthread_cond_broadcast(&sem->not_empty);
		else pthread_cond_signal(&sem->not_empty);
	}
	pthread_mutex_unlock(&sem->mutex);
}

extern "C"
int aloe_sem_wait(aloe_sem_t *sem, unsigned long dur_sec, unsigned long dur_us,
		char from_isr) {
	int r;
	struct timespec ts, ts_due;

	(void)from_isr;

	aloe_mutex_lock_infinite(&sem->mutex);

#if 0
	// for timedwait
	if (dur_sec != -1ul && dur_us != -1ul) {
		if (clock_gettime(CLOCK_REALTIME, &ts_due) != 0) return errno;
		dur_us *= 1000ul;
		ALOE_TIMESEC_ADD(ts_due.tv_sec, ts_due.tv_nsec, dur_sec, dur_us,
				ts_due.tv_sec, ts_due.tv_nsec, 1000000000ul);
		do {
			if (sem->cnt != 0) {
				sem->cnt--;
				r = 0;
				goto finally;
			}

			wait_remain_time();
		} while (!timeout());
	}
#endif

	while (sem->cnt == 0) {
		if ((r = aloe_cond_wait(&sem->not_empty, &sem->mutex,
				dur_sec, dur_us)) != 0) {
			goto finally;
		}
	}
	sem->cnt--;
	r = 0;
finally:
	pthread_mutex_unlock(&sem->mutex);
	return r;
}

extern "C"
void aloe_sem_destroy(aloe_sem_t *sem) {

}

static void* thread_runner(void *_thd) {
	aloe_thread_t *thd = (aloe_thread_t*)_thd;

	(*thd->run)(thd);
	return NULL;
}

extern "C"
int aloe_thread_run(aloe_thread_t *thd, void(*on_run)(aloe_thread_t*),
		size_t stack, int prio, const char *name) {

	(void)stack;
	(void)prio;

#if defined(ALOE_THREAD_NAME_SIZE) && ALOE_THREAD_NAME_SIZE > 0
	if (name != thd->name) {
		snstrcpy(thd->name, ALOE_THREAD_NAME_SIZE, name);
	}
#else
	(void)name;
#endif
	thd->run = on_run;
	return pthread_create(&thd->thread, NULL, &thread_runner, thd);
}

extern "C"
int aloe_file_nonblock(int fd, int en) {
	int r;

	if ((r = fcntl(fd, F_GETFL, NULL)) == -1) {
		r = errno;
		log_e("Failed to get file flag: %s(%d)\n", strerror(r), r);
		return r;
	}
	if (en) r |= O_NONBLOCK;
	else r &= (~O_NONBLOCK);
	if ((r = fcntl(fd, F_SETFL, r)) != 0) {
		r = errno;
		log_e("Failed to set nonblocking file flag: %s(%d)\n", strerror(r), r);
		return r;
	}
	return 0;
}

extern "C"
int aloe_so_reuseaddr(int fd) {
	int r, opt;

	opt = 1;
	if ((r = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) != 0) {
		r = errno;
		log_e("Failed to set SO_REUSEADDR: %s(%d)\n", strerror(r), r);
		return r;
	}
	return 0;
}

extern "C"
int aloe_so_keepalive(int fd) {
	int r, opt;

	opt = 1;
	if ((r = setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt))) != 0) {
		r = errno;
		log_e("Failed to set SO_KEEPALIVE: %s(%d)\n", strerror(r), r);
		return r;
	}
	return 0;
}

extern "C"
aloe_mmap_t* aloe_mmap_reset(aloe_mmap_t *mm) {
	mm->fmem = MAP_FAILED;
	return mm;
}

extern "C"
int aloe_mmap_file(const char *fn, aloe_mmap_t *mm) {
	int r, fd = -1;
	struct stat st;
	void *fmem = MAP_FAILED;

	if ((fd = open(fn, O_RDWR, 0660)) == -1) {
		r = errno;
		log_e("Failed open %s, %s\n", fn, strerror(r));
		goto finally;
	}

	if ((r = fstat(fd, &st)) != 0) {
		r = errno;
		log_e("Failed get file stat, %s\n", strerror(r));
		goto finally;
	}

	if ((fmem = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED,
			fd, 0)) == MAP_FAILED) {
		r = errno;
		log_e("Failed mmap to %s, %s\n", fn, strerror(r));
		goto finally;
	}
	mm->fmem = fmem;
	fmem = MAP_FAILED;
	mm->fmem_sz = st.st_size;
	r = 0;
finally:
	if (fd != -1) close(fd);
	if (fmem != MAP_FAILED) munmap(fmem, st.st_size);
	return r;
}

extern "C"
void aloe_munmap(aloe_mmap_t *mm) {
	int r;

	if (mm->fmem != MAP_FAILED) {
		if ((r = munmap(mm->fmem, mm->fmem_sz)) != 0) {
			r = errno;
			log_e("Failed munmap\n");
			return;
		}
		aloe_mmap_reset(mm);
	}
}



