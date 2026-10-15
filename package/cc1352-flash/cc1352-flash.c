/* SPDX-License-Identifier: MIT
 * CC1352P7 ROM UART bootloader host. Protocol: TI SWRA466E / SWCU192.
 * Linux GPIO character-device ABI v1; no libgpiod dependency.
 */
#define _DEFAULT_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/gpio.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define FLASH_SIZE (704u * 1024u)
#define ACK 0xcc
#define NACK 0x33
static int uart = -1;
static struct termios saved;
static int have_saved;
static int timeout_ms = 10000;

static void fail(const char *s)
{
	fprintf(stderr, "%s: %s\n", s, strerror(errno));
	exit(EXIT_FAILURE);
}
static void cleanup(void)
{
	if (uart >= 0) {
		if (have_saved) tcsetattr(uart, TCSANOW, &saved);
		close(uart);
	}
}
static int64_t now_ms(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts)) fail("clock_gettime");
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static void transfer(void *buf, size_t len, int writing, int64_t deadline)
{
	uint8_t *p = buf;
	while (len) {
		int64_t left = deadline - now_ms();
		struct pollfd f = { .fd = uart, .events = writing ? POLLOUT : POLLIN };
		if (left <= 0) { errno = ETIMEDOUT; fail("UART transaction"); }
		int r = poll(&f, 1, (int)left);
		if (r < 0 && errno == EINTR) continue;
		if (r < 0) fail("poll");
		if (!r) { errno = ETIMEDOUT; fail("UART transaction"); }
		if (!(f.revents & f.events)) { errno = EIO; fail("UART disconnected"); }
		ssize_t n = writing ? write(uart, p, len) : read(uart, p, len);
		if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
		if (n <= 0) { if (!n) errno = EIO; fail("UART I/O"); }
		p += n;
		len -= (size_t)n;
	}
}
static uint8_t nonzero(int64_t deadline)
{
	uint8_t b;
	do { transfer(&b, 1, 0, deadline); } while (!b);
	return b;
}
static void expect_ack(int64_t deadline)
{
	uint8_t b = nonzero(deadline);
	if (b != ACK) {
		fprintf(stderr, "Expected ACK, received 0x%02x%s\n", b,
			b == NACK ? " (NACK)" : "");
		exit(EXIT_FAILURE);
	}
}
static void command(uint8_t cmd, const uint8_t *data, size_t n)
{
	uint8_t packet[255] = { (uint8_t)(n + 3), cmd, cmd };
	if (n > 252) { errno = EINVAL; fail("packet length"); }
	for (size_t i = 0; i < n; ++i) {
		packet[i + 3] = data[i];
		packet[1] += data[i];
	}
	int64_t deadline = now_ms() + timeout_ms;
	transfer(packet, n + 3, 1, deadline);
	expect_ack(deadline);
}
static void response(uint8_t *data, size_t n)
{
	int64_t deadline = now_ms() + timeout_ms;
	uint8_t len = nonzero(deadline), sum, expected = 0, ack = ACK;
	if (len != n + 2) {
		fprintf(stderr, "Unexpected response length %u (expected %zu)\n", len, n + 2);
		exit(EXIT_FAILURE);
	}
	transfer(&sum, 1, 0, deadline);
	transfer(data, n, 0, deadline);
	for (size_t i = 0; i < n; ++i) expected += data[i];
	if (expected != sum) ack = NACK;
	transfer(&ack, 1, 1, deadline);
	if (ack == NACK) { fprintf(stderr, "Response checksum mismatch\n"); exit(EXIT_FAILURE); }
}
static void status(void)
{
	uint8_t s;
	command(0x23, NULL, 0);
	response(&s, 1);
	if (s != 0x40) {
		fprintf(stderr, "Bootloader status 0x%02x (41 unknown command, 42 invalid command, "
			"43 invalid address, 44 flash failure)\n", s);
		exit(EXIT_FAILURE);
	}
}
static void be32(uint8_t *p, uint32_t v)
{
	p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}
