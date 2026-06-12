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
#include <sys/wait.h>
#include <linux/if.h>
#include <linux/wireless.h>
#include <linux/rtnetlink.h>

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

typedef struct {
	const char *name;
	union {
		unsigned vu;
		void *vv;
	};
	const char *desc;
} strval_t;

#define STRVAL_ENT(_st, ...) { aloe_stringify(_st), {_st}, ##__VA_ARGS__}

static strval_t aloe_ifflag_lut[] = {
	STRVAL_ENT(IFF_UP, "Interface is running."),
	STRVAL_ENT(IFF_BROADCAST, "Valid broadcast address set."),
	STRVAL_ENT(IFF_DEBUG, "Internal debugging flag."),
	STRVAL_ENT(IFF_LOOPBACK, "Interface is a loopback interface."),
	STRVAL_ENT(IFF_POINTOPOINT, "Interface is a point-to-point link."),
	STRVAL_ENT(IFF_RUNNING, "Resources allocated."),
	STRVAL_ENT(IFF_NOARP, "No arp protocol, L2 destination address not set."),
	STRVAL_ENT(IFF_PROMISC, "Interface is in promiscuous mode."),
	STRVAL_ENT(IFF_NOTRAILERS, "Avoid use of trailers."),
	STRVAL_ENT(IFF_ALLMULTI, "Receive all multicast packets."),
	STRVAL_ENT(IFF_MASTER, "Master of a load balancing bundle."),
	STRVAL_ENT(IFF_SLAVE, "Slave of a load balancing bundle."),
	STRVAL_ENT(IFF_MULTICAST, "Supports multicast"),
	STRVAL_ENT(IFF_PORTSEL, "Is able to select media type via ifmap."),
	STRVAL_ENT(IFF_AUTOMEDIA, "Auto media selection active."),
	STRVAL_ENT(IFF_DYNAMIC, "The addresses are lost when the interface goes down."),
#if defined(IFF_LOWER_UP)
	STRVAL_ENT(IFF_LOWER_UP, "Driver signals L1 up (since Linux 2.6.17)"),
#endif
#if defined(IFF_DORMANT)
	STRVAL_ENT(IFF_DORMANT, "Driver signals dormant (since Linux 2.6.17)"),
#endif
#if defined(IFF_ECHO)
	STRVAL_ENT(IFF_ECHO, "Echo sent packets (since Linux 2.6.25)"),
#endif
	{NULL}
};

static strval_t aloe_rtscope_lut[] = {
	STRVAL_ENT(RT_SCOPE_UNIVERSE, "Globally reachable address"),
	STRVAL_ENT(RT_SCOPE_SITE, "Site-local"),
	STRVAL_ENT(RT_SCOPE_LINK, "Local link"),
	STRVAL_ENT(RT_SCOPE_HOST, "Local host"),
	STRVAL_ENT(RT_SCOPE_NOWHERE, "Not reachable"),
	{NULL}
};

