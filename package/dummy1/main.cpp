/* $Id$
 *
 * Copyright 2025, joelai
 * This is proprietary information of joelai
 * All Rights Reserved. Reproduction of this documentation or the
 * accompanying programs in any manner whatsoever without the written
 * permission of joelai is strictly forbidden.
 *
 * @author joelai
 */

#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h> // For TCP_KEEPIDLE, TCP_KEEPINTVL, etc. (Linux)
#include "tester_evsvc1.h"
#include "priv.h"
#include <aloe/ev.h>
#include <aloe/compat/openbsd/sys/queue.h>
#include <aloe/compat/openbsd/sys/tree.h>

typedef struct evconn_rec {
	int fd;
	void *ev_ctx, *ev;
	TAILQ_ENTRY(evconn_rec) qent;
} evconn_t;
typedef TAILQ_HEAD(evconn_list_rec, evconn_rec) evconn_list_t;

static struct {
	void *ev_ctx;
	char quit;
} impl;

#define dump_argv(_argc, _argv) for (int i = 0; i < _argc; i++) { \
	log_d("argv[%d/%d]: %s\n", i + 1, _argc, _argv[i]); \
}

#define evconn_list_add(_q, _e) TAILQ_INSERT_TAIL(_q, _e, qent)
#define evconn_list_rm(_q, _e) TAILQ_REMOVE(_q, _e, qent)

static int test_mm1(void*,int,const char**) {
	char *mm;

	mm = (char*)aloe_calloc(3, 2);
	if (aloe_free(mm) != 0) {
		log_e("Sanity check unexpected aloe_free\n");
	}

	mm = (char*)aloe_calloc(3, 2);
	mm[6] = 'c';
	log_d("expect report overflow\n");
	if (aloe_free(mm) == 0) {
		log_e("Sanity check unexpected aloe_free\n");
	}
	return 1;
}

static void cycletime_cb(int fd, unsigned ev, void *arg) {
	static struct timespec report_ts = {}, cycle_ts = {}, cycle_td = {};
	struct timespec ts;
	time_t t_epoch;
	struct tm tm;

	clock_gettime(CLOCK_REALTIME, &ts);

	if (cycle_ts.tv_sec != 0 && cycle_ts.tv_nsec != 0 // initial
			&& ALOE_TIMESEC_CMP(ts.tv_sec, ts.tv_nsec,
					cycle_ts.tv_sec, cycle_ts.tv_nsec) >= 0
					) {
		ALOE_TIMESEC_SUB(ts.tv_sec, ts.tv_nsec,
				cycle_ts.tv_sec, cycle_ts.tv_nsec,
				cycle_td.tv_sec, cycle_td.tv_nsec, 1000000000ul);
	}
	cycle_ts = ts;

	t_epoch = (time_t)ts.tv_sec;
	localtime_r(&t_epoch, &tm);

	if (report_ts.tv_sec == 0 // initial
			|| ts.tv_sec < report_ts.tv_sec // rollback
			|| ts.tv_sec - report_ts.tv_sec >= 1 // report interval
			) {
		uint64_t cycle_us = cycle_td.tv_sec * 1000000 + cycle_td.tv_nsec / 1000;
		log_d("tick %llu, cycle_us %llu\n",
				(unsigned long long)t_epoch * 1000 + ts.tv_nsec / 1000000,
				(unsigned long long)cycle_us);
		report_ts = ts;
	}
finally:
	if (aloe_ev_put(impl.ev_ctx, -1, &cycletime_cb, NULL, 0, 0, 0) == NULL) {
		log_e("Failure aloe_ev_put\n");
	}
}

typedef struct mgmt1_client_rec {
	evconn_t evconn;
	struct sockaddr_un sa;
	void *mgmt;
} mgmt1_client_t;

typedef struct {
	evconn_t evconn;
	struct sockaddr_un sa;
	evconn_list_t client_list;
} mgmt1_t;

static void mgmt1_client_add(mgmt1_t *mgmt, mgmt1_client_t *client) {
	evconn_list_add(&mgmt->client_list, &client->evconn);
}

static void mgmt1_client_rm(mgmt1_t *mgmt, mgmt1_client_t *client) {
	evconn_list_rm(&mgmt->client_list, &client->evconn);
}

static void mgmt1_client_release(mgmt1_client_t *client) {
	if (client->evconn.ev) aloe_ev_cancel(client->evconn.ev_ctx, client->evconn.ev);
	if (client->evconn.fd) close(client->evconn.fd);
	if (client->mgmt) mgmt1_client_rm((mgmt1_t*)client->mgmt, client);
	aloe_free(client);
}

