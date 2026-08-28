/* $Id$
 *
 * This file is part of the project algae-bp
 *
 * Aug 25, 2026
 *
 * @author joelai
 *
 * @file /algae-bp/package/tester1/hid.cpp
 * @brief hid
 */

#include <sys/socket.h>

#include <aloe/usbhid_mini.h>
#include <aloe/udev_mini.h>

#include "usb.h"
#include "priv.h"

#define UDEV_KEY_SUBSYSTEM "SUBSYSTEM"
#define UDEV_KEY_ACTION "ACTION"
#define UDEV_KEY_DEVNAME "DEVNAME"
#define UDEV_KEY_SUBSYSTEM_HIDRAW "hidraw"

typedef struct {
	evconn_t evconn;
	char buf[16 * 1024];
} usb2_t;

extern "C" {
void *usb_global;
}

static void usb2_poll_cb(int fd, unsigned ev, void *cbarg) {
	int r;
	usb2_t *usb2 = (usb2_t*)cbarg;
	aloe_buf_t fb = {.data = usb2->buf,
			.cap = sizeof(usb2->buf),
	};

	if (usb2->evconn.fd != fd) {
		log_e("sanity check mismatch fd\n");
		return;
	}

	if (!(ev & ALOE_EVB2_FLAG_READ)) return;

	for (;;) {
		ssize_t len;
		const char *udev_subsystem, *udev_action, *udev_devname;

		len = recv(fd, fb.data, fb.cap - 1, MSG_DONTWAIT);
		if (len < 0) {
			r = errno;
			if (r == EINTR) continue;
			if (r == EAGAIN
#ifdef EWOULDBLOCK
					|| r == EWOULDBLOCK
#endif
					) {
				break;
			}
			log_e("failed recv; %s\n", strerror(r));
			break;
		}
		if (len == 0) break;

		((char*)fb.data)[fb.pos = len] = '\0';

#if 0
		log_d("%s\n", (char*)fb.data);
#elif 1
		{
			const char *p = (char*)fb.data;
			const char *end = p + fb.pos;

			if (p < end && *p) {
				log_d("%s\n", p);
				p += strlen(p) + 1;
			}

			while (p < end && *p) {
				log_d("  %s\n", p);
				p += strlen(p) + 1;
			}
		}
#endif

		udev_subsystem = mini_udev_uevent_check((char*)fb.data, fb.pos, UDEV_KEY_SUBSYSTEM);
		udev_action = mini_udev_uevent_check((char*)fb.data, fb.pos, UDEV_KEY_ACTION);
		udev_devname = mini_udev_uevent_check((char*)fb.data, fb.pos, UDEV_KEY_DEVNAME);

		log_d("%s=%s; %s=%s; %s=%s\n",
				UDEV_KEY_SUBSYSTEM, udev_subsystem ? udev_subsystem : "<Null>",
				UDEV_KEY_ACTION, udev_action ? udev_action : "<Null>",
				UDEV_KEY_DEVNAME, udev_devname ? udev_devname : "<Null>");
	}
}

extern "C"
void* usb_init(void *evctx) {
	usb2_t *usb2 = NULL;
	int ret = -1;

	if ((usb2 = (usb2_t*)aloe_calloc(1, sizeof(*usb2))) == NULL) {
		log_e("failed alloc usb2\n");
		goto finally;
	}
	usb2->evconn.fd = -1;

	if ((usb2->evconn.fd = mini_udev_uevent_open()) == -1) {
		log_e("failed open uevent\n");
		goto finally;
	}
	if (aloe_file_nonblock(usb2->evconn.fd, 1) != 0) {
		log_e("failure set nonblock\n");
		goto finally;
	}
	usb2->evconn.ev_ctx = evctx;
	if (evconn_add_read(&usb2->evconn, &usb2_poll_cb, usb2) == NULL) {
		log_e("Failure aloe_evb2_add_fd\n");
		goto finally;
	}
	log_d("usb2 initialized\n");
	ret = 0;
finally:
	if (ret != 0 && usb2) {
		evconn_cancel(&usb2->evconn);
		if (usb2->evconn.fd != -1) mini_udev_uevent_close(usb2->evconn.fd);
		aloe_free(usb2);
		usb2 = NULL;
	}
	return usb2;
}

extern "C"
void usb_destroy(void *_usb2) {
	usb2_t *usb2 = (usb2_t*)_usb2;

	if (!usb2) return;
	evconn_cancel(&usb2->evconn);
	if (usb2->evconn.fd != -1) close(usb2->evconn.fd);
	aloe_free(usb2);
}

int cli_cmd_usb(void*, int argc, const char **argv) {

}

