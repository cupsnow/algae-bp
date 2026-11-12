/* $Id$
 *
 * This file is part of the project esh-ws
 *
 * Nov 10, 2026
 *
 * @author joelai
 *
 * @file /esh-ws/agt-app/esghub/app-dk/dexatek/main_application/tester/tester_main.c
 * @brief tester_main
 */

#include "tester_main.h"
#include "priv.h"
#include <aloe/sys.h>

static int tester_test_run(const tester_section_entry_t *tester_entrance, int level,
		int argc, const char **argv) {
	const char *name;
	char tmp[33];
	int r = 0;
	static int annoy_idx = 0;
	struct timeval ts0, ts, tv;

	if (!tester_entrance->name) {
		snprintf(tmp, sizeof(tmp), "Anonymous%d", ++annoy_idx);
		tmp[sizeof(tmp) - 1] = '\0';
		name = tmp;
	} else {
		name = tester_entrance->name;
	}

	printf("\n"
"Start test case: %s\n"
"================================================\n"
			, name);
	gettimeofday(&ts0, NULL);

	if (tester_entrance->run) r = (*tester_entrance->run)((void*)(unsigned long)level, argc, argv);

	gettimeofday(&ts, NULL);
	ALOE_TIMESEC_SUB(ts.tv_sec, ts.tv_usec, ts0.tv_sec, ts0.tv_usec,
			tv.tv_sec, tv.tv_usec, 1000000);
	printf("\n"
"================================================\n"
"End test case: %s, Duration %ld.%06ld seconds\n\n"
			, name, (long)tv.tv_sec, (long)tv.tv_usec);

	return r;
}

__attribute__((weak))
int main(int argc, const char **argv) {
	int tester_cnt = (__stop_tester_section - __start_tester_section)
			/ TESTER_SECTION_ALIGN;
	unsigned long addr;
	int err_ret = 0, err_cnt = 0;

//	silk_log_args(argc, argv);

	for (addr = (unsigned long)__start_tester_section;
			addr < (unsigned long)__stop_tester_section;
			addr += TESTER_SECTION_ALIGN) {
		const tester_section_entry_t *tester_entrance = (tester_section_entry_t*)addr;
		int err;
		if ((err = tester_test_run(tester_entrance, 1, argc, argv)) != 0) {
			err_ret = err;
		}
	}
	return err_ret;
}
