/* $Id$
 *
 * Copyright (c) 2026, joelai
 * All Rights Reserved
 *
 * SPDX-License-Identifier: MIT
 *
 * @file wifi.cpp
 * @brief noname
 *
 */

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <asm/types.h>
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <netlink/msg.h>
#include <netlink/attr.h>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/nl80211.h>
#include <linux/if.h>

#if defined(USE_WPASUPCLIENT)
#include "wpa_ctrl.h"
#endif

#include "wifi.h"
#include "priv.h"

#define MAX_EVENTS 8

typedef enum {
	ST_INIT = 0,
	ST_RESET,
	ST_WPASUP_CTRL,
	ST_WPASUP_READY,
	ST_WPASUP_CONNECT,
	ST_DHCPC,
	ST_ZCIP,
	ST_MON,
	ST_PAUSE
} state_t;

typedef enum {
	STIN_ENTER,
	STIN_TIMEOUT,
	STIN_IFFUP,
	STIN_IFFRUNNING,
	STIN_IPADD,
	STIN_IFFLOWDOWN,
} state_input_t;


typedef struct {
	evconn_t evconn, evconn_wpasup_mon;
	state_t state;
	unsigned ifflag;
	int ifindex;
	int epfd;
	int timer_fd;
	int rtnl_fd;
#if defined(USE_WPASUPCLIENT)
	struct wpa_ctrl *wpasup_cln, *wpasup_mon;
#endif
	int wpasup_pid;
	char iface[16];
	int udhcpc_pid;
	int zcip_pid;
	struct {
		char ssid[64], psk[100];
		unsigned flag;
	} network[1], *network_apply;
} wifi2_t;

extern "C" {
void *wifi2_global;
}

typedef struct {
	const char *name;
	union {
		unsigned vu;
		void *vv;
	};
	const char *desc;
} strval_t;

#define STRVAL_ENT(_st, ...) { aloe_stringify(_st), {_st}, ##__VA_ARGS__}

static strval_t state_lut[] = {
	STRVAL_ENT(ST_INIT),
	STRVAL_ENT(ST_RESET),
	STRVAL_ENT(ST_WPASUP_CTRL),
	STRVAL_ENT(ST_WPASUP_READY),
	STRVAL_ENT(ST_WPASUP_CONNECT),
	STRVAL_ENT(ST_DHCPC),
	STRVAL_ENT(ST_ZCIP),
	STRVAL_ENT(ST_MON),
	STRVAL_ENT(ST_PAUSE),
	{NULL}
};

static strval_t state_input_lut[] = {
	STRVAL_ENT(STIN_ENTER),
	STRVAL_ENT(STIN_TIMEOUT),
	STRVAL_ENT(STIN_IFFUP),
	STRVAL_ENT(STIN_IFFRUNNING),
	STRVAL_ENT(STIN_IPADD),
	STRVAL_ENT(STIN_IFFLOWDOWN),
	{NULL}
};
#undef STRVAL_ENT

static const strval_t* stval_find(const strval_t *lut, int lut_cnt,
		const char *name, unsigned st) {
	int idx;

	for (idx = 0; lut->name && (lut_cnt <= 0 || idx < lut_cnt); lut++, idx++) {
		if (lut_cnt > 0 && idx >= lut_cnt) break;
		if (name) {
			if (strcasecmp(name, lut->name) == 0) return lut;
			continue;
		}
		if (st == lut->vu) return lut;
	}
	return NULL;
}	

#define STRVAL_LUT_FUNC(_rs, _pf, _lut) \
_rs unsigned _pf ## _val(const char *str, unsigned def) { \
	const strval_t *ent = stval_find(_lut, -1, str, 0); \
	return ent ? ent->vu : def; \
} \
_rs const char* _pf ## _str(int st, const char *def) { \
	const strval_t *ent = stval_find(_lut, -1, NULL, st); \
	return ent ? ent->name : def; \
}

STRVAL_LUT_FUNC(static, st, state_lut)
STRVAL_LUT_FUNC(static, stin, state_input_lut)

#undef STRVAL_LUT_FUNC

static int pid_kill(int pid, const char *hint) {
	int r;

	if (pid < 0) return 0;
	if ((r = kill(pid, SIGKILL)) != 0) {
		r = errno;
		log_e("Failed kill %s (%d): %s\n",
				(hint ? hint : ""), pid, strerror(r));
		return r;
	}
	return aloe_waitpid(pid);
}

__attribute__((format(printf, 1, 2)))
static int system_cmd(const char *fmt, ...) {
	char cmd[128], *argv[20];
	int ret = -1, argc = 0, argv_cnt, r, pos;
	pid_t pid;
	va_list va;

	va_start(va, fmt);
	r = vsnprintf(cmd, sizeof(cmd), fmt, va);
	va_end(va);
	if ((r <= 0 || r >= sizeof(cmd))) {
		goto finally;
	}
	log_d("execute: %s\n", cmd);
	ret = system(cmd);
finally:
	return ret;
}

#define SHNULL " >/dev/null 2>&1"

static int ip_flush(const char *ifce) {
	return system_cmd("ip a flush dev %s" SHNULL, ifce);
}

static void timer_set(wifi2_t *wifi2, unsigned long ms) {
	struct itimerspec its = {0};
	if (ms > 0) {
		its.it_value.tv_sec = ms / 1000;
		its.it_value.tv_nsec = (ms % 1000) * 1000000;
	}
	timerfd_settime(wifi2->timer_fd, 0, &its, NULL);
}

static int dhcpc_start(wifi2_t *wifi2, int en) {
	char cmd[128], *argv[20];
	int ret = -1, argc = 0, argv_cnt;
	pid_t pid;

	argv_cnt = aloe_arraysize(argv);

	if (wifi2->udhcpc_pid > 0) {
		pid_kill(wifi2->udhcpc_pid, "udhcpc");
		wifi2->udhcpc_pid = -1;
	}
	if (en <= 0) {
		ret = 0;
		goto finally;
	}

	pid = fork();
	if (pid < 0) {
		log_e("Failed fork\n");
		goto finally;
	}

	if (pid == 0) {
		argc = 0;
		argv[argc++] = (char*)"udhcpc";
		do {
			if (argc < argv_cnt) argv[argc++] = (char*)"-i";
			if (argc < argv_cnt) argv[argc++] = wifi2->iface;

			if (argc < argv_cnt) argv[argc++] = (char*)"-f"; // foreground (so we can supervise)
			if (argc < argv_cnt) argv[argc++] = (char*)"-q"; // quit after lease
			if (argc < argv_cnt) argv[argc++] = (char*)"-n"; // fail fast if no lease
		} while(0);
		if (argc >= argv_cnt) {
			log_e("insufficient argv\n");
			_exit(127);
		}
		argv[argc] = NULL;

		execvp(argv[0], argv);
		_exit(127);
		log_e("Unreachable\n");
	}
	wifi2->udhcpc_pid = pid;
	ret = 0;
finally:
	return ret;
}

