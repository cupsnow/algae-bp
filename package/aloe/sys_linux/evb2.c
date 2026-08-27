/* $Id$
 *
 * This file is part of the project esh-ws
 *
 * Aug 26, 2026
 *
 * @author joelai
 *
 * @file /esh-ws/package/esh-tester/compat/aloe_evb2.c
 * @brief aloe_evb2
 */

#define _GNU_SOURCE
#include <aloe/evb2.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <pthread.h>
#include <sys/epoll.h>

#define log_m(_lvl, _msg, _args...) do { \
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
#define log_d(...) log_m("Debug", __VA_ARGS__)
#define log_e(...) log_m("ERROR", __VA_ARGS__)

enum {
	/** internal used flags. */
	EVB2_FLAG_ONESHOT = 1 << 8,
	EVB2_FLAG_DEAD = 1 << 9,
	EVB2_FLAG_INTERNAL = 1 << 10,
};

#define EVB2_EPOLL_MAX 32
/** Floor when re-arming a persistent 0ms timer, to avoid a busy loop. */
#define EVB2_MIN_REARM_MS 1

/* Opaque structures */
typedef struct aloe_evb2_ctx evb2_t;
typedef struct aloe_evb2_handle evb2_handle_t;

typedef struct qent {
	struct qent *next;
} qent_t;

struct aloe_evb2_handle {
	aloe_evb2_cb_t cb;
	void *user_data;
	int fd;
	uint64_t timeout_ms;
	uint64_t timeout_at;
	evb2_t *evb;
	unsigned flag; /* requested ALOE_EVB2_FLAG_* */
	unsigned flag2; /* EVB2_FLAG_* internal */
	unsigned noti; /* triggered flags when on ready_list */
	qent_t fdmon_ent; /* fdmon, ready, or async pending */
	qent_t timer_ent;
};

struct aloe_evb2_ctx {
	int epfd;
	int async_pipe[2];
	qent_t *fdmon_list;
	qent_t *timer_list;
	qent_t *ready_list;

	pthread_mutex_t async_lock;
	qent_t *async_list; /* pending oneshot timers from any thread */

	evb2_handle_t *notifying;
	int stopping;
	int in_run;
	int finalized;
};

#define handle_of(_q, _m) \
	((evb2_handle_t*)((char*)(_q) - offsetof(evb2_handle_t, _m)))

static void async_handler(int fd, unsigned flag, void *user_data);
static int epoll_refresh(evb2_t *evb, int fd);
static void evb2_finalize(evb2_t *evb);
static void evb2_notify_cancel(evb2_handle_t *h);
static void evb2_install_async(evb2_t *evb);

static uint64_t evb2_now_ms(void) {
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		int r = errno;
		log_e("clock_gettime: %s(%d)\n", strerror(r), r);
		return 0;
	}
	return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000u);
}

static void qent_insert_tail(qent_t **head, qent_t *e) {
	qent_t **pp;

	e->next = NULL;
	pp = head;
	while (*pp) pp = &(*pp)->next;
	*pp = e;
}

static int qent_remove(qent_t **head, qent_t *e) {
	qent_t **pp = head;

	while (*pp) {
		if (*pp == e) {
			*pp = e->next;
			e->next = NULL;
			return 1;
		}
		pp = &(*pp)->next;
	}
	return 0;
}

static int qent_contains(qent_t *head, qent_t *e) {
	for (; head; head = head->next) {
		if (head == e) return 1;
	}
	return 0;
}

static qent_t* qent_pop(qent_t **head) {
	qent_t *e = *head;

	if (e) {
		*head = e->next;
		e->next = NULL;
	}
	return e;
}

static void timer_insert(evb2_t *evb, evb2_handle_t *h) {
	qent_t **pp = &evb->timer_list;

	while (*pp) {
		evb2_handle_t *cur = handle_of(*pp, timer_ent);
		if (h->timeout_at < cur->timeout_at) break;
		pp = &(*pp)->next;
	}
	h->timer_ent.next = *pp;
	*pp = &h->timer_ent;
}

