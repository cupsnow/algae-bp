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
#include "mgmt.h"

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

extern "C"
void* mgmt1_init(void *evctx, const char *path) {
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
	mgmt->evconn.ev_ctx = evctx;


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
			mgmt = NULL;
		}
	}
	return mgmt;
}