static int zcip_start(wifi2_t *wifi2, int en) {
	char cmd[128], *argv[20];
	int ret = -1, argc = 0, argv_cnt;
	pid_t pid;

	argv_cnt = aloe_arraysize(argv);

	if (wifi2->zcip_pid > 0) {
		pid_kill(wifi2->zcip_pid, "zcip");
		wifi2->zcip_pid = -1;
	}
	if (en <= 0) {
		ret = 0;
		goto finally;
	}

	pid = fork();
	if (pid < 0) {
		log_e("Failed fork\n");
		goto finally;
	}

	if (pid == 0) {
		argc = 0;
		argv[argc++] = (char*)"zcip";
		do {
			if (argc < argv_cnt) argv[argc++] = wifi2->iface;
			if (argc < argv_cnt) argv[argc++] = (char*)"/usr/share/zcip/default.script";
		} while(0);
		if (argc >= argv_cnt) {
			log_e("insufficient argv\n");
			_exit(127);
		}
		argv[argc] = NULL;

		execvp(argv[0], argv);
		_exit(127);
		log_e("Unreachable\n");
	}
	wifi2->zcip_pid = pid;
	ret = 0;
finally:
	return ret;
}

#if defined(USE_WPASUPCLIENT)
static void wpasup_ctrl_close(struct wpa_ctrl *ctrl) {
	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);
}

static struct wpa_ctrl* wpasup_ctrl_open(const char *ifce, int attach) {
	char ctrl_path[64] = "/var/run/wpa_supplicant/wlx94186551a58a";
	int ret = -1, r;
	struct wpa_ctrl *ctrl = NULL;

	r = snprintf(ctrl_path, sizeof(ctrl_path), "/var/run/wpa_supplicant/%s", ifce);
	if (r >= sizeof(ctrl_path)) {
		log_e("Insufficient buffer\n");
		goto finally;
	}

	if ((ctrl = wpa_ctrl_open(ctrl_path)) == NULL) {
		log_e("failed open %s\n", ctrl_path);
		goto finally;
	}

	if (attach && wpa_ctrl_attach(ctrl) != 0) {
		log_e("failed attach\n");
		goto finally;
	}
	ret = 0;
finally:
	if (ret != 0) {
		if (ctrl) {
			wpasup_ctrl_close(ctrl);
			ctrl = NULL;
		}
	}
	return ctrl;
}

#endif // #if defined(USE_WPASUPCLIENT)

static int wpasup_start(wifi2_t *wifi2, int en) {
	char wpasup_cfg[] = "/var/run/wpa_supplicant.conf";
	char wpasup_ctrldir[] = "/var/run/wpa_supplicant";
	char cmd[128], *argv[20];
	int ret = -1, argc = 0, argv_cnt, r, pos;
	pid_t pid;

	argv_cnt = aloe_arraysize(argv);

	if (wifi2->wpasup_pid > 0) {
		pid_t wp;
#if defined(USE_WPASUPCLIENT)
		if (wifi2->wpasup_cln) {
			log_d("Close wpasup_cln\n");
			wpasup_ctrl_close(wifi2->wpasup_cln);
			wifi2->wpasup_cln = NULL;
		}

		if (wifi2->evconn_wpasup_mon.ev) {
			evconn_cancel(&wifi2->evconn_wpasup_mon);
		}

		if (wifi2->wpasup_mon) {
			log_d("Close wpasup_mon\n");
			wpasup_ctrl_close(wifi2->wpasup_mon);
			wifi2->wpasup_mon = NULL;
		}
#endif // #if defined(USE_WPASUPCLIENT)
		if ((r = kill(wifi2->wpasup_pid, SIGTERM)) != 0) {
			r = errno;
			log_e("Sanity check, failed kill wpasup (%d): %s\n",
					wifi2->wpasup_pid, strerror(r));
		}
		for (int i = 0; i < 10; i++) {
			wp = waitpid(wifi2->wpasup_pid, NULL, WNOHANG);

			if (wp == wifi2->wpasup_pid) {
				log_d("wpasup exit\n");
				wifi2->wpasup_pid = -1;
				break;
			}
			if (wp < 0) {
				r = errno;
				if (r == EINTR) continue;
				if (r == ECHILD) {
					log_d("Sanity check, the pid %d is not a child of this process",
							wifi2->wpasup_pid);
					wifi2->wpasup_pid = -1;
					break;
				}
				log_e("Failed waitpid wpasup (%d) %s\n",
						wifi2->wpasup_pid, strerror(r));
				break;
			}
			usleep(100 * 1000);
		}
		pid_kill(wifi2->wpasup_pid, "wpasup");
		wifi2->wpasup_pid = -1;
	}

	if (en <= 0) {
		ret = 0;
		goto finally;
	}

	pos = snprintf(cmd, sizeof(cmd),
			"ctrl_interface=%s\n"
			"sae_groups=19\n" \
			"sae_pwe=2\n"
			, wpasup_ctrldir
			);
	if (aloe_bio_write_fn(wpasup_cfg, cmd, pos, 0) != pos) {
		log_e("Failed write %s\n", wpasup_cfg);
		goto finally;
	}

	pid = fork();
	if (pid < 0) {
		log_e("Failed fork\n");
		goto finally;
	}

	if (pid == 0) {
		// child

		argc = 0;
		argv[argc++] = (char*)"wpa_supplicant";
		do {
			if (argc < argv_cnt) argv[argc++] = (char*)"-i";
			if (argc < argv_cnt) argv[argc++] = wifi2->iface;

			if (argc < argv_cnt) argv[argc++] = (char*)"-c";
			if (argc < argv_cnt) argv[argc++] = wpasup_cfg;
#if 0
			if (argc < argv_cnt) argv[argc++] = (char*)"-C";
			if (argc < argv_cnt) argv[argc++] = (char*)wpasup_ctrldir;
#endif
#define WPASUP_START_DEBUG 2
			if (en >= WPASUP_START_DEBUG) {
				if (argc < argv_cnt) argv[argc++] = (char*)"-f";
				if (argc < argv_cnt) argv[argc++] = (char*)"/var/run/wpa_supplicant.log"; // optional
				if (en >= (WPASUP_START_DEBUG + 2)) {
					if (argc < argv_cnt) argv[argc++] = (char*)"-dd";
				} else if (en >= (WPASUP_START_DEBUG + 1)) {
					if (argc < argv_cnt) argv[argc++] = (char*)"-d";
				}
			}
		} while(0);
		if (argc >= argv_cnt) {
			log_e("insufficient argv\n");
			_exit(127);
		}
		argv[argc] = NULL;

		execvp(argv[0], argv);
		_exit(127);
		log_e("Unreachable\n");
	}
	wifi2->wpasup_pid = pid;
	ret = 0;
finally:
	return ret;
}

