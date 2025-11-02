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
#include "cli.h"

typedef struct cli_cmd_rec {
	const char *str;
	int (*run)(void*, int, const char**);
	const char *detail;
	void *cbarg;
	RB_ENTRY(cli_cmd_rec) qent;
} cli_cmd_t;

typedef RB_HEAD(cli_cmdq_rec, cli_cmd_rec) cli_cmdq_t;

typedef struct cli_rec {
	evconn_t evconn;
	char line_fb_data[2048];
	aloe_buf_t line_fb;
	cli_cmdq_t cmdq;
} cli1_t;

extern "C" {
void *cli_global;
}

static int cli_cmd_cmp(cli_cmd_t *a, cli_cmd_t *b) {
	return strcmp(a->str, b->str);
}

RB_GENERATE_STATIC(cli_cmdq_rec, cli_cmd_rec, qent, cli_cmd_cmp);

static int cli_help(void *cbarg, int argc, const char **argv);
cli_cmd_t cli_lut[] = {
	{"help", &cli_help, "Show this help"}
};

static cli_cmd_t* cli_cmdq_find(cli_cmdq_t *cmdq, const char *str) {
	cli_cmd_t cli_ref = {.str = str}, *found = NULL;

	found = RB_FIND(cli_cmdq_rec, cmdq, &cli_ref);
	return found;
}

static void cli_cmdq_add(cli_cmdq_t *cmdq, cli_cmd_t *cmd) {
	RB_INSERT(cli_cmdq_rec, cmdq, cmd);
}

static cli_cmd_t* cli_cmdq_foreach(cli_cmdq_t *cmdq, cli_cmd_t *cmd) {
	if (!cmd) {
		cmd = RB_MIN(cli_cmdq_rec, cmdq);
	} else {
		cmd = RB_NEXT(cli_cmdq_rec, cmdq, cmd);
	}
	return cmd;
}

static cli_cmd_t* cli_lut_find(cli1_t *cli, const char *str) {
	cli_cmd_t *cli_ref;
	size_t cli_cnt = aloe_arraysize(cli_lut);
	int i;

	for (i = 0; i < (int)cli_cnt; i++) {
		cli_ref = &cli_lut[i];
		if (strcasecmp(str, cli_ref->str) == 0) return cli_ref;
	}

	if ((cli_ref = cli_cmdq_find(&cli->cmdq, str))) {
		return cli_ref;
	}

	return NULL;
}

static int cli_help(void *cbarg, int argc, const char **argv) {
	cli_cmd_t *cli_ref;
	const char *tgt = (argc >= 3 ? argv[2] : NULL);
	size_t cli_cnt = aloe_arraysize(cli_lut);
	cli1_t *cli = (cli1_t*)cbarg;
	int i;

	for (i = 0; i < (int)cli_cnt; i++) {
		cli_ref = &cli_lut[i];
		if (tgt && strcasecmp(tgt, cli_ref->str) != 0) continue;

		if (cli_ref->detail) {
			printf("%s - %s\n", cli_ref->str, cli_ref->detail);
		} else {
			printf("%s\n", cli_ref->str);
		}
		if (tgt) return 0;
	}

	if (cli) {
		cli_ref = NULL;
		while ((cli_ref = cli_cmdq_foreach(&cli->cmdq, cli_ref))) {
			if (tgt && strcasecmp(tgt, cli_ref->str) != 0) continue;

			if (cli_ref->detail) {
				printf("%s - %s\n", cli_ref->str, cli_ref->detail);
			} else {
				printf("%s\n", cli_ref->str);
			}
			if (tgt) return 0;
		}
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
	if ((r = (*cli_ref->run)(cli_ref->cbarg, argc, argv)) == 0) {
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

extern "C"
void* cli1_init(void *evctx) {
	int ret = -1, r;
	cli1_t *cli = NULL;

	if ((cli = (cli1_t*)aloe_calloc(1, sizeof(*cli))) == NULL) {
		log_e("failure alloc cli1\n");
		goto finally;
	}
	cli->evconn.fd = -1;
	cli->evconn.ev_ctx = evctx;
	cli->line_fb.data = cli->line_fb_data;
	cli->line_fb.cap = sizeof(cli->line_fb_data);
	RB_INIT(&cli->cmdq);

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

	cli1_cmd_add(cli, "help2", &cli_help, cli, "Show this help");

	log_d("listen on cli\n");
	ret = 0;
finally:
	if (ret != 0) {
		if (cli) {
			cli1_release(cli);
			cli = NULL;
		}
	}
	return cli;
}

extern "C"
void cli1_test1(void) {
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

extern "C"
int cli1_cmd_add(void *_clictx, const char *str,
		int (*run)(void*, int, const char**), void *cbarg, const char *detail) {
	cli1_t *cli = (cli1_t*)_clictx;
	cli_cmd_t *cmd = NULL;
	size_t str_sz = str ? strlen(str) : 0;
	size_t detail_sz = detail ? strlen(detail) : 0;
	size_t cmd_sz = sizeof(*cmd) + (str_sz + 1) + (detail_sz + 1);
	char *field_pos;

	if (str_sz <= 0 || !run || !cli) {
		log_e("Invalid argument\n");
		return -1;
	}

	if ((cmd = cli_lut_find(cli, str))) {
		log_e("already existed cmd %s\n", str);
		return -1;
	}

	if ((cmd = (cli_cmd_t*)aloe_calloc(1, cmd_sz)) == NULL) {
		log_e("alloc cmd %s\n", str);
		return -1;
	}

	cmd->str = field_pos = (char*)(cmd + 1);
	memcpy(field_pos, str, str_sz);
	field_pos[str_sz] = '\0';

	if (detail) {
		cmd->detail = (field_pos += str_sz + 1);
		memcpy(field_pos, detail, detail_sz);
		field_pos[detail_sz] = '\0';
	}

	cmd->run = run;
	cmd->cbarg = cbarg;
	cli_cmdq_add(&cli->cmdq, cmd);
	return 0;
}
