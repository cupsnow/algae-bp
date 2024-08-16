/* $Id$
 *
 * Copyright 2024, Dexatek Technology Ltd.
 * This is proprietary information of Dexatek Technology Ltd.
 * All Rights Reserved. Reproduction of this documentation or the
 * accompanying programs in any manner whatsoever without the written
 * permission of Dexatek Technology Ltd. is strictly forbidden.
 *
 * @author joelai
 */

#include "tester_main.h"

static int tester_test_run(tester_test_t *test, int level, int argc,
		const char **argv) {
	const char *name;
	char tmp[33];
	int r = 0;
	static int annoy_idx = 0;
	struct timeval ts0, ts, tv;

	if (!test->name) {
		snprintf(tmp, sizeof(tmp), "Anonymous%d", ++annoy_idx);
		tmp[sizeof(tmp) - 1] = '\0';
		name = tmp;
	} else {
		name = test->name;
	}

	printf("\n"
"Start test case: %s\n"
"================================================\n"
			, name);
	gettimeofday(&ts0, NULL);

	if (test->run) r = (*test->run)(level, argc, argv);

	gettimeofday(&ts, NULL);
	timesec_sub(ts.tv_sec, ts.tv_usec, ts0.tv_sec, ts0.tv_usec,
			tv.tv_sec, tv.tv_usec, 1000000);
	printf("\n"
"================================================\n"
"End test case: %s, Duration %ld.%06ld seconds\n\n"
			, name, (long)tv.tv_sec, (long)tv.tv_usec);

	return r;
}

int main(int argc, const char **argv) {
	unsigned long addr;
	int tester_cnt;

#if 0
	for (int i = 0; i < argc; i++) {
		log_d("argv[%d/%d]: %s\n", i + 1, argc, argv[i]);
	}
#endif

#if 0
	tester_cnt = (__stop__tester_section - __start__tester_section) /
			align2(sizeof(tester_test_t), TESTER_SECTION_ALIGN);
	if (tester_cnt == 1) {
		addr = (unsigned long)__start__tester_section;
		return tester_test_run((tester_test_t*)addr, 1, argc, argv);
	}
#endif

	for (addr = (unsigned long)__start__tester_section;
			addr < (unsigned long)__stop__tester_section;
			addr += align2(sizeof(tester_test_t), TESTER_SECTION_ALIGN)) {
		tester_test_run((tester_test_t*)addr, 1, argc, argv);
	}
	return 0;
}
