/* $Id$
 *
 * @author joelai
 *
 * @file /algae-bp/package/tester1/epd_v21.c
 * @brief epd_v21
 */

#define _GNU_SOURCE

#include "epd_v21.h"
#include "epd_v21_lut.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <gpiod.h>

#include "priv.h"

#define SPI_MODE    SPI_MODE_0
#define SPI_BITS    8
#define SPI_SPEED   1000000U

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

extern int spi_trunk;

/* ------------------------------------------------------------------------- */
/* Utility                                                                   */
/* ------------------------------------------------------------------------- */

static void delay_ms(unsigned int ms)
{
	usleep(ms * 1000U);
}

static int spi_write_bytes(struct epd *epd,
		const uint8_t *data,
		size_t len)
{
	ssize_t n, pos = 0;

	if (len == 0)
		return 0;

	while (pos < len) {
		size_t trunk = len;

		if (spi_trunk > 0 && trunk > spi_trunk) trunk = spi_trunk;
		n = write(epd->spi_fd, data + pos, trunk);

		if (n < 0 && errno != EINTR) {
			log_e("spi write; %s\n", strerror(errno));
			return -1;
		}

		if (pos > 0) {
			log_d(
					"SPI write (%d + %d) / %d\n", (int)pos, (int)n, (int)len);
		}
		pos += n;
	}

//	n = write(epd->spi_fd, data, len);
//
//	if (n < 0) {
//		log_e("spi write; %s\n", strerror(errno));
//		return -1;
//	}
//
//	if ((size_t)n != len) {
//		log_e(
//				"SPI short write: %zd / %zu bytes\n",
//				n, len);
//		return -1;
//	}

	return 0;
}

/* ------------------------------------------------------------------------- */
/* GPIO                                                                      */
/* ------------------------------------------------------------------------- */

static int gpio_set(struct epd *epd,
		unsigned int offset,
		int value)
{
	struct gpiod_line_request *request;

	request = (struct gpiod_line_request*)epd->gpio_request;

	if (!request)
		return -1;

	if (gpiod_line_request_set_value(request,
			offset,
			value ? GPIOD_LINE_VALUE_ACTIVE
					: GPIOD_LINE_VALUE_INACTIVE) < 0) {
		log_e(
				"gpio set offset %u failed: %s\n",
				offset,
				strerror(errno));
		return -1;
	}

	return 0;
}

static int gpio_get(struct epd *epd,
		unsigned int offset,
		int *value)
{
	enum gpiod_line_value v;
	struct gpiod_line_request *request;

	request = (struct gpiod_line_request*)epd->gpio_request;

	if (!request)
		return -1;

	v = gpiod_line_request_get_value(request, offset);

	if (v == GPIOD_LINE_VALUE_ERROR) {
		log_e(
				"gpio get offset %u failed: %s\n",
				offset,
				strerror(errno));
		return -1;
	}

	*value = (v == GPIOD_LINE_VALUE_ACTIVE);

	return 0;
}

/* ------------------------------------------------------------------------- */
/* SPI / GPIO initialization                                                 */
/* ------------------------------------------------------------------------- */