static uint32_t crc32(const uint8_t *p, size_t n)
{
	uint32_t crc = UINT32_MAX;
	while (n--) {
		crc ^= *p++;
		for (int i = 0; i < 8; ++i)
			crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
	}
	return crc ^ UINT32_MAX;
}
/* Find named GPIOs rather than depending on gpiochip numbering. */
static int request_gpio(const char *name, int value)
{
	glob_t paths = {0};
	if (glob("/dev/gpiochip*", 0, NULL, &paths)) {
		fprintf(stderr, "No GPIO chips found\n"); exit(EXIT_FAILURE);
	}
	for (size_t i = 0; i < paths.gl_pathc; ++i) {
		int fd = open(paths.gl_pathv[i], O_RDONLY | O_CLOEXEC);
		if (fd < 0) continue;
		struct gpiochip_info chip;
		if (ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &chip)) { close(fd); continue; }
		for (unsigned j = 0; j < chip.lines; ++j) {
			struct gpioline_info line = { .line_offset = j };
			if (ioctl(fd, GPIO_GET_LINEINFO_IOCTL, &line) || strcmp(line.name, name)) continue;
			struct gpiohandle_request req = {
				.lineoffsets = {j}, .flags = GPIOHANDLE_REQUEST_OUTPUT,
				.default_values = {(uint8_t)value}, .lines = 1,
				.consumer_label = "cc1352-flash"
			};
			if (ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req)) fail(name);
			printf("%s: %s offset %u\n", name, paths.gl_pathv[i], j);
			close(fd); globfree(&paths); return req.fd;
		}
		close(fd);
	}
	globfree(&paths);
	fprintf(stderr, "GPIO %s not accessible; check permissions, DT line names, and GPIO ABI v1 support\n", name);
	exit(EXIT_FAILURE);
}
static void gpio_set(int fd, int value)
{
	struct gpiohandle_data d = { .values = {(uint8_t)value} };
	if (ioctl(fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &d)) fail("GPIO set");
}
static void bootloader(void)
{
	int boot = request_gpio("CC1352P7_BOOT", 0);
	int reset = request_gpio("CC1352P7_RSTN", 0);
	usleep(200000);
	gpio_set(reset, 1);
	usleep(200000);
	gpio_set(boot, 1);
	usleep(200000);
	close(reset); close(boot);
}
static void usage(const char *name)
{
	printf("Usage: %s [-p UART] [--manual] [--verify-only] firmware.bin\n"
	       "Default UART: /dev/play/cc1352/uart, 115200 8N1\n"
	       "Default action: enter bootloader, mass erase, program, CRC verify, reset.\n"
	       "--manual: skip GPIO control; target must already be in ROM bootloader.\n"
	       "--verify-only: no erase/write; compare full-flash CRC and reset on success.\n"
	       "Raw binary at address zero, 1..720896 bytes; pad with FF to full flash.\n", name);
}
int main(int argc, char **argv)
{
	const char *port = "/dev/play/cc1352/uart", *file = NULL;
	int manual = 0, verify = 0;
	for (int i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(argv[0]); return 0; }
		if (!strcmp(argv[i], "--manual")) manual = 1;
		else if (!strcmp(argv[i], "--verify-only")) verify = 1;
		else if (!strcmp(argv[i], "-p") && i + 1 < argc) port = argv[++i];
		else if (argv[i][0] == '-' || file) { usage(argv[0]); return 1; }
		else file = argv[i];
	}
	if (!file) { usage(argv[0]); return 1; }
	uint8_t *image = malloc(FLASH_SIZE);
	if (!image) fail("malloc");
	memset(image, 0xff, FLASH_SIZE);
	FILE *f = fopen(file, "rb");
	if (!f) fail(file);
	size_t size = fread(image, 1, FLASH_SIZE, f);
	int extra = fgetc(f);
	if (ferror(f)) fail("read firmware");
	fclose(f);
	if (!size || extra != EOF) {
		fprintf(stderr, "Image must contain 1..%u bytes\n", FLASH_SIZE); return 1;
	}
	uint32_t expected = crc32(image, FLASH_SIZE);
	printf("Image: %zu bytes, FF padded to %u; CRC32 %08x\n", size, FLASH_SIZE, expected);
	uart = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
	if (uart < 0) fail(port);
	atexit(cleanup);
	if (flock(uart, LOCK_EX | LOCK_NB)) fail("UART lock");
	if (ioctl(uart, TIOCEXCL)) fail("UART exclusive access");
	if (tcgetattr(uart, &saved)) fail("tcgetattr");
	have_saved = 1;
	struct termios t = saved;
	cfmakeraw(&t);
	cfsetispeed(&t, B115200); cfsetospeed(&t, B115200);
	t.c_cflag = (t.c_cflag & ~(CSIZE | PARENB | CSTOPB | CRTSCTS)) | CS8 | CLOCAL | CREAD;
	t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0;
	if (tcsetattr(uart, TCSANOW, &t)) fail("tcsetattr");
	if (!manual) bootloader();
	if (tcflush(uart, TCIOFLUSH)) fail("tcflush");
	uint8_t sync[] = {0x55, 0x55};
	int64_t deadline = now_ms() + timeout_ms;
	transfer(sync, sizeof(sync), 1, deadline); expect_ack(deadline);
	status();
	if (!verify) {
		puts("Erasing customer flash...");
		command(0x2c, NULL, 0); status();
		uint8_t args[8];
		be32(args, 0); be32(args + 4, FLASH_SIZE);
		command(0x21, args, sizeof(args)); status();
		for (size_t off = 0; off < FLASH_SIZE; off += 252) {
			size_t n = FLASH_SIZE - off;
			if (n > 252) n = 252;
			command(0x24, image + off, n); status();
			if (off % (252 * 128) == 0) printf("Programmed %zu/%u bytes\n", off + n, FLASH_SIZE);
		}
	}
	uint8_t args[12] = {0}, result[4];
	be32(args + 4, FLASH_SIZE); /* address zero, read-repeat zero */
	command(0x27, args, sizeof(args)); response(result, sizeof(result));
	uint32_t actual = (uint32_t)result[0] << 24 | (uint32_t)result[1] << 16 |
		(uint32_t)result[2] << 8 | result[3];
	if (actual != expected) {
		fprintf(stderr, "CRC mismatch: host %08x, target %08x; target left in bootloader\n", expected, actual);
		return 1;
	}
	puts("CRC verified. Resetting CC1352.");
	command(0x25, NULL, 0);
	free(image);
	return 0;
}
