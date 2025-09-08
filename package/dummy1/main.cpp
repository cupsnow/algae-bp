/* $Id$
 *
 * Copyright 2025, joelai
 * This is proprietary information of joelai
 * All Rights Reserved. Reproduction of this documentation or the
 * accompanying programs in any manner whatsoever without the written
 * permission of joelai is strictly forbidden.
 *
 * @author joelai
 */

#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h> // For TCP_KEEPIDLE, TCP_KEEPINTVL, etc. (Linux)

#include "priv_ev.h"

#include "mgmt.h"
#include "cli.h"
#include "ipc.h"

static struct {
	void *ev_ctx;
	char quit;
} impl;

#define dump_argv(_argc, _argv) for (int i = 0; i < _argc; i++) { \
	log_d("argv[%d/%d]: %s\n", i + 1, _argc, _argv[i]); \
}

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
	static struct timespec report_ts = {}, cycle_ts = {}, cycle_td = {};
	struct timespec ts;
	time_t t_epoch;
	struct tm tm;

	clock_gettime(CLOCK_REALTIME, &ts);

	if (cycle_ts.tv_sec != 0 && cycle_ts.tv_nsec != 0 // initial
			&& ALOE_TIMESEC_CMP(ts.tv_sec, ts.tv_nsec,
					cycle_ts.tv_sec, cycle_ts.tv_nsec) >= 0
					) {
		ALOE_TIMESEC_SUB(ts.tv_sec, ts.tv_nsec,
				cycle_ts.tv_sec, cycle_ts.tv_nsec,
				cycle_td.tv_sec, cycle_td.tv_nsec, 1000000000ul);
	}
	cycle_ts = ts;

	t_epoch = (time_t)ts.tv_sec;
	localtime_r(&t_epoch, &tm);

	if (report_ts.tv_sec == 0 // initial
			|| ts.tv_sec < report_ts.tv_sec // rollback
			|| ts.tv_sec - report_ts.tv_sec >= 1 // report interval
			) {
		uint64_t cycle_us = cycle_td.tv_sec * 1000000 + cycle_td.tv_nsec / 1000;
		log_d("tick %llu, cycle_us %llu\n",
				(unsigned long long)t_epoch * 1000 + ts.tv_nsec / 1000000,
				(unsigned long long)cycle_us);
		report_ts = ts;
	}
finally:
	if (aloe_ev_put(impl.ev_ctx, -1, &cycletime_cb, NULL, 0, 0, 0) == NULL) {
		log_e("Failure aloe_ev_put\n");
	}
}

static void tester_proc2(void *args) {
	int *seq = (int*)args;

	log_d("seq: %d\n", seq ? *seq : 0);
}

static void* tester_proc(void *args) {
	int r = -1, seq = 0, msg_seq;
	struct {
		void (*func)(void*);
		void* cbarg;
	} val = {.func = &tester_proc2, .cbarg = &seq};

	(void)args;

	while (1) {
		if ((r = ipc1_write(ipc_global, 1, &msg_seq,
				&val, sizeof(val))) < sizeof(val)) {
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

static int tester_cli(void *cbarg, int argc, const char **argv) {
	dump_argv(argc, argv);
	return 0;
}

int main(int argc, const char **argv) {
	int ret = -1;
	pthread_t tester = {};

	log_d("%s\n", aloe_version(NULL, 0));

	dump_argv(argc, argv)

	if (0) {
		cli1_test1();
		goto finally;
	}

	if ((impl.ev_ctx = aloe_ev_init(0)) == NULL) {
		log_e("Failure alloc ev_ctx\n");
		goto finally;
	}

#if 0 // enable for callback in each cycle
	if (aloe_ev_put(impl.ev_ctx, -1, &cycletime_cb, NULL, 0, 0, 0) == NULL) {
		log_e("Failure aloe_ev_put\n");
		goto finally;
	}
#endif

	mgmt1_init(impl.ev_ctx, "mgmt1.socket");
	cli_global = cli1_init(impl.ev_ctx);
	ipc_global = ipc1_init(impl.ev_ctx);

	cli1_cmd_add(cli_global, "nms", &tester_cli, NULL, "no man's sky");

#if 0
	pthread_create(&tester, NULL, &tester_proc, NULL);
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
