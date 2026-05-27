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

#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h> // For TCP_KEEPIDLE, TCP_KEEPINTVL, etc. (Linux)
#include <getopt.h>

#include "priv_ev.h"

#include "mgmt.h"
#include "cli.h"
#include "ipc.h"
#ifdef USE_WIFIMGR
#  include "wifi.h"
#endif

static struct {
	void *ev_ctx;
	char quit;
	struct timespec cycle_ts, cycle_td;
	int log_level;
} impl;

static int test_mm1(void*,int,const char**) {
	char *mm;

	mm = (char*)aloe_calloc(3, 2);
	if (aloe_free(mm) != 0) {
		log_e("Sanity check unexpected aloe_free\n");
	}

	mm = (char*)aloe_calloc(3, 2);
	mm[6] = 'c';
	log_d("expect report overflow\n");
	if (aloe_free(mm) == 0) {
		log_e("Sanity check unexpected aloe_free\n");
	}
	return 1;
}

static void cycletime_cb(int fd, unsigned ev, void *arg) {
	struct timespec *cycle_ts = &impl.cycle_ts, *cycle_td = &impl.cycle_td;
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);

	if (cycle_ts->tv_sec != 0 && cycle_ts->tv_nsec != 0 // initial
			&& ALOE_TIMESEC_CMP(ts.tv_sec, ts.tv_nsec,
					cycle_ts->tv_sec, cycle_ts->tv_nsec) >= 0
					) {
		ALOE_TIMESEC_SUB(ts.tv_sec, ts.tv_nsec,
				cycle_ts->tv_sec, cycle_ts->tv_nsec,
				cycle_td->tv_sec, cycle_td->tv_nsec, 1000000000ul);
	}
	*cycle_ts = ts;

finally:
	if (aloe_ev_put(impl.ev_ctx, -1, &cycletime_cb, NULL, 0, 0, 0) == NULL) {
		log_e("Failure aloe_ev_put\n");
	}
}

static int cli_cmd_cycle_time(void*, int, const char**) {
	struct timespec cycle_td = impl.cycle_td;
	uint64_t cycle_us = cycle_td.tv_sec * 1000000 + cycle_td.tv_nsec / 1000;

	log_d("cycle time %llu (microseconds)\n", (unsigned long long)cycle_us);
	return 0;
}

#ifdef USE_WIFIMGR
static int cli_cmd_wifi(void*, int argc, const char **argv) {
//	dump_argv(argc, argv);
	if (!wifi2_global) {
		const char *iface = argc >= 2 ? argv[1] : NULL;
		wifi2_global = wifi2_init(impl.ev_ctx, iface);
		return 0;
	}
	if (wifi2_global) {
		return wifi2_cli(wifi2_global, argc, argv);
	}
	return -1;
}
#endif

static void tester_proc2(void *args) {
	int *msg_seq = (int*)args;

	log_d("seq: %d\n", msg_seq ? *msg_seq : 0);
}

static void* tester_ipc(void *args) {
	int r = -1, seq = 0, msg_seq;

	(void)args;

	while (1) {
		if ((r = ipc1_register_callback(ipc_global, &msg_seq, &tester_proc2,
				&msg_seq)) != 0) {
			log_e("failed write to ipc1\n");
			goto finally;
		}
		log_d("seq %d, msg_seq %d sent\n", seq, msg_seq);
		seq++;
		sleep(1);
	}
finally:
	return (void*)(unsigned long)r;
}

static const char opt_short[] = "hv";
enum {
	opt_key_reflags = 0x201,
	opt_key_ctrlpath,
	opt_key_ctrlport,
	opt_key_max
};

static struct option opt_long[] = {
	{"help", no_argument, NULL, 'h'},
	{"verbose", no_argument, NULL, 'v'},
	{"ctrlpath", required_argument, NULL, opt_key_ctrlpath},
	{"ctrlport", required_argument, NULL, opt_key_ctrlport},
	{0},
};

static void help(int argc, const char **argv) {
	int i;

//	dump_argv(argc, argv)

	fprintf(stdout,
"COMMAND\n"
"    %s [OPTIONS] [APPLET]\n"
"\n"
"OPTIONS\n"
"    -h, --help          Show help\n"
"    -v, --verbose       Verbose output (default mimic debug and more)\n"
"    --ctrlpath=<FILE>   Start admin service, unix socket FILE for control\n"
"        interface\n"
"    --ctrlport=<PORT>   Start admin service, bound PORT for control\n"
"        interface\n"
"\n", ((argc > 0) && argv && argv[0] ? argv[0] : "Program"));
}

int main(int argc, const char **argv) {
	enum {
		opt_flag_show_help = (1 << 0),
	};
	int ret = -1, opt_op, opt_idx, i, opt_exit = 0;
	pthread_t tester = {};

	log_d("%s\n", aloe_version(NULL, 0));

//	dump_argv(argc, argv)

	optind = 0;
	while ((opt_op = getopt_long(argc, (char* const*)argv, opt_short, opt_long,
			&opt_idx)) != -1) {
		if (opt_op == 'h') {
			opt_exit |= opt_flag_show_help;
			continue;
		}
		if (opt_op == opt_key_ctrlpath) {
//			ctrl_path = optarg;
			continue;
		}
		if (opt_op == opt_key_ctrlport) {
//			ctrl_port = strtol(optarg, NULL, 10);
			continue;
		}
		if (opt_op == 'v') {
			/*if (impl.log_level < aloe_log_level_verb) */impl.log_level++;
			continue;
		}
	}

#if 1
	for (i = optind; i < argc; i++) {
		log_d("non-option argv[%d]: %s\n", i, argv[i]);
	}
#endif

	if (opt_exit) {
		if (opt_exit & opt_flag_show_help) help(argc, argv);
		goto finally;
	}

	if (0) {
		cli1_test1();
		goto finally;
	}

	if ((impl.ev_ctx = aloe_ev_init(0)) == NULL) {
		log_e("Failure alloc ev_ctx\n");
		goto finally;
	}

	if (aloe_ev_put(impl.ev_ctx, -1, &cycletime_cb, NULL, 0, 0, 0) == NULL) {
		log_e("Failure aloe_ev_put\n");
		goto finally;
	}

	mgmt1_init(impl.ev_ctx, "mgmt1.socket");
	cli_global = cli1_init(impl.ev_ctx);
	ipc_global = ipc1_init(impl.ev_ctx);

	cli1_cmd_add(cli_global, "cycle_time", &cli_cmd_cycle_time, NULL, "event cycle time");
#ifdef USE_WIFIMGR
#  if 1
	wifi2_global = wifi2_init(impl.ev_ctx, NULL);
#  endif
	cli1_cmd_add(cli_global, "wifi", &cli_cmd_wifi, NULL, "wifi manager");
#endif



#if 0
	pthread_create(&tester, NULL, &tester_ipc, NULL);
#endif

	while (!impl.quit) {
		aloe_ev_once(impl.ev_ctx);
	}
	ret = 0;
finally:
	if (impl.ev_ctx) {
		aloe_ev_destroy(impl.ev_ctx);
	}
	return ret;
}
