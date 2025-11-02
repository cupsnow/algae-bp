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

#include "priv_ev.h"

#include "mgmt.h"
#include "cli.h"
#include "ipc.h"

static struct {
	void *ev_ctx;
	char quit;
	struct timespec cycle_ts, cycle_td;
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

	if (aloe_ev_put(impl.ev_ctx, -1, &cycletime_cb, NULL, 0, 0, 0) == NULL) {
		log_e("Failure aloe_ev_put\n");
		goto finally;
	}

	mgmt1_init(impl.ev_ctx, "mgmt1.socket");
	cli_global = cli1_init(impl.ev_ctx);
	ipc_global = ipc1_init(impl.ev_ctx);

	cli1_cmd_add(cli_global, "cycle_time", &cli_cmd_cycle_time, NULL, "event cycle time");

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
