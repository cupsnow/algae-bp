/* $Id$
 *
 * @author joelai
 *
 * @file /algae-bp/package/tester1/epd_v21.h
 * @brief epd_v21
 */

#ifndef EPD_V21_H_
#define EPD_V21_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Waveshare 2.9-inch e-Paper V2.1
 *
 * Physical resolution:
 *     128 x 296
 *
 * Note:
 *     The panel is 128 pixels wide and 296 pixels high.
 *     It is NOT 296 x 128 from the controller's point of view.
 */

#define EPD_WIDTH       128
#define EPD_HEIGHT      296

#define EPD_LINE_BYTES  (EPD_WIDTH / 8)
#define EPD_BUFFER_SIZE (EPD_LINE_BYTES * EPD_HEIGHT)

/*
 * Default Linux devices for the BeaglePlay setup.
 */
#define EPD_SPI_DEVICE  "/dev/spidev0.0"
#define EPD_GPIO_DEVICE "/dev/gpiochip3"

/*
 * GPIO offsets on the BeaglePlay setup.
 */
#define EPD_GPIO_DC     10
#define EPD_GPIO_RST    12
#define EPD_GPIO_BUSY   9

struct epd {
	int spi_fd;

	int gpio_fd;

	/*
	 * libgpiod v2 line request.
	 *
	 * Kept opaque here so users of this header don't need
	 * to include gpiod.h.
	 */
	void *gpio_request;

	unsigned int dc_offset;
	unsigned int rst_offset;
	unsigned int busy_offset;
};

/*
 * Open SPI and GPIO devices.
 */
int epd_open(struct epd *epd,
		const char *spi_device,
		const char *gpio_device);

/*
 * Close SPI and GPIO devices.
 */
void epd_close(struct epd *epd);

/*
 * Initialize the V2 controller.
 */
int epd_init(struct epd *epd);

/*
 * Clear the complete display to white.
 */
int epd_clear(struct epd *epd);

/*
 * Display a complete 4736-byte framebuffer.
 *
 * Image format:
 *
 *     one bit per pixel
 *     MSB = leftmost pixel
 *
 *     byte 0 = pixels x=0..7
 *     byte 1 = pixels x=8..15
 *     ...
 *
 * White = 1
 * Black = 0
 */
int epd_display(struct epd *epd,
		const uint8_t *image);

/*
 * Write image to both old/current image buffers and refresh.
 *
 * This is useful for establishing a known initial state.
 */
int epd_display_base(struct epd *epd,
		const uint8_t *image);

/*
 * Put the controller into deep sleep.
 */
int epd_sleep(struct epd *epd);

/*
 * Framebuffer helpers.
 */
void epd_image_clear(uint8_t *image, int white);

void epd_pixel_set(uint8_t *image,
		int x,
		int y,
		int black);

void epd_draw_checkerboard(uint8_t *image,
		int block_size);

void epd_draw_border(uint8_t *image);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* EPD_V21_H_ */
