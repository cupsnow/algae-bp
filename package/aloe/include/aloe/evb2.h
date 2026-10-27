/* $Id$
 *
 * This file is part of the project esh-ws
 *
 * Aug 26, 2026
 *
 * @author joelai
 *
 * @file evb2.h
 * @brief Small epoll-based event loop for file descriptors and timers.
 */

/**
 * @defgroup aloe_evb2 Event loop
 * @{
 *
 * Linux event loop built on epoll and a monotonic timer queue.  A handle may
 * monitor a file descriptor, own a timeout, or do both.  Persistent handles
 * are re-armed after their callback; one-shot handles are released after the
 * first callback.
 *
 * The application owns the loop and calls aloe_evb2_run_once() from its loop
 * thread.  Unless stated otherwise, all functions must be called from that
 * same thread.  aloe_evb2_add_oneshot_timer() is the sole cross-thread entry
 * point; the caller must still ensure that @p ctx remains alive for the whole
 * call.
 *
 * @note Handles are opaque, are owned by the event loop after successful
 * registration, and become invalid when cancelled, dispatched as one-shot,
 * or destroyed with their context.
 */

#ifndef ALOE_EVB2_H
#define ALOE_EVB2_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** File descriptor is readable or has reached end-of-stream. */
#define ALOE_EVB2_FLAG_READ (1 << 0)
/** File descriptor is writable. */
#define ALOE_EVB2_FLAG_WRITE (1 << 1)
/** File descriptor has urgent data, an error, or a hangup. */
#define ALOE_EVB2_FLAG_EXCEPT (1 << 2)
/** The handle's timeout expired. */
#define ALOE_EVB2_FLAG_TIMEOUT (1 << 3)
/** The context is being destroyed. */
#define ALOE_EVB2_FLAG_CANCELLED (1 << 4)

/** No timeout (I/O only). */
#define ALOE_EVB2_INFINITE ((uint64_t)-1)

/**
 * Values below ALOE_EVB2_FD_TIMER are reserved for future internal
 * pseudo-events and are currently rejected.
 */
#define ALOE_EVB2_FD_TIMER (-1)

/** printf() format for an event mask; use with ALOE_EVB2_ARG(). */
#define ALOE_EVB2_FMT "%s%s%s%s%s(0x%x)"
/** Expand an event mask into the arguments required by ALOE_EVB2_FMT. */
#define ALOE_EVB2_ARG(_ev) \
	((_ev) & ALOE_EVB2_FLAG_READ ? "R" : "") \
	, ((_ev) & ALOE_EVB2_FLAG_WRITE ? "W" : "") \
	, ((_ev) & ALOE_EVB2_FLAG_EXCEPT ? "E" : "") \
	, ((_ev) & ALOE_EVB2_FLAG_TIMEOUT ? "T" : "") \
	, ((_ev) & ALOE_EVB2_FLAG_CANCELLED ? "C" : "") \
	, (unsigned)(_ev)

/**
 * Event callback.
 *
 * @param fd File descriptor supplied at registration, or
 * ALOE_EVB2_FD_TIMER for a timer.
 * @param flag Bitwise OR of ALOE_EVB2_FLAG_* notification flags.
 * @param user_data Opaque pointer supplied at registration.
 *
 * A callback may cancel its own handle or change its timeout.  Destroying the
 * context from a callback is also supported, but invalidates @p ctx when the
 * current aloe_evb2_run_once() returns.
 */
typedef void (*aloe_evb2_cb_t)(int fd, unsigned flag, void *user_data);

/**
 * Create an event-loop context.
 * @return New context, or NULL on failure.
 */
void* aloe_evb2_create(void);

/**
 * Destroy an event-loop context and cancel all registered handles.
 *
 * Remaining callbacks receive ALOE_EVB2_FLAG_CANCELLED.  When called from a
 * callback, destruction is deferred until aloe_evb2_run_once() finishes and
 * that call returns -1.  The context is invalid after this function returns,
 * or after that run-once call returns in the deferred case.
 *
 * @param ctx Context to destroy; NULL is accepted.
 */
void aloe_evb2_destroy(void *ctx);

