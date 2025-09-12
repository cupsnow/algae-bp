/* $Id$
 *
 * Copyright (c) 2025, joelai
 * All Rights Reserved
 *
 * SPDX-License-Identifier: MIT
 *
 * @file noname
 * @brief noname
 */

#ifndef _H_ALGAE_PRIV
#define _H_ALGAE_PRIV

#include <aloe/sys.h>

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

#ifdef __cplusplus
} /* extern "C" */
#endif


#endif /* _H_ALGAE_PRIV */