static void evb2_set_timeout(evb2_handle_t *h, uint64_t now) {
	if (h->timeout_ms == ALOE_EVB2_INFINITE
			|| h->timeout_ms > UINT64_MAX - now) {
		h->timeout_at = UINT64_MAX;
	} else {
		h->timeout_at = now + h->timeout_ms;
	}
}

static void evb2_arm_timer(evb2_t *evb, evb2_handle_t *h) {
	uint64_t now = evb2_now_ms();

	qent_remove(&evb->timer_list, &h->timer_ent);
	if (h->timeout_ms == 0) {
		h->timeout_at = (EVB2_MIN_REARM_MS > UINT64_MAX - now)
				? UINT64_MAX : now + EVB2_MIN_REARM_MS;
	} else {
		evb2_set_timeout(h, now);
	}
	if (h->timeout_at != UINT64_MAX) timer_insert(evb, h);
}

static void handle_detach(evb2_t *evb, evb2_handle_t *h) {
	qent_remove(&evb->fdmon_list, &h->fdmon_ent);
	qent_remove(&evb->timer_list, &h->timer_ent);
	qent_remove(&evb->ready_list, &h->fdmon_ent);
}

static void evb2_wake(evb2_t *evb) {
	char c = 1;
	ssize_t n;

	if (!evb || evb->async_pipe[1] < 0) return;
	n = write(evb->async_pipe[1], &c, 1);
	if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
		int r = errno;
		log_e("async wake: %s(%d)\n", strerror(r), r);
	}
}

static uint32_t to_epoll(unsigned ev_req) {
	uint32_t e = 0;

	if (ev_req & ALOE_EVB2_FLAG_READ) e |= EPOLLIN | EPOLLRDHUP;
	if (ev_req & ALOE_EVB2_FLAG_WRITE) e |= EPOLLOUT;
	if (ev_req & ALOE_EVB2_FLAG_EXCEPT) e |= EPOLLPRI;
	return e;
}

static unsigned from_epoll(uint32_t e) {
	unsigned f = 0;

	if (e & (EPOLLIN | EPOLLRDHUP | EPOLLHUP)) f |= ALOE_EVB2_FLAG_READ;
	if (e & EPOLLOUT) f |= ALOE_EVB2_FLAG_WRITE;
	if (e & (EPOLLPRI | EPOLLERR | EPOLLHUP)) f |= ALOE_EVB2_FLAG_EXCEPT;
	return f;
}

static int epoll_refresh(evb2_t *evb, int fd) {
	unsigned ev_req = 0;
	int found = 0;
	qent_t *q;
	struct epoll_event ev;

	if (!evb || evb->epfd < 0) {
		log_e("sanity check invalid state\n");
		return -1;
	}
	if (fd < 0) {
		// timer
		return 0;
	}

	for (q = evb->fdmon_list; q; q = q->next) {
		evb2_handle_t *h = handle_of(q, fdmon_ent);
		if (h->fd == fd) {
			ev_req |= h->flag;
			found = 1;
		}
	}

	memset(&ev, 0, sizeof(ev));
	ev.events = to_epoll(ev_req);
	ev.data.fd = fd;

	if (!found) {
		if (epoll_ctl(evb->epfd, EPOLL_CTL_DEL, fd, NULL) != 0
				&& errno != ENOENT && errno != EBADF) {
			int r = errno;
			log_e("epoll DEL fd %d: %s(%d)\n", fd, strerror(r), r);
			return -1;
		}
		return 0;
	}

	if (epoll_ctl(evb->epfd, EPOLL_CTL_ADD, fd, &ev) != 0) {
		if (errno == EEXIST) {
			if (epoll_ctl(evb->epfd, EPOLL_CTL_MOD, fd, &ev) != 0) {
				int r = errno;
				log_e("epoll MOD fd %d: %s(%d)\n", fd, strerror(r), r);
				return -1;
			}
		} else {
			int r = errno;
			log_e("epoll ADD fd %d: %s(%d)\n", fd, strerror(r), r);
			return -1;
		}
	}
	return 0;
}