static void mgmt1_client_cb(int fd, unsigned ev, void *cbarg) {
	mgmt1_client_t *client = (mgmt1_client_t*)cbarg;
	mgmt1_t *mgmt = (mgmt1_t*)client->mgmt;
	char buf[1024];
	int r;

	client->evconn.ev = NULL;

	if (ev & aloe_ev_flag_read) {
		r = read(fd, buf, sizeof(buf) - 1);
		log_d("read %d from peer\n", r);
		if (r == 0) {
			log_d("peer closed\n");
			mgmt1_client_release(client);
			client = NULL;
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
				mgmt1_client_release(client);
				client = NULL;
			}
			goto finally;
		}
		buf[r] = '\0';
		log_d("recv: %s\n", buf);
	}
finally:
	if (client) {
		if ((client->evconn.ev = aloe_ev_put(client->evconn.ev_ctx,
				client->evconn.fd, &mgmt1_client_cb, client, aloe_ev_flag_read,
				ALOE_EV_INFINITE, 0)) == NULL) {
			log_e("Failure aloe_ev_put\n");
			mgmt1_client_release(client);
		}
	}
}

static void mgmt1_accept_cb(int fd, unsigned ev, void *cbarg) {
	int client_fd = -1, r;
	mgmt1_t *mgmt = (mgmt1_t*)cbarg;
	struct sockaddr_un sa;
	socklen_t sa_len = sizeof(sa);
	mgmt1_client_t *client;

	mgmt->evconn.ev = NULL;

	if ((client_fd = accept(fd, (struct sockaddr*)&sa, &sa_len)) == -1) {
		log_e("failure accept\n");
		goto finally;
	}
#if 1
	do {
		struct ucred cred;
		socklen_t cred_len = sizeof(cred);

		if (getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &cred,
				&cred_len) != 0) {
			r = errno;
			log_e("failure get peer");
			break;
		}
		log_d("accepted unix socket from PID %d, UID %d, GID %d\n",
				cred.pid, cred.uid, cred.gid);
	} while(0);
#endif
	if (aloe_file_nonblock(client_fd, 1) != 0
			// || aloe_so_reuseaddr(client_fd) != 0
			// || aloe_so_keepalive(client_fd) != 0
		) {
		log_e("failure set nonblock or socket flag\n");
		close(client_fd);
		goto finally;
	}

	if ((client = (mgmt1_client_t*)aloe_calloc(1, sizeof(*client))) == NULL) {
		log_e("Failure aloe_ev_put\n");
		close(client_fd);
		goto finally;
	}
	client->evconn.fd = client_fd;
	client->sa = sa;
	client->mgmt = mgmt;
	client->evconn.ev_ctx = mgmt->evconn.ev_ctx;
	mgmt1_client_add(mgmt, client);

	if ((client->evconn.ev = aloe_ev_put(client->evconn.ev_ctx,
			client->evconn.fd, &mgmt1_client_cb, client, aloe_ev_flag_read,
			ALOE_EV_INFINITE, 0)) == NULL) {
		log_e("Failure aloe_ev_put\n");
		mgmt1_client_release(client);
		goto finally;
	}
finally:
	// keep listen
	if ((mgmt->evconn.ev = aloe_ev_put(mgmt->evconn.ev_ctx, mgmt->evconn.fd,
			&mgmt1_accept_cb, mgmt, aloe_ev_flag_read, ALOE_EV_INFINITE,
			0)) == NULL) {
		log_e("Failure aloe_ev_put\n");
	}
}

