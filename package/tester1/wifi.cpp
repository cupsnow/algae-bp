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

//#define USE_NLCONN 1

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
#if defined(USE_NLCONN)
	struct nl_sock *nl_sock;
	int nl80211_id;
#endif
#if defined(USE_WPASUPCLIENT)
	struct wpa_ctrl *wpasup_ctrl;
	int wpasup_ctrl_fd;
	int wpasup_pid;
#endif
	char iface[16];
	int udhcpc_pid;
} wifi2_t;

extern "C" {
void *wifi2_global;
}

static const char* st_str(int st) {
	struct {
		const char *name;
		unsigned flag;
	} lut[] = {
		{"ST_INIT", ST_INIT},
		{"ST_IF_DOWN", ST_IF_DOWN},
		{"ST_IF_UP", ST_IF_UP},
		{"ST_SCANNING", ST_SCANNING},
		{"ST_CONNECTING", ST_CONNECTING},
		{"ST_CONNECTED_L2", ST_CONNECTED_L2},
		{"ST_DHCP", ST_DHCP},
		{"ST_CONNECTED_L3", ST_CONNECTED_L3},
		{"ST_RETRY", ST_RETRY},
		{"ST_RESET", ST_RESET},
		{NULL}
	}, *lut_iter;
	int pos = 0, r;
	
	for (lut_iter = lut; lut_iter->name; lut_iter++) {
		if (st == lut_iter->flag) return lut_iter->name;
	}
	return "";
}

static void set_timer(wifi2_t *wifi2, int sec) {
	struct itimerspec its = {0};

	if (sec > 0) {
		its.it_value.tv_sec = sec;
	}
	timerfd_settime(wifi2->timer_fd, 0, &its, NULL);
}

static void set_deadline_ms(wifi2_t *wifi2, unsigned long ms) {
	struct itimerspec its = {0};
	if (ms > 0) {
		its.it_value.tv_sec = ms / 1000;
		its.it_value.tv_nsec = (ms % 1000) * 1000000;
	}
	timerfd_settime(wifi2->timer_fd, 0, &its, NULL);
}

static int iface_set_up(wifi2_t *wifi2, int up) {
	char cmd[128];
	int ret = -1;
#if 1
	ret = aloe_ifup(wifi2->iface, up);
#else
	snprintf(cmd, sizeof(cmd), "ip link set %s %s", wifi2->iface, up ? "up" : "down");
	ret = system(cmd);
#endif
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

#if 1
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
#else
	system("killall udhcpc 2>/dev/null");
	snprintf(cmd, sizeof(cmd), "udhcpc -i %s -n &", wifi2->iface);
	ret = system(cmd);
#endif
	return ret;
}

