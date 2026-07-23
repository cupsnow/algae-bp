/* $Id$
 *
 * SPDX-License-Identifier: MIT
 *
 * @author joelai
 *
 * @file /algae-bp/package/aloe/rggb10_rgb888_i420.c
 * @brief rggb10_rgb888_i420
 */

#include <stdlib.h>
#include <stdint.h>

/*
 * Select the actual location of the 10-bit data inside the 16-bit word.

 * 0: lower 10 bits
 *    pixel = 0x03FF
 *
 * 1: upper 10 bits
 *    pixel = 0xFFC0
 */
#define RAW10_LOW_ALIGNED  0
#define RAW10_HIGH_ALIGNED 1

/*
 * Change this according to your actual RAW format.
 */
#define RAW10_ALIGNMENT RAW10_LOW_ALIGNED

/*
 * Bayer pattern: RGGB

 * R G R G R G ...
 * G B G B G B ...
 * R G R G R G ...
 * G B G B G B ...
 */

/* --------------------------------------------------------- */
/* Extract 10-bit RAW value                                  */
/* --------------------------------------------------------- */

static uint16_t extract_raw10(uint16_t pixel) {
#if RAW10_ALIGNMENT == RAW10_LOW_ALIGNED

	/*
	 * 10-bit value stored in bits [9:0]
	 */
	return pixel & 0x03FF;

#elif RAW10_ALIGNMENT == RAW10_HIGH_ALIGNED

    /*
     * 10-bit value stored in bits [15:6]
     */
    return pixel >> 6;

#else

#error "Invalid RAW10_ALIGNMENT"

#endif
}

/* --------------------------------------------------------- */
/* Clamp helper                                               */
/* --------------------------------------------------------- */

static uint8_t clamp_u8(int value) {
	if (value < 0) return 0;

	if (value > 255) return 255;

	return (uint8_t)value;
}

/* --------------------------------------------------------- */
/* Convert 10-bit value to 8-bit                            */
/* --------------------------------------------------------- */

static uint8_t raw10_to_u8(uint16_t value) {
	/*
	 * 10-bit range:
	 *
	 *       0 ... 1023
	 *
	 * 8-bit range:
	 *
	 *       0 ... 255
	 */

	return (uint8_t)((value * 255 + 511) / 1023);
}

/* --------------------------------------------------------- */
/* Read one Bayer pixel safely                               */
/* --------------------------------------------------------- */

static uint16_t get_pixel(int width, int height, const uint16_t *raw, int x, int y) {
	/*
	 * Clamp coordinates at image boundary.
	 */

	if (x < 0) x = 0;

	if (x >= width) x = width - 1;

	if (y < 0) y = 0;

	if (y >= height) y = height - 1;

	return extract_raw10(raw[y * width + x]);
}

/*
 * Convert RAW10 Bayer RGGB directly to planar YUV I420.
 *
 * Input:
 *
 *     RAW10 Bayer RGGB
 *     16-bit container
 *
 * Output:
 *
 *     Y plane: WIDTH     x HEIGHT
 *     U plane: WIDTH/2   x HEIGHT/2
 *     V plane: WIDTH/2   x HEIGHT/2
 *
 * Memory layout:
 *
 *     YYYYYYYYYYYYYYYYY
 *     YYYYYYYYYYYYYYYYY
 *     ...
 *     UUUUUUUUUUUUUUUUU
 *     ...
 *     VVVVVVVVVVVVVVVVV
 *
 * The Bayer pattern is:
 *
 *     R G R G R G ...
 *     G B G B G B ...
 *     R G R G R G ...
 *     G B G B G B ...
 */