#if defined(USE_WPASUPCLIENT)
static void wifi2_wpasup_mon_cb(int fd, unsigned ev, void *cbarg) {
	wifi2_t *wifi2 = (wifi2_t*)cbarg;
	int r;
	char reply[64];
	size_t reply_cap = sizeof(reply), reply_sz = reply_cap;

	(void)fd;
	(void)ev;

	if ((r = wpa_ctrl_recv(wifi2->wpasup_mon, reply, &reply_sz)) <= 0
			|| reply_sz <= 0) {
		return;
	}
	if (reply_sz >= reply_cap) {
		log_e("Insufficient reply buffer\n");
		return;
	}
	reply[reply_sz] = '\0';
	log_d("wpasup event: %s\n", reply);
}

static int wpasup_cmd(wifi2_t *wifi2, const char *cmd, char *reply, size_t *len) {
	int ret = -1;
	size_t reply_cap = len ? *len : 0;

	if (!wifi2->wpasup_cln) {
		if (((wifi2->wpasup_cln = wpasup_ctrl_open(wifi2->iface, 0))) == NULL) {
			log_e("Failed open wpasup_cln\n");
			return -1;
		}
		log_d("wpasup_cln opened\n");
	}
 
	do {
		if (!wifi2->wpasup_mon) {
			if (((wifi2->wpasup_mon = wpasup_ctrl_open(wifi2->iface, 1))) == NULL) {
				log_e("failed open wpasup_mon\n");
				break;
			}
			log_d("wpasup_mon opened\n");
		}

		if (!wifi2->evconn_wpasup_mon.ev) {
			wifi2->evconn_wpasup_mon.ev_ctx = wifi2->evconn.ev_ctx;
			wifi2->evconn_wpasup_mon.fd = wpa_ctrl_get_fd(wifi2->wpasup_mon);
			if (evconn_add_read(&wifi2->evconn_wpasup_mon,
					&wifi2_wpasup_mon_cb, wifi2) == NULL) {
				log_e("Failure aloe_evb2_add_fd\n");
				break;
			}
			log_d("evconn_wpasup_mon ready\n");
		}
	} while(0);

	ret = wpa_ctrl_request(wifi2->wpasup_cln, cmd, strlen(cmd), reply, len,
			NULL);
	if (reply_cap > 0 && *len < reply_cap) {
		reply[*len] = '\0';
	}
	return ret;
}

__attribute__((format(printf, 6, 0)))
static int wpasup_cmdvsf(wifi2_t *wifi2, char *reply, size_t *len,
		char *cmd, size_t cmd_sz, const char *fmt, va_list va) {
	int ret = -1, r;

	if ((r = vsnprintf(cmd, cmd_sz, fmt, va)) <= 0
			|| r >= cmd_sz) {
		log_e("insufficient buffer\n");
		goto finally;
	}
	ret = wpasup_cmd(wifi2, cmd, reply, len);
finally:
	return ret;
}

__attribute__((format(printf, 6, 7)))
static int wpasup_cmdsf(wifi2_t *wifi2, char *reply, size_t *len,
		char *cmd, size_t cmd_sz, const char *fmt, ...) {
	int r;

	va_list va;
	va_start(va, fmt);
	r = wpasup_cmdvsf(wifi2, reply, len, cmd, cmd_sz, fmt, va);
	va_end(va);
	return r;
}

__attribute__((format(printf, 4, 0)))
static int wpasup_cmdvf(wifi2_t *wifi2, char *reply, size_t *len,
		const char *fmt, va_list va) {
	int ret = -1, r;
	aloe_buf_t fb = {};
	
	if ((fb.data = aloe_malloc(fb.cap = 1024)) == NULL) {
		log_e("malloc\n");
		goto finally;
	}
	aloe_buf_clear(&fb);
	ret = wpasup_cmdvsf(wifi2, reply, len, (char*)fb.data, fb.lmt, fmt, va);
finally:
	if (fb.data) aloe_free(fb.data);
	return ret;
}

__attribute__((format(printf, 4, 5)))
static int wpasup_cmdf(wifi2_t *wifi2, char *reply, size_t *len,
		const char *fmt, ...) {
	int r;

	va_list va;
	va_start(va, fmt);
	r = wpasup_cmdvf(wifi2, reply, len, fmt, va);
	va_end(va);
	return r;
}

static int wpasup_ping(wifi2_t *wifi2) {
	char reply[16];
	size_t reply_sz = sizeof(reply);

	if (wpasup_cmd(wifi2, "PING", reply, &reply_sz) != 0
			|| !strstr(reply, "PONG")) {
		return -1;
	}

	return 0;
}

static int wpasup_add_network(wifi2_t *wifi2, int network_idx) {
	int ret = -1, r;
	char cmd[32], reply[32], reply_verify[32];
	size_t reply_sz = sizeof(reply);

	if ((r = wpasup_cmdsf(wifi2, reply, &reply_sz, cmd, sizeof(cmd),
			"ADD_NETWORK")) != 0 || reply_sz <= 0) {
		log_e("failed send add_network\n");
		goto finally;
	}
	if ((r = snprintf(reply_verify, sizeof(reply_verify),
			"%d\n", network_idx)) <= 0 || r >= sizeof(reply_verify)) {
		log_e("failed compose reply_verify\n");
		goto finally;
	}
	if (strncmp(reply, reply_verify, sizeof(reply_verify)) != 0) {
		log_e("failure sent add_network, %s, %s\n", reply, reply_verify);
		goto finally;
	}
	ret = 0;
finally:
	return ret;
}

