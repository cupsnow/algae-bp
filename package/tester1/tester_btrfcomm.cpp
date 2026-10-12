/* $Id$
 *
 * This file is part of the project algae-bp
 *
 * @author joelai
 *
 * @file /algae-bp/package/tester1/tester_btrfcomm.cpp
 * @brief RFCOMM session on the kernel Bluetooth socket
 *
 * CLI (tester_ev3):
 *   btrfcomm up
 *   btrfcomm discoverable on|off
 *   btrfcomm listen [channel]
 *   btrfcomm connect <bdaddr> [channel]
 *   btrfcomm send <text>
 *   btrfcomm close
 *
 * listen waits for the peer. connect is the client. After the socket is
 * connected, send writes and the event loop logs what the peer sends.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include "bt.h"
#include "priv.h"

#ifndef AF_BLUETOOTH
#define AF_BLUETOOTH 31
#endif
#ifndef BTPROTO_HCI
#define BTPROTO_HCI 1
#endif
#ifndef BTPROTO_RFCOMM
#define BTPROTO_RFCOMM 3
#endif
#ifndef SOL_BLUETOOTH
#define SOL_BLUETOOTH 274
#endif

#define BT_SECURITY 4
#define BT_SECURITY_LOW 1

#define HCIDEVUP _IOW('H', 201, int)
#define HCIGETDEVINFO _IOR('H', 211, int)
#define HCISETSCAN _IOW('H', 221, int)
#define SCAN_INQUIRY 0x01
#define SCAN_PAGE 0x02

/* Layout matches include/uapi/linux/bluetooth.h and rfcomm.h. */
typedef struct {
	uint8_t b[6];
} __attribute__((packed)) bdaddr_t;

struct sockaddr_rc {
	sa_family_t rc_family;
	bdaddr_t rc_bdaddr;
	uint8_t rc_channel;
};

struct bt_security {
	uint8_t level;
	uint8_t key_size;
};

struct hci_dev_stats {
	uint32_t err_rx;
	uint32_t err_tx;
	uint32_t cmd_tx;
	uint32_t evt_rx;
	uint32_t acl_tx;
	uint32_t acl_rx;
	uint32_t sco_tx;
	uint32_t sco_rx;
	uint32_t byte_rx;
	uint32_t byte_tx;
};

struct hci_dev_req {
	uint16_t dev_id;
	uint32_t dev_opt;
};

struct hci_dev_info {
	uint16_t dev_id;
	char name[8];
	bdaddr_t bdaddr;
	uint32_t flags;
	uint8_t type;
	uint8_t features[8];
	uint32_t pkt_type;
	uint32_t link_policy;
	uint32_t link_mode;
	uint16_t acl_mtu;
	uint16_t acl_pkts;
	uint16_t sco_mtu;
	uint16_t sco_pkts;
	struct hci_dev_stats stat;
};

enum {
	bt_st_idle = 0,
	bt_st_listen,
	bt_st_connecting,
	bt_st_connected,
};

typedef struct {
	evconn_t evconn;
	void *ev_ctx;
	int state;
	int hci_id;
	uint8_t channel;
	char peer[18];
	char rx[1024];
} bt2_t;

extern "C" {
void *bt_global;
}

static const char *bt_st_name(int st) {
	switch (st) {
	case bt_st_listen: return "listen";
	case bt_st_connecting: return "connecting";
	case bt_st_connected: return "connected";
	default: return "idle";
	}
}

static void ba2str(const bdaddr_t *ba, char *str, size_t sz) {
	snprintf(str, sz, "%02X:%02X:%02X:%02X:%02X:%02X",
			ba->b[5], ba->b[4], ba->b[3], ba->b[2], ba->b[1], ba->b[0]);
}

