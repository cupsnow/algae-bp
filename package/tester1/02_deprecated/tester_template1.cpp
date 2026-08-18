/* $Id$
 *
 * Copyright 2024, Dexatek Technology Ltd.
 * This is proprietary information of Dexatek Technology Ltd.
 * All Rights Reserved. Reproduction of this documentation or the
 * accompanying programs in any manner whatsoever without the written
 * permission of Dexatek Technology Ltd. is strictly forbidden.
 *
 * @author joelai
 *
 * @file /air192/package/tester/test_net1.cpp
 * @brief test_net1
 */

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>
#include <getopt.h>
#include <libgen.h>

#include "priv.h"

static struct {
	unsigned quit: 1;
	int log_level;
	FILE *log_ferr, *log_fout;

} impl = {0};

#if 1
extern "C"
int aloe_log_printf(const char *lvl, const char *func_name, int lno,
		const char *fmt, ...) {
	char buf[200];
	aloe_buf_t fb = {.data = buf, .cap = sizeof(buf)};
	int r, lvl_n;
	FILE *fp;
	va_list va;

	if ((lvl_n = aloe_log_lvl(lvl)) > impl.log_level) return 0;
	fp = ((lvl_n <= aloe_log_level_info) ? impl.log_ferr : impl.log_fout);

	aloe_buf_clear(&fb);

	aloe_buf_printf(&fb, "%s", "[admin]");

#if 0
	aloe_log_snprintf(&fb, lvl, func_name, lno, "");
	aloe_buf_flip(&fb);
	if (fb.lmt > 0) fwrite(fb.data, 1, fb.lmt, fp);
	va_start(va, fmt);
	r = vfprintf(fp,fmt, va);
	va_end(va);
	fflush(fp);
	return (r > 0 ? r : 0) + (fb.lmt > 0 ? fb.lmt : 0);
#else
	va_start(va, fmt);
	r = aloe_log_vsnprintf(&fb, lvl, func_name, lno, fmt, va);
	va_end(va);
	if ((r <= 0)) return 0;
	aloe_buf_flip(&fb);
	if (fb.lmt > 0) {
		fwrite(fb.data, 1, fb.lmt, fp);
		fflush(fp);
	}
	return fb.lmt;
#endif
}
#endif /* if 1 */

enum {
	opt_key_reflags = 0x201,
	opt_key_max
};

static const char opt_short[] = "h";
static struct option opt_long[] = {
	{"help", no_argument, NULL, 'h'},
	{0},
};


int main(int argc, const char **argv) {
	int opt_op, opt_idx, i;

	impl.log_level = aloe_log_level_debug;
	impl.log_fout = stdout;
	impl.log_ferr = stderr;
#if 0
	for (int i = 0; i < argc; i++) {
		log_d("argv[%d/%d]: %s\n", i + 1, argc, argv[i]);
	}
#endif
	optind = 0;
	while ((opt_op = getopt_long(argc, (char* const*)argv, opt_short, opt_long,
			&opt_idx)) != -1) {
		if (opt_op == 'h') {
			continue;
		}
	}

	for (i = optind; i < argc; i++) {
		log_d("non-option argv[%d]: %s\n", i, argv[i]);
	}

	return 0;
}
