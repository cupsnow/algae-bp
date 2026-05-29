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
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <netlink/msg.h>
#include <netlink/attr.h>
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

//#define IFACE "wlan0"
#define MAX_EVENTS 8

typedef enum {
	ST_INIT = 0,
	ST_IF_DOWN,
	ST_IF_UP,
	ST_WPA_START,
	ST_WPA_READY,
	ST_WPA_COMPLETED,
	ST_SCANNING,
	ST_CONNECTING,
	ST_ASSOCIATING,
	ST_CONNECTED_L2,
	ST_DHCP,
	ST_CONNECTED_L3,
	ST_RETRY,
	ST_RESET,

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

	its.it_value.tv_sec = sec;
	timerfd_settime(wifi2->timer_fd, 0, &its, NULL);
}

static void set_deadline_ms(wifi2_t *wifi2, int ms) {
	struct itimerspec its = {0};
	its.it_value.tv_sec = ms / 1000;
	its.it_value.tv_nsec = (ms % 1000) * 1000000;
	timerfd_settime(wifi2->timer_fd, 0, &its, NULL);
}

static void iface_set_up(wifi2_t *wifi2, int up) {
	char cmd[128];

#if 1
	int s = -1, r;
	struct ifreq ifr = {};

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		log_e("failed open socket\n");
		goto finally;
	}

	strncpy(ifr.ifr_name, wifi2->iface, IFNAMSIZ);

	if (ioctl(s, SIOCGIFFLAGS, &ifr) < 0) {
		log_e("failed get ifflag\n");
		goto finally;
	}
	if (up) {
		ifr.ifr_flags |= IFF_UP;
	} else {
		ifr.ifr_flags &= ~IFF_UP;
	}

	if ((r = ioctl(s, SIOCSIFFLAGS, &ifr)) < 0) {
		log_e("failed set ifflag\n");
		goto finally;
	}
finally:
	if (s != -1) close(s);
#else
	snprintf(cmd, sizeof(cmd), "ip link set %s %s", wifi2->iface, up ? "up" : "down");
	system(cmd);
#endif
}

static void dhcp_start(wifi2_t *wifi2) {
	char cmd[128];

#if 1
	if (wifi2->udhcpc_pid > 0) {
		kill(wifi2->udhcpc_pid, SIGTERM);
		waitpid(wifi2->udhcpc_pid, NULL, 0);
		wifi2->udhcpc_pid = -1;
	}

	pid_t pid = fork();
	if (pid < 0) {
		log_e("Failed fork\n");
		return;
	}

	if (pid == 0) {
		execlp("udhcpc", "udhcpc",
			   "-i", wifi2->iface,
			   "-f",            // foreground (so we can supervise)
			   "-q",            // quit after lease
			   "-n",            // fail fast if no lease
			   NULL);
		_exit(127);
	}
	wifi2->udhcpc_pid = pid;
#else
	system("killall udhcpc 2>/dev/null");
	snprintf(cmd, sizeof(cmd), "udhcpc -i %s -n &", wifi2->iface);
	system(cmd);
#endif
}

