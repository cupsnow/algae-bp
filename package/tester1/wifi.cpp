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
	ST_IF_DOWN,
	ST_IF_UP,
	ST_SCANNING,
	ST_CONNECTING,
	ST_CONNECTED_L2,
	ST_DHCP,
	ST_CONNECTED_L3,
	ST_RETRY,
	ST_RESET,

	ST_WPA_START = ST_IF_DOWN,
	ST_WPA_READY = ST_IF_UP,
	ST_ASSOCIATING = ST_CONNECTING,
	ST_WPA_COMPLETED = ST_CONNECTED_L2,
	ST_WPA_CONNECTED = ST_CONNECTED_L3,

//	not used for FSM
	ST_PAUSE
} state_t;

typedef struct {
	evconn_t evconn;
	state_t state, pause;
	int ifindex;
	int epfd;
	int timer_fd;
	int rtnl_fd;
#if defined(USE_WPASUPCLIENT)
	struct wpa_ctrl *wpasup_cln;
	int wpasup_ctrl_fd;
	int wpasup_pid;
#endif
	char iface[16];
	int udhcpc_pid;
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
} strval_t;

static strval_t state_lut[] = {
	{"ST_INIT", {ST_INIT}},
	{"ST_IF_DOWN", {ST_IF_DOWN}},
	{"ST_IF_UP", {ST_IF_UP}},
	{"ST_SCANNING", {ST_SCANNING}},
	{"ST_CONNECTING", {ST_CONNECTING}},
	{"ST_CONNECTED_L2", {ST_CONNECTED_L2}},
	{"ST_DHCP", {ST_DHCP}},
	{"ST_CONNECTED_L3", {ST_CONNECTED_L3}},
	{"ST_RETRY", {ST_RETRY}},
	{"ST_RESET", {ST_RESET}},
	{NULL}
};

static strval_t* st_find(const char *name, unsigned st) {
	strval_t *ent;

	for (ent = state_lut; ent->name; ent++) {
		if (name) {
			if (strcasecmp(name, ent->name) == 0) return ent;
		} else {
			if (st == ent->vu) return ent;
		}
	}
	return NULL;
}

static unsigned st_val(const char *str, unsigned def) {
	strval_t *ent = st_find(str, 0);
	return ent ? ent->vu : def;
}

static const char* st_str(int st, const char *def) {
	strval_t *ent = st_find(NULL, st);
	return ent ? ent->name : def;
}

static void timer_set(wifi2_t *wifi2, unsigned long ms) {
	struct itimerspec its = {0};
	if (ms > 0) {
		its.it_value.tv_sec = ms / 1000;
		its.it_value.tv_nsec = (ms % 1000) * 1000000;
	}
	timerfd_settime(wifi2->timer_fd, 0, &its, NULL);
}

static int iface_set_up(wifi2_t *wifi2, int up) {
	int ret = -1;
	ret = aloe_ifup(wifi2->iface, up);
	return ret;
}

static int pid_kill(int pid, const char *hint) {
	int r;

	if (pid < 0) return 0;
	if ((r = kill(pid, SIGKILL)) != 0) {
		r = errno;
		log_e("Failed kill %s (%d): %s\n",
				(hint ? hint : ""), pid, strerror(r));
		return r;
	}
	while ((r = waitpid(pid, NULL, 0)) < 0) {
		r = errno;
		if (r == EINTR) continue;
		log_e("Sanity check, failed waitpid %s (%d) %s\n",
				(hint ? hint : ""), pid, strerror(r));
		break;
	}
	return 0;
}

