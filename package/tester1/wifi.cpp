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

#define IFACE "wlan0"
#define MAX_EVENTS 8

//====================
// State machine
//====================
/*
| Current State  | Event            | Next State     |
| -------------- | ---------------- | -------------- |
| IF_UP          | start            | SCANNING       |
| SCANNING       | scan done        | AUTHENTICATING |
| AUTHENTICATING | connect success  | CONNECTED_L2   |
| AUTHENTICATING | disconnect       | RETRY_BACKOFF  |
| CONNECTED_L2   | start DHCP       | DHCP           |
| DHCP           | IP assigned      | CONNECTED_L3   |
| ANY            | disconnect event | DISCONNECTED   |
| ANY            | timeout          | RESET          |
*/
typedef enum {
    ST_INIT,
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
} wifi2_t;

static state_t state = ST_INIT;

//====================
// Globals
//====================
static int ifindex;
static int epfd;
static int timer_fd;
static int rtnl_fd;

static struct nl_sock *nl_sock;
static int nl80211_id;

//====================
// Utility
//====================
static void set_timer(int sec)
{
    struct itimerspec its = {0};
    its.it_value.tv_sec = sec;
    timerfd_settime(timer_fd, 0, &its, NULL);
}

static void iface_set_up(int up)
{
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "ip link set %s %s", IFACE, up ? "up" : "down");
    system(cmd);
}

//====================
// DHCP control
//====================
static void dhcp_start(void)
{
    system("killall udhcpc 2>/dev/null");
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "udhcpc -i %s -n &", IFACE);
    system(cmd);
}

//====================
// nl80211 helpers
//====================
static int send_connect(void)
{
    struct nl_msg *msg = nlmsg_alloc();
    if (!msg) return -1;

    genlmsg_put(msg, 0, 0, nl80211_id, 0, 0,
                NL80211_CMD_CONNECT, 0);

    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex);
    nla_put(msg, NL80211_ATTR_SSID, 4, "TEST"); // <-- change SSID

    int ret = nl_send_auto(nl_sock, msg);
    nlmsg_free(msg);
    return ret;
}

static int send_scan(void)
{
    struct nl_msg *msg = nlmsg_alloc();
    if (!msg) return -1;

    genlmsg_put(msg, 0, 0, nl80211_id, 0, 0,
                NL80211_CMD_TRIGGER_SCAN, 0);

    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex);

    int ret = nl_send_auto(nl_sock, msg);
    nlmsg_free(msg);
    return ret;
}

//====================
// nl80211 event handler
//====================
static int nl_event_handler(struct nl_msg *msg, void *arg)
{
    struct nlmsghdr *nlh = (struct nlmsghdr*)nlmsg_hdr(msg);
    struct genlmsghdr *ghdr = (struct genlmsghdr*)nlmsg_data(nlh);

    switch (ghdr->cmd) {
    case NL80211_CMD_CONNECT:
        printf("NL: CONNECTED\n");
        state = ST_CONNECTED_L2;
        break;

    case NL80211_CMD_DISCONNECT:
        printf("NL: DISCONNECTED\n");
        state = ST_RETRY;
        break;

    case NL80211_CMD_NEW_SCAN_RESULTS:
        printf("NL: SCAN DONE\n");
        state = ST_CONNECTING;
        break;
    }
    return NL_OK;
}

//====================
// rtnetlink handler
//====================
static void handle_rtnl(void)
{
    char buf[4096];
    int len = recv(rtnl_fd, buf, sizeof(buf), 0);

    for (struct nlmsghdr *nh = (struct nlmsghdr *)buf;
         NLMSG_OK(nh, len);
         nh = NLMSG_NEXT(nh, len)) {

        if (nh->nlmsg_type == RTM_NEWADDR) {
            printf("RTNL: IP assigned\n");
            if (state == ST_DHCP)
                state = ST_CONNECTED_L3;
        }

        if (nh->nlmsg_type == RTM_NEWLINK) {
            struct ifinfomsg *ifi = (struct ifinfomsg*)NLMSG_DATA(nh);
            if (ifi->ifi_index == ifindex) {
                if (!(ifi->ifi_flags & IFF_RUNNING)) {
                    printf("RTNL: LINK DOWN\n");
                    state = ST_RETRY;
                }
            }
        }
    }
}

