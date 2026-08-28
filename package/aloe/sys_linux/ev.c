/* $Id$
 *
 * Copyright (c) 2026, joelai
 * All Rights Reserved
 *
 * SPDX-License-Identifier: MIT
 *
 * @author joelai
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#define _GNU_SOURCE

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <sys/epoll.h>
#include <string.h>

#include <aloe/compat/openbsd/sys/queue.h>
#include <aloe/ev.h>

/** Minimal wait to prevent busy looping, value in milli-seconds. */
#define ALOE_EV_PREVENT_BUSY_SELECT 1

#define ALOE_EV_EPOLL_MAX 32

#define log_m(_lvl, _fmt, _args...) do { \
    char log_m_dtstr[32]; \
    fprintf(stdout, "[%s]" _lvl "[%s][#%d]" _fmt, \
    		aloe_localtime_str(log_m_dtstr, sizeof(log_m_dtstr)), \
			__func__, __LINE__, ##_args); \
    fflush(stdout); \
} while(0)
#define log_d(...) log_m("[DEBUG]", __VA_ARGS__)
#define log_e(...) log_m("[ERROR]", __VA_ARGS__)

/** Formalize time value. */
#define ALOE_TIMESEC_NORM(_sec, _subsec, _subscale) if ((_subsec) >= _subscale) { \
	(_sec) += (_subsec) / _subscale; \
	(_subsec) %= (_subscale); \
}

/** Compare 2 time value. */
#define ALOE_TIMESEC_CMP(_a_sec, _a_subsec, _b_sec, _b_subsec) ( \
	((_a_sec) > (_b_sec)) ? 1 : \
	((_a_sec) < (_b_sec)) ? -1 : \
	((_a_subsec) > (_b_subsec)) ? 1 : \
	((_a_subsec) < (_b_subsec)) ? -1 : \
	0)

/** Subtraction for time value. */
#define ALOE_TIMESEC_SUB(_a_sec, _a_subsec, _b_sec, _b_subsec, _c_sec, \
	_c_subsec, _subscale) \
if ((_a_subsec) < (_b_subsec)) { \
	(_c_sec) = (_a_sec) - (_b_sec) - 1; \
	(_c_subsec) = (_subscale) + (_a_subsec) - (_b_subsec); \
} else { \
	(_c_sec) = (_a_sec) - (_b_sec); \
	(_c_subsec) = (_a_subsec) - (_b_subsec); \
}

/** Addition for time value. */
#define ALOE_TIMESEC_ADD(_a_sec, _a_subsec, _b_sec, _b_subsec, _c_sec, \
	_c_subsec, _subscale) do { \
	(_c_sec) = (_a_sec) + (_b_sec); \
	(_c_subsec) = (_a_subsec) + (_b_subsec); \
	ALOE_TIMESEC_NORM(_c_sec, _c_subsec, _subscale); \
} while(0)

/** Notify event to user. */
typedef struct aloe_ev_ctx_noti_rec {
	int fd;
	aloe_ev_noti_cb_t cb; /**< Callback to user. */
	void *cbarg; /**< Callback argument. */
	unsigned ev_wait; /**< Event to wait. */
	struct timespec due; /**< Monotonic timeout. */
	unsigned ev_noti; /**< Notified event. */
	TAILQ_ENTRY(aloe_ev_ctx_noti_rec) qent;
} aloe_ev_ctx_noti_t;

/** Queue of aloe_ev_noti_t. */
typedef TAILQ_HEAD(aloe_ev_ctx_noti_queue_rec, aloe_ev_ctx_noti_rec) aloe_ev_ctx_noti_queue_t;

