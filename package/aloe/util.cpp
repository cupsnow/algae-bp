/* $Id$
 *
 * Copyright 2025, joelai
 * This is proprietary information of joelai
 * All Rights Reserved. Reproduction of this documentation or the
 * accompanying programs in any manner whatsoever without the written
 * permission of joelai is strictly forbidden.
 *
 * @author joelai
 *
 * @file /algae-bp/package/aloe/util.cpp
 * @brief util
 */

#include <aloe/sys.h>
#include <stdint.h>
#include <arpa/inet.h>
#include "log.h"

extern "C" {
const char *aloe_hex_chars = aloe_stringify(ALOE_HEX_CHARS);
}

extern "C"
const char* aloe_version(int *ver, size_t cnt) {
	static char ver_str[] = "aloe "
			"v" aloe_stringify(ALOE_VERSION_MAJOR)
			"." aloe_stringify(ALOE_VERSION_MINOR)
			"." aloe_stringify(ALOE_VERSION_BUILD);
	if (ver) {
		if (cnt > 0) ver[0] = ALOE_VERSION_MAJOR;
		if (cnt > 1) ver[1] = ALOE_VERSION_MINOR;
		if (cnt > 2) ver[2] = ALOE_VERSION_BUILD;
		if (cnt > 3) memset(&ver[3], 0, (cnt - 3) * sizeof(ver[0]));
	}
	return ver_str;
}

extern "C"
aloe_buf_t* aloe_buf_clear(aloe_buf_t *buf) {
	_aloe_buf_clear(buf);
	return buf;
}

extern "C"
aloe_buf_t* aloe_buf_flip(aloe_buf_t *buf) {
	_aloe_buf_flip(buf);
	return buf;
}

extern "C"
aloe_buf_t* aloe_buf_rewind(aloe_buf_t *buf) {
	size_t sz;

	if (buf->pos <= 0) return buf;
	if (buf->pos < buf->lmt) {
		sz = buf->lmt - buf->pos;
		if (sz > 0) memmove(buf->data, (char*)buf->data + buf->pos, sz);
	} else {
		if (buf->lmt > buf->cap || buf->pos > buf->lmt) {
			aloe_log_e("Sanity check invalid %lu <= %lu <= %lu\n",
					(unsigned long)buf->pos, (unsigned long)buf->lmt,
					(unsigned long)buf->cap);
		}
		sz = 0;
	}
	buf->pos = 0;
	buf->lmt = sz;
	return buf;
}

extern "C"
int aloe_buf_expand(aloe_buf_t *buf, size_t cap, int retain) {
	void *data;

	if (buf->cap >= cap) return 0;
	if (!(data = malloc(cap))) return ENOMEM;
	if (buf->data) {
		if (buf->pos > 0) {
			if (retain == ALOE_BUF_FLAG_RETAIN_NORMAL
					|| retain > 0) {
				memcpy(data, buf->data, buf->pos);
			}
		}
		free(buf->data);
	}
	if (buf->lmt == buf->cap) buf->lmt = cap;
	buf->data = data;
	buf->cap = cap;
	return 0;
}

static const char _aloe_str_sep[] = " \r\n\t";

extern "C" {
const char *aloe_str_sep = _aloe_str_sep;
}

extern "C"
int aloe_cli_tok(char *cli, int *argc, const char **argv, const char *sep) {
	int argmax = *argc;

	if (!sep) sep = aloe_str_sep;
	_aloe_cli_tok(cli, *argc, argv, sep, argmax);
	return 0;
}

extern "C"
double aloe_avg_calc_weight_remain(double *weight, size_t weight_cnt,
		size_t cap) {
	double weight_remain = 0.0, sum;
	int pos;

	if (!weight || weight_cnt <= 0) {
		weight_remain = 1.0 / cap;
		return weight_remain;
	}

	if (weight_cnt >= cap) return 0.0;

	sum = 0.0;
	for (pos = 0; pos < weight_cnt; pos++) {
		sum += weight[pos];
	}
	if (sum >= 1.0) {
		aloe_log_e("Sanity check invalid weight\n");
		return 0.0;
	}
	weight_remain = (1.0 - sum) / (cap - weight_cnt);
	return weight_remain;
}

