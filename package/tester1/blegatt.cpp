/* $Id$
 *
 * This file is part of the project algae-bp
 *
 * @author joelai
 *
 * @file /algae-bp/package/tester1/tester_blegatt.cpp
 * @brief BLE GATT peripheral on the kernel Bluetooth socket
 *
 * CLI (tester_ev3):
 *   blegatt up
 *   blegatt advertise on|off
 *   blegatt notify <text>
 *   blegatt close
 *
 * advertise on starts legacy connectable advertising and an ATT server.
 * nRF Connect connects as a GATT client. One custom characteristic accepts
 * read, write, and notify. Stop bluetoothd first so it does not own
 * advertising or the ATT socket. The process needs CAP_NET_RAW.
 *
 * Service        12345678-1234-5678-1234-56789abcdef0
 * Characteristic 12345678-1234-5678-1234-56789abcdef1
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include "blegatt.h"
#include "priv.h"

#ifndef AF_BLUETOOTH
#define AF_BLUETOOTH 31
#endif
#ifndef BTPROTO_HCI
#define BTPROTO_HCI 1
#endif
#ifndef BTPROTO_L2CAP
#define BTPROTO_L2CAP 0
#endif
#ifndef SOL_HCI
#define SOL_HCI 0
#endif
#ifndef SOL_BLUETOOTH
#define SOL_BLUETOOTH 274
#endif

#define HCI_FILTER 2
#define HCI_COMMAND_PKT 0x01
#define HCI_EVENT_PKT 0x04
#define HCI_EV_CMD_COMPLETE 0x0e
#define HCI_EV_CMD_STATUS 0x0f
#define HCI_CHANNEL_RAW 0

#define BT_SECURITY 4
#define BT_SECURITY_LOW 1

#define HCIDEVUP _IOW('H', 201, int)
#define HCIGETDEVINFO _IOR('H', 211, int)

#define HCI_OP_WRITE_LE_HOST_SUPPORTED 0x0c6d
#define HCI_OP_LE_SET_ADV_PARAM 0x2006
#define HCI_OP_LE_SET_ADV_DATA 0x2008
#define HCI_OP_LE_SET_SCAN_RSP_DATA 0x2009
#define HCI_OP_LE_SET_ADV_ENABLE 0x200a

#define BDADDR_LE_PUBLIC 0x01
#define L2CAP_CID_ATT 0x0004

#define ATT_MTU_DEFAULT 23
#define ATT_MTU_MAX 128
#define BLE_VALUE_MAX 64

#define ATT_OP_ERROR 0x01
#define ATT_OP_MTU_REQ 0x02
#define ATT_OP_MTU_RSP 0x03
#define ATT_OP_FIND_INFO_REQ 0x04
#define ATT_OP_FIND_INFO_RSP 0x05
#define ATT_OP_FIND_BY_TYPE_REQ 0x06
#define ATT_OP_FIND_BY_TYPE_RSP 0x07
#define ATT_OP_READ_BY_TYPE_REQ 0x08
#define ATT_OP_READ_BY_TYPE_RSP 0x09
#define ATT_OP_READ_REQ 0x0a
#define ATT_OP_READ_RSP 0x0b
#define ATT_OP_READ_BLOB_REQ 0x0c
#define ATT_OP_READ_BLOB_RSP 0x0d
#define ATT_OP_READ_BY_GROUP_REQ 0x10
#define ATT_OP_READ_BY_GROUP_RSP 0x11
#define ATT_OP_WRITE_REQ 0x12
#define ATT_OP_WRITE_RSP 0x13
#define ATT_OP_HANDLE_NFY 0x1b
#define ATT_OP_WRITE_CMD 0x52

#define ATT_ECODE_INVALID_HANDLE 0x01
#define ATT_ECODE_READ_NOT_PERM 0x02
#define ATT_ECODE_WRITE_NOT_PERM 0x03
#define ATT_ECODE_INVALID_PDU 0x04
#define ATT_ECODE_REQ_NOT_SUPP 0x06
#define ATT_ECODE_INVALID_OFFSET 0x07
#define ATT_ECODE_ATTR_NOT_FOUND 0x0a
#define ATT_ECODE_INVAL_ATTR_VALUE_LEN 0x0d
#define ATT_ECODE_UNSUPP_GRP_TYPE 0x10

#define ATT_PROP_READ 0x02
#define ATT_PROP_WRITE_CMD 0x04
#define ATT_PROP_WRITE 0x08
#define ATT_PROP_NOTIFY 0x10

enum {
	HDL_GAP_SVC = 1,
	HDL_GAP_NAME_CH = 2,
	HDL_GAP_NAME = 3,
	HDL_GAP_APP_CH = 4,
	HDL_GAP_APP = 5,
	HDL_GATT_SVC = 6,
	HDL_CUS_SVC = 7,
	HDL_CUS_CH = 8,
	HDL_CUS_VAL = 9,
	HDL_CUS_CCCD = 10,
	HDL_MAX = 10,
};

/* 12345678-1234-5678-1234-56789abcdef0, air byte order. */
static const uint8_t uuid_svc[16] = {
	0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
	0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12,
};
/* 12345678-1234-5678-1234-56789abcdef1 */
static const uint8_t uuid_chr[16] = {
	0xf1, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
	0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12,
};