static void handle_to_ready(evb2_t *evb, evb2_handle_t *h, unsigned noti) {
	int fd = h->fd;

	qent_remove(&evb->fdmon_list, &h->fdmon_ent);
	qent_remove(&evb->timer_list, &h->timer_ent);
	h->noti = noti;
	qent_insert_tail(&evb->ready_list, &h->fdmon_ent);
	if (fd >= 0) epoll_refresh(evb, fd);
}

static void evb2_install_async(evb2_t *evb) {
	qent_t *q;

	pthread_mutex_lock(&evb->async_lock);
	while ((q = qent_pop(&evb->async_list))) {
		evb2_handle_t *h = handle_of(q, fdmon_ent);

		if (evb->stopping) {
			pthread_mutex_unlock(&evb->async_lock);
			evb2_notify_cancel(h);
			pthread_mutex_lock(&evb->async_lock);
			continue;
		}
		if (h->timeout_at != UINT64_MAX) timer_insert(evb, h);
		else {
			pthread_mutex_unlock(&evb->async_lock);
			evb2_notify_cancel(h);
			pthread_mutex_lock(&evb->async_lock);
		}
	}
	pthread_mutex_unlock(&evb->async_lock);
}

/*
 * Drain wake pipe, then arm queued oneshot timers on the loop thread.
 */
static void async_handler(int fd, unsigned flag, void *user_data) {
	evb2_t *evb = (evb2_t*)user_data;
	char buf[64];

	if (!evb) return;
	if (flag & ALOE_EVB2_FLAG_CANCELLED) return;
	while (read(fd, buf, sizeof(buf)) > 0) { }
	evb2_install_async(evb);
}

static evb2_handle_t* handle_alloc(evb2_t *evb) {
	evb2_handle_t *h;

	if (!(h = (evb2_handle_t*)calloc(1, sizeof(*h)))) {
		log_e("malloc handle\n");
		return NULL;
	}
	h->evb = evb;
	h->fd = -1;
	return h;
}

/* Core API
 * - create pipe to wake loop thread for queued oneshot timers
 */
void* aloe_evb2_create(void) {
	evb2_t *evb;
	evb2_handle_t *h;

	if (!(evb = (evb2_t*)calloc(1, sizeof(*evb)))) {
		log_e("malloc ctx\n");
		return NULL;
	}
	evb->epfd = -1;
	evb->async_pipe[0] = -1;
	evb->async_pipe[1] = -1;
	if (pthread_mutex_init(&evb->async_lock, NULL) != 0) {
		log_e("mutex_init\n");
		free(evb);
		return NULL;
	}

	if ((evb->epfd = epoll_create1(EPOLL_CLOEXEC)) < 0) {
		int r = errno;
		log_e("epoll_create1: %s(%d)\n", strerror(r), r);
		goto fail;
	}

	if (pipe2(evb->async_pipe, O_CLOEXEC | O_NONBLOCK) != 0) {
		int r = errno;
		log_e("pipe2: %s(%d)\n", strerror(r), r);
		goto fail;
	}

	h = (evb2_handle_t*)aloe_evb2_add_fd(evb, evb->async_pipe[0],
			ALOE_EVB2_FLAG_READ, ALOE_EVB2_INFINITE, async_handler, evb);
	if (!h) goto fail;
	h->flag2 |= EVB2_FLAG_INTERNAL;
	return evb;

fail:
	aloe_evb2_destroy(evb);
	return NULL;
}

static void evb2_notify_cancel(evb2_handle_t *h) {
	if (h->cb) (*h->cb)(h->fd, ALOE_EVB2_FLAG_CANCELLED, h->user_data);
	free(h);
}