extern "C"
void aloe_rggb10_to_rgb888_i420(int width, int height, const uint16_t *raw,
		uint8_t *i420, uint8_t *rgb) {
	uint8_t *y_plane;
	uint8_t *u_plane;
	uint8_t *v_plane;
	int x;
	int y;

	if (i420) {
		/*
		 * Calculate plane sizes.
		 */
		size_t y_size = width * height;
		size_t uv_width = width / 2;
		size_t uv_height = height / 2;
		size_t uv_size = uv_width * uv_height;

		/*
		 * Set plane pointers.
		 */
		y_plane = i420;
		u_plane = i420 + y_size;
		v_plane = u_plane + uv_size;
	}

    /*
	 * ----------------------------------------------------
	 * Generate Y plane
	 * ----------------------------------------------------
	 *
	 * For every Bayer pixel, perform bilinear interpolation
	 * to estimate R, G, and B.
	 *
	 * Then convert RGB to Y.
	 */
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			uint16_t r;
			uint16_t g;
			uint16_t b;
			int row_even;
			int col_even;

			row_even = ((y & 1) == 0);
			col_even = ((x & 1) == 0);

			/*
			 * RGGB Bayer pattern:
			 *
			 *       x even   x odd
			 *
			 * y even    R        G
			 * y odd     G        B
			 */

			if (row_even && col_even) {
				/*
				 * R pixel
				 *
				 *       G
				 *     G R G
				 *       G
				 *
				 * B is diagonally adjacent.
				 */
				r = get_pixel(width, height, raw, x, y);
				g = (
						get_pixel(width, height, raw, x - 1, y) +
						get_pixel(width, height, raw, x + 1, y) +
						get_pixel(width, height, raw, x, y - 1) +
						get_pixel(width, height, raw, x, y + 1)
						) / 4;
				b = (
						get_pixel(width, height, raw, x - 1, y - 1) +
						get_pixel(width, height, raw, x + 1, y - 1) +
						get_pixel(width, height, raw, x - 1, y + 1) +
						get_pixel(width, height, raw, x + 1, y + 1)
						) / 4;
			} else if (row_even && !col_even) {
				/*
				 * G pixel on R row
				 *
				 *     R G R
				 *       B
				 */
				g = get_pixel(width, height, raw, x, y);
				r = (
						get_pixel(width, height, raw, x - 1, y) +
						get_pixel(width, height, raw, x + 1, y)
						) / 2;
				b = (
						get_pixel(width, height, raw, x, y - 1) +
						get_pixel(width, height, raw, x, y + 1)
						) / 2;
			} else if (!row_even && col_even) {
				/*
				 * G pixel on B row
				 *
				 *       R
				 *     B G B
				 */
				g = get_pixel(width, height, raw, x, y);
				r = (
						get_pixel(width, height, raw, x, y - 1) +
						get_pixel(width, height, raw, x, y + 1)
						) / 2;
				b = (
						get_pixel(width, height, raw, x - 1, y) +
						get_pixel(width, height, raw, x + 1, y)
						) / 2;
			} else {
				/*
				 * B pixel
				 *
				 *       R
				 *     G B G
				 *       G
				 */
				b = get_pixel(width, height, raw, x, y);
				g = (
						get_pixel(width, height, raw, x - 1, y) +
						get_pixel(width, height, raw, x + 1, y) +
						get_pixel(width, height, raw, x, y - 1) +
						get_pixel(width, height, raw, x, y + 1)
						) / 4;
				r = (
						get_pixel(width, height, raw, x - 1, y - 1) +
						get_pixel(width, height, raw, x + 1, y - 1) +
						get_pixel(width, height, raw, x - 1, y + 1) +
						get_pixel(width, height, raw, x + 1, y + 1)
						) / 4;
			}

			/*
			 * Convert 10-bit RGB to 8-bit RGB.
			 */
			int r8 = raw10_to_u8(r);
			int g8 = raw10_to_u8(g);
			int b8 = raw10_to_u8(b);

			if (rgb) {
				rgb[(y * width + x) * 3 + 0] = r8;
				rgb[(y * width + x) * 3 + 1] = g8;
				rgb[(y * width + x) * 3 + 2] = b8;
			}

			if (i420) {
				/*
				 * BT.601 limited-range RGB -> Y.
				 *
				 * Y = 16 + 0.257R + 0.504G + 0.098B
				 *
				 * Integer approximation:
				 *
				 * Y = ((66R + 129G + 25B + 128) >> 8) + 16
				 */
				int Y = ((66 * r8 + 129 * g8 + 25 * b8 + 128) >> 8) + 16;
				y_plane[y * width + x] = clamp_u8(Y);
			} // i420
		}
	}

	if (i420) {
		/*
		 * ----------------------------------------------------
		 * Generate U and V planes
		 * ----------------------------------------------------
		 *
		 * One U and one V sample are generated for each
		 * 2x2 Bayer/RGB block.
		 *
		 * This produces YUV 4:2:0 planar I420.
		 */
		for (y = 0; y < height; y += 2) {
			for (x = 0; x < width; x += 2) {
				int r_sum = 0;
				int g_sum = 0;
				int b_sum = 0;
				int dx;
				int dy;

				/*
				 * Demosaic each of the four pixels in the
				 * 2x2 block.
				 */
				for (dy = 0; dy < 2; dy++) {
					for (dx = 0; dx < 2; dx++) {
						int px = x + dx;
						int py = y + dy;
						uint16_t r;
						uint16_t g;
						uint16_t b;
						int row_even;
						int col_even;

						row_even = ((py & 1) == 0);
						col_even = ((px & 1) == 0);

						if (row_even && col_even) {
							/*
							 * R pixel
							 */
							r = get_pixel(width, height, raw, px, py);
							g = (
									get_pixel(width, height, raw, px - 1, py) +
									get_pixel(width, height, raw, px + 1, py) +
									get_pixel(width, height, raw, px, py - 1) +
									get_pixel(width, height, raw, px, py + 1)
									) / 4;
							b = (
									get_pixel(width, height, raw, px - 1, py - 1) +
									get_pixel(width, height, raw, px + 1, py - 1) +
									get_pixel(width, height, raw, px - 1, py + 1) +
									get_pixel(width, height, raw, px + 1, py + 1)
									) / 4;
						} else if (row_even && !col_even) {
							/*
							 * G pixel on R row
							 */
							g = get_pixel(width, height, raw, px, py);
							r = (
									get_pixel(width, height, raw, px - 1, py) +
									get_pixel(width, height, raw, px + 1, py)
									) / 2;
							b = (
									get_pixel(width, height, raw, px, py - 1) +
									get_pixel(width, height, raw, px, py + 1)
									) / 2;
						} else if (!row_even && col_even) {
							/*
							 * G pixel on B row
							 */
							g = get_pixel(width, height, raw, px, py);
							r = (
									get_pixel(width, height, raw, px, py - 1) +
									get_pixel(width, height, raw, px, py + 1)
									) / 2;
							b = (
									get_pixel(width, height, raw, px - 1, py) +
									get_pixel(width, height, raw, px + 1, py)
									) / 2;
						} else {
							/*
							 * B pixel
							 */
							b = get_pixel(width, height, raw, px, py);
							g = (
									get_pixel(width, height, raw, px - 1, py) +
									get_pixel(width, height, raw, px + 1, py) +
									get_pixel(width, height, raw, px, py - 1) +
									get_pixel(width, height, raw, px, py + 1)
									) / 4;
							r = (
									get_pixel(width, height, raw, px - 1, py - 1) +
									get_pixel(width, height, raw, px + 1, py - 1) +
									get_pixel(width, height, raw, px - 1, py + 1) +
									get_pixel(width, height, raw, px + 1, py + 1)
									) / 4;
						}

						/*
						 * Convert RAW10 to RGB888.
						 */
						r_sum += raw10_to_u8(r);
						g_sum += raw10_to_u8(g);
						b_sum += raw10_to_u8(b);
					}
				}

				/*
				 * Average the 2x2 RGB block.
				 */
				int r8 = r_sum / 4;
				int g8 = g_sum / 4;
				int b8 = b_sum / 4;
				/*
				 * BT.601 limited-range conversion:
				 *
				 * U = 128 - 0.148R - 0.291G + 0.439B
				 * V = 128 + 0.439R - 0.368G - 0.071B
				 *
				 * Integer approximation:
				 */

				int U = ((-38 * r8 - 74 * g8 + 112 * b8 + 128) >> 8) + 128;
				int V = ((112 * r8 - 94 * g8 - 18 * b8 + 128) >> 8) + 128;
				int uv_x = x / 2;
				int uv_y = y / 2;

				/*
				 * Store planar U and V.
				 */
				u_plane[uv_y * (width / 2) + uv_x] = clamp_u8(U);
				v_plane[uv_y * (width / 2) + uv_x] = clamp_u8(V);
			}
		}
	} // i420
}
