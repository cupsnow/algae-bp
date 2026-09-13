/* $Id$
 *
 * @author joelai
 *
 * @file /algae-bp/package/tester1/tester_spi2.cpp
 * @brief tester_spi2
 */
/*
 * epd_gpio_spi_test.c
 *
 * MicroBus
 *
 * | DC   | GPIO1_10 | AN   | PWM | GPIO1_11 |      |
 * | RST  | GPIO1_12 | RST  | INT | GPIO1_9  | BUSY |
 * |      |          | CS   | RX  |          |      |
 * |      |          | SCK  | TX  |          |      |
 * |      |          | MISO | SCL |          |      |
 * |      |          | MOSI | SDA |          |      |
 * |      |          | 3.3V | 5V  |          |      |
 * |      |          | GND  | GND |          |      |
 *
 *
 *
 * Simple Waveshare 2.9" e-paper SPI/GPIO test
 *
 * SPI:
 *   /dev/spidev0.0
 *   mode 0
 *   8 bits
 *   1 MHz
 *
 * GPIO:
 *   DC
 *   RST
 *   BUSY
 *
 * This program does NOT implement the complete e-paper initialization.
 * It is intended to verify:
 *
 *   - SPI
 *   - DC
 *   - RST
 *   - BUSY
 *
 * Build:
 *   gcc -Wall -Wextra -O2 epd_gpio_spi_test.c \
 *       $(pkg-config --cflags --libs libgpiod) \
 *       -o epd_gpio_spi_test
 *
 * Example:
 *   ./epd_gpio_spi_test /dev/gpiochip1 10 12 9
 *
 * Arguments:
 *   argv[1] = GPIO chip
 *   argv[2] = DC offset
 *   argv[3] = RST offset
 *   argv[4] = BUSY offset
 */

#include <errno.h>
#include <fcntl.h>
#include <gpiod.h>
#include <linux/spi/spidev.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "epd_v21.h"

#define SPI_DEV         "/dev/spidev0.0"
#define SPI_SPEED_HZ    1000000
#define SPI_BITS        8

/*
 * Waveshare e-paper reset timing.
 *
 * We deliberately keep this conservative during bring-up.
 */
#define RESET_LOW_MS    20
#define RESET_HIGH_MS   20

int spi_trunk = 300;

static struct gpiod_line_request *gpio_request = NULL;

static unsigned int dc_offset;
static unsigned int rst_offset;
static unsigned int busy_offset;

/* ------------------------------------------------------------ */
/* Time helper                                                  */
/* ------------------------------------------------------------ */

static void delay_ms(unsigned int ms) {
	usleep(ms * 1000);
}

static int main_spidev_test(void*, int argc, const char **argv) {
	const char *dev = SPI_DEV;
	uint8_t mode = SPI_MODE_0;
	uint8_t bits = SPI_BITS;
	uint32_t speed = SPI_SPEED_HZ;

	uint8_t tx[] = {
			0x00, 0x11, 0x22, 0x33,
			0x44, 0x55, 0xAA, 0xFF
	};

	int fd = open(dev, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "open %s: %s\n", dev, strerror(errno));
		return 1;
	}

	if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0) {
		perror("SPI_IOC_WR_MODE");
		close(fd);
		return 1;
	}

	if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
		perror("SPI_IOC_WR_BITS_PER_WORD");
		close(fd);
		return 1;
	}

	if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
		perror("SPI_IOC_WR_MAX_SPEED_HZ");
		close(fd);
		return 1;
	}

	printf("SPI device: %s\n", dev);
	printf("mode:       %u\n", mode);
	printf("bits:       %u\n", bits);
	printf("speed:      %u Hz\n", speed);

	for (;;) {
		ssize_t n = write(fd, tx, sizeof(tx));

		if (n < 0) {
			fprintf(stderr, "write: %s\n", strerror(errno));
			break;
		}

		if ((size_t)n != sizeof(tx)) {
			fprintf(stderr, "short write: %zd/%zu\n",
					n, sizeof(tx));
			break;
		}

		usleep(100000); /* 100 ms */
	}

	close(fd);
	return 0;
}

/* ------------------------------------------------------------ */
/* GPIO                                                         */
/* ------------------------------------------------------------ */