/** FD for epoll. */
typedef struct aloe_ev_ctx_fd_rec {
	int fd; /**< FD to monitor. */
	unsigned ep_wait; /**< Last epoll interest mask. */
	unsigned triggered; /**< IO bits from the current epoll_wait. */
	unsigned ep_hup; /**< EPOLLERR/EPOLLHUP in the current epoll_wait. */
	aloe_ev_ctx_noti_queue_t noti_q; /**< Queue of aloe_ev_noti_t for this FD. */
	TAILQ_ENTRY(aloe_ev_ctx_fd_rec) qent;
} aloe_ev_ctx_fd_t;

/** Queue of aloe_ev_fd_t. */
typedef TAILQ_HEAD(aloe_ev_ctx_fd_queue_rec, aloe_ev_ctx_fd_rec) aloe_ev_ctx_fd_queue_t;

/** Information about control flow and running context. */
typedef struct aloe_ev_ctx_rec {
	union {
		unsigned flag;
		struct {
			unsigned with_busy_select: 1;
		};
	};
	int epfd;
	aloe_ev_ctx_fd_queue_t fd_q; /**< Queue to epoll. */
	aloe_ev_ctx_fd_queue_t spare_fd_q; /**< Queue for cached memory. */
	aloe_ev_ctx_noti_queue_t noti_q; /**< Queue for ready to notify. */
	aloe_ev_ctx_noti_queue_t spare_noti_q; /**< Queue for cached memory. */
} aloe_ev_ctx_t;

static const char* aloe_localtime_str(char *dtstr, size_t cap) {
	struct timespec ts;
	struct tm tm_info;
	size_t pos = 0;
	int r;

	clock_gettime(CLOCK_REALTIME, &ts);

	localtime_r(&ts.tv_sec, &tm_info);
	if ((r = strftime(dtstr + pos, cap - pos,
				"%Y-%m-%d %H:%M:%S", &tm_info)) <= 0
			|| r >= cap - pos) {
		goto finally;
	}

	if ((pos += r) >= cap) goto finally;

	if ((r = snprintf(dtstr + pos, cap - pos,
					"%05d", (int)(ts.tv_nsec / 1000))) <= 0
			|| r >= cap - pos) {
		goto finally;
	}

	if ((pos += r) >= cap) goto finally;

finally:
	if (pos >= cap) pos = cap - 1;
	dtstr[pos] = '\0';
	return dtstr;
}

static aloe_ev_ctx_fd_t* fd_q_pop_first(aloe_ev_ctx_fd_queue_t *q) {
	aloe_ev_ctx_fd_t *ev_fd = TAILQ_FIRST(q);

	if (ev_fd) {
		TAILQ_REMOVE(q, ev_fd, qent);
	}
	return ev_fd;
}

static aloe_ev_ctx_fd_t* fd_q_find(aloe_ev_ctx_fd_queue_t *q, int fd) {
	aloe_ev_ctx_fd_t *ev_fd;

	TAILQ_FOREACH(ev_fd, q, qent) {
		if (ev_fd->fd == fd) return ev_fd;
	}
	return NULL;
}

static aloe_ev_ctx_noti_t* noti_q_find(aloe_ev_ctx_noti_queue_t *q,
		const aloe_ev_ctx_noti_t *_ev_noti, char pop) {
	aloe_ev_ctx_noti_t *ev_noti;

	if (!_ev_noti) {
		// return first item with optional removal
		if ((ev_noti = TAILQ_FIRST(q)) && pop) {
			TAILQ_REMOVE(q, ev_noti, qent);
		}
		return ev_noti;
	}

	TAILQ_FOREACH(ev_noti, q, qent) {
		if (ev_noti == _ev_noti) {
			if (pop) {
				TAILQ_REMOVE(q, ev_noti, qent);
			}
			return ev_noti;
		}
	}
	return NULL;
}

static uint32_t to_epoll(unsigned ev_wait) {
	uint32_t e = 0;

	if (ev_wait & aloe_ev_flag_read) e |= EPOLLIN;
	if (ev_wait & aloe_ev_flag_write) e |= EPOLLOUT;
	if (ev_wait & aloe_ev_flag_except) e |= EPOLLPRI;
	return e;
}

