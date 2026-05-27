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
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>

#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <netlink/msg.h>
#include <netlink/attr.h>
#include <linux/nl80211.h>

#include "wifi.h"
#include "priv_ev.h"

//#define IFACE "wlan0"
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
	ST_RESET
} state_t;

typedef struct {
	evconn_t evconn;
	state_t state, pause;
	int ifindex;
	int epfd;
	int timer_fd;
	int nlrt_fd;
	struct nl_sock *nl_sock;
	int nl80211_id;
	char iface[16];
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

static int ifflag_str(char *str, size_t str_sz, unsigned iflag, const char *sep) {
	struct {
		const char *name;
		unsigned flag;
	} lut[] = {
		{"UP", IFF_UP},
		{"BROADCAST", IFF_BROADCAST},
		{"LOOPBACK", IFF_LOOPBACK},
		{"RUNNING", IFF_RUNNING},
		{"PROMISC", IFF_PROMISC},
		{"MULTICAST", IFF_MULTICAST},
		{"PORTSEL", IFF_PORTSEL},
		{"AUTOMEDIA", IFF_AUTOMEDIA},
		{"DYNAMIC", IFF_DYNAMIC},
		{NULL}
	}, *lut_iter;
	int pos = 0, r;
	unsigned found = 0;

	if (!sep) sep = ", ";
	for (lut_iter = lut; lut_iter->name; lut_iter++) {
		log_d("pos %d, name %s\n", pos, lut_iter->name);
		if (!(iflag & lut_iter->flag)) continue;
		log_d("pos %d, name %s, 0x%x\n", pos, lut_iter->name, iflag & lut_iter->flag);
		if (pos >= str_sz || (r = snprintf(str + pos, str_sz - pos, 
				"%s%s", (pos > 0 ? sep : ""), lut_iter->name)) <= 0
				|| (r + pos) >= str_sz) {
			goto finally;
		}
		pos += r;
		found |= lut_iter->flag;
	}
finally:
	if (pos < str_sz && (iflag != found) && (r = snprintf(str + pos, str_sz - pos,
			"%s%s", (pos > 0 ? sep : ""), "...") > 0 && (r + pos) < str_sz)) {
		pos += r;
	}
	if (pos >= str_sz) pos = str_sz - 1;
	str[pos] = '\0';
	return pos;
}

static void set_timer(wifi2_t *wifi2, int sec) {
	struct itimerspec its = {0};

	its.it_value.tv_sec = sec;
	timerfd_settime(wifi2->timer_fd, 0, &its, NULL);
}

static void iface_set_up(wifi2_t *wifi2, int up) {
	char cmd[128];

	snprintf(cmd, sizeof(cmd), "ip link set %s %s", wifi2->iface, up ? "up" : "down");
	system(cmd);
}

static void dhcp_start(wifi2_t *wifi2) {
	char cmd[128];

	system("killall udhcpc 2>/dev/null");
	snprintf(cmd, sizeof(cmd), "udhcpc -i %s -n &", wifi2->iface);
	system(cmd);
}

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

static void handle_nlrt(wifi2_t *wifi2) {
	char buf[4096];
	int len;

	len = recv(wifi2->nlrt_fd, buf, sizeof(buf), 0);

	log_d("recv len %d\n", len);

	for (struct nlmsghdr *nh = (struct nlmsghdr*)buf; NLMSG_OK(nh, len);
			nh = NLMSG_NEXT(nh, len)) {

		if (nh->nlmsg_type == RTM_NEWADDR) {
			log_d("nlrt: IP assigned\n");
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

				ifflag_str(flag_str, sizeof(flag_str), ifi->ifi_flags, NULL);

				log_d("RTM_NEWLINK flag: 0x%x (%s)\n", ifi->ifi_flags, flag_str);
				if (!(ifi->ifi_flags & IFF_RUNNING)) {
					log_d("NLRT: LINK DOWN\n");
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

				ifflag_str(flag_str, sizeof(flag_str), ifi->ifi_flags, NULL);

				log_d("RTM_DELADDR flag: 0x%x (%s)\n", ifi->ifi_flags, flag_str);
			}
			continue;
		}

		log_d("nlmsg_type: %d\n", (int)nh->nlmsg_type);
	}
}

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
		send_scan(wifi2);
		set_timer(wifi2, 5);
		wifi2->state = ST_SCANNING;
		break;

	case ST_SCANNING:
		log_d("%s -> %s\n", st_str(wifi2->state), st_str(wifi2->state));
		break;

	case ST_CONNECTING:
		log_d("%s -> %s\n", st_str(wifi2->state), st_str(wifi2->state));
		send_connect(wifi2);
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

static void init_nl(wifi2_t *wifi2) {
	wifi2->nl_sock = nl_socket_alloc();
	genl_connect(wifi2->nl_sock);

	wifi2->nl80211_id = genl_ctrl_resolve(wifi2->nl_sock, "nl80211");

	nl_socket_modify_cb(wifi2->nl_sock, NL_CB_VALID, NL_CB_CUSTOM,
			nl_event_handler, wifi2);
}

static void init_nlrt(wifi2_t *wifi2) {
    wifi2->nlrt_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);

    struct sockaddr_nl addr = {
        .nl_family = AF_NETLINK,
        .nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR
    };

    bind(wifi2->nlrt_fd, (struct sockaddr*)&addr, sizeof(addr));
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
		if (events[i].data.fd == wifi2->nlrt_fd) {
			handle_nlrt(wifi2);
		} else if (events[i].data.fd == nl_socket_get_fd(wifi2->nl_sock)) {
			nl_recvmsgs_default(wifi2->nl_sock);
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

void* wifi2_init(void *evctx, const char *iface) {
	struct epoll_event ev;
	wifi2_t *wifi2 = NULL;
	int ret = -1, r;

	if ((wifi2 = (wifi2_t*)aloe_calloc(1, sizeof(*wifi2))) == NULL) {
		log_e("failed alloc wifi2\n");
		goto finally;
	}
	wifi2->timer_fd = wifi2->epfd = wifi2->nlrt_fd = -1;
	wifi2->ifindex = 0;
	wifi2->state = ST_INIT;
	strncpy(wifi2->iface, (iface ? iface : "wlan0"), sizeof(wifi2->iface));
	wifi2->iface[sizeof(wifi2->iface) - 1] = '\0';

	if ((wifi2->ifindex = if_nametoindex(wifi2->iface)) == 0) {
		r = errno;
		log_e("failed get interface %s index; %s\n", wifi2->iface, strerror(r));
		goto finally;
	}

	init_nl(wifi2);
	init_nlrt(wifi2);

	wifi2->timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);

	wifi2->epfd = epoll_create1(0);

	memset(&ev, 0, sizeof(ev));

	ev.events = EPOLLIN;
	ev.data.fd = wifi2->nlrt_fd;
	epoll_ctl(wifi2->epfd, EPOLL_CTL_ADD, wifi2->nlrt_fd, &ev);

	ev.data.fd = nl_socket_get_fd(wifi2->nl_sock);
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
	log_d("wifi2 initialized, ctx: 0x%x\n", (unsigned)(unsigned long)wifi2);
	ret = 0;
finally:
	if (ret != 0 && wifi2) {
		if (wifi2->evconn.ev) aloe_ev_cancel(wifi2->evconn.ev_ctx, wifi2->evconn.ev);
		if (wifi2->epfd != -1) close(wifi2->epfd);
		if (wifi2->timer_fd != -1) close(wifi2->timer_fd);
		if (wifi2->nlrt_fd) close(wifi2->nlrt_fd);
		if (wifi2->nl_sock) nl_socket_free(wifi2->nl_sock);
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
		wifi2->pause = wifi2->pause ? ST_INIT : ST_RESET;
		log_d("%s (%d)%s\n", st_str(wifi2->state), wifi2->state,
				(wifi2->pause ? "(paused)" : ""));
		return 0;
	}


	return 1;
}