static int wpasup_flush(wifi2_t *wifi2) {
	int ret = -1, r;
	char cmd[32], reply[32];
	size_t reply_sz = sizeof(reply);

	if ((r = wpasup_cmdsf(wifi2, reply, &reply_sz, cmd, sizeof(cmd),
			"FLUSH")) != 0 || reply_sz <= 0) {
		log_e("failed send flush\n");
		goto finally;
	}

	if (strncasecmp(reply, "OK\n", 3) != 0) {
		log_e("failed send flush, %s\n", reply);
		goto finally;
	}
	ret = 0;
finally:
	return ret;
}

static int wpasup_network_connect(wifi2_t *wifi2, const char *ssid,
		const char *psk, unsigned flag) {
	int ret = -1, r, network_idx = 0;
	char cmd[256], reply[32];
	size_t reply_sz = sizeof(reply);

	if ((r = wpasup_cmdsf(wifi2, reply, &reply_sz, cmd, sizeof(cmd),
			"FLUSH")) != 0 || reply_sz <= 0
			|| strncasecmp(reply, "OK\n", 3) != 0) {
		log_e("failed send flush\n");
		goto finally;
	}
//	log_d("sent flush\n");

	if (wpasup_add_network(wifi2, network_idx) != 0) {
		log_e("send add_network\n");
		goto finally;
	}
//	log_d("sent add_network\n");

	if ((r = wpasup_cmdsf(wifi2, reply, &reply_sz, cmd, sizeof(cmd),
			"SET_NETWORK %d scan_ssid 1", network_idx)) != 0
			|| reply_sz <= 0 || strncasecmp(reply, "OK\n", 3) != 0) {
		log_e("set network scan_ssid 1\n");
		goto finally;
	}
//	log_d("set network scan_ssid 1\n");

	if ((r = wpasup_cmdsf(wifi2, reply, &reply_sz, cmd, sizeof(cmd),
			"SET_NETWORK %d ssid \"%s\"", network_idx, ssid)) != 0
			|| reply_sz <= 0 || strncasecmp(reply, "OK\n", 3) != 0) {
		log_e("set network ssid\n");
		goto finally;
	}
//	log_d("set network ssid\n");

	if (!psk) {
		if ((r = wpasup_cmdsf(wifi2, reply, &reply_sz, cmd, sizeof(cmd),
				"SET_NETWORK %d key_mgmt NONE", network_idx)) != 0
				|| reply_sz <= 0 || strncasecmp(reply, "OK\n", 3) != 0) {
			log_e("set network key_mgmt NONE\n");
			goto finally;
		}
//		log_d("set network key_mgmt NONE\n");
	} else {
		if ((r = wpasup_cmdsf(wifi2, reply, &reply_sz, cmd, sizeof(cmd),
				"SET_NETWORK %d psk \"%s\"", network_idx, psk)) != 0
				|| reply_sz <= 0 || strncasecmp(reply, "OK\n", 3) != 0) {
			log_e("set network psk\n");
			goto finally;
		}
//		log_d("set network psk\n");

		if (flag) {
			if ((r = wpasup_cmdsf(wifi2, reply, &reply_sz, cmd, sizeof(cmd),
					"SET_NETWORK %d psk \"%s\"", network_idx, psk)) != 0
					|| reply_sz <= 0 || strncasecmp(reply, "OK\n", 3) != 0) {
				log_e("set network psk\n");
				goto finally;
			}
//			log_d("set network psk\n");

			if ((r = wpasup_cmdsf(wifi2, reply, &reply_sz, cmd, sizeof(cmd),
					"SET_NETWORK %d key_mgmt SAE", network_idx)) != 0
					|| reply_sz <= 0 || strncasecmp(reply, "OK\n", 3) != 0) {
				log_e("set network key_mgmt SAE\n");
				goto finally;
			}
//			log_d("set network key_mgmt SAE\n");

			if ((r = wpasup_cmdsf(wifi2, reply, &reply_sz, cmd, sizeof(cmd),
					"SET_NETWORK %d ieee80211w 2", network_idx)) != 0
					|| reply_sz <= 0 || strncasecmp(reply, "OK\n", 3) != 0) {
				log_e("set network ieee80211w 2\n");
				goto finally;
			}
//			log_d("set network ieee80211w 2\n");
		}
	}

	if ((r = wpasup_cmdsf(wifi2, reply, &reply_sz, cmd, sizeof(cmd),
			"SELECT_NETWORK %d", network_idx)) != 0
			|| reply_sz <= 0 || strncasecmp(reply, "OK\n", 3) != 0) {
		log_e("select network 0\n");
		goto finally;
	}
	log_d("select network %d, ssid: %s\n", network_idx, ssid);
	ret = 0;
finally:
	return ret;
}

//static void handle_wpa_event(wifi2_t *wifi2) {
//	char buf[256];
//	size_t len = sizeof(buf) - 1;
//
//	if (wpa_ctrl_recv(wifi2->wpasup_cln, buf, &len) == 0) {
//		buf[len] = '\0';
//		printf("WPA: %s\n", buf);
//
//		if (strstr(buf, "CTRL-EVENT-CONNECTED")) {
//			wifi2->state = ST_WPA_COMPLETED;
//		}
//		else if (strstr(buf, "CTRL-EVENT-DISCONNECTED")) {
//			wifi2->state = ST_RETRY;
//		}
//		else if (strstr(buf, "CTRL-EVENT-SCAN-RESULTS")) {
//			wifi2->state = ST_ASSOCIATING;
//		}
//	}
//}

