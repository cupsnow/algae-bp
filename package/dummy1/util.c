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

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "priv.h"

aloe_buf_t* aloe_buf_clear(aloe_buf_t *buf) {
	_aloe_buf_clear(buf);
	return buf;
}

aloe_buf_t* aloe_buf_flip(aloe_buf_t *buf) {
	_aloe_buf_flip(buf);
	return buf;
}

const char* algae_version(algae_version_t *ver) {
	static char ver_str[64] = {};
	int r;

	if (!ver_str[0]) {
		if ((r = snprintf(ver_str, sizeof(ver_str), "%s%d.%d.%d %d\n",
				"algae v",
				ALGAE_VER_MAJOR, ALGAE_VER_MINOR, ALGAE_VER_RELEASE,
				ALGAE_VER_CODE)) <= 0
				|| r >= sizeof(ver_str)) {
			log_e("Insufficient buf\n");
			ver_str[0] = '\0';
		}
	}
	if (ver) {
		ver->major = ALGAE_VER_MAJOR;
		ver->minor = ALGAE_VER_MINOR;
		ver->release = ALGAE_VER_RELEASE;
		ver->code = ALGAE_VER_CODE;
	}
	return ver_str;
}

static const char _aloe_str_sep[] = " \r\n\t";
const char *aloe_str_sep = _aloe_str_sep;

int aloe_cli_tok(char *cli, int *argc, const char **argv, const char *sep) {
	int argmax = *argc;

	if (!sep) sep = aloe_str_sep;
	_aloe_cli_tok(cli, *argc, argv, sep, argmax);
	return 0;
}

double calc_factorial(int n) { // Max n = 170
	if (n == 1) {
		return 1;
	}
	return n * calc_factorial(n - 1);
}

double calc_euler(int n) {
	if (n == 0) {
		return 1;
	}
	return (1 / calc_factorial(n) + calc_euler(n - 1));
}

/* Set in lv_conf.h as `LV_TICK_CUSTOM_SYS_TIME_EXPR` */
__attribute__((weak))
uint32_t custom_tick_get(void);
uint32_t custom_tick_get(void) {
	static uint64_t start_ms = 0;
	if (start_ms == 0) {
		struct timeval tv_start;
		gettimeofday(&tv_start, NULL);
		start_ms = (tv_start.tv_sec * 1000000 + tv_start.tv_usec) / 1000;
	}

	struct timeval tv_now;
	gettimeofday(&tv_now, NULL);
	uint64_t now_ms;
	now_ms = (tv_now.tv_sec * 1000000 + tv_now.tv_usec) / 1000;

	uint32_t time_ms = now_ms - start_ms;
	return time_ms;
}

int ping_poll(const char *ping_svc, int cdt) {
	int r;
	char cli_buf[300];

	if ((r = snprintf(cli_buf, sizeof(cli_buf), "ping -c1 %s",
			ping_svc)) <= 0 || r >= sizeof(cli_buf)) {
		log_e("Failed build cli\n");
		return -1;
	}

	r = -1;
	while (cdt > 0) {
		log_d("try ping %d more\n", cdt);
		if ((r = system(cli_buf)) == 0) {
			break;
		}
		if (--cdt > 0) sleep(1);
	}
	return r;
}

int mutex_lock(pthread_mutex_t *mutex, char sw, unsigned long dur_us) {
	struct timespec tv;

	if (sw <= 0) {
		return pthread_mutex_unlock(mutex);
	}
	if (dur_us == 0ul) return pthread_mutex_trylock(mutex);
	if (dur_us == -1ul) return pthread_mutex_lock(mutex);
	if (clock_gettime(CLOCK_REALTIME, &tv) != 0) return -1;
	tv.tv_sec += dur_us / 1000000ul;
	tv.tv_nsec += (dur_us % 1000000ul) * 1000ul;
	return pthread_mutex_timedlock(mutex, &tv);
}

int rand_data(void *data, size_t data_sz) {
	int i;

	srandom(time(NULL));
	for (i = 0; i < data_sz; i++) {
		((uint8_t*)data)[i] = random() & 0xff;
	}
	return i;
}

int frun(char *cli_buf, size_t cli_buf_len, const char *fmt, ...) {
	int r;
	va_list va;

	va_start(va, fmt);
	r = vsnprintf(cli_buf, cli_buf_len, fmt, va);
	va_end(va);
	if (r <= 0 || r >= cli_buf_len) {
		log_e("Insufficient buffer to compose cli command %s ...\n", fmt);
		return -1;
	}

	log_d("Execute: %s\n", cli_buf);

	if ((r = system(cli_buf)) != 0) {
		log_e("Failed run %s\n", cli_buf);
	}
	return r;
}