extern "C"
double aloe_avg_calc_f(aloe_buf_t *rec, double val,
		double *weight, size_t weight_cnt, double weight_remain) {
	typedef double aloe_avg_calc_val_t;
	aloe_avg_calc_val_t *vl = (aloe_avg_calc_val_t*)rec->data;
	int pos, lmt;
	double sum = 0.0;

	// set to tail
	pos = (rec->pos + rec->lmt) % rec->cap;
	vl[pos] = val;
	if (rec->lmt < rec->cap) {
		// append
		rec->lmt++;
	} else if (rec->pos == rec->cap - 1) {
		// overwrite and wrap
		rec->pos = 0;
	} else {
		// overwrite
		rec->pos++;
	}

	sum = 0.0;

	// average
	if (!weight || weight_cnt <= 0) {
		// from tail to head
		for (lmt = 0; lmt < rec->lmt; lmt++) {
			sum += (double)vl[pos];

			if (pos <= 0) {
				pos = rec->cap - 1;
			} else {
				pos--;
			}
		}
		sum = sum / rec->lmt;
		return sum;
	}

	// weighted
	for (lmt = 0; lmt < rec->lmt; lmt++) {
		double weight_apply = lmt < weight_cnt ? weight[lmt] : weight_remain;

		if (lmt < weight_cnt) {
			sum += (double)vl[pos] * weight[lmt];
		} else if (weight_remain != 0.0) {
			sum += (double)vl[pos] * weight_remain;
		} else {
			// remain weighted 0
			break;
		}

		if (pos <= 0) {
			pos = rec->cap - 1;
		} else {
			pos--;
		}
	}
	return sum;
}

extern "C"
int aloe_hexstr(void *buf, size_t buf_len, const void *data, size_t sz,
		const char *sep, size_t sep_len) {
	char *ox = (char*)buf;
	const uint8_t *p = (uint8_t*)data;
	int pos = 0, i;

	if (!sep) sep_len = 0;

	if (!ox || buf_len == 0) goto finally;
	for (i = 0; i < sz; i++) {
		if (pos + 2 >= buf_len) break;

		ox[pos++] = aloe_hex_chars[(p[i] >> 4) & 0x0F];
		ox[pos++] = aloe_hex_chars[p[i] & 0x0F];
		if (sep_len > 0 && i + 1 < sz) {
			if (pos + sep_len >= buf_len) break;
			memcpy(ox + pos, sep, sep_len);
			pos += sep_len;
		}
	}
finally:
	if (pos < buf_len) ox[pos] = '\0';
	return pos;
}

extern "C"
void aloe_hexdump(const void *data, size_t sz, const char *fmt, ...) {
#define ALOE_HEXDUMP_ASCII
	va_list args;
	char line_buffer[7 + 3 * 16 + 1]; // 0000| 00 11 22 33 44 55 66 77 88 99 aa bb cc dd ee ff\0
#ifdef ALOE_HEXDUMP_ASCII
	char ascii_buffer[16 + 1]; // 0123456789abcdef
#endif
	int i;

	if (fmt) {
		va_start(args, fmt);
		vprintf(fmt, args);
		va_end(args);
	}

#if 1
    printf("    | 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f\n"
           "----+------------------------------------------------\n");
#endif

	for (i = 0; i < sz; i+=16) {
		int j, k = 0;

		k += snprintf(line_buffer + k, sizeof(line_buffer) - k, "%04x|", i);
		for (j = 0; j < 16; j++) {
			int c;
			if (i + j >= sz) break;
			c = ((char*)data)[i + j];
			k += snprintf(line_buffer + k, sizeof(line_buffer) - k, " %02x", c);
#ifdef ALOE_HEXDUMP_ASCII
			ascii_buffer[j] = isprint(c) ? c : '.';
#endif
		}
		line_buffer[k] = '\0';
#ifdef ALOE_HEXDUMP_ASCII
		ascii_buffer[j] = '\0';
#endif
#ifdef ALOE_HEXDUMP_ASCII
		printf("%s | %s\n", line_buffer, ascii_buffer);
#else
		printf("%s\n", line_buffer);
#endif
	}
}