static unsigned from_epoll(uint32_t e) {
	unsigned f = 0;

	if (e & (EPOLLIN | EPOLLHUP)) f |= aloe_ev_flag_read;
	if (e & EPOLLOUT) f |= aloe_ev_flag_write;
	if (e & (EPOLLPRI | EPOLLERR | EPOLLHUP)) f |= aloe_ev_flag_except;
	return f;
}

static int fd_ep_refresh(aloe_ev_ctx_t *ctx, aloe_ev_ctx_fd_t *ev_fd) {
	aloe_ev_ctx_noti_t *ev_noti;
	unsigned mask = 0;
	struct epoll_event ev;

	if (ev_fd->fd < 0) return 0;

	TAILQ_FOREACH(ev_noti, &ev_fd->noti_q, qent) {
		mask |= ev_noti->ev_wait;
	}

	if (!mask) {
		if (epoll_ctl(ctx->epfd, EPOLL_CTL_DEL, ev_fd->fd, NULL) != 0
				&& errno != ENOENT && errno != EBADF) {
			int r = errno;
			log_e("epoll DEL fd %d: %s(%d)\n", ev_fd->fd, strerror(r), r);
			return -1;
		}
		ev_fd->ep_wait = 0;
		return 0;
	}

	if (mask == ev_fd->ep_wait) return 0;

	memset(&ev, 0, sizeof(ev));
	ev.events = to_epoll(mask);
	ev.data.ptr = ev_fd;

	if (epoll_ctl(ctx->epfd, EPOLL_CTL_ADD, ev_fd->fd, &ev) != 0) {
		if (errno == EEXIST) {
			if (epoll_ctl(ctx->epfd, EPOLL_CTL_MOD, ev_fd->fd, &ev) != 0) {
				int r = errno;
				log_e("epoll MOD fd %d: %s(%d)\n", ev_fd->fd, strerror(r), r);
				return -1;
			}
		} else {
			int r = errno;
			log_e("epoll ADD fd %d: %s(%d)\n", ev_fd->fd, strerror(r), r);
			return -1;
		}
	}
	ev_fd->ep_wait = mask;
	return 0;
}

static int due_to_timeout_ms(const struct timespec *remain) {
	long ms;

	if (remain->tv_sec > (INT_MAX / 1000) - 1) return INT_MAX;
	ms = remain->tv_sec * 1000L
			+ (remain->tv_nsec + 999999L) / 1000000L;
	if (ms > INT_MAX) return INT_MAX;
	return (int)ms;
}

void* aloe_ev_get(void *_ctx, int fd, aloe_ev_noti_cb_t cb) {
	aloe_ev_ctx_t *ctx = (aloe_ev_ctx_t*)_ctx;
	aloe_ev_ctx_fd_t *ev_fd;
	aloe_ev_ctx_noti_t *ev_noti;

	if (!(ev_fd = fd_q_find(&ctx->fd_q, fd))) return NULL;

	TAILQ_FOREACH(ev_noti, &ev_fd->noti_q, qent) {
		if (ev_noti->cb == cb) return (void*)ev_noti;
	}
	return NULL;
}