static int str2ba(const char *str, bdaddr_t *ba) {
	unsigned int b[6];
	int i;

	if (sscanf(str, "%x:%x:%x:%x:%x:%x",
			&b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
		return -1;
	}
	for (i = 0; i < 6; i++) {
		if (b[i] > 0xff) return -1;
		ba->b[5 - i] = (uint8_t)b[i];
	}
	return 0;
}

static int bt_hci_fd(void) {
	int fd;

	fd = socket(AF_BLUETOOTH, SOCK_RAW | SOCK_CLOEXEC, BTPROTO_HCI);
	if (fd < 0) {
		log_e("hci socket: %s\n", strerror(errno));
	}
	return fd;
}

static int bt_hci_addr(int dev_id, bdaddr_t *ba) {
	int fd, r;
	struct hci_dev_info di;

	fd = bt_hci_fd();
	if (fd < 0) return -1;
	memset(&di, 0, sizeof(di));
	di.dev_id = (uint16_t)dev_id;
	if (ioctl(fd, HCIGETDEVINFO, &di) != 0) {
		r = errno;
		log_e("HCIGETDEVINFO hci%d: %s\n", dev_id, strerror(r));
		close(fd);
		return -1;
	}
	*ba = di.bdaddr;
	close(fd);
	return 0;
}

static int bt_hci_up(int dev_id) {
	int fd, r;
	bdaddr_t ba;
	char addr[18];

	fd = bt_hci_fd();
	if (fd < 0) return -1;
	if (ioctl(fd, HCIDEVUP, dev_id) != 0 && errno != EALREADY) {
		r = errno;
		log_e("HCIDEVUP hci%d: %s\n", dev_id, strerror(r));
		close(fd);
		return -1;
	}
	close(fd);
	if (bt_hci_addr(dev_id, &ba) == 0) {
		ba2str(&ba, addr, sizeof(addr));
		log_d("hci%d up %s\n", dev_id, addr);
	} else {
		log_d("hci%d up\n", dev_id);
	}
	return 0;
}

static int bt_scan_set(int dev_id, uint32_t scan) {
	int fd, r;
	struct hci_dev_req dr;

	fd = bt_hci_fd();
	if (fd < 0) return -1;
	memset(&dr, 0, sizeof(dr));
	dr.dev_id = (uint16_t)dev_id;
	dr.dev_opt = scan;
	if (ioctl(fd, HCISETSCAN, &dr) != 0) {
		r = errno;
		log_e("HCISETSCAN hci%d: %s\n", dev_id, strerror(r));
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

static int bt_discoverable(bt2_t *bt2, int on) {
	uint32_t scan;

	if (bt_hci_up(bt2->hci_id) != 0) return -1;
	if (on) {
		scan = SCAN_INQUIRY | SCAN_PAGE;
	} else if (bt2->state == bt_st_listen) {
		scan = SCAN_PAGE;
	} else {
		scan = 0;
	}
	if (bt_scan_set(bt2->hci_id, scan) != 0) return -1;
	log_d("discoverable %s (scan 0x%x)\n", on ? "on" : "off", scan);
	return 0;
}

static void bt_session_close(bt2_t *bt2) {
	evconn_cancel(&bt2->evconn);
	if (bt2->evconn.fd >= 0) {
		close(bt2->evconn.fd);
		bt2->evconn.fd = -1;
	}
	bt2->state = bt_st_idle;
	bt2->peer[0] = '\0';
}

static int bt_arm(bt2_t *bt2, unsigned ev, uint64_t timeout_ms,
		aloe_evb2_cb_t cb) {
	evconn_cancel(&bt2->evconn);
	bt2->evconn.ev = aloe_evb2_add_fd(bt2->ev_ctx, bt2->evconn.fd, ev,
			timeout_ms, cb, bt2);
	if (!bt2->evconn.ev) {
		log_e("Failure aloe_evb2_add_fd\n");
		return -1;
	}
	return 0;
}

static int bt_set_security_low(int fd) {
	struct bt_security sec;

	memset(&sec, 0, sizeof(sec));
	sec.level = BT_SECURITY_LOW;
	if (setsockopt(fd, SOL_BLUETOOTH, BT_SECURITY, &sec, sizeof(sec)) != 0) {
		log_e("BT_SECURITY: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

static int bt_rfcomm_socket(void) {
	int fd;

	fd = socket(AF_BLUETOOTH, SOCK_STREAM | SOCK_CLOEXEC, BTPROTO_RFCOMM);
	if (fd < 0) {
		log_e("rfcomm socket: %s\n", strerror(errno));
		return -1;
	}
	if (bt_set_security_low(fd) != 0) {
		close(fd);
		return -1;
	}
	if (aloe_file_nonblock(fd, 1) != 0) {
		log_e("failure set nonblock\n");
		close(fd);
		return -1;
	}
	return fd;
}

static void bt_rx_cb(int fd, unsigned ev, void *cbarg) {
	bt2_t *bt2 = (bt2_t*)cbarg;
	char *buf = bt2->rx;

	if (bt2->evconn.fd != fd) {
		log_e("sanity check mismatch fd\n");
		return;
	}
	if (!(ev & ALOE_EVB2_FLAG_READ)) return;

	for (;;) {
		ssize_t len;

		len = recv(fd, buf, sizeof(bt2->rx) - 1, MSG_DONTWAIT);
		if (len < 0) {
			if (errno == EINTR) continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK) break;
			log_e("recv: %s\n", strerror(errno));
			bt_session_close(bt2);
			return;
		}
		if (len == 0) {
			log_d("peer closed\n");
			bt_session_close(bt2);
			return;
		}
		buf[len] = '\0';
		log_d("rx %d bytes from %s: %s\n", (int)len, bt2->peer, buf);
	}
}

static void bt_accept_cb(int fd, unsigned ev, void *cbarg);
static void bt_connect_cb(int fd, unsigned ev, void *cbarg);

static int bt_arm_rx(bt2_t *bt2) {
	return bt_arm(bt2, ALOE_EVB2_FLAG_READ, ALOE_EVB2_INFINITE, &bt_rx_cb);
}

static void bt_accept_cb(int fd, unsigned ev, void *cbarg) {
	bt2_t *bt2 = (bt2_t*)cbarg;
	struct sockaddr_rc peer;
	socklen_t peer_len;
	int conn;

	if (bt2->evconn.fd != fd) {
		log_e("sanity check mismatch fd\n");
		return;
	}
	if (!(ev & ALOE_EVB2_FLAG_READ)) return;

	peer_len = sizeof(peer);
	memset(&peer, 0, sizeof(peer));
	conn = accept4(fd, (struct sockaddr*)&peer, &peer_len, SOCK_CLOEXEC);
	if (conn < 0) {
		if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) return;
		log_e("accept: %s\n", strerror(errno));
		return;
	}
	ba2str(&peer.rc_bdaddr, bt2->peer, sizeof(bt2->peer));
	log_d("accepted %s channel %u\n", bt2->peer, peer.rc_channel);
	{
		char peer_str[18];
		uint8_t ch = peer.rc_channel;

		snprintf(peer_str, sizeof(peer_str), "%s", bt2->peer);
		bt_session_close(bt2);
		snprintf(bt2->peer, sizeof(bt2->peer), "%s", peer_str);
		bt2->channel = ch;
	}
	bt2->evconn.fd = conn;
	bt2->state = bt_st_connected;
	if (aloe_file_nonblock(conn, 1) != 0 || bt_arm_rx(bt2) != 0) {
		bt_session_close(bt2);
		return;
	}
	log_d("connected, ready to send and receive\n");
}

static void bt_connect_cb(int fd, unsigned ev, void *cbarg) {
	bt2_t *bt2 = (bt2_t*)cbarg;
	int err = 0;
	socklen_t err_len = sizeof(err);

	if (bt2->evconn.fd != fd) {
		log_e("sanity check mismatch fd\n");
		return;
	}
	if (ev & (ALOE_EVB2_FLAG_TIMEOUT | ALOE_EVB2_FLAG_EXCEPT)) {
		log_e("connect timeout\n");
		bt_session_close(bt2);
		return;
	}
	if (!(ev & ALOE_EVB2_FLAG_WRITE)) return;
	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &err_len) != 0 || err != 0) {
		log_e("connect: %s\n", strerror(err ? err : errno));
		bt_session_close(bt2);
		return;
	}
	bt2->state = bt_st_connected;
	if (bt_arm_rx(bt2) != 0) {
		bt_session_close(bt2);
		return;
	}
	log_d("connected to %s channel %u, ready to send and receive\n",
			bt2->peer, bt2->channel);
}

static int bt_listen(bt2_t *bt2, int channel) {
	struct sockaddr_rc local;
	int fd;

	if (channel < 1 || channel > 30) {
		log_e("channel %d out of range\n", channel);
		return -1;
	}
	if (bt_hci_up(bt2->hci_id) != 0) return -1;
	bt_session_close(bt2);

	fd = bt_rfcomm_socket();
	if (fd < 0) return -1;

	memset(&local, 0, sizeof(local));
	local.rc_family = AF_BLUETOOTH;
	local.rc_channel = (uint8_t)channel;
	if (bind(fd, (struct sockaddr*)&local, sizeof(local)) != 0) {
		log_e("bind channel %d: %s\n", channel, strerror(errno));
		close(fd);
		return -1;
	}
	if (listen(fd, 1) != 0) {
		log_e("listen: %s\n", strerror(errno));
		close(fd);
		return -1;
	}

	bt2->evconn.fd = fd;
	bt2->channel = (uint8_t)channel;
	bt2->state = bt_st_listen;
	if (bt_arm(bt2, ALOE_EVB2_FLAG_READ, ALOE_EVB2_INFINITE, &bt_accept_cb) != 0) {
		bt_session_close(bt2);
		return -1;
	}
	log_d("waiting for connect on channel %d\n", channel);
	return 0;
}

static int bt_connect(bt2_t *bt2, const char *addr, int channel) {
	struct sockaddr_rc peer;
	int fd, r;

	if (channel < 1 || channel > 30) {
		log_e("channel %d out of range\n", channel);
		return -1;
	}
	memset(&peer, 0, sizeof(peer));
	peer.rc_family = AF_BLUETOOTH;
	peer.rc_channel = (uint8_t)channel;
	if (str2ba(addr, &peer.rc_bdaddr) != 0) {
		log_e("invalid bdaddr %s\n", addr);
		return -1;
	}
	if (bt_hci_up(bt2->hci_id) != 0) return -1;
	bt_session_close(bt2);

	fd = bt_rfcomm_socket();
	if (fd < 0) return -1;
	if (connect(fd, (struct sockaddr*)&peer, sizeof(peer)) != 0) {
		r = errno;
		if (r != EINPROGRESS) {
			log_e("connect %s: %s\n", addr, strerror(r));
			close(fd);
			return -1;
		}
	}

	ba2str(&peer.rc_bdaddr, bt2->peer, sizeof(bt2->peer));
	bt2->evconn.fd = fd;
	bt2->channel = (uint8_t)channel;
	bt2->state = bt_st_connecting;
	if (bt_arm(bt2, ALOE_EVB2_FLAG_WRITE | ALOE_EVB2_FLAG_EXCEPT,
			30000, &bt_connect_cb) != 0) {
		bt_session_close(bt2);
		return -1;
	}
	log_d("waiting for connect to %s channel %d\n", bt2->peer, channel);
	return 0;
}

static int bt_send(bt2_t *bt2, const char *text) {
	size_t len, off;

	if (bt2->state != bt_st_connected || bt2->evconn.fd < 0) {
		log_e("not connected\n");
		return -1;
	}
	len = strlen(text);
	off = 0;
	while (off < len) {
		ssize_t n;

		n = send(bt2->evconn.fd, text + off, len - off, MSG_NOSIGNAL);
		if (n < 0) {
			if (errno == EINTR) continue;
			log_e("send: %s\n", strerror(errno));
			bt_session_close(bt2);
			return -1;
		}
		off += (size_t)n;
	}
	log_d("tx %d bytes to %s\n", (int)len, bt2->peer);
	return 0;
}

extern "C"
void* bt_init(void *evctx) {
	bt2_t *bt2;

	bt2 = (bt2_t*)aloe_calloc(1, sizeof(*bt2));
	if (!bt2) {
		log_e("failed alloc bt2\n");
		return NULL;
	}
	bt2->evconn.fd = -1;
	bt2->evconn.ev_ctx = evctx;
	bt2->ev_ctx = evctx;
	bt2->hci_id = 0;
	bt2->state = bt_st_idle;
	log_d("btrfcomm initialized\n");
	return bt2;
}

extern "C"
void bt_destroy(void *_bt2) {
	bt2_t *bt2 = (bt2_t*)_bt2;

	if (!bt2) return;
	bt_session_close(bt2);
	aloe_free(bt2);
}

int cli_cmd_btrfcomm(void*, int argc, const char **argv) {
	bt2_t *bt2 = (bt2_t*)bt_global;
	const char *sub;
	int channel;

	if (!bt2) {
		log_e("btrfcomm is not initialized\n");
		return -1;
	}
	if (argc < 2) {
		log_d("state %s peer %s channel %u\n",
				bt_st_name(bt2->state),
				bt2->peer[0] ? bt2->peer : "-",
				bt2->channel);
		log_d("btrfcomm up | discoverable on|off | listen [ch] | connect <bdaddr> [ch] | send <text> | close\n");
		return 0;
	}
	sub = argv[1];
	if (strcmp(sub, "up") == 0) {
		return bt_hci_up(bt2->hci_id);
	}
	if (strcmp(sub, "discoverable") == 0) {
		if (argc < 3 || (strcmp(argv[2], "on") != 0 && strcmp(argv[2], "off") != 0)) {
			log_e("btrfcomm discoverable on|off\n");
			return -1;
		}
		return bt_discoverable(bt2, strcmp(argv[2], "on") == 0);
	}
	if (strcmp(sub, "listen") == 0) {
		channel = (argc >= 3) ? atoi(argv[2]) : 1;
		return bt_listen(bt2, channel);
	}
	if (strcmp(sub, "connect") == 0) {
		if (argc < 3) {
			log_e("btrfcomm connect <bdaddr> [channel]\n");
			return -1;
		}
		channel = (argc >= 4) ? atoi(argv[3]) : 1;
		return bt_connect(bt2, argv[2], channel);
	}
	if (strcmp(sub, "send") == 0) {
		char text[512];
		size_t off = 0;
		int i;

		if (argc < 3) {
			log_e("btrfcomm send <text>\n");
			return -1;
		}
		text[0] = '\0';
		for (i = 2; i < argc; i++) {
			int n;

			n = snprintf(text + off, sizeof(text) - off, "%s%s",
					(i == 2) ? "" : " ", argv[i]);
			if (n < 0 || (size_t)n >= sizeof(text) - off) {
				log_e("send text too long\n");
				return -1;
			}
			off += (size_t)n;
		}
		return bt_send(bt2, text);
	}
	if (strcmp(sub, "close") == 0) {
		bt_session_close(bt2);
		log_d("closed\n");
		return 0;
	}
	log_e("unknown btrfcomm command %s\n", sub);
	return -1;
}
