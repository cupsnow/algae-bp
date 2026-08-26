/* $Id$
 *
 * This file is part of the project algae-bp
 *
 * Aug 24, 2026
 *
 * @author joelai
 *
 * @file /algae-bp/package/aloe/sys_linux/mini_udev.c
 * @brief mini_udev
 */

#include <aloe/sys.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <asm/types.h>
#include <sys/socket.h>
#include <linux/netlink.h>

#include <aloe/udev_mini.h>
#include <aloe/usbhid_mini.h>

#  define log_m(_lvl, _msg, _args...) do { \
	struct timespec ts; \
	struct tm tm; \
	clock_gettime(CLOCK_REALTIME, &ts); \
	localtime_r(&ts.tv_sec, &tm); \
	fprintf(stdout, "[%02ld:%02ld:%02ld.%06ld][%s][%s][#%d]" _msg, \
			(long)tm.tm_hour, (long)tm.tm_min, (long)tm.tm_sec, \
			(long)ts.tv_nsec / 1000, \
			_lvl, __func__, __LINE__, ##_args); \
	fflush(stdout); \
} while(0)
#  define log_d(...) log_m("Debug", __VA_ARGS__)
#  define log_e(...) log_m("ERROR", __VA_ARGS__)

int mini_udev_uevent_open(void) {
	int fd = -1, r, ret = -1;
	struct sockaddr_nl addr;

	if ((fd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT)) == -1) {
		r = errno;
		log_e("Create socket NETLINK_KOBJECT_UEVENT; %s\n", strerror(r));
		goto finally;
	}

#if 1
	r = 4096;
	setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &r, sizeof(r));
#endif

	memset(&addr, 0, sizeof(addr));
	addr.nl_family = AF_NETLINK;
	addr.nl_pid = getpid();
	addr.nl_groups = 1;

	if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		r = errno;
		log_e("Failed bind netlink; %s\n", strerror(r));
		goto finally;
	}
	ret = 0;
finally:
	if (ret != 0) {
		if (fd != -1) close(fd);
	}
	return fd;
}

void mini_udev_uevent_close(int fd) {
	if (fd < 0) return;
	close(fd);
}

/*
 * Check whether the uevent contains a particular key.
 *
 * uevent is a sequence of NUL terminated strings:
 *
 *   ACTION=add
 *   DEVPATH=/devices/...
 *   SUBSYSTEM=hidraw
 *   DEVNAME=hidraw0
 *   ...
 */
const char* mini_udev_uevent_check(const char *buf, ssize_t len,
		const char *key) {
	size_t key_len = strlen(key);
	const char *p = buf;
	const char *end = buf + len;

	while (p < end && *p) {
		if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
			return p + key_len + 1;
		}
		p += strlen(p) + 1;
	}
	return NULL;
}

/*
 * Check for USB/HID related uevent.
 */
static int mini_udev_uevent_is_hidraw(const char *buf, ssize_t len) {
	const char *subsystem;
	const char *key = "hidraw";
	size_t key_len = strlen(key);

	if ((subsystem = mini_udev_uevent_check(buf, len, "SUBSYSTEM"))
			&& strncmp(subsystem, key, key_len) == 0) {
		return 1;
	}
	return 0;
}

int mini_udev_test(uint16_t target_vid, uint16_t target_pid) {
    int uevent_fd;

    uevent_fd = mini_udev_uevent_open();

    if (uevent_fd < 0)
        return 1;

    printf("Monitoring HID devices...\n");
    printf("Target VID=%04X PID=%04X\n",
           target_vid,
           target_pid);

    /*
     * First check whether the device is already connected.
     */
    char path[256];

	int hid_fd = mini_hid_open_hidraw(NULL, target_vid, target_pid, path,
			sizeof(path));

    if (hid_fd >= 0) {

        printf("Device already connected: %s\n",
               path);

        /*
         * hid_fd can now be used for HID communication.
         */

        close(hid_fd);
    }

    for (;;) {

        char buf[4096];

        ssize_t len;

        len = recv(uevent_fd,
                   buf,
                   sizeof(buf) - 1,
                   0);

        if (len < 0) {

            if (errno == EINTR)
                continue;

            perror("recv");
            break;
        }

        buf[len] = '\0';

        if (!mini_udev_uevent_is_hidraw(buf, len))
            continue;

        const char *action;
        const char *devname;

        action  = mini_udev_uevent_check(buf, len, "ACTION");
        devname = mini_udev_uevent_check(buf, len, "DEVNAME");

        if (!action)
            continue;

        printf("\nHIDRAW event: %s", action);

        if (devname)
            printf(" %s", devname);

        printf("\n");

        /*
         * Device insertion.
         */
        if (strcmp(action, "add") == 0) {

            /*
             * hidraw device node may not exist yet.
             *
             * Normally a very short delay is enough.
             */
            usleep(100 * 1000);

			hid_fd = mini_hid_open_hidraw(NULL, target_vid, target_pid, path,
					sizeof(path));

            if (hid_fd >= 0) {

                printf(">>> Target HID connected: %s\n",
                       path);

                /*
                 * Use hid_fd here.
                 *
                 * Example:
                 *
                 * mini_hid_get_feature(...)
                 * read(hid_fd, ...)
                 * write(hid_fd, ...)
                 */

                close(hid_fd);

            } else {

                printf("No matching HID device\n");
            }
        }

        /*
         * Device removal.
         */
        else if (strcmp(action, "remove") == 0) {

            /*
             * If you keep an open fd for the device,
             * your application should mark that device
             * as disconnected here.
             */
            printf(">>> HID device removed\n");
        }
    }

    close(uevent_fd);

    return 0;
}