static int gpio_init(const char *chip_path) {
	struct gpiod_chip *chip;
	struct gpiod_line_settings *dc_settings;
	struct gpiod_line_settings *rst_settings;
	struct gpiod_line_settings *busy_settings;
	struct gpiod_line_config *line_config;
	struct gpiod_request_config *req_config;

	chip = gpiod_chip_open(chip_path);
	if (!chip) {
		fprintf(stderr,
				"gpiod_chip_open(%s): %s\n",
				chip_path, strerror(errno));
		return -1;
	}

	dc_settings = gpiod_line_settings_new();
	rst_settings = gpiod_line_settings_new();
	busy_settings = gpiod_line_settings_new();

	line_config = gpiod_line_config_new();
	req_config = gpiod_request_config_new();

	if (!dc_settings || !rst_settings || !busy_settings ||
			!line_config || !req_config) {
		fprintf(stderr, "Failed to allocate libgpiod objects\n");
		return -1;
	}

	/*
	 * DC: output, initially LOW.
	 */
	gpiod_line_settings_set_direction(
			dc_settings,
			GPIOD_LINE_DIRECTION_OUTPUT);

	gpiod_line_settings_set_output_value(
			dc_settings,
			GPIOD_LINE_VALUE_INACTIVE);

	/*
	 * RST: output, initially HIGH.
	 *
	 * RST is active LOW.
	 */
	gpiod_line_settings_set_direction(
			rst_settings,
			GPIOD_LINE_DIRECTION_OUTPUT);

	gpiod_line_settings_set_output_value(
			rst_settings,
			GPIOD_LINE_VALUE_ACTIVE);

	/*
	 * BUSY: input.
	 *
	 * Waveshare documentation specifies BUSY HIGH while busy.
	 */
	gpiod_line_settings_set_direction(
			busy_settings,
			GPIOD_LINE_DIRECTION_INPUT);

	if (gpiod_line_config_add_line_settings(
			line_config,
			&dc_offset,
			1,
			dc_settings) < 0) {
		perror("add DC line");
		return -1;
	}

	if (gpiod_line_config_add_line_settings(
			line_config,
			&rst_offset,
			1,
			rst_settings) < 0) {
		perror("add RST line");
		return -1;
	}

	if (gpiod_line_config_add_line_settings(
			line_config,
			&busy_offset,
			1,
			busy_settings) < 0) {
		perror("add BUSY line");
		return -1;
	}

	gpiod_request_config_set_consumer(
			req_config,
			"epd_gpio_spi_test");

	gpio_request = gpiod_chip_request_lines(
			chip,
			req_config,
			line_config);

	if (!gpio_request) {
		fprintf(stderr,
				"gpiod_chip_request_lines(%s): %s\n",
				chip_path, strerror(errno));
		return -1;
	}

	gpiod_chip_close(chip);

	gpiod_line_settings_free(dc_settings);
	gpiod_line_settings_free(rst_settings);
	gpiod_line_settings_free(busy_settings);
	gpiod_line_config_free(line_config);
	gpiod_request_config_free(req_config);

	return 0;
}

static int gpio_set(unsigned int offset, int value) {
	enum gpiod_line_value v;

	v = value ? GPIOD_LINE_VALUE_ACTIVE
			: GPIOD_LINE_VALUE_INACTIVE;

	return gpiod_line_request_set_value(
			gpio_request,
			offset,
			v);
}

static int gpio_get(unsigned int offset) {
	enum gpiod_line_value v;

	v = gpiod_line_request_get_value(
			gpio_request,
			offset);

	if (v == GPIOD_LINE_VALUE_ACTIVE)
		return 1;

	if (v == GPIOD_LINE_VALUE_INACTIVE)
		return 0;

	return -1;
}

/* ------------------------------------------------------------ */
/* E-paper GPIO functions                                       */
/* ------------------------------------------------------------ */

static int epd_reset(void) {
	printf("EPD reset...\n");

	/*
	 * RST active LOW.
	 */
	if (gpio_set(rst_offset, 0) < 0) {
		perror("RST LOW");
		return -1;
	}

	// measured edp busy about 6ms

	delay_ms(RESET_LOW_MS);

	if (gpio_set(rst_offset, 1) < 0) {
		perror("RST HIGH");
		return -1;
	}

	// measured edp busy about 400us

	delay_ms(RESET_HIGH_MS);

	printf("EPD reset complete\n");

	return 0;
}