static int mgmt1_init(const char *path) {
	int ret = -1, r, sock_type;
	mgmt1_t *mgmt = NULL;
	size_t path_len = strlen(path);

	if (path_len >= sizeof(mgmt->sa.sun_path)) {
		log_e("too long path\n");
		goto finally;
	}

	unlink(path);

	if ((mgmt = (mgmt1_t*)aloe_calloc(1, sizeof(*mgmt))) == NULL) {
		log_e("failure alloc mgmt1\n");
		goto finally;
	}
	TAILQ_INIT(&mgmt->client_list);
	mgmt->evconn.fd = -1;
	mgmt->evconn.ev_ctx = impl.ev_ctx;


	sock_type = SOCK_STREAM;
//	sock_type = SOCK_DGRAM;
	if ((mgmt->evconn.fd = socket(AF_UNIX, sock_type, 0)) == -1) {
		r = errno;
		log_e("failure socket AF_UNIX: %s\n", strerror(r));
		goto finally;
	}
	mgmt->sa.sun_family = AF_UNIX;
	memcpy(mgmt->sa.sun_path, path, path_len);
	((char*)mgmt->sa.sun_path)[path_len] = '\0';
	if (bind(mgmt->evconn.fd, (struct sockaddr*)&mgmt->sa,
			sizeof(mgmt->sa)) != 0) {
		r = errno;
		log_e("failure bind socket: %s\n", strerror(r));
		goto finally;
	}
	if (aloe_file_nonblock(mgmt->evconn.fd, 1) != 0
#if 1
			|| aloe_so_reuseaddr(mgmt->evconn.fd) != 0
			|| aloe_so_keepalive(mgmt->evconn.fd) != 0
#endif
			) {
		log_e("failure set nonblock or socket flag\n");
		goto finally;
	}
	if ((sock_type == SOCK_STREAM)
			&& listen(mgmt->evconn.fd, 5) != 0) {
		r = errno;
		log_e("failure listen: %s\n", strerror(r));
		goto finally;
	}

	if ((mgmt->evconn.ev = aloe_ev_put(mgmt->evconn.ev_ctx, mgmt->evconn.fd,
			&mgmt1_accept_cb, mgmt, aloe_ev_flag_read, ALOE_EV_INFINITE,
			0)) == NULL) {
		log_e("Failure aloe_ev_put\n");
		goto finally;
	}

	log_d("listen on %s\n", path);
	ret = 0;
finally:
	if (ret != 0) {
		if (mgmt) {
			if (mgmt->evconn.ev) {
				aloe_ev_cancel(mgmt->evconn.ev_ctx, mgmt->evconn.ev);
			}
			if (mgmt->evconn.fd != -1) close(mgmt->evconn.fd);
			aloe_free(mgmt);
		}
	}
	return ret;
}

typedef struct cli_cmd_rec {
	const char *str;
	int (*run)(int, const char**);
	const char *detail;
	RB_ENTRY(cli_cmd_rec) qent;
} cli_cmd_t;

typedef RB_HEAD(cli_cmdq_rec, cli_cmd_rec) cli_cmdq_t;

static int cli_cmd_cmp(cli_cmd_t *a, cli_cmd_t *b) {
	return strcmp(a->str, b->str);
}
RB_GENERATE_STATIC(cli_cmdq_rec, cli_cmd_rec, qent, cli_cmd_cmp);

typedef struct cli_rec {
	evconn_t evconn;
	char line_fb_data[2048];
	aloe_buf_t line_fb;
	cli_cmdq_t cmdq;
} cli1_t;

static int cli_help(int argc, const char **argv);
cli_cmd_t cli_lut[] = {
//	{"n2", &test_n2, "<0 | 1>"},
//	{"fan", &test_fan, "<1 | 2 | 3> <0 | 1>"},
//	{"poe", &test_poe, "<0 | 1>"},
	{"help", &cli_help, "Show this help"}
};

static cli_cmd_t* cli_lut_find(cli1_t *cli, const char *str) {
	cli_cmd_t *cli_ref;
	size_t cli_cnt = aloe_arraysize(cli_lut);
	int i;

	for (i = 0; i < (int)cli_cnt; i++) {
		cli_ref = &cli_lut[i];
		if (strcasecmp(str, cli_ref->str) == 0) return cli_ref;
	}
	return NULL;
}

static int cli_help(int argc, const char **argv) {
	cli_cmd_t *cli_ref;
	const char *tgt = (argc >= 3 ? argv[2] : NULL);
	size_t cli_cnt = aloe_arraysize(cli_lut);
	int i;

	for (i = 0; i < (int)cli_cnt; i++) {
		cli_ref = &cli_lut[i];
		if (tgt && strcasecmp(tgt, cli_ref->str) != 0) continue;

		if (cli_ref->detail) {
			printf("%s - %s\n", cli_ref->str, cli_ref->detail);
		} else {
			printf("%s\n", cli_ref->str);
		}
		if (tgt) break;
	}
	return 0;
}

/**
 * trim starting and trailing whitespace and un-printable
 */
static const char* cli_line_trim(const char *data, size_t *data_sz) {
	size_t line_sz = *data_sz;

	// trim starting
	while (line_sz > 0) {
		int ch = data[0];

		if (isprint(ch) && !strchr(" \r\n\t", ch)) {
			break;
		}
		data++;
		line_sz--;
	}
	if (!line_sz) return NULL;

	// trim trailing
	while (line_sz > 0) {
		int ch = data[line_sz - 1];

		if (isprint(ch) && !strchr(" \r\n\t", ch)) {
			break;
		}
		line_sz--;
	}
	if (!line_sz) return NULL;

	*data_sz = line_sz;
	return data;
}