static int wpasup_start(wifi2_t *wifi2, int en) {
	char wpasup_cfg[] = "/var/run/wpa_supplicant.conf";
	char cmd[128];
	int ret = -1;

#if 1
	pid_t pid;
	int argc = 0, argv_cnt, r;
	char *argv[20];

	argv_cnt = aloe_arraysize(argv);

	if (wifi2->wpasup_pid > 0) {
		pid_t wp;

		if ((r = kill(wifi2->wpasup_pid, SIGTERM)) != 0) {
			r = errno;
			log_e("Sanity check, failed kill wpasup (%d): %s\n",
					wifi2->wpasup_pid, strerror(r));
		}
		for (int i = 0; i < 10; i++) {
			wp = waitpid(wifi2->wpasup_pid, NULL, WNOHANG);

			if (wp == wifi2->wpasup_pid) {
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

	snprintf(cmd, sizeof(cmd), "echo >%s", wpasup_cfg);
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
#else
	system("killall wpa_supplicant 2>/dev/null");

	snprintf(cmd, sizeof(cmd), "echo >%s", wpasup_cfg);
	system(cmd);

	snprintf(cmd, sizeof(cmd), "wpa_supplicant -D nl80211 -i %s -c %s -B",
			wifi2->iface, wpasup_cfg);
	ret = system(cmd);
#endif
	return ret;
}

#if defined(USE_NLCONN)
static int send_connect(wifi2_t *wifi2) {
	struct nl_msg *msg;

	if ((msg = nlmsg_alloc()) == NULL) return -1;

	genlmsg_put(msg, 0, 0, wifi2->nl80211_id, 0, 0, NL80211_CMD_CONNECT, 0);

	nla_put_u32(msg, NL80211_ATTR_IFINDEX, wifi2->ifindex);
	nla_put(msg, NL80211_ATTR_SSID, 4, "TEST"); // <-- change SSID

	int ret = nl_send_auto(wifi2->nl_sock, msg);
	nlmsg_free(msg);
	return ret;
}

static int send_scan(wifi2_t *wifi2) {
	struct nl_msg *msg;
	int ret;

	if ((msg = nlmsg_alloc()) == NULL) return -1;

	genlmsg_put(msg, 0, 0, wifi2->nl80211_id, 0, 0, NL80211_CMD_TRIGGER_SCAN,
			0);

	nla_put_u32(msg, NL80211_ATTR_IFINDEX, wifi2->ifindex);

	ret = nl_send_auto(wifi2->nl_sock, msg);
	nlmsg_free(msg);
	return ret;
}
#endif // USE_NLCONN

#if defined(USE_WPASUPCLIENT)
static int wpa_connect_ctrl(wifi2_t *wifi2) {
	char ctrl_path[64] = "/var/run/wpa_supplicant/wlx94186551a58a";
	int ret = -1, r;

	r = snprintf(ctrl_path, sizeof(ctrl_path), "/var/run/wpa_supplicant/%s", wifi2->iface);
	if (r >= sizeof(ctrl_path)) {
		ctrl_path[sizeof(ctrl_path) - 1] = '\0';
	}

	if ((wifi2->wpasup_ctrl = wpa_ctrl_open(ctrl_path)) == NULL) {
		log_e("failed open %s\n", ctrl_path);
		goto finally;
	}

	if (wpa_ctrl_attach(wifi2->wpasup_ctrl) != 0) {
		log_e("failed attach wpasup control socket\n");
		goto finally;
	}

	if ((wifi2->wpasup_ctrl_fd = wpa_ctrl_get_fd(wifi2->wpasup_ctrl)) == -1) {
		log_e("failed get wpasup control socket fd\n");
		goto finally;
	}
	ret = 0;
finally:
	if (ret != 0) {
		if (wifi2->wpasup_ctrl) {
			wpa_ctrl_close(wifi2->wpasup_ctrl);
			wifi2->wpasup_ctrl = NULL;
		}
	}
	return ret;
}

static int wpa_cmd(wifi2_t *wifi2, const char *cmd, char *reply, size_t *len) {
	if (!wifi2->wpasup_ctrl) {
		log_e("no open wpasup control socket\n");
		return -1;
	}
	return wpa_ctrl_request(wifi2->wpasup_ctrl, cmd, strlen(cmd), reply, len,
			NULL);
}

static int wpa_ping(wifi2_t *wifi2) {
	char r[16];
	size_t l = sizeof(r);

	if (wpa_cmd(wifi2, "PING", r, &l) == 0 && strstr(r, "PONG")) {
		return 0;
	}
	return -1;
}

static void handle_wpa_event(wifi2_t *wifi2) {
	char buf[256];
	size_t len = sizeof(buf) - 1;

	if (wpa_ctrl_recv(wifi2->wpasup_ctrl, buf, &len) == 0) {
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

static void wpa_scan(wifi2_t *wifi2) {
	char reply[32];
	size_t len = sizeof(reply);
	wpa_cmd(wifi2, "SCAN", reply, &len);
}

static void wpa_select_network(wifi2_t *wifi2) {
	char reply[32];
	size_t len = sizeof(reply);

	wpa_cmd(wifi2, "SELECT_NETWORK 0", reply, &len);
}

#endif // USE_WPASUPCLIENT

#if defined(USE_NLCONN)
static int nl_event_handler(struct nl_msg *msg, void *arg) {
	wifi2_t *wifi2 = (wifi2_t*)arg;
	struct nlmsghdr *nlh = (struct nlmsghdr*)nlmsg_hdr(msg);
	struct genlmsghdr *ghdr = (struct genlmsghdr*)nlmsg_data(nlh);

	log_d("cmd: %d\n", (int)ghdr->cmd);

	switch (ghdr->cmd) {
	case NL80211_CMD_CONNECT:
		log_d("NL: CONNECTED\n");
		log_d("%s -> %s\n", st_str(wifi2->state), st_str(ST_CONNECTED_L2));
		wifi2->state = ST_CONNECTED_L2;
		break;

	case NL80211_CMD_DISCONNECT:
		log_d("NL: DISCONNECTED\n");
		log_d("%s -> %s\n", st_str(wifi2->state), st_str(ST_RETRY));
		wifi2->state = ST_RETRY;
		break;

	case NL80211_CMD_NEW_SCAN_RESULTS:
		log_d("NL: SCAN DONE\n");
		log_d("%s -> %s\n", st_str(wifi2->state), st_str(ST_CONNECTING));
		wifi2->state = ST_CONNECTING;
		break;
	}
	return NL_OK;
}
#endif // USE_NLCONN

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
//					log_d("%s -> %s\n", st_str(wifi2->state), st_str(ST_CONNECTED_L3));
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
//					log_d("%s -> %s\n", st_str(wifi2->state), st_str(ST_RETRY));
					wifi2->state = ST_RETRY;
				}
			}
			continue;
		}

		log_d("nlmsg[%d], nlmsg_type: %d\n", nlmsg_idx, (int)nh->nlmsg_type);
	}
}

