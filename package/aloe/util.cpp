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
#include "log.h"

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