void* aloe_ev_put(void *_ctx, int fd, aloe_ev_noti_cb_t cb, void *cbarg,
		unsigned ev_wait, unsigned long sec, unsigned long usec) {
	aloe_ev_ctx_t *ctx = (aloe_ev_ctx_t*)_ctx;
	aloe_ev_ctx_fd_t *ev_fd;
	aloe_ev_ctx_noti_t *ev_noti;
	struct timespec due;
	struct {
		unsigned ev_fd_inq: 1;
		unsigned ev_noti_inq: 1;
	} flag = {0};

	if (sec == ALOE_EV_INFINITE) {
		due.tv_sec = ALOE_EV_INFINITE;
	} else {
		if ((clock_gettime(CLOCK_MONOTONIC, &due)) != 0) {
			int r = errno;
			log_e("monotonic timestamp: %s(%d)\n", strerror(r), r);
			return NULL;
		}
		ALOE_TIMESEC_ADD(due.tv_sec, due.tv_nsec, sec, usec * 1000ul,
		        due.tv_sec, due.tv_nsec, 1000000000ul);
	}

	// get fd group
	if ((ev_fd = fd_q_find(&ctx->fd_q, fd))) {
		// fd group exists
		flag.ev_fd_inq = 1;
	} else if (!(ev_fd = fd_q_pop_first(&ctx->spare_fd_q)) && !(ev_fd =
	        malloc(sizeof(*ev_fd)))) {
		log_e("malloc ev_fd\n");
		return NULL;
	}

	// get noti holder
	if ((ev_noti = noti_q_find(&ctx->spare_noti_q, NULL, 1))) {
		// reuse
		flag.ev_noti_inq = 1;
	} else if (!(ev_noti = malloc(sizeof(*ev_noti)))) {
		log_e("malloc ev_noti\n");
		if (!flag.ev_fd_inq) {
			// since allocated, retain for next use
			TAILQ_INSERT_TAIL(&ctx->spare_fd_q, ev_fd, qent);
		}
		return NULL;
	}

	// new fd group
	if (!flag.ev_fd_inq) {
		TAILQ_INIT(&ev_fd->noti_q);
		TAILQ_INSERT_TAIL(&ctx->fd_q, ev_fd, qent);
		ev_fd->fd = fd;
		ev_fd->ep_wait = 0;
		ev_fd->triggered = 0;
		ev_fd->ep_hup = 0;
	}

	// chain noti holder
	TAILQ_INSERT_TAIL(&ev_fd->noti_q, ev_noti, qent);
	ev_noti->fd = fd;
	ev_noti->cb = cb;
	ev_noti->cbarg = cbarg;
	ev_noti->ev_wait = ev_wait;
	ev_noti->due = due;

	if (fd_ep_refresh(ctx, ev_fd) != 0) {
		TAILQ_REMOVE(&ev_fd->noti_q, ev_noti, qent);
		TAILQ_INSERT_TAIL(&ctx->spare_noti_q, ev_noti, qent);
		if (!flag.ev_fd_inq && TAILQ_EMPTY(&ev_fd->noti_q)) {
			TAILQ_REMOVE(&ctx->fd_q, ev_fd, qent);
			TAILQ_INSERT_TAIL(&ctx->spare_fd_q, ev_fd, qent);
		}
		return NULL;
	}
	return (void*)ev_noti;
}

void aloe_ev_cancel(void *_ctx, void *ev) {
	aloe_ev_ctx_t *ctx = (aloe_ev_ctx_t*)_ctx;
	aloe_ev_ctx_noti_t *ev_noti = (aloe_ev_ctx_noti_t*)ev;
	aloe_ev_ctx_fd_t *ev_fd;

	// find active then move to spare queue
	if ((ev_fd = fd_q_find(&ctx->fd_q, ev_noti->fd))
	        && noti_q_find(&ev_fd->noti_q, ev_noti, 1)) {
		TAILQ_INSERT_TAIL(&ctx->spare_noti_q, ev_noti, qent);
		fd_ep_refresh(ctx, ev_fd);

		// handle the empty fd group to prevent fd group keeping on memory
		if (TAILQ_EMPTY(&ev_fd->noti_q)) {
			TAILQ_REMOVE(&ctx->fd_q, ev_fd, qent);
			TAILQ_INSERT_TAIL(&ctx->spare_fd_q, ev_fd, qent);
		}
		return;
	}

	// might already queued to be notified
	if (noti_q_find(&ctx->noti_q, ev_noti, 1)) {
		TAILQ_INSERT_TAIL(&ctx->spare_noti_q, ev_noti, qent);
		return;
	}
}

