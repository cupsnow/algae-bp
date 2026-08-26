/* $Id$
 *
 * This file is part of the project esh-ws
 *
 * Aug 26, 2026
 *
 * @author joelai
 *
 * @file /esh-ws/package/esh-tester/compat/aloe_evb2.h
 * @brief aloe_evb2
 */

/**

 event based framework (aloe_evb2)
 - linux epoll, to poll fd also act as a timeout timer
 - linked list to timer, sooner timeout at first, simple to check first timeout to epoll wait
 - linked list to polling fd, preserve adding order
 - api to add polling fd with timeout and interesting events, including exception
 - api to add single shoot polling fd with timeout
 - api to cancel the added
 - api to change timeout of an added handle
 - the api to add with fd is -1 for a simple timer
 - linked list for ready event, after epoll wait, including fd event, timeout and cancelled for framework stopping
 - api to run once epoll wait iteration, optional timeout, caller duty to host loop thread
 - add/cancel/run_once/destroy must run on the loop thread
 - add_oneshot_timer is also safe from another thread (queued via async pipe)
 - destroy from a callback is deferred until run_once returns (ctx then freed)
 - coding pure c
 - coding public named starting with aloe_evb2

 */

#ifndef ALOE_EVB2_H
#define ALOE_EVB2_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* interesting / triggering events */
#define ALOE_EVB2_FLAG_READ (1 << 0)
#define ALOE_EVB2_FLAG_WRITE (1 << 1)
#define ALOE_EVB2_FLAG_EXCEPT (1 << 2)
#define ALOE_EVB2_FLAG_TIMEOUT (1 << 3)
#define ALOE_EVB2_FLAG_CANCELLED (1 << 4)

/** No timeout (I/O only). */
#define ALOE_EVB2_INFINITE ((uint64_t)-1)

#define ALOE_EVB2_FMT "%s%s%s%s%s(0x%x)"
#define ALOE_EVB2_ARG(_ev) \
	((_ev) & ALOE_EVB2_FLAG_READ ? "R" : "") \
	, ((_ev) & ALOE_EVB2_FLAG_WRITE ? "W" : "") \
	, ((_ev) & ALOE_EVB2_FLAG_EXCEPT ? "E" : "") \
	, ((_ev) & ALOE_EVB2_FLAG_TIMEOUT ? "T" : "") \
	, ((_ev) & ALOE_EVB2_FLAG_CANCELLED ? "C" : "") \
	, (unsigned)(_ev)

/* Callback signature */
typedef void (*aloe_evb2_cb_t)(int fd, unsigned flag, void *user_data);

/* Core API. add/cancel/run_once/destroy: loop thread only, except
 * add_oneshot_timer which may be called from another thread.
 * If destroy() is called from a callback, teardown is deferred; run_once()
 * frees ctx before returning -1. Do not use ctx after that. */
void* aloe_evb2_create(void);
void aloe_evb2_destroy(void *ctx);

/* Register persistent FD monitoring (re-armed after each callback).
 * fd == -1 is a simple timer. timeout_ms == ALOE_EVB2_INFINITE means no
 * timeout (fd >= 0 only; a timer with INFINITE is rejected).
 * @return opaque handle, or NULL on error. */
void* aloe_evb2_add_fd(void *ctx, int fd,
		unsigned ev_req, uint64_t timeout_ms, aloe_evb2_cb_t cb,
		void *user_data);

/* Register single-shot FD monitoring (freed after one callback). */
void* aloe_evb2_add_oneshot_fd(void *ctx, int fd,
		unsigned ev_req, uint64_t timeout_ms, aloe_evb2_cb_t cb,
		void *user_data);

/* Register persistent simple timer (fd = -1). timeout_ms must be finite. */
void* aloe_evb2_add_timer(void *ctx,
		uint64_t timeout_ms, aloe_evb2_cb_t cb, void *user_data);

/* Register single-shot simple timer (fd = -1). timeout_ms must be finite.
 * Safe from another thread: queued and armed on the loop thread; the timeout
 * starts at this call. cancel() of the returned handle is loop-thread. */
void* aloe_evb2_add_oneshot_timer(void *ctx,
		uint64_t timeout_ms, aloe_evb2_cb_t cb, void *user_data);

/* Cancel monitored handle. Silent; no callback.
 * @return 0 on success, -1 if not found. */
int aloe_evb2_cancel(void *ctx, void *handle);

/* Cancel all handles of this fd. fd < 0 is rejected (not "all timers").
 * @return number cancelled, or -1 on error. */
int aloe_evb2_cancel_fd(void *ctx, int fd);

/* Change timeout of a handle. Loop thread only.
 * timeout_ms == ALOE_EVB2_INFINITE clears the timer (fd >= 0 only).
 * From the handle's own callback, the new value is used when it is re-armed
 * (persistent only; oneshot is still freed after the callback).
 * @return 0 on success, -1 on error. */
int aloe_evb2_set_timeout(void *ctx, void *handle, uint64_t timeout_ms);

/* Process single iteration pass (epoll wait + dispatch).
 * Not reentrant. Counts user callbacks (internal pipe handler excluded).
 * timeout_ms caps this wait (ALOE_EVB2_INFINITE = until next timer / I/O).
 * @return number of callbacks invoked, or -1 on error / after destroy. */
int aloe_evb2_run_once(void *ctx, uint64_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* ALOE_EVB2_H */