extern "C"
int aloe_ip_str(char *str, size_t str_sz, struct sockaddr *sa, unsigned flag) {
#define aloe_ip_str_addr 1
#define aloe_ip_str_port 2
	int ret = -1, pos = 0, r;

	if (flag == 0) flag == 1;

	if (sa->sa_family == AF_INET) {
		if (flag & aloe_ip_str_addr) {
			if (!inet_ntop(AF_INET, &((struct sockaddr_in*)sa)->sin_addr,
					(char*)str + pos, (socklen_t)str_sz - pos - 1)) {
				goto finally;
			}
			pos += strlen((char*)str + pos);
		}
		if (flag & aloe_ip_str_port) {
			if (pos >= str_sz || (r = snprintf((char*)str + pos, str_sz - pos,
					":%d", ntohs(((struct sockaddr_in*)sa)->sin_port))) <= 0
					|| pos + r >= str_sz) {
				goto finally;
			}
			pos += r;
		}
		ret = 0;
		goto finally;
	}

	if (sa->sa_family == AF_INET6) {
		if (flag & aloe_ip_str_addr) {
			if (!inet_ntop(AF_INET6, &((struct sockaddr_in6*)sa)->sin6_addr,
					(char*)str + pos, (socklen_t)str_sz - pos - 1)) {
				goto finally;
			}
			pos += strlen((char*)str + pos);
		}
		if (flag & aloe_ip_str_port) {
			if (pos >= str_sz || (r = snprintf((char*)str + pos, str_sz - pos,
					":%d", ntohs(((struct sockaddr_in6*)sa)->sin6_port))) <= 0) {
				goto finally;
			}
			pos += r;
		}
		ret = 0;
		goto finally;
	}
finally:
	if (ret != 0) pos = 0;
	str[pos] = '\0';
	return pos;
#undef aloe_ip_str_addr
#undef aloe_ip_str_port
}

extern "C"
int aloe_buf_vzprintf(aloe_buf_t *fb, const char *fmt, va_list va) {
	int r, ch;

	if (fb->pos >= fb->lmt || !fmt) {
		r = 0;
		goto finally;
	}
	if ((r = vsnprintf((char*)fb->data + fb->pos, fb->lmt - fb->pos,
			fmt, va)) < 0 || fb->pos + r >= fb->lmt) {
		// likely insufficient buffer
		r = 0;
		goto finally;
	}
	fb->pos += r;
finally:
	if (fb->pos < fb->lmt) ((char*)fb->data)[fb->pos] = '\0';
	return r;
}

extern "C"
int aloe_buf_zprintf(aloe_buf_t *fb, const char *fmt, ...) {
	int r;
	va_list va;

	va_start(va, fmt);
	r = aloe_buf_vzprintf(fb, fmt, va);
	va_end(va);
	return r;
}

extern "C"
int aloe_bio_read(int fd, void *buf, size_t buf_sz) {
	int r;
	size_t pos;

	pos = 0;
	while (pos < buf_sz) {
		r = read(fd, (char*)buf + pos, buf_sz - pos);
		if (r < 0) {
			r = errno;
			if (r == EINTR) continue;
			aloe_log_e("Failed read, %s\n", strerror(r));
			break;
		}
		if (r == 0) {
//			log_e("Failed read, might closed\n");
			break;
		}
		pos += r;
	}
	if (pos < buf_sz) ((char*)buf)[pos] = '\0';
	return (int)pos;
}

extern "C"
int aloe_bio_write(int fd, const void *data, size_t data_sz) {
	int r;
	size_t pos;

	pos = 0;
	while (pos < data_sz) {
		r = write(fd, (char*)data + pos, data_sz - pos);
		if (r < 0) {
			r = errno;
			if (r == EINTR) continue;
			aloe_log_e("Failed write, %s\n", strerror(r));
			break;
		}
		if (r == 0) {
			aloe_log_e("Failed write, might closed\n");
			break;
		}
		pos += r;
	}
	return (int)pos;
}

extern "C"
int aloe_bio_read_fn(const char *fn, void *buf, size_t buf_sz) {
	int fd = -1, ret = -1, r;

	if ((fd = open(fn, O_RDONLY, 0666)) == -1) {
		r = errno;
		aloe_log_e("Failed open %s; %s\n", fn, strerror(r));
		goto finally;
	}
	ret = aloe_bio_read(fd, buf, buf_sz);
finally:
	if (fd != -1) close(fd);
	return ret;
}

extern "C"
int aloe_bio_write_fn(const char *fn, const void *data, size_t data_sz,
		int mode) {
	int fd = -1, ret = -1, r;

	if (mode == 0) mode = O_WRONLY | O_TRUNC | O_CREAT;
	if ((fd = open(fn, mode, 0666)) == -1) {
		r = errno;
		aloe_log_e("Failed open %s; %s\n", fn, strerror(r));
		goto finally;
	}
	ret = aloe_bio_write(fd, data, data_sz);
finally:
	if (fd != -1) close(fd);
	return ret;
}

extern "C"
uint32_t aloe_crc32(const void *data, size_t len, uint32_t cksum) {
	const uint8_t *p = (const uint8_t*)data;

	cksum = ~cksum;
	while (len--) {
		cksum ^= *p++;
		for (int i = 0; i < 8; i++) {
			if (cksum & 1) {
				cksum = (cksum >> 1) ^ 0xEDB88320;
			} else {
				cksum >>= 1;
			}
		}
	}
	return ~cksum;
}
