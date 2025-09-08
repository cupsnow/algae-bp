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

#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h> // For TCP_KEEPIDLE, TCP_KEEPINTVL, etc. (Linux)
#include "priv_ev.h"
#include "ipc.h"

typedef enum {
	ipc1_parse_state_sync = 0,
	ipc1_parse_state_header,
	ipc1_parse_state_payload,
} ipc1_parse_state_t;

typedef struct {
	evconn_t evconn;
	int fd[2];
	char msg_fb_data[2048];
	aloe_buf_t msg_fb;
	int parse_state;
	size_t parse_expect_size;
} ipc1_t;

typedef struct __attribute__((packed)) {
	uint8_t leading[4];
	uint16_t seq;
	uint16_t type;
	uint16_t length;
	uint8_t val[1];
} ipc1_msg_t;
#define IPC1_MSG_LEADNG_INITIALIZER 1, 0xa5, 1, 0x5a
static const uint8_t IPC1_MSG_LEADNG[sizeof(aloe_member_of(ipc1_msg_t, leading))] = {IPC1_MSG_LEADNG_INITIALIZER};

static pthread_mutex_t ipc_global_mutex = PTHREAD_MUTEX_INITIALIZER;

extern "C" {
void *ipc_global;
}


static void ipc1_release(ipc1_t *ipc) {
	if (ipc->evconn.ev) {
		aloe_ev_cancel(ipc->evconn.ev_ctx, ipc->evconn.ev);
	}
	if (ipc->evconn.fd != -1) {
		close(ipc->evconn.fd);
	}
	for (int i = 0; i < aloe_arraysize(ipc->fd); i++) {
		if (ipc->fd[i] != -1 && ipc->fd[i] != ipc->evconn.fd) {
			close(ipc->fd[i]);
		}
	}
	aloe_free(ipc);
}

static int ipc1_input_msg(ipc1_t *ipc, const ipc1_msg_t *msg) {
	log_d("ipc msg seq %d, type %d, length %d\n", (int)msg->seq,
			(int)msg->type, (int)msg->length);

	if (msg->type == 1) {
		typedef void (*func_t)(void*);
		func_t func;
		void *cbarg = NULL;

		if (msg->length >= sizeof(func)) {
			memcpy(&func, msg->val, sizeof(func));
		}
		if (msg->length >= sizeof(func) + sizeof(cbarg)) {
			memcpy(&cbarg, &msg->val[sizeof(func)], sizeof(cbarg));
		}
		if (func) func(cbarg);
	}

	return 0;
}

static int ipc1_input(ipc1_t *ipc, const char *data, size_t data_sz) {
	int r;
	const ipc1_msg_t *msg;
	size_t hdr_len = aloe_offsetafter(ipc1_msg_t, length);
	size_t msg_sz;

	while (data_sz-- > 0) {
		int c = *data++;

		if (ipc->msg_fb.pos >= ipc->msg_fb.cap) {
			log_e("too long data\n");
			ipc->parse_state = ipc1_parse_state_sync;
			ipc->msg_fb.pos = 0;
		}

		((char*)ipc->msg_fb.data)[ipc->msg_fb.pos++] = c;

		if (ipc->parse_state == ipc1_parse_state_sync) {
			if (ipc->msg_fb.pos < hdr_len) {
//				log_e("more data for header\n");
				continue;
			}
			if (memcmp(ipc->msg_fb.data, IPC1_MSG_LEADNG,
					sizeof(IPC1_MSG_LEADNG)) != 0) {
				log_e("expect leading\n");
				memmove(ipc->msg_fb.data, (char*)ipc->msg_fb.data + 1,
						ipc->msg_fb.pos -= 1);
				continue;
			}
			ipc->parse_state = ipc1_parse_state_payload;
			// fall through since payload length may zero
		}
		if (ipc->parse_state == ipc1_parse_state_payload) {
			msg = (ipc1_msg_t*)ipc->msg_fb.data;
			msg_sz = hdr_len + msg->length;

			if (ipc->msg_fb.pos < msg_sz) {
//				log_d("more data for payload\n");
				continue;
			}

			ipc1_input_msg(ipc, msg);

			if (msg_sz >= ipc->msg_fb.pos) {
//				log_d("more data for msg\n");
				ipc->msg_fb.pos = 0;
				continue;
			}

			log_d("sanity check, more data existed");
			memmove(ipc->msg_fb.data, (char*)ipc->msg_fb.data + msg_sz,
					ipc->msg_fb.pos -= msg_sz);
		}
	}
	return 0;
}

