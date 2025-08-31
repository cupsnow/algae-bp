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
 * @file /algae-bp/package/aloe/sys.cpp
 * @brief sys
 */

#include <aloe/sys.h>

#  define log_m(_lvl, _msg, _args...) do { \
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
#  define log_d(...) log_m("Debug", __VA_ARGS__)
#  define log_e(...) log_m("ERROR", __VA_ARGS__)

extern "C"
const int aloe_mem_sig = __LINE__;

extern "C"
void aloe_mem_init(aloe_mem_t *mm, int id, size_t sz) {
	mm->sig = &aloe_mem_sig;
	mm->id = id;
	mm->sz = sz;
	*(aloe_mem_t**)((char*)(mm + 1) + sz) = mm;
}

extern "C"
int aloe_mem_check(aloe_mem_t *mm) {
	if (mm->sig != &aloe_mem_sig) {
		log_e("aloe mem invalid\n");
		return -1;
	}
	if (*(aloe_mem_t**)((char*)(mm + 1) + mm->sz) != mm) {
		log_e("aloe mem overflow\n");
		return 1;
	}
	return 0;
}
