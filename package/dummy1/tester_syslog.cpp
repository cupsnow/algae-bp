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

#include <getopt.h>
#include <syslog.h>
#include "tester_main.h"

static const char opt_short[] = "hf:p:i:";
enum {
	opt_key_null = 0x200,
};
static struct option opt_long[] = {
	{"help", no_argument, NULL, 'h'},
	{NULL}
};

static void show_help(const char *prog) {

	fprintf(stdout,
"COMMAND\n"
"    %s [OPTIONS] \n"
"\n"
"OPTIONS\n"
"    -h, --help       Show help\n"
"    -f <FACILITY>    Facility\n"
"    -p <PRIORITY>    Priority\n"
"    -i <IDENT>       Ident\n"
"\n"
"FACILITY\n"
"    KERN USER MAIL DAEMON AUTH SYSLOG LPR NEWS UUCP CRON AUTHPRIV FTP\n"
"    LOCAL0 to LOCAL7\n"
"\n"
"PRIORITY\n"
"    EMERG ALERT CRIT ERR WARNING NOTICE INFO DEBUG\n"
"\n", (prog ? prog : "APPLICATION"));
}


typedef struct {
	const char *str;
	int val;
} str_lut_t;

static str_lut_t faci_lut[] = {
	{"KERN", LOG_KERN, },
	{"USER", LOG_USER, },
	{"MAIL", LOG_MAIL, },
	{"DAEMON", LOG_DAEMON, },
	{"AUTH", LOG_AUTH, },
	{"SYSLOG", LOG_SYSLOG, },
	{"LPR", LOG_LPR, },
	{"NEWS", LOG_NEWS, },
	{"UUCP", LOG_UUCP, },
	{"CRON", LOG_CRON, },
	{"AUTHPRIV", LOG_AUTHPRIV, },
	{"FTP", LOG_FTP, },
	{"LOCAL0", LOG_LOCAL0, },
	{"LOCAL1", LOG_LOCAL1, },
	{"LOCAL2", LOG_LOCAL2, },
	{"LOCAL3", LOG_LOCAL3, },
	{"LOCAL4", LOG_LOCAL4, },
	{"LOCAL5", LOG_LOCAL5, },
	{"LOCAL6", LOG_LOCAL6, },
	{"LOCAL7", LOG_LOCAL7, },
	{NULL}
}, prio_lut[] = {
	{"EMERG", LOG_EMERG},
	{"ALERT", LOG_ALERT},
	{"CRIT", LOG_CRIT},
	{"ERR", LOG_ERR},
	{"WARNING", LOG_WARNING},
	{"NOTICE", LOG_NOTICE},
	{"INFO", LOG_INFO},
	{"DEBUG", LOG_DEBUG},
	{NULL}
};

str_lut_t* str_lut_find(const char *str, str_lut_t* lut) {
	str_lut_t *iter;

	for (iter = lut; iter->str; iter++) {
		if (strcasecmp(str, iter->str) == 0) return iter;
	}
	return NULL;
}

static int run(int level, int argc, const char **argv) {
	int opt_op, opt_idx;
	struct timeval ts0, ts, tv;
	const char *opt_ident = NULL;
	str_lut_t *log_faci = NULL, *log_prio = NULL;

	optind = 0;
	while ((opt_op = getopt_long(argc, (char *const*)argv, opt_short, opt_long,
			&opt_idx)) != -1) {
		if (opt_op == 'h') {
			show_help(argc > 0 ? argv[0] : NULL);
			return 1;
		}
		if (opt_op == 'f') {
			if (!(log_faci = str_lut_find(optarg, faci_lut))) {
				log_e("Invalid facility: %s\n", optarg);
				return -1;
			}
			continue;
		}
		if (opt_op == 'p') {
			if (!(log_prio = str_lut_find(optarg, prio_lut))) {
				log_e("Invalid priority: %s\n", optarg);
				return -1;
			}
			continue;
		}
		if (opt_op == 'i') {
			opt_ident = optarg;
			continue;
		}
	}

	for (int i = optind; i < argc; i++) {
		log_d("argv[%d/%d]: %s\n", i + 1, argc, argv[i]);
	}

	if (opt_ident || log_faci) {
		if (!log_faci) log_faci = str_lut_find("USER", faci_lut);
		log_d("openlog %s %s\n",
				((opt_ident) ? opt_ident : "<NULL>"),
				log_faci->str);
		openlog(opt_ident, 0, log_faci->val);
	}

	syslog((log_prio ? log_prio->val : LOG_INFO), "%s",
			((optind < argc) ? argv[optind] : "Nothing to say"));

	gettimeofday(&ts0, NULL);
	log_d("Tester template1 run\n");
	gettimeofday(&ts, NULL);
	timesec_sub(ts.tv_sec, ts.tv_usec, ts0.tv_sec, ts0.tv_usec,
			tv.tv_sec, tv.tv_usec, 1000000);
	log_d("Duration %ld.%06ld seconds\n",
			(long)tv.tv_sec, (long)tv.tv_usec);
	return 0;
}

static tester_test_t TESTER_SECTION_ATTR(_tester_section) test_case = {
	.name = "Test syslog",
	.run = &run,
};

__attribute__((weak))
int main(int argc, const char **argv);
int main(int argc, const char **argv) {
	return (*test_case.run)(1, argc, argv);
}