/**
 * Register persistent file-descriptor monitoring, optionally with a timeout.
 *
 * @param ctx Event-loop context.
 * @param fd Descriptor to monitor, or ALOE_EVB2_FD_TIMER to create a
 * timer-only handle.
 * @param ev_req Bitwise OR of requested READ, WRITE, and EXCEPT flags.
 * @param timeout_ms Relative timeout in milliseconds, or ALOE_EVB2_INFINITE.
 * @param cb Callback invoked for matching events or timeout.
 * @param user_data Opaque value passed to @p cb.
 * @return Opaque handle, or NULL on invalid input or registration failure.
 *
 * A timer-only handle must have a finite timeout.  After each callback, a
 * persistent timeout is restarted from the end of that callback.
 */
void* aloe_evb2_add_fd(void *ctx, int fd,
		unsigned ev_req, uint64_t timeout_ms, aloe_evb2_cb_t cb,
		void *user_data);

/**
 * Register one-shot file-descriptor monitoring.
 * @copydetails aloe_evb2_add_fd()
 *
 * The handle is released after its first callback, whether triggered by I/O
 * or timeout.
 */
void* aloe_evb2_add_oneshot_fd(void *ctx, int fd,
		unsigned ev_req, uint64_t timeout_ms, aloe_evb2_cb_t cb,
		void *user_data);

/**
 * Register a persistent timer.
 * @param ctx Event-loop context.
 * @param timeout_ms Finite interval in milliseconds.
 * @param cb Callback invoked with fd == ALOE_EVB2_FD_TIMER and the TIMEOUT
 * flag.
 * @param user_data Opaque value passed to @p cb.
 * @return Opaque handle, or NULL on error.
 */
void* aloe_evb2_add_timer(void *ctx,
		uint64_t timeout_ms, aloe_evb2_cb_t cb, void *user_data);

/**
 * Register a one-shot timer, including from a non-loop thread.
 * @param ctx Event-loop context whose lifetime is protected by the caller.
 * @param timeout_ms Finite delay in milliseconds, measured from this call.
 * @param cb Callback invoked with fd == ALOE_EVB2_FD_TIMER and the TIMEOUT
 * flag.
 * @param user_data Opaque value passed to @p cb.
 * @return Opaque handle, or NULL on error or during destruction.
 *
 * The request is queued through the context's wake pipe and installed by the
 * loop thread.  Cancellation of the returned handle remains loop-thread-only.
 */
void* aloe_evb2_add_oneshot_timer(void *ctx,
		uint64_t timeout_ms, aloe_evb2_cb_t cb, void *user_data);

/**
 * Cancel a registered handle without invoking its callback.
 * @param ctx Event-loop context.
 * @param handle Handle returned by an add function.
 * @return 0 on success, or -1 if the handle is invalid or not registered.
 */
int aloe_evb2_cancel(void *ctx, void *handle);

/**
 * Cancel every handle monitoring a file descriptor.
 * @param ctx Event-loop context.
 * @param fd Non-negative descriptor; ALOE_EVB2_FD_TIMER does not mean all
 * timers.
 * @return Number of handles cancelled, or -1 on invalid input.
 */
int aloe_evb2_cancel_fd(void *ctx, int fd);

/**
 * Change a handle's timeout.
 * @param ctx Event-loop context.
 * @param handle Registered handle to update.
 * @param timeout_ms New relative timeout, or ALOE_EVB2_INFINITE to disable
 * the timeout of a descriptor handle.
 * @return 0 on success, or -1 on invalid input or state.
 *
 * When called from the handle's callback, the new timeout takes effect on
 * persistent re-arm.  A one-shot handle is still released after its callback.
 */
int aloe_evb2_set_timeout(void *ctx, void *handle, uint64_t timeout_ms);

/**
 * Wait for and dispatch one batch of events.
 * @param ctx Event-loop context.
 * @param timeout_ms Maximum wait in milliseconds, or ALOE_EVB2_INFINITE to
 * wait until the next registered timer or I/O event.
 * @return Number of user callbacks invoked, 0 on interruption/no event, or -1
 * on error, re-entry, invalid state, or deferred destruction.
 *
 * This function is not reentrant.  Internal wake-pipe callbacks are excluded
 * from the returned count.
 */
int aloe_evb2_run_once(void *ctx, uint64_t timeout_ms);

#ifdef __cplusplus
}
#endif

/** @} */

#endif /* ALOE_EVB2_H */
