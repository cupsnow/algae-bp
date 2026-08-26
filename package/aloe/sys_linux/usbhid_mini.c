/* $Id$
 *
 * Copyright 2026, Dexatek Technology Ltd.
 * This is proprietary information of Dexatek Technology Ltd.
 * All Rights Reserved. Reproduction of this documentation or the
 * accompanying programs in any manner whatsoever without the written
 * permission of Dexatek Technology Ltd. is strictly forbidden.
 *
 * @author joelai
 *
 * @file /esh-ws/package/esh-tester/usbhid_mini.c
 * @brief usbhid_mini
 */

#include <aloe/sys.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <linux/hidraw.h>

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

int mini_hid_open_hidraw(const char *devdir, uint16_t vid, uint16_t pid,
		char *found_path, size_t found_path_size) {
	int r;
	DIR *dir;
	struct dirent *ent;

	if (devdir == NULL) devdir = "/dev";

	dir = opendir(devdir);

	if (!dir) {
		perror("opendir");
		return -1;
	}

	while ((ent = readdir(dir)) != NULL) {
		char path[256];

		if (strncmp(ent->d_name, "hidraw", 6) != 0)
			continue;

		if ((r = snprintf(path, sizeof(path),
				"%s/%s", devdir, ent->d_name)) <= 0
				|| r >= sizeof(path)) {
			log_e("Insufficient buffer to compose path\n");
			continue;
		}

		int fd = open(path, O_RDWR | O_NONBLOCK);

		if (fd < 0)
			continue;

		struct hidraw_devinfo info;

		memset(&info, 0, sizeof(info));

		if (ioctl(fd, HIDIOCGRAWINFO, &info) < 0) {
			close(fd);
			continue;
		}

		printf("found %s: VID=%04X PID=%04X BUS=%u\n",
				path,
				info.vendor,
				info.product,
				info.bustype);

		if (info.vendor == vid &&
				info.product == pid) {

			if (found_path && found_path_size > 0) {
				snprintf(found_path,
						found_path_size,
						"%s",
						path);
			}

			closedir(dir);

			/*
			 * Put the device back into blocking mode.
			 */
			int flags = fcntl(fd, F_GETFL, 0);

			if (flags >= 0)
				fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

			return fd;
		}

		close(fd);
	}

	closedir(dir);

	return -1;
}

int mini_hid_open(const char *path) {
	int fd = -1;

	if (!path) return -1;
	if ((fd = open(path, O_RDWR)) < 0) return -1;
	return fd;
}

void mini_hid_close(int fd) {
	if (fd < 0) return;
	close(fd);
}

int mini_hid_write(int fd, const void *buf, size_t len) {
	if (fd < 0) return -1;
	return write(fd, buf, len);
}

int mini_hid_read(int fd, void *buf, size_t len, int timeout_ms) {
	struct pollfd pfd;
	int r;

	if (fd < 0) return -1;
	pfd.fd = fd;
	pfd.events = POLLIN;
	if ((r = poll(&pfd, 1, timeout_ms)) <= 0) return r;
	return read(fd, buf, len);
}

int mini_hid_get_info( int fd, struct mini_hid_info *info) {
	struct hidraw_devinfo raw;

	if (!info)
		return -1;

	if (ioctl(fd, HIDIOCGRAWINFO, &raw) < 0)
		return -1;

	info->bustype = raw.bustype;
	info->vendor = raw.vendor;
	info->product = raw.product;

	return 0;
}

int mini_hid_get_name( int fd, char *buf, size_t size) {
	if (!buf || size == 0)
		return -1;

	memset(buf, 0, size);

	return ioctl(fd, HIDIOCGRAWNAME(size), buf);
}

int mini_hid_get_phys( int fd, char *buf, size_t size) {
	if (!buf || size == 0)
		return -1;

	memset(buf, 0, size);

	return ioctl(fd, HIDIOCGRAWPHYS(size), buf);
}

int mini_hid_get_report_descriptor( int fd, uint8_t *buf, size_t bufsize) {
	int desc_size;

	if (!buf)
		return -1;

	if (ioctl(fd, HIDIOCGRDESCSIZE, &desc_size) < 0)
		return -1;

	if ((size_t)desc_size > bufsize)
		desc_size = bufsize;

	struct hidraw_report_descriptor rpt;

	memset(&rpt, 0, sizeof(rpt));

	rpt.size = desc_size;

	if (ioctl(fd, HIDIOCGRDESC, &rpt) < 0)
		return -1;

	memcpy(buf, rpt.value, rpt.size);

	return rpt.size;
}

int mini_hid_get_serial(int fd, char *buf, size_t size) {
#ifdef HIDIOCGRAWUNIQ
	if (!buf || size == 0)
		return -1;

	memset(buf, 0, size);

	return ioctl(fd, HIDIOCGRAWUNIQ(size), buf);
#else
	(void)fd;
	(void)buf;
	(void)size;
	return -1;
#endif
}

int mini_hid_get_feature( int fd, unsigned char *buf, size_t size) {
    if (!buf || size == 0)
        return -1;

    return ioctl(fd, HIDIOCGFEATURE(size), buf);
}

int mini_hid_set_feature( int fd, unsigned char *buf, size_t size) {
    if (!buf || size == 0)
        return -1;

    return ioctl(fd, HIDIOCSFEATURE(size), buf);
}