int mutex_init(pthread_mutex_t *mutex, char shared) {
	int r;
	pthread_mutexattr_t mutex_attr;

	if ((r = pthread_mutexattr_init(&mutex_attr)) != 0) {
		log_e("Failed init mutex attr\n");
		return r;
	}

	if (shared && (r = pthread_mutexattr_setpshared(&mutex_attr,
			PTHREAD_PROCESS_SHARED)) != 0) {
		log_e("Failed set mutex attr process shared\n");
		pthread_mutexattr_destroy(&mutex_attr);
		return r;
	}

	if ((r = pthread_mutex_init(mutex, &mutex_attr)) != 0) {
		log_e("Failed init mutex\n");
		pthread_mutexattr_destroy(&mutex_attr);
		return r;
	}
	return 0;
}

int cond_init(pthread_cond_t *cond, char shared) {
	int r;
	pthread_condattr_t cond_attr;

	if ((r = pthread_condattr_init(&cond_attr)) != 0) {
		log_e("Failed init cond attr\n");
		return r;
	}

	if (shared && (r = pthread_condattr_setpshared(&cond_attr,
			PTHREAD_PROCESS_SHARED)) != 0) {
		log_e("Failed set cond attr process shared\n");
		pthread_condattr_destroy(&cond_attr);
		return r;
	}

	if ((r = pthread_cond_init(cond, &cond_attr)) != 0) {
		log_e("Failed init cond\n");
		pthread_condattr_destroy(&cond_attr);
		return r;
	}
	return 0;
}

void shm_destroy(shm_t *shm) {
	munmap(shm, shm->shared_mem_len);
	if (shm->shm_fd != -1) {
		close(shm->shm_fd);
		shm_unlink(shm->shared_name);
	}
}

shm_t* shm_create(const char *shm_name, size_t extra_len) {
	int r, shm_fd = -1;
	shm_t *shm = MAP_FAILED;
	size_t shared_name_len = strlen(shm_name);
	size_t shared_mem_len = sizeof(*shm) + extra_len;

	if (shared_name_len >= sizeof(shm->shared_name)) {
		log_e("shared name too long\n");
		return NULL;
	}

	if ((shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666)) == -1) {
		log_e("open shared mem\n");
		return NULL;
	}

    if ((r = ftruncate(shm_fd, shared_mem_len)) != 0) {
		log_e("set shared mem size\n");
		close(shm_fd);
		shm_unlink(shm_name);
		return NULL;
    }

	if ((shm = (shm_t*)mmap(NULL, shared_mem_len,
			PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0)) == MAP_FAILED) {
		log_e("mamp shared cond\n");
		close(shm_fd);
		shm_unlink(shm_name);
		return NULL;
	}

	shm->shm_fd = shm_fd;
	memcpy(shm->shared_name, shm_name, shared_name_len);
	shm->shared_name[shared_name_len] = '\0';
	shm->shared_mem_len = shared_mem_len;
	return shm;
}

void shnoti_destroy(shnoti_t *shnoti) {
	pthread_mutex_destroy(&shnoti->mutex);
	pthread_cond_destroy(&shnoti->cond);
	shm_destroy((shm_t*)shnoti);
}

shnoti_t* shnoti_create(const char *shm_name, size_t extra_len) {
	int r;
	shnoti_t *shnoti;
	size_t shm_extra_len = sizeof(*shnoti) - sizeof(shnoti->shm) + extra_len;

	if ((shnoti = (shnoti_t*)shm_create(shm_name, shm_extra_len)) == NULL) {
		log_e("Failed create shm\n");
		return NULL;
	}

	if ((r = mutex_init(&shnoti->mutex, 1)) != 0) {
		log_e("Failed create mutex\n");
		shm_destroy((shm_t*)shnoti);
		return NULL;
	}

	if ((r = cond_init(&shnoti->cond, 1)) != 0) {
		log_e("Failed create cond\n");
		pthread_mutex_destroy(&shnoti->mutex);
		shm_destroy((shm_t*)shnoti);
		return NULL;
	}
	return shnoti;
}