static void wpasup_start(wifi2_t *wifi2) {
	char wpasup_cfg[] = "/var/run/wpa_supplicant.conf";
	char cmd[128];

#if 1
    if (wifi2->wpasup_pid > 0) {
        kill(wifi2->wpasup_pid, SIGTERM);
        for (int i = 0; i < 10; i++) {
            if (waitpid(wifi2->wpasup_pid, NULL, WNOHANG) > 0) break;
            usleep(100 * 1000);
        }
        kill(wifi2->wpasup_pid, SIGKILL);
        waitpid(wifi2->wpasup_pid, NULL, 0);
        wifi2->wpasup_pid = -1;
    }

	snprintf(cmd, sizeof(cmd), "echo >%s", wpasup_cfg);
	system(cmd);

	pid_t pid = fork();
	if (pid < 0) {
		log_e("Failed fork\n");
		return;
	}

	if (pid == 0) {
		// child
		execlp("wpa_supplicant",
				"wpa_supplicant",
				"-i", wifi2->iface,
				"-c", wpasup_cfg,
				"-C", "/var/run/wpa_supplicant", // ctrl dir
				"-f", "/var/log/wpa_supplicant.log", // optional
				NULL);
		_exit(127);
	}
	wifi2->wpasup_pid = pid;
#else
	system("killall wpa_supplicant 2>/dev/null");

	snprintf(cmd, sizeof(cmd), "echo >%s", wpasup_cfg);
	system(cmd);

	snprintf(cmd, sizeof(cmd), "wpa_supplicant -D nl80211 -i %s -c %s -B",
			wifi2->iface, wpasup_cfg);
	system(cmd);
#endif
	return;
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

static void handle_rtnl(wifi2_t *wifi2) {
	char buf[4096];
	int len, nlmsg_idx = 0;

	len = recv(wifi2->rtnl_fd, buf, sizeof(buf), 0);

	log_d("nlmsg len %d\n", len);

	for (struct nlmsghdr *nh = (struct nlmsghdr*)buf; NLMSG_OK(nh, len);
			nh = NLMSG_NEXT(nh, len), nlmsg_idx++) {

		if (nh->nlmsg_type == RTM_NEWADDR) {
			log_d("nlmsg[%d], RTM_NEWADDR\n", nlmsg_idx);

			if (wifi2->state == ST_DHCP) {
				log_d("%s -> %s\n", st_str(wifi2->state), st_str(ST_CONNECTED_L3));
				wifi2->state = ST_CONNECTED_L3;
			}
			continue;
		}

		if (nh->nlmsg_type == RTM_NEWLINK) {
			struct ifinfomsg *ifi = (struct ifinfomsg*)NLMSG_DATA(nh);
			if (ifi->ifi_index == wifi2->ifindex) {
				char flag_str[128];

				aloe_ifflag_str(flag_str, sizeof(flag_str), ifi->ifi_flags, NULL);

				log_d("nlmsg[%d], RTM_NEWLINK flag: 0x%x (%s)\n", nlmsg_idx,
						ifi->ifi_flags, flag_str);
				if (!(ifi->ifi_flags & IFF_RUNNING)) {
					log_d("RTNL: LINK DOWN\n");
					log_d("%s -> %s\n", st_str(wifi2->state), st_str(ST_RETRY));
					wifi2->state = ST_RETRY;
				}
			}
			continue;
		}

		if (nh->nlmsg_type == RTM_DELADDR) {
			struct ifinfomsg *ifi = (struct ifinfomsg*)NLMSG_DATA(nh);
			if (ifi->ifi_index == wifi2->ifindex) {
				char flag_str[128];

				aloe_ifflag_str(flag_str, sizeof(flag_str), ifi->ifi_flags, NULL);

				log_d("nlmsg[%d], RTM_DELADDR flag: 0x%x (%s)\n", nlmsg_idx,
						ifi->ifi_flags, flag_str);
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
		wpasup_start(wifi2);
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
		dhcp_start(wifi2);
		set_timer(wifi2, 8);
		wifi2->state = ST_DHCP;
		break;

	case ST_DHCP:
		break;

	case ST_CONNECTED:
		printf("FULLY CONNECTED\n");
		break;

	case ST_RETRY:
		printf("Retrying...\n");
		set_timer(2);
		state = ST_RESET;
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
    wifi2->rtnl_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);

    struct sockaddr_nl addr = {
//        .nl_family = AF_NETLINK,
//        .nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR
    };

    addr.nl_family = AF_NETLINK;
    addr.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR;


    bind(wifi2->rtnl_fd, (struct sockaddr*)&addr, sizeof(addr));
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
		log_d("event[%d/%d]\n", i + 1, n);
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
		printf(
"wifi commands:\n"
"  state - show current state\n"
"  pause - toggle state machine\n");

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


	return 1;
}