int mini_hid_get_input( int fd, unsigned char *buf, size_t size) {
#ifdef HIDIOCGINPUT

    if (!buf || size == 0)
        return -1;

    return ioctl(fd, HIDIOCGINPUT(size), buf);

#else

    (void)fd;
    (void)buf;
    (void)size;
    return -1;

#endif
}

int mini_hid_set_output( int fd, unsigned char *buf, size_t size) {
#ifdef HIDIOCSOUTPUT

    if (!buf || size == 0)
        return -1;

    return ioctl(fd, HIDIOCSOUTPUT(size), buf);

#else

    (void)fd;
    (void)buf;
    (void)size;
    return -1;

#endif
}


/** Decode HID short-item data as unsigned (little-endian). */
static unsigned hid_item_udata(const uint8_t *p, unsigned size) {
	unsigned v = 0;
	for (unsigned i = 0; i < size; ++i)
		v |= (unsigned)p[i] << (8 * i);
	return v;
}

/** Decode HID short-item data as signed (little-endian). */
static int hid_item_sdata(const uint8_t *p, unsigned size) {
	unsigned v = hid_item_udata(p, size);
	if (size == 1) return (int8_t)v;
	if (size == 2) return (int16_t)v;
	if (size == 4) return (int32_t)v;
	return (int)v;
}

/** Parse and print a HID report descriptor (short items). */
void mini_hid_parse_report_descriptor(const uint8_t *desc, int len,
		mini_hid_printf_t outf, void *outf_args) {
	unsigned report_size = 0, report_count = 0, report_id = 0;
	unsigned usage_page = 0;
	int logical_min = 0, logical_max = 0;
	int indent = 0;

#define call_outf(fmt, ...) if (outf) { \
	outf(outf_args, fmt, ##__VA_ARGS__); \
}

	call_outf("parsed report descriptor:\n");
	for (int i = 0; i < len; ) {
		uint8_t prefix = desc[i++];
		unsigned size = prefix & 0x03;
		unsigned type = (prefix >> 2) & 0x03;
		unsigned tag = (prefix >> 4) & 0x0f;

		if (size == 3) size = 4;
		if (i + (int)size > len) {
			call_outf("truncated item at offset %d\n", i - 1);
			break;
		}
		const uint8_t *data = &desc[i];
		unsigned udata = hid_item_udata(data, size);
		int sdata = hid_item_sdata(data, size);
		i += size;

		/* Long item: not expected for CDU; skip if present. */
		if (prefix == 0xfe) {
			call_outf("  LongItem (unsupported)\n");
			continue;
		}

		const char *pad = indent > 0 ? "  " : "";

		if (type == 0) { /* Main */
			switch (tag) {
			case 8: /* Input */
				call_outf("%sInput (0x%x): size=%u bits, count=%u, total=%u bits (%u bytes)%s\n",
						pad, udata, report_size, report_count,
						report_size * report_count,
						(report_size * report_count + 7) / 8,
						report_size > 32 ? "  << exceeds kernel 32-bit extract()" : "");
				break;
			case 9: /* Output */
				call_outf("%sOutput (0x%x): size=%u bits, count=%u, total=%u bits (%u bytes)%s\n",
						pad, udata, report_size, report_count,
						report_size * report_count,
						(report_size * report_count + 7) / 8,
						report_size > 32 ? "  << exceeds kernel 32-bit extract()" : "");
				break;
			case 10: /* Collection */
				call_outf("%sCollection (0x%x)\n", pad, udata);
				indent++;
				break;
			case 11: /* Feature */
				call_outf("%sFeature (0x%x): size=%u bits, count=%u\n",
						pad, udata, report_size, report_count);
				break;
			case 12: /* End Collection */
				if (indent > 0) indent--;
				call_outf("%sEnd Collection\n", indent > 0 ? "  " : "");
				break;
			default:
				call_outf("%sMain tag=%u data=0x%x\n", pad, tag, udata);
				break;
			}
		} else if (type == 1) { /* Global */
			switch (tag) {
			case 0:
				usage_page = udata;
				call_outf("%sUsage Page (0x%x)\n", pad, usage_page);
				break;
			case 1:
				logical_min = sdata;
				call_outf("%sLogical Minimum (%d)\n", pad, logical_min);
				break;
			case 2:
				logical_max = sdata;
				call_outf("%sLogical Maximum (%d)\n", pad, logical_max);
				break;
			case 7:
				report_size = udata;
				call_outf("%sReport Size (%u bits)%s\n", pad, report_size,
						report_size > 32 ? "  << > 32" : "");
				break;
			case 8:
				report_id = udata;
				call_outf("%sReport ID (%u)\n", pad, report_id);
				break;
			case 9:
				report_count = udata;
				call_outf("%sReport Count (%u)\n", pad, report_count);
				break;
			default:
				call_outf("%sGlobal tag=%u data=0x%x\n", pad, tag, udata);
				break;
			}
		} else if (type == 2) { /* Local */
			switch (tag) {
			case 0:
				call_outf("%sUsage (0x%x)\n", pad, udata);
				break;
			default:
				call_outf("%sLocal tag=%u data=0x%x\n", pad, tag, udata);
				break;
			}
		} else {
			call_outf("%sReserved type item 0x%02x\n", pad, prefix);
		}
	}
#undef call_outf
}