static void evb2_finalize(evb2_t *evb) {
	qent_t *q;

	if (evb->finalized) return;
	evb->finalized = 1;

	while ((q = evb->fdmon_list)) {
		evb2_handle_t *h = handle_of(q, fdmon_ent);
		handle_detach(evb, h);
		evb2_notify_cancel(h);
	}
	while ((q = evb->timer_list)) {
		evb2_handle_t *h = handle_of(q, timer_ent);
		handle_detach(evb, h);
		evb2_notify_cancel(h);
	}
	while ((q = evb->ready_list)) {
		evb2_handle_t *h = handle_of(q, fdmon_ent);
		handle_detach(evb, h);
		evb2_notify_cancel(h);
	}

	pthread_mutex_lock(&evb->async_lock);
	while ((q = qent_pop(&evb->async_list))) {
		evb2_handle_t *h = handle_of(q, fdmon_ent);
		pthread_mutex_unlock(&evb->async_lock);
		evb2_notify_cancel(h);
		pthread_mutex_lock(&evb->async_lock);
	}
	pthread_mutex_unlock(&evb->async_lock);

	if (evb->async_pipe[0] >= 0) {
		close(evb->async_pipe[0]);
		evb->async_pipe[0] = -1;
	}
	if (evb->async_pipe[1] >= 0) {
		close(evb->async_pipe[1]);
		evb->async_pipe[1] = -1;
	}
	if (evb->epfd >= 0) {
		close(evb->epfd);
		evb->epfd = -1;
	}
}

static void evb2_free(evb2_t *evb) {
	evb2_finalize(evb);
	pthread_mutex_destroy(&evb->async_lock);
	free(evb);
}

static int evb2_run_leave(evb2_t *evb, int rc) {
	if (!evb->stopping) {
		evb->in_run = 0;
		return rc;
	}
	evb2_free(evb);
	return -1;
}

void aloe_evb2_destroy(void *_ctx) {
	evb2_t *evb = (evb2_t*)_ctx;
	int busy;

	if (!evb) return;

	pthread_mutex_lock(&evb->async_lock);
	evb->stopping = 1;
	busy = evb->in_run;
	if (!busy) evb->in_run = 1;
	pthread_mutex_unlock(&evb->async_lock);
	evb2_wake(evb);
	if (busy) return;
	evb2_free(evb);
}

/* Register persistent FD monitoring */
void* aloe_evb2_add_fd(void *_ctx, int fd,
		unsigned ev_req, uint64_t timeout_ms, aloe_evb2_cb_t cb,
		void *user_data) {
	evb2_t *evb = (evb2_t*)_ctx;
	evb2_handle_t *h;

	if (!evb || evb->stopping || evb->finalized || !cb || fd < -1) return NULL;
	if (fd < 0 && timeout_ms == ALOE_EVB2_INFINITE) return NULL;

	if (!(h = handle_alloc(evb))) return NULL;
	h->cb = cb;
	h->user_data = user_data;
	h->fd = fd;
	h->flag = ev_req;
	h->timeout_ms = timeout_ms;
	evb2_set_timeout(h, evb2_now_ms());

	if (fd >= 0) {
		qent_insert_tail(&evb->fdmon_list, &h->fdmon_ent);
		if (epoll_refresh(evb, fd) != 0) {
			handle_detach(evb, h);
			free(h);
			return NULL;
		}
	}
	if (h->timeout_at != UINT64_MAX) timer_insert(evb, h);
	return h;
}

/* Register single-shot FD monitoring */
void* aloe_evb2_add_oneshot_fd(void *_ctx, int fd,
		unsigned ev_req, uint64_t timeout_ms, aloe_evb2_cb_t cb,
		void *user_data) {
	evb2_handle_t *h;

	h = (evb2_handle_t*)aloe_evb2_add_fd(_ctx, fd, ev_req, timeout_ms,
			cb, user_data);
	if (h) h->flag2 |= EVB2_FLAG_ONESHOT;
	return h;
}

/* Register simple timer (fd = -1) */
void* aloe_evb2_add_timer(void *_ctx,
		uint64_t timeout_ms, aloe_evb2_cb_t cb, void *user_data) {
	return aloe_evb2_add_fd(_ctx, -1, 0, timeout_ms, cb, user_data);
}

/* Register single-shot simple timer (fd = -1).
 * Any thread: enqueue + wake; loop thread arms it onto timer_list.
 */
