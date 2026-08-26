/* $Id$
 *
 * @author joelai
 *
 * @file noname
 * @brief noname
 */

#ifndef _H_ALGAE_PRIV
#define _H_ALGAE_PRIV

#include <aloe/sys.h>
#include <aloe/evb2.h>
#include <aloe/compat/openbsd/sys/queue.h>
#include <aloe/compat/openbsd/sys/tree.h>

#ifdef __cplusplus
extern "C" {
#endif

#  define log_m(_lvl, _msg, _args...) do { \
	struct timespec _log_m_ts; \
	struct tm _log_m_tm; \
	clock_gettime(CLOCK_REALTIME, &_log_m_ts); \
	localtime_r(&_log_m_ts.tv_sec, &_log_m_tm); \
	fprintf(stdout, "[%02ld:%02ld:%02ld.%06ld][%s][%s][#%d]" _msg, \
			(long)_log_m_tm.tm_hour, (long)_log_m_tm.tm_min, (long)_log_m_tm.tm_sec, \
			(long)_log_m_ts.tv_nsec / 1000, \
			_lvl, __func__, __LINE__, ##_args); \
	fflush(stdout); \
} while(0)
#  define log_d(...) log_m("Debug", __VA_ARGS__)
#  define log_e(...) log_m("ERROR", __VA_ARGS__)
#  define log_hd(_v, _z, _msg, _args...) do { \
	struct timespec _log_m_ts; \
	struct tm _log_m_tm; \
	clock_gettime(CLOCK_REALTIME, &_log_m_ts); \
	localtime_r(&_log_m_ts.tv_sec, &_log_m_tm); \
	aloe_hexdump(_v, _z, "[%02ld:%02ld:%02ld.%06ld][%s][%s][#%d]" _msg, \
			(long)_log_m_tm.tm_hour, (long)_log_m_tm.tm_min, (long)_log_m_tm.tm_sec, \
			(long)_log_m_ts.tv_nsec / 1000, \
			"Debug", __func__, __LINE__, ##_args); \
} while(0)

#define dump_argv(_argc, _argv) for (int i = 0; i < (_argc); i++) { \
	log_d("argv[%d/%d]: %s\n", i + 1, (_argc), (_argv)[i]); \
}

//   if <SECTION NAME> is a valid c variable name
// symbol auto defined by linker when referenced
//   __start_<SECTION NAME>,
//   __stop_<SECTION NAME> (the address after the section)
//   here declared as array to reference the symbol
// and at least 1 trigger linker to define these symbol

#define TESTER_SECTION_ALIGN 64
#define TESTER_SECTION(_name) extern char __start_ ## _name[1]; \
	extern char __stop_ ## _name[1];
#define TESTER_SECTION_ATTR(_name) __attribute__(( \
		used, \
		section(aloe_stringify(_name)), \
		aligned(TESTER_SECTION_ALIGN) \
))

TESTER_SECTION(_tester_section)

typedef const struct tester_test_rec {
	const char *name;
	int (*run)(int level, int argc, const char **argv);
} tester_test_t;

typedef struct evconn_rec {
	int fd;
	void *ev_ctx, *ev;
	TAILQ_ENTRY(evconn_rec) qent;
} evconn_t;
typedef TAILQ_HEAD(evconn_list_rec, evconn_rec) evconn_list_t;

#define evconn_list_add(_q, _e) TAILQ_INSERT_TAIL(_q, _e, qent)
#define evconn_list_rm(_q, _e) TAILQ_REMOVE(_q, _e, qent)
evconn_t* evconn_list_foreach(evconn_list_t *q, evconn_t *p);

static inline void evconn_cancel(evconn_t *e) {
	if (e && e->ev) {
		aloe_evb2_cancel(e->ev_ctx, e->ev);
		e->ev = NULL;
	}
}

static inline void* evconn_add_read(evconn_t *e, aloe_evb2_cb_t cb, void *ud) {
	e->ev = aloe_evb2_add_fd(e->ev_ctx, e->fd, ALOE_EVB2_FLAG_READ,
			ALOE_EVB2_INFINITE, cb, ud);
	return e->ev;
}

#ifdef __cplusplus
} /* extern "C" */
#endif


#endif /* _H_ALGAE_PRIV */