static int epd_busy(void) {
	int value = gpio_get(busy_offset);

	if (value < 0) {
		perror("BUSY read");
		return -1;
	}

	return value;
}

static int epd_wait_busy(unsigned int timeout_ms) {
	unsigned int elapsed = 0;

	printf("BUSY = %d\n", epd_busy());

	while (elapsed < timeout_ms) {
		int busy = epd_busy();

		if (busy < 0)
			return -1;

		if (!busy) {
			printf("EPD BUSY released after %u ms\n",
					elapsed);
			return 0;
		}

		delay_ms(10);
		elapsed += 10;
	}

	fprintf(stderr,
			"Timeout waiting for EPD BUSY\n");

	return -1;
}

/* ------------------------------------------------------------ */
/* SPI                                                          */
/* ------------------------------------------------------------ */

static int spi_init(void) {
	int fd;
	uint8_t mode = SPI_MODE_0;
	uint8_t bits = SPI_BITS;
	uint32_t speed = SPI_SPEED_HZ;

	fd = open(SPI_DEV, O_RDWR);

	if (fd < 0) {
		fprintf(stderr,
				"open(%s): %s\n",
				SPI_DEV,
				strerror(errno));
		return -1;
	}

	if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0) {
		perror("SPI_IOC_WR_MODE");
		close(fd);
		return -1;
	}

	if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
		perror("SPI_IOC_WR_BITS_PER_WORD");
		close(fd);
		return -1;
	}

	if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
		perror("SPI_IOC_WR_MAX_SPEED_HZ");
		close(fd);
		return -1;
	}

	printf("SPI initialized:\n");
	printf("  device : %s\n", SPI_DEV);
	printf("  mode   : %u\n", mode);
	printf("  bits   : %u\n", bits);
	printf("  speed  : %u Hz\n", speed);

	return fd;
}

/*
 * Send a command byte.
 *
 * DC = 0
 */
static int epd_command(int spi_fd, uint8_t command) {
	if (gpio_set(dc_offset, 0) < 0) {
		perror("DC command");
		return -1;
	}

	if (write(spi_fd, &command, 1) != 1) {
		fprintf(stderr,
				"SPI command 0x%02X: %s\n",
				command,
				strerror(errno));
		return -1;
	}

	printf("CMD  0x%02X\n", command);

	return 0;
}

/*
 * Send one data byte.
 *
 * DC = 1
 */
static int epd_data(int spi_fd, uint8_t data) {
	if (gpio_set(dc_offset, 1) < 0) {
		perror("DC data");
		return -1;
	}

	if (write(spi_fd, &data, 1) != 1) {
		fprintf(stderr,
				"SPI data 0x%02X: %s\n",
				data,
				strerror(errno));
		return -1;
	}

	printf("DATA 0x%02X\n", data);

	return 0;
}

static int main_epd_gpio_spi_test(void*, int argc, const char **argv) {
	int spi_fd;

	if (argc != 5) {
		fprintf(stderr,
"Usage:\n"
"  %s <gpiochip> <dc> <rst> <busy>\n\n"
"Example:\n"
"  %s /dev/gpiochip1 10 12 9\n",
				argv[0], argv[0]);
		return 1;
	}

	dc_offset = strtoul(argv[2], NULL, 0);
	rst_offset = strtoul(argv[3], NULL, 0);
	busy_offset = strtoul(argv[4], NULL, 0);

	printf("EPD GPIO configuration:\n");
	printf("  GPIO chip : %s\n", argv[1]);
	printf("  DC        : %u\n", dc_offset);
	printf("  RST       : %u\n", rst_offset);
	printf("  BUSY      : %u\n", busy_offset);
	printf("\n");

	/*
	 * Open SPI first.
	 */
	spi_fd = spi_init();

	if (spi_fd < 0)
		return 1;

	/*
	 * Open GPIO lines.
	 */
	if (gpio_init(argv[1]) < 0) {
		close(spi_fd);
		return 1;
	}

	printf("\n");

	/*
	 * 1. Check BUSY before reset.
	 */
	printf("Initial BUSY = %d\n", epd_busy());

	/*
	 * 2. Hardware reset.
	 */
	if (epd_reset() < 0) {
		goto error;
	}

	/*
	 * 3. Check BUSY after reset.
	 */
	printf("BUSY after reset = %d\n", epd_busy());

	/*
	 * 4. Send a simple command.
	 *
	 * 0x12 is the commonly used software-reset command
	 * on controllers used by Waveshare 2.9" modules.
	 *
	 * We are using it only as a basic SPI/DC test here;
	 * this is NOT the complete controller initialization.
	 */
	if (epd_command(spi_fd, 0x12) < 0) {
		goto error;
	}

	// measured edp busy about 2ms

	/*
	 * Give the controller some time to react.
	 */
	delay_ms(10);

	/*
	 * 5. Observe BUSY.
	 */
	printf("BUSY after command = %d\n", epd_busy());

	/*
	 * 6. Wait for BUSY to become inactive.
	 */
	if (epd_wait_busy(5000) < 0) {
		goto error;
	}

	/*
	 * 7. Send one data byte.
	 *
	 * This is intentionally just a DC/data-path test.
	 * It is NOT meaningful framebuffer data yet.
	 */
	if (epd_data(spi_fd, 0xAA) < 0) {
		goto error;
	}

	printf("\n");
	printf("EPD GPIO/SPI test completed.\n");

	gpiod_line_request_release(gpio_request);
	close(spi_fd);

	return 0;

error:
	gpiod_line_request_release(gpio_request);
	close(spi_fd);

	return 1;
}