static int cli_input_line(cli1_t *cli, const char *line_start, size_t line_sz) {
	int r;
	cli_cmd_t *cli_ref;
	char line_buf[1024];
	const char *argv[20];
	int argc = aloe_arraysize(argv) - 1;

	if ((line_start = cli_line_trim(line_start, &line_sz)) == NULL) {
		return 0;
	}

	if (line_sz >= sizeof(line_buf)) {
		log_e("cli too long\n");
		return -1;
	}

	memcpy(line_buf, line_start, line_sz);
	line_buf[line_sz] = '\0';
	aloe_cli_tok(line_buf, &argc, &argv[1], NULL);
	if ((cli_ref = cli_lut_find(cli, argv[1])) == NULL) {
		log_e("Command %s not found\n", argv[1]);
		return -1;
	}
	argv[0] = "cli";
	argc++;
	if ((r = (*cli_ref->run)(argc, argv)) == 0) {
		log_d("Command %s return %d\n", argv[1], r);
		return 0;
	}
	log_e("Command %s return code: %d\n", argv[1], r);
	return r;
}

static int cli_input(cli1_t *cli, const char *data, size_t data_sz) {
	int r;
	const char *lf_pos;
	size_t lf_parse;

	// insufficient buffer, drop all
	if (cli->line_fb.pos + data_sz >= cli->line_fb.cap) {
		log_e("cli too long\n");
		_aloe_buf_clear(&cli->line_fb);
		return 0;
	}

	lf_parse = cli->line_fb.pos;

	// append data to line_fb
	memcpy((char*)cli->line_fb.data + cli->line_fb.pos, data, data_sz);
	((char*)cli->line_fb.data)[cli->line_fb.pos += data_sz] = '\0';

	// line starting from saved buffer
	data = (char*)cli->line_fb.data;

	// search newline from incoming data
	lf_pos = (char*)memmem((char*)cli->line_fb.data + lf_parse, data_sz,
			"\n", 1);

	while (lf_pos) {
		// including newline
		cli_input_line(cli, data, lf_pos - data + 1);

		// update line start position
		data = lf_pos + 1;

		if (data >= (char*)cli->line_fb.data + cli->line_fb.pos) {
			// exhausted all data
			break;
		}

		// search newline from next data
		data_sz = (char*)cli->line_fb.data + cli->line_fb.pos - data;
		lf_pos = (char*)memmem(data, data_sz, "\n", 1);
	}

	if (data >= (char*)cli->line_fb.data + cli->line_fb.pos) {
		// exhausted all data
		((char*)cli->line_fb.data)[cli->line_fb.pos = 0] = '\0';
	} else if (data > (char*)cli->line_fb.data) {
		// wrap data for next line start
		data_sz = (char*)cli->line_fb.data + cli->line_fb.pos - data;
		memmove(cli->line_fb.data, data, data_sz);
		((char*)cli->line_fb.data)[cli->line_fb.pos = data_sz] = '\0';
//	} else if (line_start == (char*)cli->line_fb.data) {
//		// no need wrap
	}

	return 0;
}

static void cli1_release(cli1_t *cli) {
	if (cli->evconn.ev) {
		aloe_ev_cancel(cli->evconn.ev_ctx, cli->evconn.ev);
	}
	if (cli->evconn.fd != -1) close(cli->evconn.fd);
	aloe_free(cli);
}

static void cli1_cb(int fd, unsigned ev, void *cbarg) {
	cli1_t *cli = (cli1_t*)cbarg;
	char buf[1024];
	int r;

	cli->evconn.ev = NULL;

	if (ev & aloe_ev_flag_read) {
		r = read(fd, buf, sizeof(buf) - 1);
//		log_d("read %d from cli\n", r);
		if (r == 0) {
			log_d("peer closed\n");
			cli1_release(cli);
			cli = NULL;
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
				cli1_release(cli);
				cli = NULL;
			}
			goto finally;
		}
		buf[r] = '\0';
//		log_d("recv: %s\n", buf);
		cli_input(cli, buf, r);
	}
finally:
	if (cli) {
		if ((cli->evconn.ev = aloe_ev_put(cli->evconn.ev_ctx, cli->evconn.fd,
				&cli1_cb, cli, aloe_ev_flag_read, ALOE_EV_INFINITE,
				0)) == NULL) {
			log_e("Failure aloe_ev_put\n");
			cli1_release(cli);
		}
	}
}