#if defined(USE_WPASUPCLIENT)
static void run_sm(wifi2_t *wifi2) {
	char cmd_buf[100];

	switch (wifi2->state) {
	case ST_INIT: {
		iface_set_up(wifi2, 0);
		iface_set_up(wifi2, 1);
		wpasup_start(wifi2, 1);
		sleep(1); // allow socket ready
		wpa_connect_ctrl(wifi2);
		wifi2->state = ST_WPA_READY;
		break;
	}
	case ST_WPA_READY:
		wpa_scan(wifi2);
		set_timer(wifi2, 5);
		wifi2->state = ST_SCANNING;
		break;

	case ST_SCANNING:
		break;

	case ST_ASSOCIATING:
		wpa_select_network(wifi2);
		set_timer(wifi2, 10);
		break;

	case ST_WPA_COMPLETED:
		printf("L2 connected\n");
		dhcp_start(wifi2, 1);
		set_timer(wifi2, 8);
		wifi2->state = ST_DHCP;
		break;

	case ST_DHCP:
		break;

	case ST_WPA_CONNECTED:
		printf("FULLY CONNECTED\n");
		break;

	case ST_RETRY:
		printf("Retrying...\n");
		set_timer(wifi2, 2);
		wifi2->state = ST_RESET;
		break;

	case ST_RESET:
		iface_set_up(wifi2, 0);
		iface_set_up(wifi2, 1);
		wifi2->state = ST_WPA_READY;
		break;
	}
}
#else
static void run_sm(wifi2_t *wifi2) {
	switch (wifi2->state) {
	case ST_INIT:
		log_d("%s -> %s\n", st_str(wifi2->state), st_str(ST_IF_DOWN));
		iface_set_up(wifi2, 0);
		wifi2->state = ST_IF_DOWN;
		break;

	case ST_IF_DOWN:
		log_d("%s -> %s\n", st_str(wifi2->state), st_str(ST_IF_UP));
		iface_set_up(wifi2, 1);
		set_timer(wifi2, 1);
		wifi2->state = ST_IF_UP;
		break;

	case ST_IF_UP:
		log_d("%s -> %s\n", st_str(wifi2->state), st_str(ST_SCANNING));
#if defined(USE_NLCONN)
		send_scan(wifi2);
#endif
		set_timer(wifi2, 5);
		wifi2->state = ST_SCANNING;
		break;

	case ST_SCANNING:
		log_d("%s -> %s\n", st_str(wifi2->state), st_str(wifi2->state));
		break;

	case ST_CONNECTING:
		log_d("%s -> %s\n", st_str(wifi2->state), st_str(wifi2->state));
#if defined(USE_NLCONN)
		send_connect(wifi2);
#endif
		set_timer(wifi2, 10);
		break;

	case ST_CONNECTED_L2:
		log_d("%s -> %s\n", st_str(wifi2->state), st_str(ST_DHCP));
		dhcp_start(wifi2);
		set_timer(wifi2, 8);
		wifi2->state = ST_DHCP;
		break;

	case ST_DHCP:
		log_d("%s -> %s\n", st_str(wifi2->state), st_str(wifi2->state));
		break;

	case ST_CONNECTED_L3:
		log_d("%s -> %s\n", st_str(wifi2->state), st_str(wifi2->state));
		break;

	case ST_RETRY:
		log_d("%s -> %s\n", st_str(wifi2->state), st_str(ST_RESET));
		set_timer(wifi2, 2);
		wifi2->state = ST_RESET;
		break;

	case ST_RESET:
		log_d("%s -> %s\n", st_str(wifi2->state), st_str(ST_IF_UP));
		iface_set_up(wifi2, 0);
		iface_set_up(wifi2, 1);
		wifi2->state = ST_IF_UP;
		break;
	}
}
#endif