static int dhcp_start(wifi2_t *wifi2, int en) {
	char cmd[128];
	int ret = -1;

	pid_t pid;
	int argc = 0, argv_cnt;
	char *argv[20];

	argv_cnt = aloe_arraysize(argv);

	if (wifi2->udhcpc_pid > 0) {
		pid_kill(wifi2->wpasup_pid, "udhcpc");
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
		do {
			if (argc < argv_cnt) argv[argc++] = (char*)"udhcpc";

			if (argc < argv_cnt) argv[argc++] = (char*)"-i";
			if (argc < argv_cnt) argv[argc++] = wifi2->iface;

			if (argc < argv_cnt) argv[argc++] = (char*)"-f"; // foreground (so we can supervise)
			if (argc < argv_cnt) argv[argc++] = (char*)"-q"; // quit after lease
			if (argc < argv_cnt) argv[argc++] = (char*)"-n"; // fail fast if no lease
		} while(0);
		if (argc >= argv_cnt) {
			log_e("insufficient argv\n");
			goto finally;
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

static int wpasup_start(wifi2_t *wifi2, int en) {
	char wpasup_cfg[] = "/var/run/wpa_supplicant.conf";
	char cmd[128];
	int ret = -1;

	pid_t pid;
	int argc = 0, argv_cnt, r;
	char *argv[20];

	argv_cnt = aloe_arraysize(argv);

	if (wifi2->wpasup_pid > 0) {
		pid_t wp;

#if defined(USE_WPASUPCLIENT)
		if (wifi2->wpasup_cln) {
			log_d("Close wpasup_ctrl\n");
			wpa_ctrl_close(wifi2->wpasup_cln);
			wifi2->wpasup_cln = NULL;
		}
#endif

		if ((r = kill(wifi2->wpasup_pid, SIGTERM)) != 0) {
			r = errno;
			log_e("Sanity check, failed kill wpasup (%d): %s\n",
					wifi2->wpasup_pid, strerror(r));
		}
		for (int i = 0; i < 10; i++) {
			wp = waitpid(wifi2->wpasup_pid, NULL, WNOHANG);

			if (wp == wifi2->wpasup_pid) {
				log_d("wpasup existed\n");
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

	snprintf(cmd, sizeof(cmd), "{\n"
			"echo \"ctrl_interface=/var/run/wpa_supplicant\"\n"
			"echo \"sae_groups=19\"\n"
			"echo \"sae_pwe=2\"\n"
			"} >%s", wpasup_cfg);
	system(cmd);

	pid = fork();
	if (pid < 0) {
		log_e("Failed fork\n");
		goto finally;
	}

	if (pid == 0) {
		// child

		argc = 0;
		do {
			if (argc < argv_cnt) argv[argc++] = (char*)"wpa_supplicant";

			if (argc < argv_cnt) argv[argc++] = (char*)"-i";
			if (argc < argv_cnt) argv[argc++] = wifi2->iface;

			if (argc < argv_cnt) argv[argc++] = (char*)"-c";
			if (argc < argv_cnt) argv[argc++] = wpasup_cfg;

			if (argc < argv_cnt) argv[argc++] = (char*)"-C";
			if (argc < argv_cnt) argv[argc++] = (char*)"/var/run/wpa_supplicant"; // ctrl dir

			if (en >= 4) {
				if (argc < argv_cnt) argv[argc++] = (char*)"-dd";
			} else if (en >= 3) {
				if (argc < argv_cnt) argv[argc++] = (char*)"-d";
			}

			if (en >= 2) {
				if (argc < argv_cnt) argv[argc++] = (char*)"-f";
				if (argc < argv_cnt) argv[argc++] = (char*)"/var/run/wpa_supplicant.log"; // optional
			}
		} while(0);
		if (argc >= argv_cnt) {
			log_e("insufficient argv\n");
			goto finally;
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
static int wpa_connect_ctrl(wifi2_t *wifi2, unsigned reopen) {
	char ctrl_path[64] = "/var/run/wpa_supplicant/wlx94186551a58a";
	int ret = -1, r;

	if (wifi2->wpasup_cln) {
		if (!reopen) {
			ret = 0;
			goto finally;
		}
		wpa_ctrl_close(wifi2->wpasup_cln);
		wifi2->wpasup_cln = NULL;
	}

	r = snprintf(ctrl_path, sizeof(ctrl_path), "/var/run/wpa_supplicant/%s", wifi2->iface);
	if (r >= sizeof(ctrl_path)) {
		ctrl_path[sizeof(ctrl_path) - 1] = '\0';
	}

	if ((wifi2->wpasup_cln = wpa_ctrl_open(ctrl_path)) == NULL) {
		log_e("failed open %s\n", ctrl_path);
		goto finally;
	}

	if (wpa_ctrl_attach(wifi2->wpasup_cln) != 0) {
		log_e("failed attach wpasup control socket\n");
		goto finally;
	}

	if ((wifi2->wpasup_ctrl_fd = wpa_ctrl_get_fd(wifi2->wpasup_cln)) == -1) {
		log_e("failed get wpasup control socket fd\n");
		goto finally;
	}
	ret = 0;
finally:
	if (ret != 0) {
		if (wifi2->wpasup_cln) {
			wpa_ctrl_close(wifi2->wpasup_cln);
			wifi2->wpasup_cln = NULL;
		}
	}
	return ret;
}

static int wpasup_cmd(wifi2_t *wifi2, const char *cmd, char *reply, size_t *len) {
	int ret = -1;
	size_t reply_cap = len ? *len : 0;

	if (!wifi2->wpasup_cln
			&& ((wpa_connect_ctrl(wifi2, 0)) != 0 || !wifi2->wpasup_cln)) {
		log_e("no open wpasup control socket\n");
		return -1;
	}
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

	if (wpasup_cmd(wifi2, "PING", reply, &reply_sz) == 0
			&& strstr(reply, "PONG")) {
		return 0;
	}
	return -1;
}

static int wpasup_scan(wifi2_t *wifi2) {
	char reply[32];
	size_t reply_sz = sizeof(reply);

	return wpasup_cmd(wifi2, "SCAN", reply, &reply_sz);
}

static int wpasup_add_network(wifi2_t *wifi2, int network_idx) {
	int ret = -1, r;
	char cmd[32] = "ADD_NETWORK %d";
	char reply[32] = "%d\n", reply_verify[32] = "%d\n";
	size_t reply_sz = sizeof(reply);

	if ((r = wpasup_cmdsf(wifi2, reply, &reply_sz, cmd, sizeof(cmd),
			"ADD_NETWORK")) != 0
			|| reply_sz <= 0) {
		log_e("failed send add_network\n");
		goto finally;
	}
	if ((r = snprintf(reply_verify, sizeof(reply_verify),
			"%d\n", network_idx)) <= 0
			|| r >= sizeof(reply_verify)) {
		log_e("failed compose reply_verify\n");
		goto finally;
	}
	if (strncasecmp(reply, reply_verify, 3) != 0) {
		log_e("failure sent add_network, %s, %s\n", reply, reply_verify);
		goto finally;
	}
	ret = 0;
finally:
	return ret;
}

static void handle_wpa_event(wifi2_t *wifi2) {
	char buf[256];
	size_t len = sizeof(buf) - 1;

	if (wpa_ctrl_recv(wifi2->wpasup_cln, buf, &len) == 0) {
		buf[len] = '\0';
		printf("WPA: %s\n", buf);

		if (strstr(buf, "CTRL-EVENT-CONNECTED")) {
			wifi2->state = ST_WPA_COMPLETED;
		}
		else if (strstr(buf, "CTRL-EVENT-DISCONNECTED")) {
			wifi2->state = ST_RETRY;
		}
		else if (strstr(buf, "CTRL-EVENT-SCAN-RESULTS")) {
			wifi2->state = ST_ASSOCIATING;
		}
	}
}

static int wpasup_network_config(wifi2_t *wifi2, const char *ssid,
		const char *psk, unsigned flag) {
	int ret = -1, r, network_idx = 0;
	char cmd[256], reply[256];
	size_t reply_sz = sizeof(reply);

	if ((r = wpasup_cmdsf(wifi2, reply, &reply_sz, cmd, sizeof(cmd),
			"FLUSH")) != 0
			|| reply_sz <= 0 || strncasecmp(reply, "OK\n", 3) != 0) {
		log_e("send flush\n");
		goto finally;
	}
	log_d("sent flush\n");

	if (wpasup_add_network(wifi2, network_idx) != 0) {
		log_e("send add_network\n");
		goto finally;
	}
	log_d("sent add_network\n");

	if ((r = wpasup_cmdsf(wifi2, reply, &reply_sz, cmd, sizeof(cmd),
			"SET_NETWORK %d scan_ssid 1", network_idx)) != 0
			|| reply_sz <= 0 || strncasecmp(reply, "OK\n", 3) != 0) {
		log_e("set network scan_ssid 1\n");
		goto finally;
	}
	log_d("set network scan_ssid 1\n");

	if ((r = wpasup_cmdsf(wifi2, reply, &reply_sz, cmd, sizeof(cmd),
			"SET_NETWORK %d ssid \"%s\"", network_idx, ssid)) != 0
			|| reply_sz <= 0 || strncasecmp(reply, "OK\n", 3) != 0) {
		log_e("set network ssid\n");
		goto finally;
	}
	log_d("set network ssid\n");

	if (!psk) {
		if ((r = wpasup_cmdsf(wifi2, reply, &reply_sz, cmd, sizeof(cmd),
				"SET_NETWORK %d key_mgmt NONE", network_idx)) != 0
				|| reply_sz <= 0 || strncasecmp(reply, "OK\n", 3) != 0) {
			log_e("set network key_mgmt NONE\n");
			goto finally;
		}
		log_d("set network key_mgmt NONE\n");
	} else {
		if ((r = wpasup_cmdsf(wifi2, reply, &reply_sz, cmd, sizeof(cmd),
				"SET_NETWORK %d psk \"%s\"", network_idx, psk)) != 0
				|| reply_sz <= 0 || strncasecmp(reply, "OK\n", 3) != 0) {
			log_e("set network psk\n");
			goto finally;
		}
		log_d("set network psk\n");

		if (flag) {
			if ((r = wpasup_cmdsf(wifi2, reply, &reply_sz, cmd, sizeof(cmd),
					"SET_NETWORK %d psk \"%s\"", network_idx, psk)) != 0
					|| reply_sz <= 0 || strncasecmp(reply, "OK\n", 3) != 0) {
				log_e("set network psk\n");
				goto finally;
			}
			log_d("set network psk\n");

			if ((r = wpasup_cmdsf(wifi2, reply, &reply_sz, cmd, sizeof(cmd),
					"SET_NETWORK %d key_mgmt SAE", network_idx)) != 0
					|| reply_sz <= 0 || strncasecmp(reply, "OK\n", 3) != 0) {
				log_e("set network key_mgmt SAE\n");
				goto finally;
			}
			log_d("set network key_mgmt SAE\n");

			if ((r = wpasup_cmdsf(wifi2, reply, &reply_sz, cmd, sizeof(cmd),
					"SET_NETWORK %d ieee80211w 2", network_idx)) != 0
					|| reply_sz <= 0 || strncasecmp(reply, "OK\n", 3) != 0) {
				log_e("set network ieee80211w 2\n");
				goto finally;
			}
			log_d("set network ieee80211w 2\n");
		}
	}

	if ((r = wpasup_cmdsf(wifi2, reply, &reply_sz, cmd, sizeof(cmd),
			"SELECT_NETWORK %d", network_idx)) != 0
			|| reply_sz <= 0 || strncasecmp(reply, "OK\n", 3) != 0) {
		log_e("select network 0\n");
		goto finally;
	}
	log_d("select network 0\n");
	ret = 0;
finally:
	return ret;
}

#endif // USE_WPASUPCLIENT

static void handle_rtnl(wifi2_t *wifi2) {
	char buf[4096];
	int len, nlmsg_idx = 0;
	char flag_str[128];

	len = recv(wifi2->rtnl_fd, buf, sizeof(buf), 0);

	log_d("nlmsg len %d\n", len);

	for (struct nlmsghdr *nh = (struct nlmsghdr*)buf; NLMSG_OK(nh, len);
			nh = NLMSG_NEXT(nh, len), nlmsg_idx++) {

		if (nh->nlmsg_type == RTM_NEWADDR || nh->nlmsg_type == RTM_DELADDR) {
			const char *nlmsg_type_str = ((nh->nlmsg_type == RTM_NEWADDR) ? "RTM_NEWADDR" :
					(nh->nlmsg_type == RTM_DELADDR) ? "RTM_DELADDR" :
					"unknown");
			const struct ifaddrmsg *ifa = (struct ifaddrmsg*)NLMSG_DATA(nh);
			const struct rtattr *rta = IFA_RTA(ifa);
			int rem = nh->nlmsg_len - NLMSG_LENGTH(sizeof(*ifa));
			char addr_str[INET6_ADDRSTRLEN], scope_str[16];

			aloe_rtscope_str(scope_str, sizeof(scope_str), ifa->ifa_scope);
			log_d("nlmsg[%d], %s\n", nlmsg_idx, nlmsg_type_str);

			log_d("ifa_family: %s, ifa_prefixlen: %d, ifa_flags: 0x%x, ifa_scope: %s (0x%x)\n",
					(ifa->ifa_family == AF_INET ? "IPv4" :
					ifa->ifa_family == AF_INET6 ? "IPv6" :
					"unknown"), (int)ifa->ifa_prefixlen, (unsigned)ifa->ifa_flags,
					(scope_str ? scope_str : "unknown"), (int)ifa->ifa_scope);

			// AF_INET prefer IFA_LOCAL
			// AF_INET6 use IFA_ADDRESS
			for (; RTA_OK(rta, rem); rta = RTA_NEXT(rta, rem)) {
				if (rta->rta_type == IFA_LABEL) {
					strcpy(addr_str, (char*)RTA_DATA(rta));
					log_d("IFA_LABEL: %s\n", addr_str);
				} else if (rta->rta_type == IFA_LOCAL) {
					if (ifa->ifa_family == AF_INET) {
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
				} else if (rta->rta_type == IFA_ADDRESS) {
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
				}
			}

			if (ifa->ifa_index != wifi2->ifindex) {
				log_d("not monitor interface but %d\n", ifa->ifa_index);
				continue;
			}

			if (nh->nlmsg_type == RTM_NEWADDR) {
				if (wifi2->state == ST_DHCP) {
//					log_d("%s -> %s\n", st_str(wifi2->state, ""), st_str(ST_CONNECTED_L3, ""));
					wifi2->state = ST_CONNECTED_L3;
				}
			}
			continue;
		}

		if (nh->nlmsg_type == RTM_NEWLINK) {
			const struct ifinfomsg *ifi = (struct ifinfomsg*)NLMSG_DATA(nh);

			aloe_ifflag_str(flag_str, sizeof(flag_str), ifi->ifi_flags, NULL);
			log_d("nlmsg[%d], RTM_NEWLINK flag: 0x%x (%s)\n", nlmsg_idx,
					ifi->ifi_flags, flag_str);

			if (ifi->ifi_index == wifi2->ifindex) {
				if (!(ifi->ifi_flags & IFF_RUNNING)) {
//					log_d("RTNL: LINK DOWN\n");
//					log_d("%s -> %s\n", st_str(wifi2->state, ""), st_str(ST_RETRY, ""));
					wifi2->state = ST_RETRY;
				}
			}
			continue;
		}

		log_d("nlmsg[%d], nlmsg_type: %d\n", nlmsg_idx, (int)nh->nlmsg_type);
	}
}

static void run_sm(wifi2_t *wifi2) {
	char cmd_buf[100];

	switch (wifi2->state) {
	case ST_INIT: {
		iface_set_up(wifi2, 0);
		iface_set_up(wifi2, 1);
		wpasup_start(wifi2, 1);
		sleep(1); // allow socket ready
		wpa_connect_ctrl(wifi2, 1);
		wifi2->state = ST_WPA_READY;
		break;
	}
	case ST_WPA_READY:
		wpasup_scan(wifi2);
		timer_set(wifi2, 5000);
		wifi2->state = ST_SCANNING;
		break;

	case ST_SCANNING:
		break;

	case ST_ASSOCIATING:
//		wpasup_select_network(wifi2, 0);
		timer_set(wifi2, 10000);
		break;

	case ST_WPA_COMPLETED:
		printf("L2 connected\n");
		dhcp_start(wifi2, 1);
		timer_set(wifi2, 8000);
		wifi2->state = ST_DHCP;
		break;

	case ST_DHCP:
		break;

	case ST_WPA_CONNECTED:
		printf("FULLY CONNECTED\n");
		break;

	case ST_RETRY:
		printf("Retrying...\n");
		timer_set(wifi2, 2000);
		wifi2->state = ST_RESET;
		break;

	case ST_RESET:
		iface_set_up(wifi2, 0);
		iface_set_up(wifi2, 1);
		wifi2->state = ST_WPA_READY;
		break;
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

static int wifi2_pause(void *_wifi2, int en) {
	wifi2_t *wifi2 = (wifi2_t*)_wifi2;

	if (en == 0) {
		wifi2->pause = ST_INIT;
	} else if (en == 1) {
		wifi2->pause = ST_PAUSE;
	} else if (en == 2) {
		wifi2->pause = wifi2->pause ? ST_INIT : ST_PAUSE;
	}
	return wifi2->pause;
}

static void wifi2_epoll_cb(int fd, unsigned ev, void *cbarg) {
	wifi2_t *wifi2 = (wifi2_t*)cbarg;
	struct epoll_event events[MAX_EVENTS];
	int n;

	if (wifi2->evconn.fd != wifi2->epfd) {
		log_e("sanity check mismatch epfd\n");
		return;
	}
	n = epoll_wait(wifi2->epfd, events, MAX_EVENTS, 0);
	for (int i = 0; i < n; i++) {
//		log_d("event[%d/%d]\n", i + 1, n);
		if (events[i].data.fd == wifi2->rtnl_fd) {
			handle_rtnl(wifi2);
		} else if (events[i].data.fd == wifi2->timer_fd) {
			uint64_t exp;
			read(wifi2->timer_fd, &exp, sizeof(exp));
			log_d("TIMER\n");
			log_d("%s -> %s%s\n", st_str(wifi2->state, ""), st_str(ST_RESET, ""),
					(wifi2->pause ? "(paused)" : ""));
			wifi2->state = ST_RESET;
		} else if (events[i].data.fd == wifi2->mgmt.fd[0]) {
			log_d("mgmt\n");
		}
	}
	if (!wifi2->pause) run_sm(wifi2);
finally:
	// keep listen
	if ((wifi2->evconn.ev = aloe_ev_put(wifi2->evconn.ev_ctx, wifi2->evconn.fd,
			&wifi2_epoll_cb, wifi2, aloe_ev_flag_read, ALOE_EV_INFINITE,
			0)) == NULL) {
		log_e("Failure aloe_ev_put\n");
	}
}

void* wifi2_init(void *evctx, const char *iface) {
	struct epoll_event ev;
	wifi2_t *wifi2 = NULL;
	int ret = -1, r;

	if ((wifi2 = (wifi2_t*)aloe_calloc(1, sizeof(*wifi2))) == NULL) {
		log_e("failed alloc wifi2\n");
		goto finally;
	}
	wifi2->timer_fd = wifi2->epfd = wifi2->rtnl_fd = -1;
	wifi2->mgmt.fd[0] = wifi2->mgmt.fd[1] = -1;
	wifi2->ifindex = 0;
	wifi2->state = ST_INIT;
	strncpy(wifi2->iface, (iface ? iface : "wlan0"), sizeof(wifi2->iface));
	wifi2->iface[sizeof(wifi2->iface) - 1] = '\0';

	if ((wifi2->ifindex = if_nametoindex(wifi2->iface)) == 0) {
		r = errno;
		log_e("failed get interface %s index; %s\n", wifi2->iface, strerror(r));
		goto finally;
	}

	init_rtnl(wifi2);

	wifi2->timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);

	wifi2->epfd = epoll_create1(0);

	memset(&ev, 0, sizeof(ev));

	ev.events = EPOLLIN;
	ev.data.fd = wifi2->rtnl_fd;
	epoll_ctl(wifi2->epfd, EPOLL_CTL_ADD, ev.data.fd, &ev);

	ev.data.fd = wifi2->timer_fd;
	epoll_ctl(wifi2->epfd, EPOLL_CTL_ADD, ev.data.fd, &ev);

	ev.data.fd = wifi2->mgmt.fd[0];
	epoll_ctl(wifi2->epfd, EPOLL_CTL_ADD, ev.data.fd, &ev);

	wifi2->evconn.fd = wifi2->epfd;
	wifi2->evconn.ev_ctx = evctx;
	if ((wifi2->evconn.ev = aloe_ev_put(wifi2->evconn.ev_ctx, wifi2->evconn.fd,
			&wifi2_epoll_cb, wifi2, aloe_ev_flag_read, ALOE_EV_INFINITE,
			0)) == NULL) {
		log_e("Failure aloe_ev_put\n");
		goto finally;
	}
	wifi2_pause(wifi2, 1);

	log_d("wifi2 initialized%s, ctx: 0x%x\n",
			(wifi2->pause ? "(paused)" : ""),
			(unsigned)(unsigned long)wifi2);
	ret = 0;
finally:
	if (ret != 0 && wifi2) {
		if (wifi2->evconn.ev) aloe_ev_cancel(wifi2->evconn.ev_ctx, wifi2->evconn.ev);
		if (wifi2->epfd != -1) close(wifi2->epfd);
		if (wifi2->timer_fd != -1) close(wifi2->timer_fd);
		if (wifi2->rtnl_fd) close(wifi2->rtnl_fd);
		aloe_free(wifi2);
		wifi2 = NULL;
	}
	return wifi2;
}

int wifi2_cli(void *_wifi2, int argc, const char **argv) {
	wifi2_t *wifi2 = (wifi2_t*)_wifi2;

	// wifi abc -> argv[1/2]: wifi; argv[2/2]: abc
//	dump_argv(argc, argv);

	if (argc < 2 || strcasecmp(argv[1], "help") == 0) {
		FILE *fout = stdout;

		fprintf(fout,
"COMMAND\n"
"    state [name]    - Set/Show current FSM state\n"
"    pause           - Toggle FSM\n"
"    timer [ms]      - Set timer timeout milliseconds\n"
"    iflink [0 | 1]  - Set interface down or up\n"
"    dhcp [0 | 1]    - udhcpc stop or restart\n"
"    wpasup [0 | 1]  - wpa_supplicant stop or restart\n"
"    wpacmd <cmd...> - wpa_cli command\n"
		);
		fflush(fout);
		return 0;
	}

	if (!wifi2) {
		log_e("wifi2 absent\n");
		return 1;
	}

	// wifi state
	if (argc >= 2 && strcasecmp(argv[1], "state") == 0) {
		if (argc >= 3) {
			unsigned st  = st_val(argv[2], -1u);

			if (st == -1u) {
				log_e("unknown state %s\n", argv[2]);
				return 1;
			}
			log_d("%s -> %s%s\n", st_str(wifi2->state, ""), st_str(st, ""),
					(wifi2->pause ? "(once)" : ""));
			wifi2->state = (state_t)st;
			run_sm(wifi2);
			return 0;
		}
		log_d("%s (%d)%s\n", st_str(wifi2->state, ""), wifi2->state,
				(wifi2->pause ? "(paused)" : ""));
		return 0;
	}

	// wifi pause
	if (argc >= 2 && strcasecmp(argv[1], "pause") == 0) {
		wifi2_pause(wifi2, 2);
		log_d("%s (%d)%s\n", st_str(wifi2->state, ""), wifi2->state,
				(wifi2->pause ? "(paused)" : ""));
		return 0;
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

		return iface_set_up(wifi2, en);
	}

	// wifi dhcp 0
	if (argc >= 2 && strcasecmp(argv[1], "dhcp") == 0) {
		int en = argc >= 3 ? strtol(argv[2], NULL, 0) : 1;

		return dhcp_start(wifi2, en);
	}

	// wifi wpasup 3
	if (argc >= 2 && strcasecmp(argv[1], "wpasup") == 0) {
		int en = argc >= 3 ? strtol(argv[2], NULL, 0) : 1;

		return wpasup_start(wifi2, en);
	}

	//	wifi wpacmd LIST_NETWORKS ->
	//	network id / ssid / bssid / flags
	//	0               any     [DISABLED]

	//	wifi wpacmd STATUS

	//	wifi wpacmd PING -> PONG\n

	//	wifi wpacmd FLUSH -> OK\n
	//	wifi wpacmd REMOVE_NETWORK 0 -> OK\n
	//	wifi wpacmd REMOVE_NETWORK all -> OK\n
	//	wifi wpacmd ADD_NETWORK -> 0\n
	//	wifi wpacmd SELECT_NETWORK 0 -> OK\n
	//	wifi wpacmd SET_NETWORK 0 scan_ssid 1 -> OK\n
	//	wifi wpacmd SET_NETWORK 0 ssid "joe3" -> OK\n
	//	wifi wpacmd SET_NETWORK 0 psk "joelaiamiami" -> OK\n

	//	wifi wpacmd SET_NETWORK 0 key_mgmt NONE

	//	wifi wpacmd SET_NETWORK 0 key_mgmt SAE
	//	wifi wpacmd SET_NETWORK 0 ieee80211w 2

	//	wifi wpacmd SELECT_NETWORK 0 -> OK\n

	//	wifi wpacmd SCAN -> OK\n

	//	wifi wpacmd SCAN_RESULTS ->
	//	bssid / frequency / signal level / flags / ssid
	//	cc:d8:43:b5:7e:eb       5240    -55     [WPA2-SAE-CCMP][SAE-H2E][ESS]
	//	ce:d8:43:c5:7e:ea       5240    -55     [WPA2-PSK-CCMP][ESS]
	//	cc:d8:43:b5:7e:ea       2452    -65     [WPA2-PSK-CCMP][WPS][ESS]       joe3
	//	28:87:ba:40:5a:ef       2422    -69     [WPA2-PSK-CCMP][WPS][ESS]       iSynReal_WIFI
	if (argc >= 3 && strcasecmp(argv[1], "wpacmd") == 0) {
		char cmd[500], resp[500];
		size_t pos = 0, cmd_sz = sizeof(cmd), resp_sz;
		int r;

		// wifi wpacmd ...
		for (int i = 2; i < argc && argv[i]; i++) {
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

	//	wifi wificfg DK_SWQA_Linksys_2.4G 5555500000
	//	wifi wificfg DK_SWQA_Linksys_5G 5555500000 1
	if (argc >= 3 && strcasecmp(argv[1], "wificfg") == 0) {
		const char *ssid = argc >= 3 ? argv[2] : NULL;
		const char *pkey = argc >= 4 ? argv[3] : NULL;
		unsigned flag = argc >= 5 ? strtol(argv[4], NULL, 0) : 0;

		return wpasup_network_config(wifi2, ssid, pkey, flag);
	}

	return 1;
}