#if 1



static void make_test_image(uint8_t *image)
{
    int x;
    int y;

    /*
     * Start white.
     */
    epd_image_clear(image, 1);

    /*
     * Border.
     */
    epd_draw_border(image);

    /*
     * Vertical stripes.
     */
    for (x = 10; x < EPD_WIDTH - 10; x += 16) {
        for (y = 10; y < EPD_HEIGHT - 10; ++y) {
            epd_pixel_set(image, x, y, 1);
        }
    }

    /*
     * Horizontal stripes.
     */
    for (y = 20; y < EPD_HEIGHT - 10; y += 20) {
        for (x = 10; x < EPD_WIDTH - 10; ++x) {
            epd_pixel_set(image, x, y, 1);
        }
    }

    /*
     * Black rectangle in the middle.
     */
    for (y = 100; y < 180; ++y) {
        for (x = 32; x < 96; ++x) {
            epd_pixel_set(image, x, y, 1);
        }
    }

    /*
     * Small black square.
     */
    for (y = 30; y < 70; ++y) {
        for (x = 40; x < 80; ++x) {
            epd_pixel_set(image, x, y, 1);
        }
    }
}

static int main_epd_v21_test(void*, int argc, const char **argv)
{
    struct epd epd;
    uint8_t image[EPD_BUFFER_SIZE];

    printf("=====================================\n");
    printf(" BeaglePlay EPD 2.9 V2.1 test\n");
    printf("=====================================\n");

    printf("Resolution : %d x %d\n",
           EPD_WIDTH,
           EPD_HEIGHT);

    printf("Framebuffer: %d bytes\n",
           EPD_BUFFER_SIZE);

    /*
     * Open Linux SPI + GPIO.
     */
    if (epd_open(&epd,
                 EPD_SPI_DEVICE,
                 EPD_GPIO_DEVICE) < 0) {
        fprintf(stderr, "epd_open failed\n");
        return EXIT_FAILURE;
    }

    /*
     * Initialize controller.
     */
    if (epd_init(&epd) < 0) {
        fprintf(stderr, "epd_init failed\n");
        epd_close(&epd);
        return EXIT_FAILURE;
    }

    /*
     * White image.
     */
    epd_image_clear(image, 1);

    printf("Display white base image...\n");

    if (epd_display_base(&epd, image) < 0) {
        fprintf(stderr, "display_base failed\n");
        epd_close(&epd);
        return EXIT_FAILURE;
    }

    /*
     * Build test pattern.
     */
    make_test_image(image);

    printf("Display test pattern...\n");

    if (epd_display(&epd, image) < 0) {
        fprintf(stderr, "display failed\n");
        epd_close(&epd);
        return EXIT_FAILURE;
    }

    /*
     * Leave the image visible.
     *
     * We can put the controller into sleep afterward.
     */
    if (epd_sleep(&epd) < 0) {
        fprintf(stderr, "sleep failed\n");
        epd_close(&epd);
        return EXIT_FAILURE;
    }

    epd_close(&epd);

    printf("Done.\n");

    return EXIT_SUCCESS;
}