int read_file2(const char *fn, void *buf, size_t buf_len,
		int (*cb)(const void*, size_t, void*), void *cbarg) {
	int r, fd = -1;
	struct stat st;
	size_t acc_len = 0, rw_len;

	if ((r = stat(fn, &st)) != 0) {
		r = errno;
		log_e("stat %s %s\n", fn, strerror(r));
		r = -1;
		goto finally;
	}

	if ((fd = open(fn, O_RDONLY, 0440)) == -1) {
		r = errno;
		log_e("open %s %s\n", fn, strerror(r));
		r = -1;
		goto finally;
	}
	while (acc_len < st.st_size) {
		r = read(fd, buf, buf_len);
		if (r < 0) {
			r = errno;
			if (r == EINTR) {
				usleep(330);
				continue;
			}
			log_e("read %s %s\n", fn, strerror(r));
			r = -1;
			goto finally;
		}
		if (r == 0) {
			log_d("read 0, supposed eof\n");
			break;
		}
		rw_len = r;
		acc_len += r;

		if (cb && (r = cb(buf, rw_len, cbarg)) != rw_len) {
			break;
		}
	}
	*(char*)buf = '\0';
	r = 0;
finally:
	if (fd != -1) fd_gc(fd);
	return r == 0 ? acc_len : r;
}

int read_file(const char *fn, void *buf, size_t buf_len) {
	int r, fd = -1;
	struct stat st;
	size_t rw_len = 0;

	if ((r = stat(fn, &st)) != 0) {
		r = errno;
		log_e("stat %s %s\n", fn, strerror(r));
		r = -1;
		goto finally;
	}

	// buf will append trailing zero
	if (st.st_size >= buf_len) {
		log_e("Insufficient buf, %d/%d\n", (int)(st.st_size + 1), (int)buf_len);
		r = -1;
		goto finally;
	}
	if ((fd = open(fn, O_RDONLY, 0440)) == -1) {
		r = errno;
		log_e("open %s %s\n", fn, strerror(r));
		r = -1;
		goto finally;
	}
	while (rw_len < st.st_size) {
		r = read(fd, buf, st.st_size - rw_len);
		if (r < 0) {
			r = errno;
			if (r == EINTR) {
				usleep(330);
				continue;
			}
			log_e("read %s %s\n", fn, strerror(r));
			r = -1;
			goto finally;
		}
		if (r == 0) {
			log_d("read 0, supposed eof\n");
			break;
		}
		rw_len += r;
		buf = (char*)buf + r;
	}
	*(char*)buf = '\0';
	r = 0;
finally:
	if (fd != -1) fd_gc(fd);
	return r == 0 ? rw_len : r;
}

int write_file(const char *fn, unsigned mode, const void *data,
		size_t buf_len) {
	int r, fd = -1;
	size_t rw_len = 0;

	if (mode == 0) mode = O_CREAT | O_WRONLY | O_TRUNC;
	if ((fd = open(fn, mode, 0664)) == -1) {
		r = errno;
		log_e("open %s %s\n", fn, strerror(r));
		r = -1;
		goto finally;
	}
	while (rw_len < buf_len) {
		r = write(fd, data, buf_len - rw_len);
		if (r < 0) {
			r = errno;
			if (r == EINTR) {
				usleep(330);
				continue;
			}
			log_e("write %s %s\n", fn, strerror(r));
			r = -1;
			goto finally;
		}
		if (r == 0) {
			log_d("write 0, supposed incomplete\n");
			break;
		}
		rw_len += r;
		data = (char*)data + r;
	}
	r = 0;
finally:
	if (fd != -1) fd_gc(fd);
	return r == 0 ? rw_len : r;
}

void hexstring(const void *data, size_t sz, void *str, const char *sep,
		size_t sep_len) {
	static char hex_chars[] = "0123456789abcdef";
	int pos;
	char *tgt = (char*)str;

	for (pos = 0; pos < sz; pos++) {
		unsigned int c = ((uint8_t*)data)[pos];

		*tgt++ = hex_chars[(c >> 4) & 0xf];
		*tgt++ = hex_chars[c & 0xf];
		if (sep) {
			memcpy(tgt, sep, sep_len);
			tgt += sep_len;
		}
	}
	*tgt = '\0';
	return;
}

aloe_mmap_t* aloe_mmap_reset(aloe_mmap_t *mm) {
	mm->fmem = MAP_FAILED;
	return mm;
}

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

int cli_input(char *buf, size_t buf_sz, FILE *fp) {
	size_t buf_pos = 0;

	if (!fp) fp = stdin;

	while (buf_pos < buf_sz) {
		int c = getc(fp);

		if (c == EOF) break;
		if (c == aloe_lf) {
			if (buf_pos > 0 && buf[buf_pos - 1] == aloe_cr) buf_pos--;
			break;
		}
		buf[buf_pos++] = c;
	}
	if (buf_pos >= buf_sz) buf_pos = buf_sz - 1;
	buf[buf_pos] = '\0';
	return buf_pos;
}