static const char ble_name[] = "tester";

typedef struct {
	uint8_t b[6];
} __attribute__((packed)) bdaddr_t;

struct sockaddr_hci {
	sa_family_t hci_family;
	unsigned short hci_dev;
	unsigned short hci_channel;
};

struct sockaddr_l2 {
	sa_family_t l2_family;
	uint16_t l2_psm;
	bdaddr_t l2_bdaddr;
	uint16_t l2_cid;
	uint8_t l2_bdaddr_type;
};

struct bt_security {
	uint8_t level;
	uint8_t key_size;
};

struct hci_ufilter {
	uint32_t type_mask;
	uint32_t event_mask[2];
	uint16_t opcode;
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
	ble_st_idle = 0,
	ble_st_adv,
	ble_st_connected,
};

typedef struct {
	evconn_t evconn;
	void *ev_ctx;
	int listen_fd;
	int state;
	int hci_id;
	int adv_wanted;
	int adv_on;
	uint16_t mtu;
	uint16_t cccd;
	uint8_t value[BLE_VALUE_MAX];
	uint16_t value_len;
	char peer[18];
	uint8_t rx[512];
} ble2_t;

extern "C" {
void *blegatt_global;
}

static const char *ble_st_name(int st) {
	switch (st) {
	case ble_st_adv: return "advertising";
	case ble_st_connected: return "connected";
	default: return "idle";
	}
}

static void ba2str(const bdaddr_t *ba, char *str, size_t sz) {
	snprintf(str, sz, "%02X:%02X:%02X:%02X:%02X:%02X",
			ba->b[5], ba->b[4], ba->b[3], ba->b[2], ba->b[1], ba->b[0]);
}

static uint16_t get_le16(const uint8_t *p) {
	return (uint16_t)(p[0] | (p[1] << 8));
}