static int cli1_init(void) {
	int ret = -1, r;
	cli1_t *cli = NULL;

	if ((cli = (cli1_t*)aloe_calloc(1, sizeof(*cli))) == NULL) {
		log_e("failure alloc cli1\n");
		goto finally;
	}
	cli->evconn.fd = -1;
	cli->evconn.ev_ctx = impl.ev_ctx;
	cli->line_fb.data = cli->line_fb_data;
	cli->line_fb.cap = sizeof(cli->line_fb_data);

	if ((cli->evconn.fd = dup(STDIN_FILENO)) == -1) {
		r = errno;
		log_e("failure dup stdin\n");
		goto finally;
	}
	if (aloe_file_nonblock(cli->evconn.fd, 1) != 0
#if 0
			|| aloe_so_reuseaddr(cli->evconn.fd) != 0
			|| aloe_so_keepalive(cli->evconn.fd) != 0
#endif
			) {
		log_e("failure set nonblock or socket flag\n");
		goto finally;
	}
	if ((cli->evconn.ev = aloe_ev_put(cli->evconn.ev_ctx, cli->evconn.fd,
			&cli1_cb, cli, aloe_ev_flag_read, ALOE_EV_INFINITE,
			0)) == NULL) {
		log_e("Failure aloe_ev_put\n");
		goto finally;
	}

	log_d("listen on cli\n");
	ret = 0;
finally:
	if (ret != 0) {
		if (cli) {
			cli1_release(cli);
		}
	}
	return ret;
}

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
static ipc1_t *ipc_global;

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

static int ipc1_write(ipc1_t *ipc, int type, int *seq,
		const void *data, size_t data_sz) {
	static int msg_seq = 1;
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

	if (!ipc) ipc = ipc_global;

	if (!ipc || (fd = ipc->fd[1]) == -1) {
		log_e("invalid ipc_global\n");
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

static ipc1_t* ipc1_init(void) {
	int ret = -1, r;
	ipc1_t *ipc = NULL;

	if ((ipc = (ipc1_t*)aloe_calloc(1, sizeof(*ipc))) == NULL) {
		log_e("failure alloc ipc1\n");
		goto finally;
	}
	ipc->evconn.fd = -1;
	ipc->evconn.ev_ctx = impl.ev_ctx;
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
		}
	}
	return ipc;
}

static void test_cli1(void) {
	cli1_t _cli = {}, *cli = &_cli;
	const char *buf;

	cli->evconn.fd = -1;
	cli->line_fb.data = cli->line_fb_data;
	cli->line_fb.cap = sizeof(cli->line_fb_data);

	buf = "hel";
	cli_input(cli, buf, strlen(buf));

	buf = "p\nhel";
	cli_input(cli, buf, strlen(buf));

	buf = "p\n";
	cli_input(cli, buf, strlen(buf));

}

static void tester_proc2(void *args) {
	int *seq = (int*)args;

	log_d("seq: %d\n", seq ? *seq : 0);
}

static void* tester_proc(void *args) {
	int r = -1, seq = 0, msg_seq;
	struct {
		void (*func)(void*);
		void* cbarg;
	} val = {.func = &tester_proc2, .cbarg = &seq};

	(void)args;

	while (1) {
		if ((r = ipc1_write(NULL, 1, &msg_seq,
				&val, sizeof(val))) < sizeof(val)) {
			log_e("failed write to ipc1\n");
			goto finally;
		}
		log_d("seq %d, msg_seq %d sent\n", seq, msg_seq);
		seq++;
		sleep(1);
	}
finally:
	return (void*)(unsigned long)r;
}

int main(int argc, const char **argv) {
	int ret = -1;
	pthread_t tester = {};

	log_d("%s\n", aloe_version(NULL, 0));

	dump_argv(argc, argv)

	if (0) {
		test_cli1();
		goto finally;
	}

	if ((impl.ev_ctx = aloe_ev_init(0)) == NULL) {
		log_e("Failure alloc ev_ctx\n");
		goto finally;
	}

#if 0 // enable for callback in each cycle
	if (aloe_ev_put(impl.ev_ctx, -1, &cycletime_cb, NULL, 0, 0, 0) == NULL) {
		log_e("Failure aloe_ev_put\n");
		goto finally;
	}
#endif

	mgmt1_init("mgmt1.socket");
	cli1_init();

	ipc_global = ipc1_init();

	pthread_create(&tester, NULL, &tester_proc, NULL);

	while (!impl.quit) {
		aloe_ev_once(impl.ev_ctx);
	}
	ret = 0;
finally:
	if (impl.ev_ctx) {
		aloe_ev_destroy(impl.ev_ctx);
	}
	return ret;
}