int aloe_ev_once(void *_ctx) {
	aloe_ev_ctx_t *ctx = (aloe_ev_ctx_t*)_ctx;
	int r, n, i, has_fd = 0, timeout_ms, nnoti;
	struct epoll_event evs[ALOE_EV_EPOLL_MAX];
	aloe_ev_ctx_noti_t *ev_noti, *ev_noti_safe;
	aloe_ev_ctx_fd_t *ev_fd, *ev_fd_safe;
	struct timespec ts, *due = NULL;

	TAILQ_FOREACH(ev_fd, &ctx->fd_q, qent) {
		ev_fd->triggered = 0;
		ev_fd->ep_hup = 0;
		if (ev_fd->fd >= 0) has_fd = 1;

		TAILQ_FOREACH(ev_noti, &ev_fd->noti_q, qent) {
			// find nearest time
			// user specify ALOE_EV_INFINITE to wait infinite
			if (ev_noti->due.tv_sec != ALOE_EV_INFINITE && (!due
					|| ALOE_TIMESEC_CMP(ev_noti->due.tv_sec,
							ev_noti->due.tv_nsec, due->tv_sec,
							due->tv_nsec) < 0)) {
				due = &ev_noti->due;
			}
		}
	}

	if (due) {
		if ((clock_gettime(CLOCK_MONOTONIC, &ts)) != 0) {
			r = errno;
			log_e("Failed to get time: %s(%d)\n", strerror(r), r);
			return -r;
		}
		if (ALOE_TIMESEC_CMP(ts.tv_sec, ts.tv_nsec,
				due->tv_sec, due->tv_nsec) < 0) {
			ALOE_TIMESEC_SUB(due->tv_sec, due->tv_nsec,
					ts.tv_sec, ts.tv_nsec, ts.tv_sec, ts.tv_nsec, 1000000000ul);
			timeout_ms = due_to_timeout_ms(&ts);
		} else {
			timeout_ms = 0;
		}
	} else if (!has_fd) {
		// no fd and no due: return promptly (busy-prevent may raise to 1ms)
		timeout_ms = 0;
	} else {
		timeout_ms = -1;
	}

#if ALOE_EV_PREVENT_BUSY_SELECT
	// here use a minimal time to prevent busy looping
	if (!ctx->with_busy_select
			&& timeout_ms >= 0
			&& timeout_ms < ALOE_EV_PREVENT_BUSY_SELECT) {
		timeout_ms = ALOE_EV_PREVENT_BUSY_SELECT;
	}
#endif

	if ((n = epoll_wait(ctx->epfd, evs, ALOE_EV_EPOLL_MAX, timeout_ms)) < 0) {
		r = errno;
		if (r == EINTR) return 0;
		log_e("Failed to wait IO: %s(%d)\n", strerror(r), r);
		return -r;
	}

	for (i = 0; i < n; i++) {
		ev_fd = (aloe_ev_ctx_fd_t*)evs[i].data.ptr;
		if (!ev_fd) continue;
		ev_fd->triggered = from_epoll(evs[i].events);
		if (evs[i].events & (EPOLLERR | EPOLLHUP)) ev_fd->ep_hup = 1;
	}

	// used for checking timeout
	if ((r = clock_gettime(CLOCK_MONOTONIC, &ts)) != 0) {
		r = errno;
		log_e("Failed to get time: %s(%d)\n", strerror(r), r);
		return -r;
	}

	TAILQ_FOREACH_SAFE(ev_fd, &ctx->fd_q, qent, ev_fd_safe) {
		unsigned triggered = ev_fd->triggered;

		// move out the triggered event pervent user change the list
		TAILQ_FOREACH_SAFE(ev_noti, &ev_fd->noti_q, qent, ev_noti_safe) {

			// save triggered event for user callback argument
			// for the io not triggered, maybe timeout
			if (!(ev_noti->ev_noti = (triggered & ev_noti->ev_wait))) {
				if (ev_fd->ep_hup) {
					ev_noti->ev_noti |= aloe_ev_flag_except;
				} else if (ev_noti->due.tv_sec != ALOE_EV_INFINITE
						&& ALOE_TIMESEC_CMP(ev_noti->due.tv_sec, ev_noti->due.tv_nsec,
								ts.tv_sec, ts.tv_nsec) <= 0) {
					// timeout
					ev_noti->ev_noti |= aloe_ev_flag_time;
				}
			}

			// move out the triggered events
			if (ev_noti->ev_noti) {
				TAILQ_REMOVE(&ev_fd->noti_q, ev_noti, qent);
				TAILQ_INSERT_TAIL(&ctx->noti_q, ev_noti, qent);
			}
		}

		fd_ep_refresh(ctx, ev_fd);

		// handle the empty fd group to prevent fd group keeping on memory
		if (TAILQ_EMPTY(&ev_fd->noti_q)) {
			TAILQ_REMOVE(&ctx->fd_q, ev_fd, qent);
			TAILQ_INSERT_TAIL(&ctx->spare_fd_q, ev_fd, qent);
		}
	}

	// user callback
	nnoti = 0;
	while ((ev_noti = TAILQ_FIRST(&ctx->noti_q))) {
		int fd = ev_noti->fd;
		aloe_ev_noti_cb_t cb = ev_noti->cb;
		void *cbarg = ev_noti->cbarg;
		unsigned triggered = ev_noti->ev_noti;

		nnoti++;
		TAILQ_REMOVE(&ctx->noti_q, ev_noti, qent);
		TAILQ_INSERT_TAIL(&ctx->spare_noti_q, ev_noti, qent);
		(*cb)(fd, triggered, cbarg);
	}

	// return the triggered event count
	return nnoti;
}