static void put_le16(uint8_t *p, uint16_t v) {
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static int ms_since(const struct timespec *t0) {
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (int)((now.tv_sec - t0->tv_sec) * 1000
			+ (now.tv_nsec - t0->tv_nsec) / 1000000);
}

static void log_payload(const char *tag, const uint8_t *p, int n) {
	char tmp[80];
	int i, o = 0;

	for (i = 0; i < n && o < (int)sizeof(tmp) - 1; i++) {
		unsigned char c = p[i];

		tmp[o++] = (c >= 32 && c < 127) ? (char)c : '.';
	}
	tmp[o] = '\0';
	log_d("%s %d bytes: %s\n", tag, n, tmp);
}

static int ble_hci_fd(void) {
	int fd;

	fd = socket(AF_BLUETOOTH, SOCK_RAW | SOCK_CLOEXEC, BTPROTO_HCI);
	if (fd < 0) log_e("hci socket: %s\n", strerror(errno));
	return fd;
}

static int ble_hci_addr(int dev_id, bdaddr_t *ba) {
	int fd, r;
	struct hci_dev_info di;

	fd = ble_hci_fd();
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

static int ble_hci_up(int dev_id) {
	int fd, r;
	bdaddr_t ba;
	char addr[18];

	fd = ble_hci_fd();
	if (fd < 0) return -1;
	if (ioctl(fd, HCIDEVUP, dev_id) != 0 && errno != EALREADY) {
		r = errno;
		log_e("HCIDEVUP hci%d: %s\n", dev_id, strerror(r));
		close(fd);
		return -1;
	}
	close(fd);
	if (ble_hci_addr(dev_id, &ba) == 0) {
		ba2str(&ba, addr, sizeof(addr));
		log_d("hci%d up %s\n", dev_id, addr);
	} else {
		log_d("hci%d up\n", dev_id);
	}
	return 0;
}

static int ble_hci_bind(int dev_id) {
	int fd;
	struct sockaddr_hci addr;

	fd = ble_hci_fd();
	if (fd < 0) return -1;
	memset(&addr, 0, sizeof(addr));
	addr.hci_family = AF_BLUETOOTH;
	addr.hci_dev = (unsigned short)dev_id;
	addr.hci_channel = HCI_CHANNEL_RAW;
	if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
		log_e("hci bind: %s\n", strerror(errno));
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

/* Return the command status, or -1 on a local error. errno is set. */
static int hci_cmd_status(int fd, uint16_t opcode, const void *param, int plen) {
	uint8_t buf[64];
	struct hci_ufilter flt;
	struct timespec t0;
	int n;

	if (plen < 0 || plen > 32) {
		errno = EINVAL;
		return -1;
	}
	memset(&flt, 0, sizeof(flt));
	flt.type_mask = (1u << HCI_EVENT_PKT);
	flt.event_mask[0] = (1u << HCI_EV_CMD_COMPLETE) | (1u << HCI_EV_CMD_STATUS);
	flt.opcode = opcode;
	if (setsockopt(fd, SOL_HCI, HCI_FILTER, &flt, sizeof(flt)) != 0) {
		log_e("HCI_FILTER: %s\n", strerror(errno));
		return -1;
	}

	buf[0] = HCI_COMMAND_PKT;
	put_le16(buf + 1, opcode);
	buf[3] = (uint8_t)plen;
	if (plen > 0) memcpy(buf + 4, param, (size_t)plen);
	n = 4 + plen;
	if (send(fd, buf, (size_t)n, 0) != n) {
		log_e("hci send 0x%04x: %s\n", opcode, strerror(errno));
		return -1;
	}

	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (;;) {
		struct pollfd pfd;
		int wait, pr;
		ssize_t rn;
		uint8_t evt;

		wait = 2000 - ms_since(&t0);
		if (wait <= 0) {
			errno = ETIMEDOUT;
			log_e("hci 0x%04x timeout\n", opcode);
			return -1;
		}
		pfd.fd = fd;
		pfd.events = POLLIN;
		pr = poll(&pfd, 1, wait);
		if (pr < 0) {
			if (errno == EINTR) continue;
			log_e("hci poll: %s\n", strerror(errno));
			return -1;
		}
		if (pr == 0) continue;
		rn = recv(fd, buf, sizeof(buf), 0);
		if (rn < 0) {
			if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
			log_e("hci recv: %s\n", strerror(errno));
			return -1;
		}
		if (rn < 7 || buf[0] != HCI_EVENT_PKT) continue;
		evt = buf[1];
		if (evt == HCI_EV_CMD_STATUS && buf[2] >= 4) {
			uint16_t op = get_le16(buf + 5);

			if (op != opcode) continue;
			if (buf[3] != 0) return buf[3];
			continue;
		}
		if (evt == HCI_EV_CMD_COMPLETE && buf[2] >= 4) {
			uint16_t op = get_le16(buf + 4);

			if (op != opcode) continue;
			return buf[6];
		}
	}
}

static int hci_cmd(int fd, uint16_t opcode, const void *param, int plen,
		int ignore_status) {
	int st;

	st = hci_cmd_status(fd, opcode, param, plen);
	if (st < 0) return -1;
	if (st != 0) {
		if (ignore_status) {
			log_d("hci 0x%04x status 0x%02x\n", opcode, st);
			return 0;
		}
		log_e("hci 0x%04x status 0x%02x\n", opcode, st);
		return -1;
	}
	return 0;
}

static int ad_put(uint8_t *dst, int max, uint8_t type, const void *v, int n) {
	if (n < 0 || n + 2 > max) return -1;
	dst[0] = (uint8_t)(n + 1);
	dst[1] = type;
	if (n > 0) memcpy(dst + 2, v, (size_t)n);
	return n + 2;
}

static int ble_adv_config(int fd, int enable) {
	uint8_t param[15];
	uint8_t data[32];
	uint8_t host[2];
	uint8_t on;
	int n, k;

	host[0] = 0x01;
	host[1] = 0x01;
	if (hci_cmd(fd, HCI_OP_WRITE_LE_HOST_SUPPORTED, host, 2, 1) != 0) return -1;

	on = 0;
	if (hci_cmd(fd, HCI_OP_LE_SET_ADV_ENABLE, &on, 1, 1) != 0) return -1;
	if (!enable) return 0;

	memset(param, 0, sizeof(param));
	put_le16(param + 0, 0x00a0);
	put_le16(param + 2, 0x00a0);
	param[4] = 0x00;
	param[13] = 0x07;
	if (hci_cmd(fd, HCI_OP_LE_SET_ADV_PARAM, param, (int)sizeof(param), 0) != 0)
		return -1;

	memset(data, 0, sizeof(data));
	n = 0;
	k = ad_put(data + 1 + n, 31 - n, 0x01, "\x02", 1);
	if (k < 0) return -1;
	n += k;
	k = ad_put(data + 1 + n, 31 - n, 0x07, uuid_svc, 16);
	if (k < 0) return -1;
	n += k;
	data[0] = (uint8_t)n;
	if (hci_cmd(fd, HCI_OP_LE_SET_ADV_DATA, data, 32, 0) != 0) return -1;

	memset(data, 0, sizeof(data));
	k = ad_put(data + 1, 31, 0x09, ble_name, (int)strlen(ble_name));
	if (k < 0) return -1;
	data[0] = (uint8_t)k;
	if (hci_cmd(fd, HCI_OP_LE_SET_SCAN_RSP_DATA, data, 32, 0) != 0) return -1;

	on = 0x01;
	if (hci_cmd(fd, HCI_OP_LE_SET_ADV_ENABLE, &on, 1, 0) != 0) return -1;
	return 0;
}

static int ble_adv_set(ble2_t *b, int enable) {
	int fd, r;

	fd = ble_hci_bind(b->hci_id);
	if (fd < 0) return -1;
	if (enable) r = ble_adv_config(fd, 1);
	else {
		uint8_t on = 0;

		r = hci_cmd(fd, HCI_OP_LE_SET_ADV_ENABLE, &on, 1, 1);
	}
	close(fd);
	if (r == 0) b->adv_on = enable ? 1 : 0;
	return r;
}

static int att_error(uint8_t *rsp, uint8_t op, uint16_t hdl, uint8_t code) {
	rsp[0] = ATT_OP_ERROR;
	rsp[1] = op;
	put_le16(rsp + 2, hdl);
	rsp[4] = code;
	return 5;
}

static int att_range(const uint8_t *req, int req_len, uint16_t *start, uint16_t *end) {
	if (req_len < 5) return -1;
	*start = get_le16(req + 1);
	*end = get_le16(req + 3);
	if (*start == 0 || *start > *end) return -1;
	return 0;
}

static uint16_t att_last(uint16_t end) {
	return end > (uint16_t)HDL_MAX ? (uint16_t)HDL_MAX : end;
}

static int att_type_get(uint16_t hdl, uint8_t *dst) {
	uint16_t type = 0;

	switch (hdl) {
	case HDL_GAP_SVC:
	case HDL_GATT_SVC:
	case HDL_CUS_SVC:
		type = 0x2800;
		break;
	case HDL_GAP_NAME_CH:
	case HDL_GAP_APP_CH:
	case HDL_CUS_CH:
		type = 0x2803;
		break;
	case HDL_GAP_NAME:
		type = 0x2a00;
		break;
	case HDL_GAP_APP:
		type = 0x2a01;
		break;
	case HDL_CUS_CCCD:
		type = 0x2902;
		break;
	case HDL_CUS_VAL:
		memcpy(dst, uuid_chr, 16);
		return 16;
	default:
		return 0;
	}
	put_le16(dst, type);
	return 2;
}

static uint16_t att_group_end(uint16_t hdl) {
	switch (hdl) {
	case HDL_GAP_SVC: return HDL_GAP_APP;
	case HDL_GATT_SVC: return HDL_GATT_SVC;
	case HDL_CUS_SVC: return HDL_CUS_CCCD;
	default: return 0;
	}
}

static int att_value(ble2_t *b, uint16_t hdl, uint8_t *out, int max) {
	uint8_t tmp[32];
	const uint8_t *src;
	int n;

	src = tmp;
	switch (hdl) {
	case HDL_GAP_SVC:
		put_le16(tmp, 0x1800);
		n = 2;
		break;
	case HDL_GATT_SVC:
		put_le16(tmp, 0x1801);
		n = 2;
		break;
	case HDL_CUS_SVC:
		src = uuid_svc;
		n = 16;
		break;
	case HDL_GAP_NAME_CH:
		tmp[0] = ATT_PROP_READ;
		put_le16(tmp + 1, HDL_GAP_NAME);
		put_le16(tmp + 3, 0x2a00);
		n = 5;
		break;
	case HDL_GAP_APP_CH:
		tmp[0] = ATT_PROP_READ;
		put_le16(tmp + 1, HDL_GAP_APP);
		put_le16(tmp + 3, 0x2a01);
		n = 5;
		break;
	case HDL_CUS_CH:
		tmp[0] = ATT_PROP_READ | ATT_PROP_WRITE_CMD | ATT_PROP_WRITE | ATT_PROP_NOTIFY;
		put_le16(tmp + 1, HDL_CUS_VAL);
		memcpy(tmp + 3, uuid_chr, 16);
		n = 19;
		break;
	case HDL_GAP_NAME:
		src = (const uint8_t*)ble_name;
		n = (int)strlen(ble_name);
		break;
	case HDL_GAP_APP:
		put_le16(tmp, 0);
		n = 2;
		break;
	case HDL_CUS_VAL:
		src = b->value;
		n = b->value_len;
		break;
	case HDL_CUS_CCCD:
		put_le16(tmp, b->cccd);
		n = 2;
		break;
	default:
		return -1;
	}
	if (n > max) return -1;
	if (n > 0) memcpy(out, src, (size_t)n);
	return n;
}

static int att_write(ble2_t *b, uint16_t hdl, const uint8_t *v, int n) {
	if (hdl == HDL_CUS_CCCD) {
		if (n != 2) return ATT_ECODE_INVAL_ATTR_VALUE_LEN;
		b->cccd = get_le16(v);
		log_d("notify %s\n", (b->cccd & 0x0001) ? "on" : "off");
		return 0;
	}
	if (hdl == HDL_CUS_VAL) {
		if (n > BLE_VALUE_MAX) return ATT_ECODE_INVAL_ATTR_VALUE_LEN;
		if (n > 0) memcpy(b->value, v, (size_t)n);
		b->value_len = (uint16_t)n;
		log_payload("write", v, n);
		return 0;
	}
	if (hdl < HDL_GAP_SVC || hdl > HDL_MAX) return ATT_ECODE_INVALID_HANDLE;
	return ATT_ECODE_WRITE_NOT_PERM;
}

static int att_on_pdu(ble2_t *b, const uint8_t *req, int req_len, uint8_t *rsp) {
	uint8_t op;
	uint16_t start, end, hdl;
	uint16_t mtu = b->mtu;
	int n;

	if (req_len < 1) return 0;
	op = req[0];
	if (mtu < ATT_MTU_DEFAULT) mtu = ATT_MTU_DEFAULT;
	if (mtu > ATT_MTU_MAX) mtu = ATT_MTU_MAX;

	switch (op) {
	case ATT_OP_MTU_REQ: {
		uint16_t client;

		if (req_len < 3) return att_error(rsp, op, 0, ATT_ECODE_INVALID_PDU);
		client = get_le16(req + 1);
		if (client < ATT_MTU_DEFAULT)
			return att_error(rsp, op, 0, ATT_ECODE_INVALID_PDU);
		b->mtu = client < ATT_MTU_MAX ? client : ATT_MTU_MAX;
		rsp[0] = ATT_OP_MTU_RSP;
		put_le16(rsp + 1, ATT_MTU_MAX);
		log_d("att mtu %u\n", b->mtu);
		return 3;
	}
	case ATT_OP_FIND_INFO_REQ: {
		int fmt = 0;
		int out = 2;
		uint16_t last;

		if (att_range(req, req_len, &start, &end) != 0)
			return att_error(rsp, op, 0, ATT_ECODE_INVALID_HANDLE);
		if (start > HDL_MAX) return att_error(rsp, op, start, ATT_ECODE_ATTR_NOT_FOUND);
		last = att_last(end);
		for (hdl = start; hdl <= last; hdl++) {
			uint8_t uuid[16];
			int ulen = att_type_get(hdl, uuid);
			int form = (ulen == 16) ? 0x02 : 0x01;
			int pair = 2 + ulen;

			if (fmt == 0) fmt = form;
			if (form != fmt || out + pair > mtu) break;
			put_le16(rsp + out, hdl);
			memcpy(rsp + out + 2, uuid, (size_t)ulen);
			out += pair;
		}
		if (fmt == 0) return att_error(rsp, op, start, ATT_ECODE_ATTR_NOT_FOUND);
		rsp[0] = ATT_OP_FIND_INFO_RSP;
		rsp[1] = (uint8_t)fmt;
		return out;
	}
	case ATT_OP_FIND_BY_TYPE_REQ: {
		uint16_t type;
		int vlen;
		int out = 1;
		uint16_t last;

		if (req_len < 7) return att_error(rsp, op, 0, ATT_ECODE_INVALID_PDU);
		if (att_range(req, req_len, &start, &end) != 0)
			return att_error(rsp, op, 0, ATT_ECODE_INVALID_HANDLE);
		type = get_le16(req + 5);
		vlen = req_len - 7;
		if (start > HDL_MAX) return att_error(rsp, op, start, ATT_ECODE_ATTR_NOT_FOUND);
		last = att_last(end);
		for (hdl = start; hdl <= last; hdl++) {
			uint8_t val[32];
			uint16_t gend;

			if (type != 0x2800) continue;
			gend = att_group_end(hdl);
			if (gend == 0) continue;
			n = att_value(b, hdl, val, (int)sizeof(val));
			if (n != vlen || memcmp(val, req + 7, (size_t)vlen) != 0) continue;
			if (out + 4 > mtu) break;
			put_le16(rsp + out, hdl);
			put_le16(rsp + out + 2, gend);
			out += 4;
		}
		if (out == 1) return att_error(rsp, op, start, ATT_ECODE_ATTR_NOT_FOUND);
		rsp[0] = ATT_OP_FIND_BY_TYPE_RSP;
		return out;
	}
	case ATT_OP_READ_BY_TYPE_REQ:
	case ATT_OP_READ_BY_GROUP_REQ: {
		uint8_t want[16];
		int wlen;
		int pair_len = -1;
		int out = 2;
		int grouped = (op == ATT_OP_READ_BY_GROUP_REQ);
		uint16_t last;

		wlen = req_len - 5;
		if (req_len < 7 || (wlen != 2 && wlen != 16))
			return att_error(rsp, op, 0, ATT_ECODE_INVALID_PDU);
		if (att_range(req, req_len, &start, &end) != 0)
			return att_error(rsp, op, 0, ATT_ECODE_INVALID_HANDLE);
		memcpy(want, req + 5, (size_t)wlen);
		if (grouped && !(wlen == 2 && get_le16(want) == 0x2800))
			return att_error(rsp, op, start, ATT_ECODE_UNSUPP_GRP_TYPE);
		if (start > HDL_MAX) return att_error(rsp, op, start, ATT_ECODE_ATTR_NOT_FOUND);
		last = att_last(end);
		for (hdl = start; hdl <= last; hdl++) {
			uint8_t typ[16];
			uint8_t val[BLE_VALUE_MAX];
			int tlen, vlen, pair, trunc;

			tlen = att_type_get(hdl, typ);
			if (tlen != wlen || memcmp(typ, want, (size_t)wlen) != 0) continue;
			if (grouped && att_group_end(hdl) == 0) continue;
			vlen = att_value(b, hdl, val, (int)sizeof(val));
			if (vlen < 0) continue;
			trunc = 0;
			pair = (grouped ? 4 : 2) + vlen;
			if (pair > mtu - 2) {
				vlen = mtu - 2 - (grouped ? 4 : 2);
				if (vlen < 0) break;
				pair = (grouped ? 4 : 2) + vlen;
				trunc = 1;
			}
			if (pair_len < 0) pair_len = pair;
			if (pair != pair_len || out + pair > mtu) break;
			put_le16(rsp + out, hdl);
			if (grouped) {
				put_le16(rsp + out + 2, att_group_end(hdl));
				memcpy(rsp + out + 4, val, (size_t)vlen);
			} else {
				memcpy(rsp + out + 2, val, (size_t)vlen);
			}
			out += pair;
			if (trunc) break;
		}
		if (pair_len < 0) return att_error(rsp, op, start, ATT_ECODE_ATTR_NOT_FOUND);
		rsp[0] = grouped ? ATT_OP_READ_BY_GROUP_RSP : ATT_OP_READ_BY_TYPE_RSP;
		rsp[1] = (uint8_t)pair_len;
		return out;
	}
	case ATT_OP_READ_REQ:
	case ATT_OP_READ_BLOB_REQ: {
		uint8_t val[BLE_VALUE_MAX];
		uint16_t off = 0;

		if (op == ATT_OP_READ_REQ) {
			if (req_len < 3) return att_error(rsp, op, 0, ATT_ECODE_INVALID_PDU);
			hdl = get_le16(req + 1);
		} else {
			if (req_len < 5) return att_error(rsp, op, 0, ATT_ECODE_INVALID_PDU);
			hdl = get_le16(req + 1);
			off = get_le16(req + 3);
		}
		n = att_value(b, hdl, val, (int)sizeof(val));
		if (n < 0) return att_error(rsp, op, hdl, ATT_ECODE_INVALID_HANDLE);
		if (off > (uint16_t)n) return att_error(rsp, op, hdl, ATT_ECODE_INVALID_OFFSET);
		n -= off;
		if (n > (int)mtu - 1) n = (int)mtu - 1;
		rsp[0] = (op == ATT_OP_READ_REQ) ? ATT_OP_READ_RSP : ATT_OP_READ_BLOB_RSP;
		if (n > 0) memcpy(rsp + 1, val + off, (size_t)n);
		return 1 + n;
	}
	case ATT_OP_WRITE_REQ:
	case ATT_OP_WRITE_CMD: {
		int code;

		if (req_len < 3) {
			if (op == ATT_OP_WRITE_CMD) return 0;
			return att_error(rsp, op, 0, ATT_ECODE_INVALID_PDU);
		}
		hdl = get_le16(req + 1);
		code = att_write(b, hdl, req + 3, req_len - 3);
		if (op == ATT_OP_WRITE_CMD) return 0;
		if (code != 0) return att_error(rsp, op, hdl, (uint8_t)code);
		rsp[0] = ATT_OP_WRITE_RSP;
		return 1;
	}
	default:
		log_d("att opcode 0x%02x not supported\n", op);
		hdl = (req_len >= 3) ? get_le16(req + 1) : 0;
		return att_error(rsp, op, hdl, ATT_ECODE_REQ_NOT_SUPP);
	}
}

static void ble_accept_cb(int fd, unsigned ev, void *cbarg);
static void ble_rx_cb(int fd, unsigned ev, void *cbarg);
static void ble_peer_left(ble2_t *b);

static int ble_arm(ble2_t *b, int fd, aloe_evb2_cb_t cb) {
	void *ev;

	evconn_cancel(&b->evconn);
	b->evconn.fd = -1;
	ev = aloe_evb2_add_fd(b->ev_ctx, fd, ALOE_EVB2_FLAG_READ,
			ALOE_EVB2_INFINITE, cb, b);
	if (!ev) {
		log_e("Failure aloe_evb2_add_fd\n");
		return -1;
	}
	b->evconn.fd = fd;
	b->evconn.ev = ev;
	return 0;
}

static int ble_tx(int fd, const uint8_t *pdu, int n) {
	ssize_t w;

	if (n <= 0) return 0;
	w = send(fd, pdu, (size_t)n, MSG_NOSIGNAL);
	if (w != n) {
		log_e("att send: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

static void ble_stop(ble2_t *b) {
	int was_on = b->adv_on;

	b->adv_wanted = 0;
	b->adv_on = 0;
	evconn_cancel(&b->evconn);
	if (b->evconn.fd >= 0 && b->evconn.fd != b->listen_fd) close(b->evconn.fd);
	b->evconn.fd = -1;
	if (b->listen_fd >= 0) {
		close(b->listen_fd);
		b->listen_fd = -1;
	}
	b->state = ble_st_idle;
	b->cccd = 0;
	b->mtu = ATT_MTU_DEFAULT;
	b->peer[0] = '\0';
	if (was_on) ble_adv_set(b, 0);
}

static void ble_peer_left(ble2_t *b) {
	log_d("peer left\n");
	evconn_cancel(&b->evconn);
	if (b->evconn.fd >= 0 && b->evconn.fd != b->listen_fd) close(b->evconn.fd);
	b->evconn.fd = -1;
	b->cccd = 0;
	b->mtu = ATT_MTU_DEFAULT;
	b->peer[0] = '\0';
	b->adv_on = 0;
	b->state = ble_st_idle;
	if (!b->adv_wanted || b->listen_fd < 0) return;
	if (ble_adv_set(b, 1) != 0 || ble_arm(b, b->listen_fd, &ble_accept_cb) != 0) {
		log_e("advertising did not restart\n");
		ble_stop(b);
		return;
	}
	b->state = ble_st_adv;
	log_d("advertising again\n");
}

static void ble_rx_cb(int fd, unsigned ev, void *cbarg) {
	ble2_t *b = (ble2_t*)cbarg;

	if (b->evconn.fd != fd) {
		log_e("sanity check mismatch fd\n");
		return;
	}
	if (!(ev & ALOE_EVB2_FLAG_READ)) return;

	for (;;) {
		ssize_t len;
		uint8_t rsp[ATT_MTU_MAX];
		int n;

		len = recv(fd, b->rx, sizeof(b->rx), MSG_DONTWAIT);
		if (len < 0) {
			if (errno == EINTR) continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK) break;
			log_e("recv: %s\n", strerror(errno));
			ble_peer_left(b);
			return;
		}
		if (len == 0) {
			ble_peer_left(b);
			return;
		}
		n = att_on_pdu(b, b->rx, (int)len, rsp);
		if (n > 0 && ble_tx(fd, rsp, n) != 0) {
			ble_peer_left(b);
			return;
		}
	}
}

static void ble_accept_cb(int fd, unsigned ev, void *cbarg) {
	ble2_t *b = (ble2_t*)cbarg;
	struct sockaddr_l2 peer;
	socklen_t peer_len;
	int conn;

	if (b->evconn.fd != fd) {
		log_e("sanity check mismatch fd\n");
		return;
	}
	if (!(ev & ALOE_EVB2_FLAG_READ)) return;

	peer_len = sizeof(peer);
	memset(&peer, 0, sizeof(peer));
	conn = accept4(fd, (struct sockaddr*)&peer, &peer_len,
			SOCK_CLOEXEC | SOCK_NONBLOCK);
	if (conn < 0) {
		if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) return;
		log_e("accept: %s\n", strerror(errno));
		return;
	}
	ba2str(&peer.l2_bdaddr, b->peer, sizeof(b->peer));
	log_d("accepted %s\n", b->peer);
	b->mtu = ATT_MTU_DEFAULT;
	b->cccd = 0;
	b->adv_on = 0;
	b->state = ble_st_connected;
	if (ble_arm(b, conn, &ble_rx_cb) != 0) {
		close(conn);
		ble_peer_left(b);
		return;
	}
}

static int ble_listen_open(ble2_t *b) {
	int fd;
	struct sockaddr_l2 local;
	struct bt_security sec;

	fd = socket(AF_BLUETOOTH, SOCK_SEQPACKET | SOCK_CLOEXEC, BTPROTO_L2CAP);
	if (fd < 0) {
		log_e("l2cap socket: %s\n", strerror(errno));
		return -1;
	}
	memset(&local, 0, sizeof(local));
	local.l2_family = AF_BLUETOOTH;
	local.l2_cid = L2CAP_CID_ATT;
	local.l2_bdaddr_type = BDADDR_LE_PUBLIC;
	if (bind(fd, (struct sockaddr*)&local, sizeof(local)) != 0) {
		log_e("att bind: %s\n", strerror(errno));
		if (errno == EADDRINUSE) log_e("stop bluetoothd before blegatt advertise\n");
		close(fd);
		return -1;
	}
	memset(&sec, 0, sizeof(sec));
	sec.level = BT_SECURITY_LOW;
	if (setsockopt(fd, SOL_BLUETOOTH, BT_SECURITY, &sec, sizeof(sec)) != 0) {
		log_e("BT_SECURITY: %s\n", strerror(errno));
		close(fd);
		return -1;
	}
	if (listen(fd, 1) != 0) {
		log_e("att listen: %s\n", strerror(errno));
		close(fd);
		return -1;
	}
	if (aloe_file_nonblock(fd, 1) != 0) {
		log_e("failure set nonblock\n");
		close(fd);
		return -1;
	}
	b->listen_fd = fd;
	return 0;
}

static int ble_advertise(ble2_t *b, int on) {
	bdaddr_t ba;
	char addr[18];

	if (!on) {
		ble_stop(b);
		log_d("advertising off\n");
		return 0;
	}
	if (b->state != ble_st_idle) ble_stop(b);
	if (ble_hci_up(b->hci_id) != 0) return -1;
	if (ble_listen_open(b) != 0) return -1;
	b->adv_wanted = 1;
	if (ble_adv_set(b, 1) != 0 || ble_arm(b, b->listen_fd, &ble_accept_cb) != 0) {
		ble_stop(b);
		return -1;
	}
	b->state = ble_st_adv;
	addr[0] = '\0';
	if (ble_hci_addr(b->hci_id, &ba) == 0) ba2str(&ba, addr, sizeof(addr));
	log_d("advertising %s %s\n", ble_name, addr[0] ? addr : "");
	log_d("service 12345678-1234-5678-1234-56789abcdef0\n");
	log_d("characteristic 12345678-1234-5678-1234-56789abcdef1 read write notify\n");
	return 0;
}

static int ble_notify(ble2_t *b, const uint8_t *text, int n) {
	uint8_t pdu[3 + BLE_VALUE_MAX];
	int max;

	if (b->state != ble_st_connected || b->evconn.fd < 0) {
		log_e("not connected\n");
		return -1;
	}
	if ((b->cccd & 0x0001) == 0) {
		log_e("peer has not enabled notify\n");
		return -1;
	}
	max = (int)b->mtu - 3;
	if (max > BLE_VALUE_MAX) max = BLE_VALUE_MAX;
	if (n > max) {
		log_e("notify length %d exceeds %d\n", n, max);
		return -1;
	}
	if (n > 0) memcpy(b->value, text, (size_t)n);
	b->value_len = (uint16_t)n;
	pdu[0] = ATT_OP_HANDLE_NFY;
	put_le16(pdu + 1, HDL_CUS_VAL);
	if (n > 0) memcpy(pdu + 3, text, (size_t)n);
	if (ble_tx(b->evconn.fd, pdu, 3 + n) != 0) {
		ble_peer_left(b);
		return -1;
	}
	log_payload("notify", text, n);
	return 0;
}

static int ble_join_text(int argc, const char **argv, int from, char *text, size_t sz) {
	size_t off = 0;
	int i;

	text[0] = '\0';
	for (i = from; i < argc; i++) {
		int n;

		n = snprintf(text + off, sz - off, "%s%s", (i == from) ? "" : " ", argv[i]);
		if (n < 0 || (size_t)n >= sz - off) {
			log_e("text too long\n");
			return -1;
		}
		off += (size_t)n;
	}
	return (int)off;
}

extern "C"
void* blegatt_init(void *evctx) {
	ble2_t *b;

	b = (ble2_t*)aloe_calloc(1, sizeof(*b));
	if (!b) {
		log_e("failed alloc blegatt\n");
		return NULL;
	}
	b->evconn.fd = -1;
	b->evconn.ev_ctx = evctx;
	b->ev_ctx = evctx;
	b->listen_fd = -1;
	b->hci_id = 0;
	b->mtu = ATT_MTU_DEFAULT;
	b->state = ble_st_idle;
	log_d("blegatt initialized\n");
	return b;
}

extern "C"
void blegatt_destroy(void *_b) {
	ble2_t *b = (ble2_t*)_b;

	if (!b) return;
	ble_stop(b);
	aloe_free(b);
}

int cli_cmd_blegatt(void*, int argc, const char **argv) {
	ble2_t *b = (ble2_t*)blegatt_global;
	const char *sub;

	if (!b) {
		log_e("blegatt is not initialized\n");
		return -1;
	}
	if (argc < 2) {
		log_d("state %s peer %s mtu %u notify %s value %u\n",
				ble_st_name(b->state),
				b->peer[0] ? b->peer : "-",
				b->mtu,
				(b->cccd & 0x0001) ? "on" : "off",
				b->value_len);
		log_d("blegatt up | advertise on|off | notify <text> | close\n");
		return 0;
	}
	sub = argv[1];
	if (strcmp(sub, "up") == 0) return ble_hci_up(b->hci_id);
	if (strcmp(sub, "advertise") == 0) {
		if (argc < 3 || (strcmp(argv[2], "on") != 0 && strcmp(argv[2], "off") != 0)) {
			log_e("blegatt advertise on|off\n");
			return -1;
		}
		return ble_advertise(b, strcmp(argv[2], "on") == 0);
	}
	if (strcmp(sub, "notify") == 0) {
		char text[BLE_VALUE_MAX + 1];
		int n;

		if (argc < 3) {
			log_e("blegatt notify <text>\n");
			return -1;
		}
		n = ble_join_text(argc, argv, 2, text, sizeof(text));
		if (n < 0) return -1;
		return ble_notify(b, (const uint8_t*)text, n);
	}
	if (strcmp(sub, "close") == 0) {
		ble_stop(b);
		log_d("closed\n");
		return 0;
	}
	log_e("unknown blegatt command %s\n", sub);
	return -1;
}