//====================
// State machine runner
//====================
static void run_sm(void)
{
    switch (state) {

    case ST_INIT:
        iface_set_up(0);
        state = ST_IF_DOWN;
        break;

    case ST_IF_DOWN:
        iface_set_up(1);
        set_timer(1);
        state = ST_IF_UP;
        break;

    case ST_IF_UP:
        printf("→ SCAN\n");
        send_scan();
        set_timer(5);
        state = ST_SCANNING;
        break;

    case ST_SCANNING:
        break;

    case ST_CONNECTING:
        printf("→ CONNECT\n");
        send_connect();
        set_timer(10);
        break;

    case ST_CONNECTED_L2:
        printf("→ DHCP\n");
        dhcp_start();
        set_timer(8);
        state = ST_DHCP;
        break;

    case ST_DHCP:
        break;

    case ST_CONNECTED_L3:
        printf("CONNECTED OK\n");
        break;

    case ST_RETRY:
        printf("→ RETRY\n");
        set_timer(2);
        state = ST_RESET;
        break;

    case ST_RESET:
        iface_set_up(0);
        iface_set_up(1);
        state = ST_IF_UP;
        break;
    }
}

//====================
// Init
//====================
static void init_nl(void)
{
    nl_sock = nl_socket_alloc();
    genl_connect(nl_sock);

    nl80211_id = genl_ctrl_resolve(nl_sock, "nl80211");

    nl_socket_modify_cb(nl_sock, NL_CB_VALID,
                        NL_CB_CUSTOM, nl_event_handler, NULL);
}

static void init_rtnl(void)
{
    rtnl_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);

    struct sockaddr_nl addr = {
        .nl_family = AF_NETLINK,
        .nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR
    };

    bind(rtnl_fd, (struct sockaddr*)&addr, sizeof(addr));
}

static void wifi2_epoll_cb(int fd, unsigned ev, void *cbarg) {
	wifi2_t *wifi2 = (wifi2_t*)cbarg;
	struct epoll_event events[MAX_EVENTS];
	int n;

	if (wifi2->evconn.fd != epfd) {
		log_e("sanity check mismatch epfd\n");
		return;
	}

	n = epoll_wait(epfd, events, MAX_EVENTS, -1);

	for (int i = 0; i < n; i++) {

		if (events[i].data.fd == rtnl_fd)
			handle_rtnl();

		else if (events[i].data.fd == nl_socket_get_fd(nl_sock))
			nl_recvmsgs_default(nl_sock);

		else if (events[i].data.fd == timer_fd) {
			uint64_t exp;
			read(timer_fd, &exp, sizeof(exp));
			printf("TIMER\n");
			state = ST_RESET;
		}
	}

	run_sm();
finally:
	// keep listen
	if ((wifi2->evconn.ev = aloe_ev_put(wifi2->evconn.ev_ctx, wifi2->evconn.fd,
			&wifi2_epoll_cb, wifi2, aloe_ev_flag_read, ALOE_EV_INFINITE,
			0)) == NULL) {
		log_e("Failure aloe_ev_put\n");
	}
}

void* wifi2_init(void *evctx)
{
	struct epoll_event ev;
	wifi2_t *wifi2 = NULL;

	if ((wifi2 = (wifi2_t*)aloe_calloc(1, sizeof(*wifi2))) == NULL) {
		log_e("failed alloc wifi2\n");
		goto finally;
	}

    ifindex = if_nametoindex(IFACE);

    init_nl();
    init_rtnl();

    timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);

    epfd = epoll_create1(0);

    memset(&ev, 0, sizeof(ev));

    ev.events = EPOLLIN;
    ev.data.fd = rtnl_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, rtnl_fd, &ev);

    ev.data.fd = nl_socket_get_fd(nl_sock);
    epoll_ctl(epfd, EPOLL_CTL_ADD, ev.data.fd, &ev);

    ev.data.fd = timer_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, timer_fd, &ev);

    wifi2->evconn.fd = epfd;
    wifi2->evconn.ev_ctx = evctx;
	if ((wifi2->evconn.ev = aloe_ev_put(wifi2->evconn.ev_ctx, wifi2->evconn.fd,
			&wifi2_epoll_cb, wifi2, aloe_ev_flag_read, ALOE_EV_INFINITE,
			0)) == NULL) {
		log_e("Failure aloe_ev_put\n");
		goto finally;
	}

finally:
	return wifi2;
}