static void run_sm(wifi2_t *wifi2, int stin, void *args) {
	int sm_continue = 1;
	char cmd_buf[100];

	while (sm_continue) {
		if (wifi2->state == ST_MON && stin == STIN_TIMEOUT) {

		} else {
			log_d("%s / %s\n", st_str(wifi2->state, ""), stin_str(stin, ""));
		}
		sm_continue = 0;

		switch (wifi2->state) {
#define SM_GOTO(_st, _stin) do { \
	wifi2->state = _st; stin = _stin; sm_continue = 1; \
	break; \
} while(0)

		case ST_INIT: {
			if (stin == STIN_ENTER) {
				timer_set(wifi2, 10);
				break;
			}
			if (stin == STIN_TIMEOUT) {
				SM_GOTO(ST_RESET, STIN_ENTER);
			}
			break;
		}
		case ST_RESET: {
			if (stin == STIN_ENTER) {
				dhcpc_start(wifi2, 0);
				zcip_start(wifi2, 0);

				system_cmd("killall udhcpc" SHNULL);
				system_cmd("killall zcip" SHNULL);
				system_cmd("killall -9 wpa_supplicant" SHNULL);

				aloe_ifup(wifi2->iface, 0);
				ip_flush(wifi2->iface);
				aloe_ifup(wifi2->iface, 1);

				wpasup_start(wifi2, 1);
				break;
			}
			if (stin == STIN_IFFUP) {
				SM_GOTO(ST_WPASUP_CTRL, STIN_ENTER);
			}
			break;
		}
		case ST_WPASUP_CTRL: {
			if (stin == STIN_ENTER || stin == STIN_TIMEOUT) {
				if (wpasup_ping(wifi2) == 0) {
					SM_GOTO(ST_WPASUP_READY, STIN_ENTER);
				}
				timer_set(wifi2, 100);
				break;
			}
			 break;
		}
		case ST_WPASUP_READY: {
			if (stin == STIN_ENTER || stin == STIN_TIMEOUT) {
				if (wifi2->network_apply && wifi2->network_apply->ssid
						&& wifi2->network_apply->ssid[0]) {
					SM_GOTO(ST_WPASUP_CONNECT, STIN_ENTER);
				}
				timer_set(wifi2, 1000);
				break;
			}
			break;
		}
		case ST_WPASUP_CONNECT: {
			if (stin == STIN_ENTER) {
				wpasup_network_connect(wifi2, wifi2->network_apply->ssid,
						wifi2->network_apply->psk, wifi2->network_apply->flag);
				timer_set(wifi2, 15000);
				break;
			}
			if (stin == STIN_IFFRUNNING) {
				SM_GOTO(ST_DHCPC, STIN_ENTER);
				break;
			}
			if (stin == STIN_TIMEOUT) {
				SM_GOTO(ST_WPASUP_READY, STIN_ENTER);
				break;
			}
			break;
		}
		case ST_DHCPC:
			if (stin == STIN_ENTER) {
				dhcpc_start(wifi2, 1);
				timer_set(wifi2, 10000);
				break;
			}
			if (stin == STIN_TIMEOUT) {
				SM_GOTO(ST_ZCIP, STIN_ENTER);
				break;
			}
			if (stin == STIN_IPADD) {
				SM_GOTO(ST_MON, STIN_ENTER);
				break;
			}
			break;
		case ST_ZCIP:
			if (stin == STIN_ENTER) {
				zcip_start(wifi2, 1);
				timer_set(wifi2, 10000);
				break;
			}
			if (stin == STIN_TIMEOUT) {
				SM_GOTO(ST_MON, STIN_ENTER);
				break;
			}
			if (stin == STIN_IPADD) {
				SM_GOTO(ST_MON, STIN_ENTER);
				break;
			}
			break;
		case ST_MON:
			if (stin == STIN_ENTER) {
				log_d("connected\n");
				timer_set(wifi2, 5000);
				break;
			}
			if (stin == STIN_TIMEOUT) {
				timer_set(wifi2, 5000);
				break;
			}
			if (stin == STIN_IFFLOWDOWN) {
				ip_flush(wifi2->iface);
				SM_GOTO(ST_WPASUP_READY, STIN_ENTER);
			}
			break;
		case ST_PAUSE:
		default:
			break;
		}
	}
#undef SM_GOTO
}

#endif // USE_WPASUPCLIENT