void* aloe_evb2_add_oneshot_timer(void *_ctx,
		uint64_t timeout_ms, aloe_evb2_cb_t cb, void *user_data) {
	evb2_t *evb = (evb2_t*)_ctx;
	evb2_handle_t *h;

	if (!evb || !cb || timeout_ms == ALOE_EVB2_INFINITE) return NULL;
	if (!(h = handle_alloc(evb))) return NULL;

	h->cb = cb;
	h->user_data = user_data;
	h->fd = -1;
	h->flag = 0;
	h->timeout_ms = timeout_ms;
	h->flag2 = EVB2_FLAG_ONESHOT;
	evb2_set_timeout(h, evb2_now_ms());

	pthread_mutex_lock(&evb->async_lock);
	if (evb->stopping || evb->finalized) {
		pthread_mutex_unlock(&evb->async_lock);
		free(h);
		return NULL;
	}
	qent_insert_tail(&evb->async_list, &h->fdmon_ent);
	evb2_wake(evb);
	pthread_mutex_unlock(&evb->async_lock);
	return h;
}

/* Cancel monitored handle */
int aloe_evb2_cancel(void *_ctx, void *_handle) {
	evb2_t *evb = (evb2_t*)_ctx;
	evb2_handle_t *h = (evb2_handle_t*)_handle;
	int fd;

	if (!evb || !h || h->evb != evb) return -1;
	if (h->flag2 & EVB2_FLAG_INTERNAL) return -1;

	if (evb->notifying == h) {
		h->flag2 |= EVB2_FLAG_DEAD;
		return 0;
	}

	pthread_mutex_lock(&evb->async_lock);
	if (qent_remove(&evb->async_list, &h->fdmon_ent)) {
		pthread_mutex_unlock(&evb->async_lock);
		free(h);
		return 0;
	}
	pthread_mutex_unlock(&evb->async_lock);

	fd = h->fd;
	if (!qent_remove(&evb->fdmon_list, &h->fdmon_ent)
			&& !qent_remove(&evb->ready_list, &h->fdmon_ent)
			&& !qent_remove(&evb->timer_list, &h->timer_ent)) {
		return -1;
	}
	qent_remove(&evb->timer_list, &h->timer_ent);
	if (fd >= 0) epoll_refresh(evb, fd);
	free(h);
	return 0;
}

/* Cancel monitored fd */
int aloe_evb2_cancel_fd(void *_ctx, int fd) {
	evb2_t *evb = (evb2_t*)_ctx;
	qent_t *q, *next;
	int n = 0;

	if (!evb) return -1;
	if (fd < 0) return -1;
	if (fd == evb->async_pipe[0] || fd == evb->async_pipe[1]) return -1;

	for (q = evb->fdmon_list; q; q = next) {
		evb2_handle_t *h = handle_of(q, fdmon_ent);
		next = q->next;
		if (h->fd == fd && aloe_evb2_cancel(evb, h) == 0) n++;
	}
	for (q = evb->timer_list; q; q = next) {
		evb2_handle_t *h = handle_of(q, timer_ent);
		next = q->next;
		if (h->fd == fd && aloe_evb2_cancel(evb, h) == 0) n++;
	}
	for (q = evb->ready_list; q; q = next) {
		evb2_handle_t *h = handle_of(q, fdmon_ent);
		next = q->next;
		if (h->fd == fd && aloe_evb2_cancel(evb, h) == 0) n++;
	}
	return n;
}

int aloe_evb2_set_timeout(void *_ctx, void *_handle, uint64_t timeout_ms) {
	evb2_t *evb = (evb2_t*)_ctx;
	evb2_handle_t *h = (evb2_handle_t*)_handle;
	int pending;

	if (!evb || !h || h->evb != evb) return -1;
	if (h->flag2 & (EVB2_FLAG_INTERNAL | EVB2_FLAG_DEAD)) return -1;
	if (evb->stopping || evb->finalized) return -1;
	if (h->fd < 0 && timeout_ms == ALOE_EVB2_INFINITE) return -1;

	h->timeout_ms = timeout_ms;
	evb2_set_timeout(h, evb2_now_ms());

	/* Own callback: not on timer_list; persistent re-arm uses timeout_ms. */
	if (evb->notifying == h) return 0;

	pthread_mutex_lock(&evb->async_lock);
	pending = qent_contains(evb->async_list, &h->fdmon_ent);
	pthread_mutex_unlock(&evb->async_lock);
	if (pending) return 0;

	/* Waiting to dispatch this round: keep ready; new timeout is for re-arm. */
	if (qent_contains(evb->ready_list, &h->fdmon_ent)) return 0;

	qent_remove(&evb->timer_list, &h->timer_ent);
	if (h->timeout_at != UINT64_MAX) timer_insert(evb, h);
	return 0;
}