#if defined(USE_NLCONN)
static void init_nl(wifi2_t *wifi2) {
	int ret = -1, r;

	if ((wifi2->nl_sock = nl_socket_alloc()) == NULL) {
		log_e("nl_socket_alloc\n");
		goto finally;
	}
	if ((r = genl_connect(wifi2->nl_sock)) != 0) {
		log_e("genl_connect: %s\n", nl_geterror(r));;
		goto finally;
	}

	if ((r = genl_ctrl_resolve(wifi2->nl_sock, "nl80211")) < 0) {
		log_e("genl_ctrl_resolve: %s\n", nl_geterror(r));;
		goto finally;
	}
	wifi2->nl80211_id = r;
	log_d("nl80211_id (Generic Netlink family): %d\n", (int)wifi2->nl80211_id);

	nl_socket_modify_cb(wifi2->nl_sock, NL_CB_VALID, NL_CB_CUSTOM,
			nl_event_handler, wifi2);
	ret = 0;
finally:
	if (ret != 0) {
		if (wifi2->nl_sock) {
			nl_socket_free(wifi2->nl_sock);
			wifi2->nl_sock = NULL;
		}
	}
}
#endif // USE_NLCONN

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

	if (wifi2->evconn.fd != wifi2->epfd) {
		log_e("sanity check mismatch epfd\n");
		return;
	}
	n = epoll_wait(wifi2->epfd, events, MAX_EVENTS, 0);
	for (int i = 0; i < n; i++) {
//		log_d("event[%d/%d]\n", i + 1, n);
		if (events[i].data.fd == wifi2->rtnl_fd) {
			handle_rtnl(wifi2);
#if defined(USE_NLCONN)
		} else if (events[i].data.fd == nl_socket_get_fd(wifi2->nl_sock)) {
			nl_recvmsgs_default(wifi2->nl_sock);
#endif
		} else if (events[i].data.fd == wifi2->timer_fd) {
			uint64_t exp;
			read(wifi2->timer_fd, &exp, sizeof(exp));
			log_d("TIMER\n");
			log_d("%s -> %s\n", st_str(wifi2->state), st_str(ST_RESET));
			wifi2->state = ST_RESET;
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

void* wifi2_init(void *evctx, const char *iface) {
	struct epoll_event ev;
	wifi2_t *wifi2 = NULL;
	int ret = -1, r;

	if ((wifi2 = (wifi2_t*)aloe_calloc(1, sizeof(*wifi2))) == NULL) {
		log_e("failed alloc wifi2\n");
		goto finally;
	}
	wifi2->timer_fd = wifi2->epfd = wifi2->rtnl_fd = -1;
	wifi2->ifindex = 0;
	wifi2->state = ST_INIT;
	strncpy(wifi2->iface, (iface ? iface : "wlan0"), sizeof(wifi2->iface));
	wifi2->iface[sizeof(wifi2->iface) - 1] = '\0';

	if ((wifi2->ifindex = if_nametoindex(wifi2->iface)) == 0) {
		r = errno;
		log_e("failed get interface %s index; %s\n", wifi2->iface, strerror(r));
		goto finally;
	}

#if defined(USE_NLCONN)
	init_nl(wifi2);
#endif
	init_rtnl(wifi2);

	wifi2->timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);

	wifi2->epfd = epoll_create1(0);

	memset(&ev, 0, sizeof(ev));

	ev.events = EPOLLIN;
	ev.data.fd = wifi2->rtnl_fd;
	epoll_ctl(wifi2->epfd, EPOLL_CTL_ADD, wifi2->rtnl_fd, &ev);

#if defined(USE_NLCONN)
	ev.data.fd = nl_socket_get_fd(wifi2->nl_sock);
#endif
	epoll_ctl(wifi2->epfd, EPOLL_CTL_ADD, ev.data.fd, &ev);

	ev.data.fd = wifi2->timer_fd;
	epoll_ctl(wifi2->epfd, EPOLL_CTL_ADD, wifi2->timer_fd, &ev);

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
#if defined(USE_NLCONN)
		if (wifi2->nl_sock) nl_socket_free(wifi2->nl_sock);
#endif
		aloe_free(wifi2);
		wifi2 = NULL;
	}
	return wifi2;
}