static void handle_rtnl(wifi2_t *wifi2) {
	char nlmsg_buf[4096], buf_str[128];
	int len, nlmsg_idx = 0;
	struct {
		unsigned ifflag;
		unsigned update: 1;
	} ifflag_update = {};

	len = recv(wifi2->rtnl_fd, nlmsg_buf, sizeof(nlmsg_buf), 0);

//	log_d("nlmsg len %d\n", len);

	for (struct nlmsghdr *nh = (struct nlmsghdr*)nlmsg_buf; NLMSG_OK(nh, len);
			nh = NLMSG_NEXT(nh, len), nlmsg_idx++) {

		if (nh->nlmsg_type == RTM_NEWADDR || nh->nlmsg_type == RTM_DELADDR) {
			const char *nlmsg_type_str = ((nh->nlmsg_type == RTM_NEWADDR) ? "RTM_NEWADDR" :
					(nh->nlmsg_type == RTM_DELADDR) ? "RTM_DELADDR" :
					"unknown");
			const struct ifaddrmsg *ifa = (struct ifaddrmsg*)NLMSG_DATA(nh);
			const struct rtattr *rta = IFA_RTA(ifa);
			int rem = nh->nlmsg_len - NLMSG_LENGTH(sizeof(*ifa));
			char addr_str[INET6_ADDRSTRLEN], scope_str[16];
			union {
				struct {
					unsigned ipv4_linkaddr: 1;
					unsigned ipv4_uniaddr: 1;
				};
				unsigned ui;
			} stin_ipadd = {};

			if (ifa->ifa_index != wifi2->ifindex) {
//				log_d("skip index %d\n", ifa->ifa_index);
				continue;
			}

			if (nh->nlmsg_type == RTM_DELADDR) {
//				log_d("skip RTM_DELADDR\n");
				continue;
			}

			scope_str[0] = '\0';
			aloe_rtscope_str(scope_str, sizeof(scope_str), ifa->ifa_scope);
//			log_d("nlmsg[%d], %s\n", nlmsg_idx, nlmsg_type_str);

			log_d("ifa_family: %s, ifa_prefixlen: %d, ifa_flags: 0x%x, ifa_scope: %s (0x%x)\n",
					(ifa->ifa_family == AF_INET ? "IPv4" :
					ifa->ifa_family == AF_INET6 ? "IPv6" :
					"unknown"), (int)ifa->ifa_prefixlen, (unsigned)ifa->ifa_flags,
					(scope_str[0] ? scope_str : "unknown"), (int)ifa->ifa_scope);

			// AF_INET prefer IFA_LOCAL
			// AF_INET6 use IFA_ADDRESS
			for (; RTA_OK(rta, rem); rta = RTA_NEXT(rta, rem)) {
				if (rta->rta_type == IFA_LABEL) {
					strcpy(addr_str, (char*)RTA_DATA(rta));
//					log_d("IFA_LABEL: %s\n", addr_str);
					continue;
				}
				if (rta->rta_type == IFA_LOCAL) {
					if (ifa->ifa_family == AF_INET) {

						if (ifa->ifa_scope == RT_SCOPE_UNIVERSE) {
							stin_ipadd.ipv4_uniaddr = 1;
						} else if (ifa->ifa_scope == RT_SCOPE_LINK) {
							stin_ipadd.ipv4_linkaddr = 1;
						}

						if (inet_ntop(AF_INET, RTA_DATA(rta),
								addr_str, sizeof(addr_str)) == NULL) {
							log_e("insufficient buffer to compose address\n");
							continue;
						}
						log_d("IFA_LOCAL IPv4: %s/%d\n", addr_str, ifa->ifa_prefixlen);
					} else if (ifa->ifa_family == AF_INET6) {
						if (inet_ntop(AF_INET6, RTA_DATA(rta),
								addr_str, sizeof(addr_str)) == NULL) {
							log_e("insufficient buffer to compose address\n");
							continue;
						}
						log_d("IFA_LOCAL IPv6: %s/%d\n", addr_str, ifa->ifa_prefixlen);
					}
					continue;
				}
				if (rta->rta_type == IFA_ADDRESS) {
					if (ifa->ifa_family == AF_INET6) {
						if (inet_ntop(AF_INET6, RTA_DATA(rta),
								addr_str, sizeof(addr_str)) == NULL) {
							log_e("insufficient buffer to compose address\n");
							continue;
						}
						log_d("IFA_ADDRESS IPv6: %s/%d\n", addr_str, ifa->ifa_prefixlen);
					} else if (ifa->ifa_family == AF_INET) {
						if (inet_ntop(AF_INET, RTA_DATA(rta),
								addr_str, sizeof(addr_str)) == NULL) {
							log_e("insufficient buffer to compose address\n");
							continue;
						}
						log_d("IFA_ADDRESS IPv4: %s/%d\n", addr_str, ifa->ifa_prefixlen);
					}
					continue;
				}
			}
#if defined(USE_WPASUPCLIENT)
			if (stin_ipadd.ipv4_uniaddr ||
					stin_ipadd.ipv4_linkaddr) {
				run_sm(wifi2, STIN_IPADD, (void*)(unsigned long)stin_ipadd.ui);
			}
#endif
			continue;
		}

		if (nh->nlmsg_type == RTM_NEWLINK) {
			const struct ifinfomsg *ifi = (struct ifinfomsg*)NLMSG_DATA(nh);
			const struct rtattr *rta;
			int ra_len;

			if (ifi->ifi_index != wifi2->ifindex) {
				log_d("skip index %d\n", ifi->ifi_index);
				continue;
			}

//			aloe_ifflag_str(buf_str, sizeof(buf_str), ifi->ifi_flags, NULL);
//			log_d("nlmsg[%d], RTM_NEWLINK flag: 0x%x (%s)\n", nlmsg_idx,
//					ifi->ifi_flags, buf_str);
#if 0
			for (rta = IFLA_RTA(ifi), ra_len = IFLA_PAYLOAD(nh);
					RTA_OK(rta, ra_len); rta = RTA_NEXT(rta, ra_len)) {
				switch (rta->rta_type) {
				case IFLA_IFNAME:
					log_d("  ifname=%s\n", (char*)RTA_DATA(rta));
					break;
				case IFLA_ADDRESS: {
					uint8_t *mac = (uint8_t*)RTA_DATA(rta);
					int alen = RTA_PAYLOAD(rta);

					aloe_hexstr(buf_str, sizeof(buf_str), mac, alen, ":", 1);
					log_d("  mac=%s\n", buf_str);
					break;
				}
				case IFLA_MTU: {
					unsigned mtu = *(unsigned*)RTA_DATA(rta);

					log_d("  mtu=%u\n", mtu);
					break;
				}
				case IFLA_OPERSTATE: {
					unsigned operstate = *(unsigned char*)RTA_DATA(rta);

					log_d("  operstate=%u\n", operstate);
					break;
				}
//				default:
//					printf("  ifla type=%u len=%u\n",
//							rta->rta_type,
//							rta->rta_len);
					break;
				}
			}
#endif
			if ((ifi->ifi_flags ^ wifi2->ifflag) & IFF_UP) {
				log_d("IFF_UP -> %u\n", ifi->ifi_flags & IFF_UP);
#if defined(USE_WPASUPCLIENT)
				if (ifi->ifi_flags & IFF_UP) {
					run_sm(wifi2, STIN_IFFUP, NULL);
				}
#endif
			}

			if ((ifi->ifi_flags ^ wifi2->ifflag) & IFF_RUNNING) {
				log_d("IFF_RUNNING -> %u\n", ifi->ifi_flags & IFF_RUNNING);
#if defined(USE_WPASUPCLIENT)
				if (ifi->ifi_flags & IFF_RUNNING) {
					run_sm(wifi2, STIN_IFFRUNNING, NULL);
				}
#endif
			}

			if ((ifi->ifi_flags ^ wifi2->ifflag) & IFF_LOWER_UP) {
				log_d("IFF_LOWER_UP -> %u\n", ifi->ifi_flags & IFF_LOWER_UP);
#if defined(USE_WPASUPCLIENT)
				if (!(ifi->ifi_flags & IFF_LOWER_UP)) {
					run_sm(wifi2, STIN_IFFLOWDOWN, NULL);
				}
#endif
			}

			ifflag_update.ifflag = ifi->ifi_flags;
			ifflag_update.update = 1;

			continue;
		}

		log_d("nlmsg[%d], nlmsg_type: %d\n", nlmsg_idx, (int)nh->nlmsg_type);
	}

	if (ifflag_update.update) {
		wifi2->ifflag = ifflag_update.ifflag;
	}
}