static void ipc1_cb(int fd, unsigned ev, void *cbarg) {
	ipc1_t *ipc = (ipc1_t*)cbarg;
	char buf[1024];
	int r;

	ipc->evconn.ev = NULL;

	if (ev & aloe_ev_flag_read) {
		r = read(fd, buf, sizeof(buf) - 1);
		if (r == 0) {
			log_d("peer closed\n");
			ipc1_release(ipc);
			ipc = NULL;
			goto finally;
		}
		if (r < 0) {
			r = errno;

			if (r != EAGAIN
#ifdef EWOULDBLOCK
					&& r != EWOULDBLOCK
#endif
					) {
				log_e("failure read %s\n", strerror(r));
				ipc1_release(ipc);
				ipc = NULL;
			}
			goto finally;
		}
		buf[r] = '\0';
//		log_d("recv: %s\n", buf);
		ipc1_input(ipc, buf, r);
	}
finally:
	if (ipc) {
		if ((ipc->evconn.ev = aloe_ev_put(ipc->evconn.ev_ctx, ipc->evconn.fd,
				&ipc1_cb, ipc, aloe_ev_flag_read, ALOE_EV_INFINITE,
				0)) == NULL) {
			log_e("Failure aloe_ev_put\n");
			ipc1_release(ipc);
		}
	}
}

extern "C"
int ipc1_write(void *_ipc, int type, int *seq, const void *data,
		size_t data_sz) {
	static int msg_seq = 1;
	ipc1_t *ipc = (ipc1_t*)_ipc;
	int ret = 0, fd = -1, r, data_pos = 0;
	char locked = 0;
	struct __attribute__((packed)) {
		ipc1_msg_t base;
		uint8_t _data[1024];
	} msg = {
		.base = {
			.leading = {IPC1_MSG_LEADNG_INITIALIZER}, .type = (uint16_t)type,
			.length = (uint16_t)data_sz
		},
	};
	size_t hdr_len = aloe_offsetafter(ipc1_msg_t, length);

	if ((pthread_mutex_lock(&ipc_global_mutex)) != 0) {
		r = errno;
		log_e("failed lock: %s\n", strerror(r));
		goto finally;
	}
	locked = 1;

	msg.base.seq = (uint16_t)msg_seq++;
	if (seq) *seq = msg.base.seq;

	if (!ipc || (fd = ipc->fd[1]) == -1) {
		log_e("invalid argument\n");
		goto finally;
	}

	data_pos = 0;
	while (data_pos < hdr_len) {
		r = write(fd, (char*)&msg + data_pos, hdr_len - data_pos);
		if (r == 0) {
			log_e("ipc_global closed\n");
			goto finally;
		}
		if (r < 0) {
			r = errno;
			log_e("failure send to ipc_global, %s\n", strerror(r));
			goto finally;
		}
		data_pos += r;
	}

	data_pos = 0;
	while (data_pos < data_sz) {
		r = write(fd, (char*)data + data_pos, data_sz - data_pos);
		if (r == 0) {
			log_e("ipc_global closed\n");
			ret = data_pos;
			goto finally;
		}
		if (r < 0) {
			r = errno;
			log_e("failure send to ipc_global, %s\n", strerror(r));
			ret = data_pos;
			goto finally;
		}
		data_pos += r;
	}
	ret = data_pos;
finally:
	if (locked) {
		pthread_mutex_unlock(&ipc_global_mutex);
	}
	return ret;
}

extern "C"
void* ipc1_init(void *evctx) {
	int ret = -1, r;
	ipc1_t *ipc = NULL;

	if ((ipc = (ipc1_t*)aloe_calloc(1, sizeof(*ipc))) == NULL) {
		log_e("failure alloc ipc1\n");
		goto finally;
	}
	ipc->evconn.fd = -1;
	ipc->evconn.ev_ctx = evctx;
	ipc->msg_fb.data = ipc->msg_fb_data;
	ipc->msg_fb.cap = sizeof(ipc->msg_fb_data);
	ipc->fd[0] = ipc->fd[1] = -1;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, ipc->fd) == -1) {
		r = errno;
		log_e("failure socketpair %s\n", strerror(r));
		ipc->fd[0] = ipc->fd[1] = -1;
		goto finally;
	}
	ipc->evconn.fd = ipc->fd[0];

	if (aloe_file_nonblock(ipc->evconn.fd, 1) != 0
#if 1
			|| aloe_so_reuseaddr(ipc->evconn.fd) != 0
			|| aloe_so_keepalive(ipc->evconn.fd) != 0
#endif
			) {
		log_e("failure set nonblock or socket flag\n");
		goto finally;
	}

	if ((ipc->evconn.ev = aloe_ev_put(ipc->evconn.ev_ctx, ipc->evconn.fd,
			&ipc1_cb, ipc, aloe_ev_flag_read, ALOE_EV_INFINITE,
			0)) == NULL) {
		log_e("Failure aloe_ev_put\n");
		goto finally;
	}

	ret = 0;
finally:
	if (ret != 0) {
		if (ipc) {
			ipc1_release(ipc);
			ipc = NULL;
		}
	}
	return ipc;
}
