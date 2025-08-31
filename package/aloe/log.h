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
 * @file /algae-bp/package/aloe/include/aloe/log.h
 * @brief log
 */

#ifndef _H_ALOE_LOG
#define _H_ALOE_LOG

#include <time.h>
#include <aloe/sys.h>

#ifdef __cplusplus
extern "C" {
#endif

#  define aloe_log_m(_lvl, _msg, _args...) do { \
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
#  define aloe_log_d(...) aloe_log_m("Debug", __VA_ARGS__)
#  define aloe_log_e(...) aloe_log_m("ERROR", __VA_ARGS__)

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _H_ALOE_LOG */