void* aloe_ev_init(unsigned flag) {
	aloe_ev_ctx_t *ctx;

	if (!(ctx = malloc(sizeof(*ctx)))) {
		log_e("malloc ev ctx\n");
		return NULL;
	}
	ctx->with_busy_select = !!flag;
	if ((ctx->epfd = epoll_create1(EPOLL_CLOEXEC)) < 0) {
		int r = errno;
		log_e("epoll_create1: %s(%d)\n", strerror(r), r);
		free(ctx);
		return NULL;
	}
	TAILQ_INIT(&ctx->fd_q);
	TAILQ_INIT(&ctx->spare_fd_q);
	TAILQ_INIT(&ctx->noti_q);
	TAILQ_INIT(&ctx->spare_noti_q);
	return (void*)ctx;
}

void aloe_ev_destroy(void *_ctx) {
	aloe_ev_ctx_t *ctx = (aloe_ev_ctx_t*)_ctx;
	aloe_ev_ctx_noti_t *ev_noti;
	aloe_ev_ctx_fd_t *ev_fd;

	while ((ev_fd = TAILQ_FIRST(&ctx->fd_q))) {
		TAILQ_REMOVE(&ctx->fd_q, ev_fd, qent);
		while ((ev_noti = TAILQ_FIRST(&ev_fd->noti_q))) {
			TAILQ_REMOVE(&ev_fd->noti_q, ev_noti, qent);
			free(ev_noti);
		}
		free(ev_fd);
	}
	while ((ev_fd = TAILQ_FIRST(&ctx->spare_fd_q))) {
		TAILQ_REMOVE(&ctx->spare_fd_q, ev_fd, qent);
		free(ev_fd);
	}
	while ((ev_noti = TAILQ_FIRST(&ctx->noti_q))) {
		TAILQ_REMOVE(&ctx->noti_q, ev_noti, qent);
		free(ev_noti);
	}
	while ((ev_noti = TAILQ_FIRST(&ctx->spare_noti_q))) {
		TAILQ_REMOVE(&ctx->spare_noti_q, ev_noti, qent);
		free(ev_noti);
	}
	if (ctx->epfd >= 0) close(ctx->epfd);
	free(ctx);
}