extern "C"
int aloe_ifflag_str(char *str, size_t str_sz, unsigned iflag, const char *sep) {
	strval_t *lut_iter;
	int pos = 0, r;
	unsigned unknown_flag = iflag;

	if (!sep) sep = ", ";
	for (lut_iter = aloe_ifflag_lut; lut_iter->name; lut_iter++) {
		if (!(iflag & lut_iter->vu)) continue;
		if (pos >= str_sz || (r = snprintf(str + pos, str_sz - pos,
				"%s%s", (pos > 0 ? sep : ""), lut_iter->name)) <= 0
				|| (r + pos) >= str_sz) {
			goto finally;
		}
		pos += r;
		unknown_flag &= ~lut_iter->vu;
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

extern "C"
int aloe_rtscope_str(char *str, size_t str_sz, unsigned rtscope) {
	strval_t *lut_iter;
	int pos = 0, r;

	for (lut_iter = aloe_rtscope_lut; lut_iter->name; lut_iter++) {
		if (rtscope == lut_iter->vu) {
			if (pos >= str_sz || (r = snprintf(str + pos, str_sz - pos,
					"%s", lut_iter->name)) <= 0
					|| (r + pos) >= str_sz) {
				goto finally;
			}
			pos += r;
			break;
		}
	}
finally:
	if (pos >= str_sz) pos = str_sz - 1;
	str[pos] = '\0';
	return pos;
}

extern "C"
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

extern "C"
int aloe_ifflag(const char *iface, unsigned *iflag) {
	int ret = -1, s = -1, r;
	struct ifreq ifr = {};

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		log_e("failed open socket\n");
		goto finally;
	}

	strncpy(ifr.ifr_name, iface, IFNAMSIZ);

	if (ioctl(s, SIOCGIFFLAGS, &ifr) < 0) {
		r = errno;
		log_e("failed get ifflag: %s\n", strerror(r));
		goto finally;
	}
	if (iflag) *iflag = ifr.ifr_flags;
	ret = 0;
finally:
	if (s != -1) close(s);
	return ret;
}

extern "C"
int aloe_ifup(const char *iface, int up) {
	int ret = -1, s = -1, r;
	struct ifreq ifr = {};

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		log_e("failed open socket\n");
		goto finally;
	}

	strncpy(ifr.ifr_name, iface, IFNAMSIZ);

	if (ioctl(s, SIOCGIFFLAGS, &ifr) < 0) {
		r = errno;
		log_e("failed get ifflag: %s\n", strerror(r));
		goto finally;
	}
	if (up) {
		ifr.ifr_flags |= IFF_UP;
	} else {
		ifr.ifr_flags &= ~IFF_UP;
	}

	if ((r = ioctl(s, SIOCSIFFLAGS, &ifr)) < 0) {
		r = errno;
		log_e("failed set ifflag: %s\n", strerror(r));
		goto finally;
	}
	ret = 0;
finally:
	if (s != -1) close(s);
	return ret;
}

extern "C"
pid_t aloe_fork_execv(const char *prog, char *const argz[]) {
	pid_t pid = -1;

	pid = fork();
	if (pid < 0) {
		log_e("Failed fork\n");
		return pid;
	}
	if (pid > 0) return pid;

	execvp(prog, argz);
	_exit(127);
	log_e("Unreachable\n");
	return 0;
}

extern "C"
pid_t aloe_fork_exec(const char *prog, ...) {
	char *argz[20];
	const char *s;
	int argc = 0, argv_cnt;
	pid_t pid = -1;
	va_list va;

	argv_cnt = aloe_arraysize(argz);

	pid = fork();
	if (pid < 0) {
		log_e("Failed fork\n");
		return pid;
	}
	if (pid > 0) return pid;

	argc = 0;

	va_start(va, prog);
	s = va_arg(va, const char *);
	while (s && argc < argv_cnt) {
		argz[argc++] = (char*)s;
		s = va_arg(va, const char *);
	}
	va_end(va);
	if (argc >= argv_cnt) {
		log_e("insufficient argv\n");
		_exit(127);
	}
	argz[argc] = NULL;

	execvp(prog, argz);
	_exit(127);
	log_e("Unreachable\n");
	return 0;
}

extern "C"
int aloe_waitpid(pid_t pid) {
	int r, es;

	while ((r = waitpid(pid, &es, 0)) < 0) {
		r = errno;
		if (r == EINTR) continue;
		log_e("waitpid(%d) -> %s\n", (int)pid, strerror(r));
		return -1;
	}
	if (r == pid) {
		if (WIFEXITED(es)) {
			r = WEXITSTATUS(es);
			return r;
		}
		if (WIFSIGNALED(es)) {
			r = WTERMSIG(es);
			log_e("waitpid(%d) -> signaled %d\n", (int)pid, r);
			return -1;
		}
	}
	return -1;
}