int wifi2_cli(void *_wifi2, int argc, const char **argv) {
	wifi2_t *wifi2 = (wifi2_t*)_wifi2;

//	dump_argv(argc, argv);

	if (argc < 2 || strcasecmp(argv[1], "help") == 0) {
		FILE *fout = stdout;

		fprintf(fout,
"COMMAND\n"
"    wifi state      - Show current FSM\n"
"    wifi pause      - Toggle FSM\n"
"    wifi timer [ms] - Set timer timeout milliseconds\n"
"    iflink [0 | 1]  - Set interface down or up\n"
"    dhcp [0 | 1]    - udhcpc stop or restart\n"
"    wpasup [0 | 1]  - wpa_supplicant stop or restart\n"
		);
		fflush(fout);
		return 0;
	}
	if (!wifi2) {
		log_e("wifi2 absent\n");
		return 1;
	}
	if (argc >= 2 && strcasecmp(argv[1], "state") == 0) {
		log_d("%s (%d)%s\n", st_str(wifi2->state), wifi2->state,
				(wifi2->pause ? "(paused)" : ""));
		return 0;
	}
	if (argc >= 2 && strcasecmp(argv[1], "pause") == 0) {
		// toggle
		wifi2_pause(wifi2, 2);
		log_d("%s (%d)%s\n", st_str(wifi2->state), wifi2->state,
				(wifi2->pause ? "(paused)" : ""));
		return 0;
	}
	if (argc >= 2 && strcasecmp(argv[1], "timer") == 0) {
		unsigned long dur = argc >= 3 ? strtol(argv[2], NULL, 0) : -1ul;

		if (dur != -1ul) {
			set_deadline_ms(wifi2, dur);
			log_d("set %lu milliseconds latter\n", dur);
		}
		return 0;
	}
	if (argc >= 2 && strcasecmp(argv[1], "iflink") == 0) {
		int en = argc >= 3 ? strtol(argv[2], NULL, 0) : 0;

		return iface_set_up(wifi2, en);
	}
	if (argc >= 2 && strcasecmp(argv[1], "dhcp") == 0) {
		int en = argc >= 3 ? strtol(argv[2], NULL, 0) : 0;

		return dhcp_start(wifi2, en);
	}
	if (argc >= 2 && strcasecmp(argv[1], "wpasup") == 0) {
		int en = argc >= 3 ? strtol(argv[2], NULL, 0) : 0;

		return wpasup_start(wifi2, en);
	}
	if (argc >= 2 && strcasecmp(argv[1], "wificfg") == 0) {
		int en = argc >= 3 ? strtol(argv[2], NULL, 0) : 0;

		return wpasup_start(wifi2, en);
	}
	return 1;
}
