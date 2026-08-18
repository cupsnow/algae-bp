/* $Id$
 *
 * Copyright 2024, Dexatek Technology Ltd.
 * This is proprietary information of Dexatek Technology Ltd.
 * All Rights Reserved. Reproduction of this documentation or the
 * accompanying programs in any manner whatsoever without the written
 * permission of Dexatek Technology Ltd. is strictly forbidden.
 *
 * @author joelai
 *
 * @file /air192/package/tester1/priv.h
 * @brief priv
 */

#ifndef PRIV_H_
#define PRIV_H_

#define TEST_AIR192 1

#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <sys/times.h>
#include <syslog.h>
#include <sys/random.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
#include <pthread.h>
#include <getopt.h>
#include <math.h>

#if defined(TEST_AIR192) && TEST_AIR192
//#include <admin/unitest.h>
#include <admin/air192.h>
#include <admin/sa7715.h>
#endif

#include <cjson/cJSON.h>

extern "C" {

#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>

}

#if WITH_OPENSSL
#  include <openssl/sha.h>
#  include <openssl/crypto.h>
#  include <openssl/err.h>
#  include <openssl/evp.h>
#endif

#include <admin/nl.h>

#define log_m(_lvl, _fmt, _args...) aloe_log_printf((char*)_lvl, __func__, __LINE__, _fmt, ##_args)
#define log_e(_args...) log_m(aloe_log_level_err, ##_args)
#define log_i(_args...) log_m(aloe_log_level_info, ##_args)
#define log_d(_args...) log_m("Debug ", ##_args)
#define log_v(_args...) log_m("verbose ", ##_args)

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif


#endif /* PRIV_H_ */