int epd_open(struct epd *epd,
		const char *spi_device,
		const char *gpio_device)
{
	struct spi_ioc_transfer dummy;
	struct gpiod_chip *chip;
	struct gpiod_line_request *request;
	struct gpiod_line_settings *settings;
	struct gpiod_line_config *config;
    struct gpiod_request_config *request_config;
	unsigned int offsets[3];

	uint8_t mode = SPI_MODE;
	uint8_t bits = SPI_BITS;
	uint32_t speed = SPI_SPEED;

	memset(epd, 0, sizeof(*epd));

	epd->spi_fd = -1;
	epd->gpio_fd = -1;

	epd->dc_offset = EPD_GPIO_DC;
	epd->rst_offset = EPD_GPIO_RST;
	epd->busy_offset = EPD_GPIO_BUSY;

	/*
	 * SPI
	 */
	epd->spi_fd = open(spi_device, O_RDWR);

	if (epd->spi_fd < 0) {
		log_e(
				"open %s: %s\n",
				spi_device,
				strerror(errno));
		return -1;
	}

	if (ioctl(epd->spi_fd, SPI_IOC_WR_MODE, &mode) < 0) {
		log_e("SPI_IOC_WR_MODE; %s\n", strerror(errno));
		goto fail;
	}

	if (ioctl(epd->spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
		log_e("SPI_IOC_WR_BITS_PER_WORD; %s\n", strerror(errno));
		goto fail;
	}

	if (ioctl(epd->spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
		log_e("SPI_IOC_WR_MAX_SPEED_HZ; %s\n", strerror(errno));
		goto fail;
	}

	/*
	 * Read back configuration for diagnostics.
	 */
	memset(&dummy, 0, sizeof(dummy));

	printf("SPI: %s\n", spi_device);
	printf("SPI mode: 0x%02x\n", mode);
	printf("SPI bits: %u\n", bits);
	printf("SPI speed: %u Hz\n", speed);

	/*
	 * GPIO
	 *
	 * We use one request containing:
	 *
	 *     DC
	 *     RST
	 *     BUSY
	 *
	 * DC/RST are outputs.
	 * BUSY is input.
	 */
	chip = gpiod_chip_open(gpio_device);

	if (!chip) {
		log_e(
				"gpiod_chip_open(%s): %s\n",
				gpio_device,
				strerror(errno));
		goto fail;
	}

	settings = gpiod_line_settings_new();

	if (!settings) {
		log_e(
				"gpiod_line_settings_new: %s\n",
				strerror(errno));
		gpiod_chip_close(chip);
		goto fail;
	}

	config = gpiod_line_config_new();

	if (!config) {
		log_e(
				"gpiod_line_config_new: %s\n",
				strerror(errno));
		gpiod_line_settings_free(settings);
		gpiod_chip_close(chip);
		goto fail;
	}

	request_config = gpiod_request_config_new();
	if (!request_config) {
		log_e(
				"gpiod_line_config_new: %s\n",
				strerror(errno));
		gpiod_line_config_free(config);
		gpiod_line_settings_free(settings);
		gpiod_chip_close(chip);
		goto fail;
	}


	offsets[0] = epd->dc_offset;
	offsets[1] = epd->rst_offset;
	offsets[2] = epd->busy_offset;

	/*
	 * Configure DC and RST as outputs.
	 */
	gpiod_line_settings_set_direction(settings,
			GPIOD_LINE_DIRECTION_OUTPUT);

	gpiod_line_settings_set_output_value(
			settings,
			GPIOD_LINE_VALUE_INACTIVE);

	if (gpiod_line_config_add_line_settings(
			config,
			offsets,
			2,
			settings) < 0) {
		log_e(
				"configure DC/RST: %s\n",
				strerror(errno));
		gpiod_line_config_free(config);
		gpiod_line_settings_free(settings);
		gpiod_request_config_free(request_config);
		gpiod_chip_close(chip);
		goto fail;
	}

	/*
	 * Configure BUSY as input.
	 */
	gpiod_line_settings_set_direction(
			settings,
			GPIOD_LINE_DIRECTION_INPUT);

	if (gpiod_line_config_add_line_settings(
			config,
			&offsets[2],
			1,
			settings) < 0) {
		log_e(
				"configure BUSY: %s\n",
				strerror(errno));
		gpiod_line_config_free(config);
		gpiod_line_settings_free(settings);
		gpiod_request_config_free(request_config);
		gpiod_chip_close(chip);
		goto fail;
	}

//	request = gpiod_chip_request_lines(
//			chip,
//			"epd_v21",
//			NULL,
//			config);
    request = gpiod_chip_request_lines(
        chip,
        request_config,
		config);

	gpiod_line_config_free(config);
	gpiod_line_settings_free(settings);
	gpiod_request_config_free(request_config);
	gpiod_chip_close(chip);

	if (!request) {
		log_e(
				"gpiod_chip_request_lines: %s\n",
				strerror(errno));
		goto fail;
	}

	epd->gpio_request = request;

	return 0;

fail:
	epd_close(epd);
	return -1;
}

void epd_close(struct epd *epd)
{
	struct gpiod_line_request *request;

	if (!epd)
		return;

	request =
			(struct gpiod_line_request*)epd->gpio_request;

	if (request)
		gpiod_line_request_release(request);

	epd->gpio_request = NULL;

	if (epd->spi_fd >= 0)
		close(epd->spi_fd);

	epd->spi_fd = -1;
}

/* ------------------------------------------------------------------------- */
/* EPD command/data                                                          */
/* ------------------------------------------------------------------------- */

static int epd_command(struct epd *epd,
		uint8_t command)
{
	if (gpio_set(epd, epd->dc_offset, 0) < 0)
		return -1;

	return spi_write_bytes(epd, &command, 1);
}

static int epd_data(struct epd *epd,
		uint8_t data)
{
	if (gpio_set(epd, epd->dc_offset, 1) < 0)
		return -1;

	return spi_write_bytes(epd, &data, 1);
}

static int epd_data_buffer(struct epd *epd,
		const uint8_t *data,
		size_t len)
{
	if (gpio_set(epd, epd->dc_offset, 1) < 0)
		return -1;

	return spi_write_bytes(epd, data, len);
}

/* ------------------------------------------------------------------------- */
/* Reset / BUSY                                                               */
/* ------------------------------------------------------------------------- */

static int epd_reset(struct epd *epd)
{
	/*
	 * Official Waveshare V2 sequence:
	 *
	 * RST = 1
	 * 10 ms
	 * RST = 0
	 * 2 ms
	 * RST = 1
	 * 10 ms
	 */
	if (gpio_set(epd, epd->rst_offset, 1) < 0)
		return -1;

	delay_ms(10);

	if (gpio_set(epd, epd->rst_offset, 0) < 0)
		return -1;

	delay_ms(2);

	if (gpio_set(epd, epd->rst_offset, 1) < 0)
		return -1;

	delay_ms(10);

	return 0;
}

static int epd_wait_busy(struct epd *epd)
{
	int busy;

	printf("EPD: waiting BUSY");

	for (;;) {
		if (gpio_get(epd, epd->busy_offset, &busy) < 0)
			return -1;

		if (!busy)
			break;

		printf(".");
		fflush(stdout);

		delay_ms(50);
	}

	printf(" done\n");

	/*
	 * Waveshare adds another 50 ms after BUSY releases.
	 */
	delay_ms(50);

	return 0;
}

/* ------------------------------------------------------------------------- */
/* Controller configuration                                                   */
/* ------------------------------------------------------------------------- */

static int epd_set_window(struct epd *epd,
		int x_start,
		int y_start,
		int x_end,
		int y_end)
{
	uint8_t x0;
	uint8_t x1;

	if (x_start < 0 ||
			x_end >= EPD_WIDTH ||
			y_start < 0 ||
			y_end >= EPD_HEIGHT ||
			x_start > x_end ||
			y_start > y_end)
		return -1;

	/*
	 * X address is byte based.
	 */
	x0 = (uint8_t)((x_start >> 3) & 0xff);
	x1 = (uint8_t)((x_end >> 3) & 0xff);

	if (epd_command(epd, 0x44) < 0)
		return -1;

	if (epd_data(epd, x0) < 0)
		return -1;

	if (epd_data(epd, x1) < 0)
		return -1;

	/*
	 * Y address is 16-bit.
	 */
	if (epd_command(epd, 0x45) < 0)
		return -1;

	if (epd_data(epd, y_start & 0xff) < 0)
		return -1;

	if (epd_data(epd, (y_start >> 8) & 0xff) < 0)
		return -1;

	if (epd_data(epd, y_end & 0xff) < 0)
		return -1;

	if (epd_data(epd, (y_end >> 8) & 0xff) < 0)
		return -1;

	return 0;
}

static int epd_set_cursor(struct epd *epd,
		int x,
		int y)
{
	if (epd_command(epd, 0x4e) < 0)
		return -1;

	if (epd_data(epd, x & 0xff) < 0)
		return -1;

	if (epd_command(epd, 0x4f) < 0)
		return -1;

	if (epd_data(epd, y & 0xff) < 0)
		return -1;

	if (epd_data(epd, (y >> 8) & 0xff) < 0)
		return -1;

	return 0;
}

/* ------------------------------------------------------------------------- */
/* LUT                                                                       */
/* ------------------------------------------------------------------------- */

static int epd_load_lut(struct epd *epd)
{
	size_t i;

	if (epd_command(epd, 0x32) < 0)
		return -1;

	/*
	 * The official V2 driver sends the first 153 bytes
	 * through command 0x32.
	 */
	if (gpio_set(epd, epd->dc_offset, 1) < 0)
		return -1;

	for (i = 0; i < 153; ++i) {
		if (spi_write_bytes(epd,
				&EPD_V21_WS_20_30[i],
				1) < 0)
			return -1;
	}

	if (epd_wait_busy(epd) < 0)
		return -1;

	/*
	 * Remaining host-LUT parameters.
	 */
	if (epd_command(epd, 0x3f) < 0)
		return -1;

	if (epd_data(epd, EPD_V21_WS_20_30[153]) < 0)
		return -1;

	if (epd_command(epd, 0x03) < 0)
		return -1;

	if (epd_data(epd, EPD_V21_WS_20_30[154]) < 0)
		return -1;

	if (epd_command(epd, 0x04) < 0)
		return -1;

	if (epd_data(epd, EPD_V21_WS_20_30[155]) < 0)
		return -1;

	if (epd_data(epd, EPD_V21_WS_20_30[156]) < 0)
		return -1;

	if (epd_data(epd, EPD_V21_WS_20_30[157]) < 0)
		return -1;

	if (epd_command(epd, 0x2c) < 0)
		return -1;

	if (epd_data(epd, EPD_V21_WS_20_30[158]) < 0)
		return -1;

	return 0;
}

/* ------------------------------------------------------------------------- */
/* Refresh                                                                    */
/* ------------------------------------------------------------------------- */

static int epd_refresh(struct epd *epd)
{
	/*
	 * Display Update Control 2
	 */
	if (epd_command(epd, 0x22) < 0)
		return -1;

	if (epd_data(epd, 0xc7) < 0)
		return -1;

	/*
	 * Master activation.
	 */
	if (epd_command(epd, 0x20) < 0)
		return -1;

	return epd_wait_busy(epd);
}

/* ------------------------------------------------------------------------- */
/* Initialization                                                             */
/* ------------------------------------------------------------------------- */

int epd_init(struct epd *epd)
{
	printf("EPD: reset\n");

	if (epd_reset(epd) < 0)
		return -1;

	delay_ms(100);

	if (epd_wait_busy(epd) < 0)
		return -1;

	/*
	 * Software reset.
	 */
	printf("EPD: software reset\n");

	if (epd_command(epd, 0x12) < 0)
		return -1;

	if (epd_wait_busy(epd) < 0)
		return -1;

	/*
	 * Driver output control.
	 */
	if (epd_command(epd, 0x01) < 0)
		return -1;

	if (epd_data(epd, 0x27) < 0)
		return -1;

	if (epd_data(epd, 0x01) < 0)
		return -1;

	if (epd_data(epd, 0x00) < 0)
		return -1;

	/*
	 * Data entry mode.
	 */
	if (epd_command(epd, 0x11) < 0)
		return -1;

	if (epd_data(epd, 0x03) < 0)
		return -1;

	/*
	 * Full RAM window.
	 */
	if (epd_set_window(epd,
			0,
			0,
			EPD_WIDTH - 1,
			EPD_HEIGHT - 1) < 0)
		return -1;

	/*
	 * Display update control.
	 *
	 * This was one of the important missing pieces
	 * in the previous implementation.
	 */
	if (epd_command(epd, 0x21) < 0)
		return -1;

	if (epd_data(epd, 0x00) < 0)
		return -1;

	if (epd_data(epd, 0x80) < 0)
		return -1;

	if (epd_set_cursor(epd, 0, 0) < 0)
		return -1;

	if (epd_wait_busy(epd) < 0)
		return -1;

	/*
	 * Waveform.
	 */
	printf("EPD: loading waveform LUT\n");

	if (epd_load_lut(epd) < 0)
		return -1;

	printf("EPD: initialization complete\n");

	return 0;
}

/* ------------------------------------------------------------------------- */
/* Display operations                                                        */
/* ------------------------------------------------------------------------- */

int epd_clear(struct epd *epd)
{
	static uint8_t white[EPD_BUFFER_SIZE];

	memset(white, 0xff, sizeof(white));

	printf("EPD: clear\n");

	return epd_display_base(epd, white);
}

int epd_display(struct epd *epd,
		const uint8_t *image)
{
	if (!image)
		return -1;

	if (epd_command(epd, 0x24) < 0)
		return -1;

	if (epd_data_buffer(epd,
			image,
			EPD_BUFFER_SIZE) < 0)
		return -1;

	return epd_refresh(epd);
}

int epd_display_base(struct epd *epd,
		const uint8_t *image)
{
	if (!image)
		return -1;

	/*
	 * Current image.
	 */
	if (epd_command(epd, 0x24) < 0)
		return -1;

	if (epd_data_buffer(epd,
			image,
			EPD_BUFFER_SIZE) < 0)
		return -1;

	/*
	 * Previous image.
	 */
	if (epd_command(epd, 0x26) < 0)
		return -1;

	if (epd_data_buffer(epd,
			image,
			EPD_BUFFER_SIZE) < 0)
		return -1;

	return epd_refresh(epd);
}

int epd_sleep(struct epd *epd)
{
	printf("EPD: sleep\n");

	if (epd_command(epd, 0x10) < 0)
		return -1;

	if (epd_data(epd, 0x01) < 0)
		return -1;

	delay_ms(100);

	return 0;
}

/* ------------------------------------------------------------------------- */
/* Framebuffer helpers                                                       */
/* ------------------------------------------------------------------------- */

void epd_image_clear(uint8_t *image,
		int white)
{
	if (!image)
		return;

	memset(image,
			white ? 0xff : 0x00,
			EPD_BUFFER_SIZE);
}

void epd_pixel_set(uint8_t *image,
		int x,
		int y,
		int black)
{
	size_t index;
	uint8_t mask;

	if (!image)
		return;

	if (x < 0 || x >= EPD_WIDTH ||
			y < 0 || y >= EPD_HEIGHT)
		return;

	index = (size_t)y * EPD_LINE_BYTES + (size_t)(x >> 3);
	mask = (uint8_t)(0x80U >> (x & 7));

	if (black)
		image[index] &= (uint8_t)~mask;
	else
		image[index] |= mask;
}

void epd_draw_checkerboard(uint8_t *image,
		int block_size)
{
	int x;
	int y;

	if (!image || block_size <= 0)
		return;

	epd_image_clear(image, 1);

	for (y = 0; y < EPD_HEIGHT; ++y) {
		for (x = 0; x < EPD_WIDTH; ++x) {
			int bx = x / block_size;
			int by = y / block_size;

			if ((bx + by) & 1)
				epd_pixel_set(image, x, y, 1);
		}
	}
}

void epd_draw_border(uint8_t *image)
{
	int x;
	int y;

	if (!image)
		return;

	for (x = 0; x < EPD_WIDTH; ++x) {
		epd_pixel_set(image, x, 0, 1);
		epd_pixel_set(image, x, EPD_HEIGHT - 1, 1);
	}

	for (y = 0; y < EPD_HEIGHT; ++y) {
		epd_pixel_set(image, 0, y, 1);
		epd_pixel_set(image, EPD_WIDTH - 1, y, 1);
	}
}