static void init_rtnl(wifi2_t *wifi2) {
	struct sockaddr_nl addr = {};
	int r;

	wifi2->rtnl_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);

	addr.nl_family = AF_NETLINK;
	addr.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR;

	if (bind(wifi2->rtnl_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
		r = errno;
		log_e("bind NETLINK_ROUTE; %s\n", strerror(r));
	}
}

static void wifi2_epoll_cb(int fd, unsigned ev, void *cbarg) {
	wifi2_t *wifi2 = (wifi2_t*)cbarg;
	struct epoll_event events[MAX_EVENTS];
	int n;

	(void)ev;

	if (wifi2->evconn.fd != wifi2->epfd) {
		log_e("sanity check mismatch epfd\n");
		return;
	}

	if (fd == wifi2->epfd) {
		n = epoll_wait(wifi2->epfd, events, MAX_EVENTS, 0);
		for (int i = 0; i < n; i++) {
	//		log_d("event[%d/%d]\n", i + 1, n);
			if (events[i].data.fd == wifi2->rtnl_fd) {
				handle_rtnl(wifi2);
			} else if (events[i].data.fd == wifi2->timer_fd) {
				uint64_t exp;
				int st_old = wifi2->state;

				read(wifi2->timer_fd, &exp, sizeof(exp));
#if defined(USE_WPASUPCLIENT)
				run_sm(wifi2, STIN_TIMEOUT, NULL);
#endif
				(void)st_old;
			}
		}
	}
}

void* wifi2_init(void *evctx, const char *iface) {
	struct epoll_event ev;
	wifi2_t *wifi2 = NULL;
	int ret = -1, r;
	char buf_str[128];

	if ((wifi2 = (wifi2_t*)aloe_calloc(1, sizeof(*wifi2))) == NULL) {
		log_e("failed alloc wifi2\n");
		goto finally;
	}
	wifi2->timer_fd = wifi2->epfd = wifi2->rtnl_fd = -1;
	wifi2->ifindex = 0;
	wifi2->state = ST_PAUSE;
	strncpy(wifi2->iface, (iface ? iface : "wlan0"), sizeof(wifi2->iface));
	wifi2->iface[sizeof(wifi2->iface) - 1] = '\0';

	if ((wifi2->ifindex = if_nametoindex(wifi2->iface)) == 0) {
		r = errno;
		log_e("failed get interface %s index; %s\n", wifi2->iface, strerror(r));
		goto finally;
	}

	if ((r = aloe_ifflag(wifi2->iface, &wifi2->ifflag)) != 0) {
		log_e("Failed get ifflag\n");
		goto finally;
	}

	aloe_ifflag_str(buf_str, sizeof(buf_str), wifi2->ifflag, NULL);
	log_d("ifflag: 0x%x (%s)\n", wifi2->ifflag, buf_str);

	init_rtnl(wifi2);

	wifi2->timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);

	wifi2->epfd = epoll_create1(0);

	memset(&ev, 0, sizeof(ev));

	ev.events = EPOLLIN;
	ev.data.fd = wifi2->rtnl_fd;
	epoll_ctl(wifi2->epfd, EPOLL_CTL_ADD, ev.data.fd, &ev);

	ev.data.fd = wifi2->timer_fd;
	epoll_ctl(wifi2->epfd, EPOLL_CTL_ADD, ev.data.fd, &ev);

	wifi2->evconn.ev_ctx = evctx;
	wifi2->evconn.fd = wifi2->epfd;
	if (evconn_add_read(&wifi2->evconn, &wifi2_epoll_cb, wifi2) == NULL) {
		log_e("Failure aloe_evb2_add_fd\n");
		goto finally;
	}

	log_d("wifi2 initialized%s\n",
			(wifi2->state == ST_PAUSE ? "(paused)" : ""));
	ret = 0;
finally:
	if (ret != 0 && wifi2) {
		evconn_cancel(&wifi2->evconn);
		if (wifi2->epfd != -1) close(wifi2->epfd);
		if (wifi2->timer_fd != -1) close(wifi2->timer_fd);
		if (wifi2->rtnl_fd) close(wifi2->rtnl_fd);
		aloe_free(wifi2);
		wifi2 = NULL;
	}
	return wifi2;
}

#if defined(USE_WPASUPCLIENT)
int wifi2_cli_wpacmd(void *_wifi2, int argc, const char **argv) {
	wifi2_t *wifi2 = (wifi2_t*)_wifi2;

	//	wpacmd LIST_NETWORKS ->
	//	network id / ssid / bssid / flags
	//	0               any     [DISABLED]

	//	wpacmd STATUS

	//	wpacmd PING -> PONG\n

	//	wpacmd FLUSH -> OK\n
	//	wpacmd REMOVE_NETWORK 0 -> OK\n
	//	wpacmd REMOVE_NETWORK all -> OK\n
	//	wpacmd ADD_NETWORK -> 0\n
	//	wpacmd SELECT_NETWORK 0 -> OK\n
	//	wpacmd SET_NETWORK 0 scan_ssid 1 -> OK\n
	//	wpacmd SET_NETWORK 0 ssid "joe3" -> OK\n
	//	wpacmd SET_NETWORK 0 psk "joelaiamiami" -> OK\n

	//	wpacmd SET_NETWORK 0 key_mgmt NONE

	//	wpacmd SET_NETWORK 0 key_mgmt SAE
	//	wpacmd SET_NETWORK 0 ieee80211w 2

	//	wpacmd SELECT_NETWORK 0 -> OK\n

	//	wpacmd SCAN -> OK\n

	//	wpacmd SCAN_RESULTS ->
	//	bssid / frequency / signal level / flags / ssid
	//	cc:d8:43:b5:7e:eb       5240    -55     [WPA2-SAE-CCMP][SAE-H2E][ESS]
	//	ce:d8:43:c5:7e:ea       5240    -55     [WPA2-PSK-CCMP][ESS]
	//	cc:d8:43:b5:7e:ea       2452    -65     [WPA2-PSK-CCMP][WPS][ESS]       joe3
	//	28:87:ba:40:5a:ef       2422    -69     [WPA2-PSK-CCMP][WPS][ESS]       iSynReal_WIFI
	char cmd[500], resp[500];
	size_t pos = 0, cmd_sz = sizeof(cmd), resp_sz;
	int r;

	// wpacmd ...
	for (int i = 1; i < argc  && argv[i]; i++) {
		if ((r = snprintf(cmd + pos, cmd_sz - pos, "%s%s",
				(pos > 0 ? " " : ""), argv[i])) <= 0 || r + pos >= cmd_sz) {
			log_e("insufficient buffer\n");
			return -1;
		}
		pos += r;
	}
	cmd[pos] = '\0';
	resp_sz = sizeof(resp);
	r = wpasup_cmd(wifi2, cmd, resp, &resp_sz);
	if (resp[0]) {
		log_hd(resp, resp_sz, "cmd: %s -> %d; reply len %d:\n%s\n",
				cmd, r, (int)resp_sz, resp);
	} else {
		log_d("cmd: %s -> %d\n", cmd, r);
	}
	return r;
}
#endif

