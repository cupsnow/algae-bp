/* $Id$
 *
 * @author joelai
 *
 * @file priv.c
 * @brief noname
 */

#include "priv.h"

extern "C"
evconn_t* evconn_list_foreach(evconn_list_t *q, evconn_t *p) {
	if (!p) {
		p = TAILQ_FIRST(q);
	} else {
		p = TAILQ_NEXT(p, qent);
	}
	return p;
}

#define LOG_LEVEL_MASK  0x000000ff
#define LOG_ERROR       1
#define LOG_WARN        2
#define LOG_INFO        3
#define LOG_DEBUG       4

/* Future flag bits (OR into flag). */
#define LOG_TO_STDERR   (1 << 8)  /* also write to stderr */
#define LOG_NO_FILE     (1 << 9)  /* skip /tmp/app.log */

#define LOG_DEFAULT_PATH "app.log"
#define LOG_CONFIG_PATH "applog.conf"

/* Minimum level that gets written; adjustable later if needed. */
static int g_log_min_level = LOG_DEBUG;

/* Write one log record to fp: prefix + fmt + newline, then flush+fdatasync. */
static void log_m2_emit(FILE *fp, int do_sync, const char *tbuf, long ms,
		const char *fnm, int lno, const char *fmt, va_list ap) {

	if (!fp || !fmt) return;

	fprintf(fp, "[%s.%03ld][%s][# %d] ", tbuf, ms, fnm ? fnm : "?", lno);
	vfprintf(fp, fmt, ap);
	fputc('\n', fp);
	fflush(fp);
	if (do_sync) {
		int fd = fileno(fp);
		if (fd >= 0)
			fdatasync(fd); /* durability; fsync also OK but heavier */
	}
}

extern "C"
void log_m2(int flag, const char *fnm, int lno, const char *fmt, ...) {
	va_list ap;
	struct timeval tv;
	struct tm tm_now;
	char tbuf[32];
	int level;
	long ms;

	if (!fmt) return;
	level = flag & LOG_LEVEL_MASK;
	/* File logging is gated by presence of LOG_CONFIG_PATH. */
	if (access(LOG_CONFIG_PATH, F_OK) != 0) flag |= LOG_NO_FILE;
	if (level == 0) level = LOG_INFO;
	if (level > g_log_min_level) return;
	/* No sink left — skip timestamp / I/O. */
	if ((flag & LOG_NO_FILE) && !(flag & LOG_TO_STDERR)) return;

	if (gettimeofday(&tv, NULL) != 0) {
		tv.tv_sec = time(NULL);
		tv.tv_usec = 0;
	}
	localtime_r(&tv.tv_sec, &tm_now);
	strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tm_now);
	ms = tv.tv_usec / 1000;

	if (!(flag & LOG_NO_FILE)) {
		FILE *fp = fopen(LOG_DEFAULT_PATH, "a");
		if (fp) {
			va_start(ap, fmt);
			log_m2_emit(fp, 1, tbuf, ms, fnm, lno, fmt, ap);
			va_end(ap);
			fclose(fp);
		}
	}

	if (flag & LOG_TO_STDERR) {
		/* Do not fclose(stderr). No fdatasync on console/pipe. */
		va_start(ap, fmt);
		log_m2_emit(stderr, 0, tbuf, ms, fnm, lno, fmt, ap);
		va_end(ap);
	}
}