/* Process single iteration pass */
int aloe_evb2_run_once(void *_ctx, uint64_t timeout_ms) {
	evb2_t *evb = (evb2_t*)_ctx;
	struct epoll_event evs[EVB2_EPOLL_MAX];
	int i, n, timeout = -1, cnt = 0;
	uint64_t now;
	qent_t *q, *next;

	if (!evb) return -1;
	if (evb->stopping || evb->finalized || evb->in_run || evb->epfd < 0) {
		return -1;
	}
	evb->in_run = 1;
	evb2_install_async(evb);

	now = evb2_now_ms();
	if (evb->timer_list) {
		evb2_handle_t *h = handle_of(evb->timer_list, timer_ent);
		if (h->timeout_at <= now) {
			timeout = 0;
		} else {
			uint64_t dt = h->timeout_at - now;
			timeout = (dt > (uint64_t)INT_MAX) ? INT_MAX : (int)dt;
		}
	}
	if (timeout_ms != ALOE_EVB2_INFINITE) {
		int user_to = (timeout_ms > (uint64_t)INT_MAX)
				? INT_MAX : (int)timeout_ms;
		if (timeout < 0 || user_to < timeout) timeout = user_to;
	}

	n = epoll_wait(evb->epfd, evs, EVB2_EPOLL_MAX, timeout);
	if (n < 0) {
		int r = errno;
		if (r == EINTR) return evb2_run_leave(evb, 0);
		log_e("epoll_wait: %s(%d)\n", strerror(r), r);
		return evb2_run_leave(evb, -1);
	}

	now = evb2_now_ms();

	for (i = 0; i < n; i++) {
		int fd = evs[i].data.fd;
		uint32_t e = evs[i].events;
		int hit = 0;

		for (q = evb->fdmon_list; q; q = next) {
			evb2_handle_t *h = handle_of(q, fdmon_ent);
			unsigned noti;

			next = q->next;
			if (h->fd != fd) continue;

			noti = from_epoll(e) & h->flag;
			if ((e & (EPOLLERR | EPOLLHUP)) && !noti) {
				noti |= ALOE_EVB2_FLAG_EXCEPT;
			}
			if (h->timeout_at <= now) noti |= ALOE_EVB2_FLAG_TIMEOUT;
			if (!noti) continue;

			qent_remove(&evb->fdmon_list, &h->fdmon_ent);
			qent_remove(&evb->timer_list, &h->timer_ent);
			h->noti = noti;
			qent_insert_tail(&evb->ready_list, &h->fdmon_ent);
			hit = 1;
		}
		if (hit) epoll_refresh(evb, fd);
	}

	while (evb->timer_list) {
		evb2_handle_t *h = handle_of(evb->timer_list, timer_ent);

		if (h->timeout_at > now) break;
		handle_to_ready(evb, h, ALOE_EVB2_FLAG_TIMEOUT);
	}

	while ((q = qent_pop(&evb->ready_list))) {
		evb2_handle_t *h = handle_of(q, fdmon_ent);
		aloe_evb2_cb_t cb = h->cb;
		void *ud = h->user_data;
		int fd = h->fd;
		unsigned noti = h->noti;
		unsigned oneshot = h->flag2 & EVB2_FLAG_ONESHOT;

		evb->notifying = h;
		if (cb) (*cb)(fd, noti, ud);
		evb->notifying = NULL;
		if (!(h->flag2 & EVB2_FLAG_INTERNAL)) cnt++;

		if (evb->stopping || (h->flag2 & EVB2_FLAG_DEAD) || oneshot) {
			free(h);
		} else {
			h->noti = 0;
			if (fd >= 0) qent_insert_tail(&evb->fdmon_list, &h->fdmon_ent);
			evb2_arm_timer(evb, h);
			if (fd >= 0) epoll_refresh(evb, fd);
		}
	}

	return evb2_run_leave(evb, cnt);
}
