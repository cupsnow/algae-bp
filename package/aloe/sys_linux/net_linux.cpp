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
 * @file /algae-bp/package/aloe/sys_linux/net_linux.cpp
 * @brief net_linux
 */

#include <aloe/sys.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <linux/wireless.h>

#define log_m(_lvl, _msg, _args...) do { \
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
#define log_d(...) log_m("Debug", __VA_ARGS__)
#define log_e(...) log_m("ERROR", __VA_ARGS__)

int aloe_ifflag_str(char *str, size_t str_sz, unsigned iflag, const char *sep) {
	struct {
		const char *name;
		unsigned flag;
		const char *desc;
	} lut[] = {
		{"UP", IFF_UP, "Interface is running."},
		{"BROADCAST", IFF_BROADCAST, "Valid broadcast address set."},
		{"DEBUG", IFF_DEBUG, "Internal debugging flag."},
		{"LOOPBACK", IFF_LOOPBACK, "Interface is a loopback interface."},
		{"POINTOPOINT", IFF_POINTOPOINT, "Interface is a point-to-point link."},
		{"RUNNING", IFF_RUNNING, "Resources allocated."},
		{"NOARP", IFF_NOARP, "No arp protocol, L2 destination address not set."},
		{"PROMISC", IFF_PROMISC, "Interface is in promiscuous mode."},
		{"NOTRAILERS", IFF_NOTRAILERS, "Avoid use of trailers."},
		{"ALLMULTI", IFF_ALLMULTI, "Receive all multicast packets."},
		{"MASTER", IFF_MASTER, "Master of a load balancing bundle."},
		{"SLAVE", IFF_SLAVE, "Slave of a load balancing bundle."},
		{"MULTICAST", IFF_MULTICAST, "Supports multicast"},
		{"PORTSEL", IFF_PORTSEL, "Is able to select media type via ifmap."},
		{"AUTOMEDIA", IFF_AUTOMEDIA, "Auto media selection active."},
		{"DYNAMIC", IFF_DYNAMIC, "The addresses are lost when the interface goes down."},
#if defined(IFF_LOWER_UP)
		{"LOWER_UP", IFF_LOWER_UP, "Driver signals L1 up (since Linux 2.6.17)"},
#endif
#if defined(IFF_DORMANT)
		{"DORMANT", IFF_DORMANT, "Driver signals dormant (since Linux 2.6.17)"},
#endif
#if defined(IFF_ECHO)
		{"ECHO", IFF_ECHO, "Echo sent packets (since Linux 2.6.25)"},
#endif
		{NULL}
	}, *lut_iter;
	int pos = 0, r;
	unsigned unknown_flag = iflag;

	if (!sep) sep = ", ";
	for (lut_iter = lut; lut_iter->name; lut_iter++) {
		if (!(iflag & lut_iter->flag)) continue;
		if (pos >= str_sz || (r = snprintf(str + pos, str_sz - pos,
				"%s%s", (pos > 0 ? sep : ""), lut_iter->name)) <= 0
				|| (r + pos) >= str_sz) {
			goto finally;
		}
		pos += r;
		unknown_flag &= ~lut_iter->flag;
	}

	if (unknown_flag && pos < str_sz && (r = snprintf(str + pos, str_sz - pos,
			"%s0x%x", (pos > 0 ? sep : ""), unknown_flag)) > 0
			&& (r + pos) < str_sz) {
		pos += r;
	}
finally:
	if (pos >= str_sz) pos = str_sz - 1;
	str[pos] = '\0';
	return pos;
}

int aloe_wext_info(const char *ifce, char *wext, size_t wext_len) {
	struct iwreq iwreq1;
	int fd = -1, r;

	memset(&iwreq1, 0, sizeof(iwreq1));

	strncpy(iwreq1.ifr_name, ifce, IFNAMSIZ);
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		r = errno;
		log_e("create socket %s\n", strerror(r));
		goto finally;
	}

	if (ioctl(fd, SIOCGIWNAME, &iwreq1) == -1) {
		r = errno;
		log_e("SIOCGIWNAME %s\n", strerror(r));
		goto finally;
	}

	if (wext && wext_len > 0) {
		r = aloe_min(IFNAMSIZ, wext_len);
		strncpy(wext, iwreq1.u.name, r);
		if (r > 0) wext[r - 1] = '\0';
	}
	r = 0;
finally:
	if (fd != -1) close(fd);
	return r;
}

