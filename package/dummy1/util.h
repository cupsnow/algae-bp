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

#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

#define aloe_cr '\r' // 0xd
#define aloe_lf '\n' // 0xa
#define aloe_endl_msw "\r\n"
#define aloe_endl_unix "\n"

#define aloe_ansi_clear "\033[0m"
#define aloe_ansi_green "\033[0;32m"

#define stringify(_s) # _s
#define stringify2(_s) stringify(_s)
#define concat(_s1, _s2) _s1 ## _s2
#define concat2(_s1, _s2) concat(_s1, _s2)

#define _concat3(_s1, _s2, _s3) _s1 ## _s2 ## _s3
#define concat3(_s1, _s2, _s3) _concat3(_s1, _s2, _s3)

#define align2(_n, _a) (((_n) + ((_a) - 1)) & ~(((_a) - 1)))
#define arraysize(_a) (sizeof(_a) / sizeof((_a)[0]))
#define min(_a, _b) ((_a) <= (_b) ? (_a) : (_b))
#define max(_a, _b) ((_a) >= (_b) ? (_a) : (_b))
#define mem_gc(_v) if (_v) { free(_v); _v = NULL; }
#define fd_gc(_fd) if (_fd != -1) { close(_fd); _fd = -1; }

/** Subtraction for time value. */
#define timesec_sub(_a_sec, _a_subsec, _b_sec, _b_subsec, _c_sec, \
		_c_subsec, _subscale) \
	if ((_a_subsec) < (_b_subsec)) { \
		(_c_sec) = (_a_sec) - (_b_sec) - 1; \
		(_c_subsec) = (_subscale) + (_a_subsec) - (_b_subsec); \
	} else { \
		(_c_sec) = (_a_sec) - (_b_sec); \
		(_c_subsec) = (_a_subsec) - (_b_subsec); \
	}

#ifdef __cplusplus
extern "C" {
#endif

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

extern const char *aloe_str_sep; // " \r\n\t"

#define _aloe_cli_tok(_cli, _argc, _argv, _sep, _argmax) if ( \
		(((_argc) = 0) < (_argmax)) && \
		((_argv)[_argc] = strtok_r(_cli, _sep, &(_cli)))) { \
	for ((_argc)++; ((_argc) < (_argmax)) && \
			((_argv)[_argc] = strtok_r(NULL, _sep, &(_cli))); \
			(_argc)++); \
}

typedef struct {
	unsigned major, minor, release, code;
} algae_version_t;

const char* algae_version(algae_version_t *ver);

/** Parse string to tokens.
 *
 * @param cli Input string
 * @param argc Input the max count of argv, output result count of argv
 * @param argv
 * @param sep
 * @return
 */
int aloe_cli_tok(char *cli, int *argc, const char **argv, const char *sep);

int ping_poll(const char *ping_svc, int cdt);
double calc_factorial(int n);
double calc_euler(int n);

/**
 *
 * simplify est. if pass uint32 to dur_us, max within 7 seconds
 * (2**32 - 1)/10e6 / 60 ~= 7.158278825
 *
 * @param mutex
 * @param sw
 * @param dur_us
 * @return
 */
int mutex_lock(pthread_mutex_t *mutex, char sw, unsigned long dur_us);

int rand_data(void *data, size_t data_sz);
int frun(char *cli_buf, size_t cli_buf_len, const char *fmt, ...);

typedef struct {
	int shm_fd;
	char shared_name[128];
	size_t shared_mem_len;
} shm_t;

typedef struct {
	shm_t shm;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	int nrf_programming, nrf_programming_eno;
} shnoti_t;

int mutex_init(pthread_mutex_t *mutex, char shared);
int cond_init(pthread_cond_t *cond, char shared);
void shm_destroy(shm_t *shm);
shm_t* shm_create(const char *shm_name, size_t extra_len);
void shnoti_destroy(shnoti_t *shnoti);
shnoti_t* shnoti_create(const char *shm_name, size_t extra_len);

int read_file(const char *fn, void *buf, size_t buf_len);
int write_file(const char *fn, unsigned mode, const void *data, size_t buf_len);

int read_file2(const char *fn, void *buf, size_t buf_len,
		int (*on_read)(const void*, size_t, void*), void *on_read_arg);

void hexstring(const void *data, size_t sz, void *str, const char *sep,
		size_t sep_len);

typedef struct {
	void *fmem;
	size_t fmem_sz;
} aloe_mmap_t;

aloe_mmap_t* aloe_mmap_reset(aloe_mmap_t *mm);
int aloe_mmap_file(const char *fn, aloe_mmap_t *mm);
void aloe_munmap(aloe_mmap_t *mm);

int cli_input(char *buf, size_t buf_sz, FILE *fp);

#ifdef __cplusplus
} /* extern "C" */
#endif




#endif /* UTIL_H_ */