int wifi2_cli(void *_wifi2, int argc, const char **argv) {
	wifi2_t *wifi2 = (wifi2_t*)_wifi2;

	// wifi abc -> argv[1/2]: wifi; argv[2/2]: abc
//	dump_argv(argc, argv);

	if (argc < 2 || strcasecmp(argv[1], "help") == 0) {
		FILE *fout = stdout;

		fprintf(fout,
"COMMAND\n"
"    state [name]    - Set/Show current FSM state\n"
"    timer [ms]      - Set timer timeout milliseconds\n"
"    iflink [0 | 1]  - Set interface down or up\n"
"    dhcp [0..2]     - udhcpc stop or restart\n"
"    zcip [0..2]     - zcip stop or restart\n"
"    wpasup [0..4]   - wpa_supplicant stop or restart\n"
"    wpacmd <cmd...> - wpa_cli command\n"
"    wificonn <ssid> [psk] [1]\n"
"                    - Connect network, last argument for wpa3 only\n"
"    wificfg <ssid> [psk] [1]\n"
"                    - Config network\n"
		);
		fflush(fout);
		return 0;
	}

	if (!wifi2) {
		log_e("wifi2 absent\n");
		return 1;
	}

	// wifi timer 1000
	if (argc >= 2 && strcasecmp(argv[1], "timer") == 0) {
		unsigned long dur = argc >= 3 ? strtol(argv[2], NULL, 0) : 0;

		if (dur != -1ul) {
			timer_set(wifi2, dur);
			log_d("set %lu milliseconds latter\n", dur);
		}
		return 0;
	}

	// wifi iflink 0
	if (argc >= 2 && strcasecmp(argv[1], "iflink") == 0) {
		int en = argc >= 3 ? strtol(argv[2], NULL, 0) : 1;

		return aloe_ifup(wifi2->iface, en);
	}

	// wifi dhcp 0
	if (argc >= 2 && strcasecmp(argv[1], "dhcp") == 0) {
		int en = argc >= 3 ? strtol(argv[2], NULL, 0) : 1;

		return dhcpc_start(wifi2, en);
	}

	// wifi zcip 0
	if (argc >= 2 && strcasecmp(argv[1], "zcip") == 0) {
		int en = argc >= 3 ? strtol(argv[2], NULL, 0) : 1;

		return zcip_start(wifi2, en);
	}

	// wifi wpasup 3
	if (argc >= 2 && strcasecmp(argv[1], "wpasup") == 0) {
		int en = argc >= 3 ? strtol(argv[2], NULL, 0) : 1;

		return wpasup_start(wifi2, en);
	}
#if defined(USE_WPASUPCLIENT)
	// wifi state reset
	if (argc >= 2 && strcasecmp(argv[1], "state") == 0) {
		if (argc >= 3) {
			unsigned st  = st_val(argv[2], -1u), st_old = wifi2->state;

			if (st == -1u) {
				log_e("unknown state %s\n", argv[2]);
				return 1;
			}
			wifi2->state = (state_t)st;
			run_sm(wifi2, STIN_ENTER, NULL);
			log_d("%s + %s -> %s\n", st_str(st_old, ""),
					stin_str(STIN_ENTER, ""), st_str(wifi2->state, ""));
			return 0;
		}
		log_d("%s\n", st_str(wifi2->state, ""));
		return 0;
	}

	//	wifi wpacmd ...
	if (argc >= 3 && strcasecmp(argv[1], "wpacmd") == 0) {
		return wifi2_cli_wpacmd(wifi2, argc - 1, &argv[1]);
	}

	if (argc >= 3 && strcasecmp(argv[1], "wificonn") == 0) {
		const char *ssid = argc >= 3 ? argv[2] : NULL;
		const char *psk = argc >= 4 ? argv[3] : NULL;
		unsigned flag = argc >= 5 ? strtol(argv[4], NULL, 0) : 0;
		return wpasup_network_connect(wifi2, ssid, psk, flag);
	}

	//	wifi wificfg DK_SWQA_Linksys_2.4G 5555500000
	//	wifi wificfg DK_SWQA_Linksys_5G 5555500000 1
	if (argc >= 2 && (strcasecmp(argv[1], "wificfg") == 0
			|| strcasecmp(argv[1], "swqa") == 0)) {
		const char *ssid = argc >= 3 ? argv[2] : NULL;
		const char *psk = argc >= 4 ? argv[3] : NULL;
		unsigned flag = argc >= 5 ? strtol(argv[4], NULL, 0) : 0;
		int r;

		if (strcasecmp(argv[1], "swqa") == 0) {
			ssid = "DK_SWQA_Linksys_5G";
			psk = "5555500000";
			flag = 1;
		}

		if (!ssid || ssid[0] == '\0') {
			wifi2->network_apply = NULL;
			return 0;
		}

		if ((r = snprintf(wifi2->network[0].ssid, sizeof(wifi2->network[0].ssid),
				"%s", ssid)) <= 0 || r >= sizeof(wifi2->network[0].ssid)) {
			log_e("failed\n");
			return -1;
		}
		if (!psk) {
			wifi2->network[0].psk[0] = '\0';
		} else if ((r = snprintf(wifi2->network[0].psk, sizeof(wifi2->network[0].psk),
				"%s", psk)) <= 0 || r >= sizeof(wifi2->network[0].psk)) {
			log_e("failed\n");
			return -1;
		}
		wifi2->network[0].flag = flag;
		wifi2->network_apply = &wifi2->network[0];

		wifi2->state = ST_INIT;
		run_sm(wifi2, STIN_ENTER, NULL);
		return 0;
	}
#endif // USE_WPASUPCLIENT
	return 1;
}