#else
static void epd_v21_draw_test_pattern(uint8_t *buffer) {
	int x;
	int y;

	epd_buffer_clear(buffer);

#if 1
	/*
	 * Border.
	 */
	for (x = 0; x < EPD_WIDTH; x++) {
		epd_buffer_set_pixel(buffer, x, 0, 1);
		epd_buffer_set_pixel(buffer, x, EPD_HEIGHT - 1, 1);
	}

	for (y = 0; y < EPD_HEIGHT; y++) {
		epd_buffer_set_pixel(buffer, 0, y, 1);
		epd_buffer_set_pixel(buffer, EPD_WIDTH - 1, y, 1);
	}

	/*
	 * Horizontal stripes.
	 */
	for (y = 10; y < 50; y += 4) {
		epd_buffer_fill_rect(
				buffer,
				10,
				y,
				100,
				2,
				1);
	}

	/*
	 * Black square.
	 */
	epd_buffer_fill_rect(
			buffer,
			130,
			20,
			50,
			50,
			1);

	/*
	 * Checkerboard.
	 */
	for (y = 80; y < 120; y += 10) {
		for (x = 200; x < 280; x += 10) {
			if (((x / 10) + (y / 10)) & 1) {
				epd_buffer_fill_rect(
						buffer,
						x,
						y,
						10,
						10,
						1);
			}
		}
	}
#endif
}

static int main_epd_v21_test(void*, int argc, const char **argv) {
	struct epd epd;
	struct epd_config config;

	uint8_t buffer[EPD_BUFFER_SIZE];

	if (argc < 5) {
		fprintf(stderr,
				"Usage:\n"
						"  %s <gpiochip> <dc> <rst> <busy> [spi_trunk]\n\n"
						"Example:\n"
						"  %s /dev/gpiochip3 10 12 9\n",
				argv[0],
				argv[0]);

		return 1;
	}

	config.gpiochip = argv[1];
	config.dc_gpio = strtoul(argv[2], NULL, 0);
	config.rst_gpio = strtoul(argv[3], NULL, 0);
	config.busy_gpio = strtoul(argv[4], NULL, 0);
	if (argc >= 6) spi_trunk = strtoul(argv[5], NULL, 0);

	config.spi_device = "/dev/spidev0.0";
	config.spi_speed_hz = 1000000;

	printf("Waveshare 2.9\" V2.1 test\n");
	printf("-------------------------\n");

	printf("SPI     : %s\n", config.spi_device);
	printf("Speed   : %u Hz\n", config.spi_speed_hz);
	printf("GPIO    : %s\n", config.gpiochip);
	printf("DC      : %u\n", config.dc_gpio);
	printf("RST     : %u\n", config.rst_gpio);
	printf("BUSY    : %u\n", config.busy_gpio);
	printf("Buffer  : %d bytes\n", EPD_BUFFER_SIZE);
	printf("Trunk   : %d bytes\n", spi_trunk);

	if (epd_open(&epd, &config) < 0)
		return 1;

	printf("\nInitializing EPD...\n");

	if (epd_init(&epd) < 0) {
		fprintf(stderr,
				"EPD initialization failed\n");
		epd_close(&epd);
		return 1;
	}

	printf("EPD initialization OK\n");

	printf("Creating test pattern...\n");

	epd_v21_draw_test_pattern(buffer);
#if 0
	printf("Sending framebuffer...\n");

	if (epd_display(
			&epd,
			buffer,
			sizeof(buffer)) < 0) {
		fprintf(stderr,
				"EPD display failed\n");
		epd_close(&epd);
		return 1;
	}

	printf("Display refresh complete\n");
#endif
	/*
	 * Put the controller into sleep mode.
	 */
	printf("Entering sleep...\n");

	if (epd_sleep(&epd) < 0) {
		fprintf(stderr,
				"EPD sleep failed\n");
	}

	epd_close(&epd);

	printf("Done\n");

	return 0;
}
#endif
int main(int argc, const char **argv) {
	if (0) {
	} else if (1) {
		main_epd_v21_test(NULL, argc, argv);
	} else if (1) {
		main_epd_gpio_spi_test(NULL, argc, argv);
	} else if (0) {
		main_spidev_test(NULL, argc, argv);
	}

	return 0;
}
