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
 * @file /algae-bp/package/dummy1/priv.h
 * @brief priv
 */

#ifndef PRIV_H_
#define PRIV_H_

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


#endif /* PRIV_H_ */
