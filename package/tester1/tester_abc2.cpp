/* $Id$
 *
 * Copyright 2026, Dexatek Technology Ltd.
 * This is proprietary information of Dexatek Technology Ltd.
 * All Rights Reserved. Reproduction of this documentation or the
 * accompanying programs in any manner whatsoever without the written
 * permission of Dexatek Technology Ltd. is strictly forbidden.
 *
 * @author joelai
 *
 * @file /algae-bp/package/tester1/tester_abc.cpp
 * @brief tester_abc
 */

#include <iostream>
#include <unistd.h>
#include <getopt.h>
#include <ifaddrs.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h> // For TCP_KEEPIDLE, TCP_KEEPINTVL, etc. (Linux)

#include "priv.h"
#include "cli.h"
#include "ipc.h"

static struct {
	void *ev_ctx;
	char quit;
	struct timespec cycle_ts, cycle_td;
	int log_level;
} impl;


static int cli_cmd_ifce(void*, int argc, const char **argv) {
	int ret = -1, r;
	struct sockaddr_in sin;
	struct ifaddrs *ifaddr = NULL;
	const char *ifce = (argc >= 2 ? argv[1] : NULL);

	if (getifaddrs(&ifaddr) != 0) {
		r = errno;
		log_e("getifaddrs -> %s\n", strerror(r));
		goto finally;
	}

	for (struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
		char str_buf[128];

		if (ifce && strcmp(ifa->ifa_name, ifce) != 0) {
			continue;
		}
		aloe_ifflag_str(str_buf, sizeof(str_buf), ifa->ifa_flags, NULL);
		log_d("%s; flags: 0x%x (%s)\n", ifa->ifa_name, ifa->ifa_flags, str_buf);
	}
	ret = 0;
finally:
	if (ifaddr) freeifaddrs(ifaddr);
	return ret;
}

/** cmd.
 *
 * example:
 * wext wlx94186551a58a
 */
static int cli_cmd_wext(void*, int argc, const char **argv) {
	char str_buf[256];
	const char *ifce = (argc >= 2 ? argv[1] : "wlan0");

	aloe_wext_info(ifce, str_buf, sizeof(str_buf));
	log_d("%s\n", str_buf);
	return 0;
}

static int cli_cmd_quit(void*, int argc, const char **argv) {
	(void)argc;
	(void)argv;

	impl.quit = 1;
	return 0;
}

static pid_t cmd_fork_exec(int argc, const char **argv) {
	char *argz[20];
	pid_t pid;
	int argv_cnt = aloe_arraysize(argz);

	if (argc < 1) {
		log_e("Invalid argument\n");
		return -1;
	}

	if (argc >= argv_cnt) {
		log_e("Insufficient argv\n");
		return -1;
	}

	memcpy(argz, argv, argc * sizeof(argz[0]));
	argz[argc] = NULL;
	pid = aloe_fork_execv(argz[0], argz);
	return pid;
}

static int cli_cmd_fork(void*, int argc, const char **argv) {
	pid_t pid;

	if (argc < 2) {
		log_e("Invalid argument\n");
		return -1;
	}

	if ((pid = cmd_fork_exec(argc - 1, &argv[1])) < 0) {
		return -1;
	}
	log_d("pid: %d\n", (int)pid);
	return 0;
}

static int cli_cmd_forkwait(void*, int argc, const char **argv) {
	pid_t pid;
	int r;

	if (argc < 2) {
		log_e("Invalid argument\n");
		return -1;
	}

	if ((pid = cmd_fork_exec(argc - 1, &argv[1])) < 0) {
		return -1;
	}
	r = aloe_waitpid(pid);
	log_d("%s (%d) -> %d\n", argv[1], (int)pid, r);
	return r;
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

	dump_argv(argc, argv)

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

	if ((impl.ev_ctx = aloe_ev_init(0)) == NULL) {
		log_e("Failure alloc ev_ctx\n");
		goto finally;
	}

	cli_global = cli1_init(impl.ev_ctx);
	ipc_global = ipc1_init(impl.ev_ctx);

	cli1_cmd_add(cli_global, "wext", &cli_cmd_wext, NULL, "wext [ifce] -> wireless extension info");
	cli1_cmd_add(cli_global, "ifce", &cli_cmd_ifce, NULL, "ifce [ifce] -> net interface info");
	cli1_cmd_add(cli_global, "quit", &cli_cmd_quit, NULL, "quit -> quit program");
	cli1_cmd_add(cli_global, "exit", &cli_cmd_quit, NULL, "exit -> quit program");
	cli1_cmd_add(cli_global, "q", &cli_cmd_quit, NULL, "q -> quit program");
	cli1_cmd_add(cli_global, "fork", &cli_cmd_fork, NULL, "fork -> fork to execute program");
	cli1_cmd_add(cli_global, "forkwait", &cli_cmd_forkwait, NULL, "forkwait -> fork to execute program");

	while (!impl.quit) {
		aloe_ev_once(impl.ev_ctx);
	}
	ret = 0;
finally:
	if (impl.ev_ctx) {
		aloe_ev_destroy(impl.ev_ctx);
	}
	cli1_destroy(cli_global);
	ipc1_destroy(ipc_global);

	return ret;
}



