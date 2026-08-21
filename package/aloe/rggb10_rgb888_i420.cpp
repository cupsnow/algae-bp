/* $Id$
 *
 * @author joelai
 *
 * @file /algae-bp/package/aloe/rggb10_rgb888_i420.c
 * @brief rggb10_rgb888_i420
 */

#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>

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

static uint16_t get_pixel(int width, int height, const uint16_t *raw, 
		int x, int y) {
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

static inline uint8_t clamp_u8_int(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

/*
 * RAW10 stored in uint16_t.
 *
 * Assumption:
 *
 *     raw[x] contains a 10-bit value in bits [9:0]
 *
 * If your RAW10 is stored left-shifted, for example:
 *
 *     0xFFC0 for 1023
 *
 * then you must shift it first.
 */
static inline uint8_t raw10_to_u8_fast(uint16_t v) {
    /*
     * 10-bit -> 8-bit
     *
     * Equivalent approximately to:
     *
     *     v / 4
     *
     * But rounded instead of truncated.
     */
    return (uint8_t)((v + 2) >> 2);
}

/*
 * Clamp coordinate to image boundary.
 */
static inline int clamp_coord(int v, int max) {
    if (v < 0) return 0;
    if (v >= max) return max - 1;
    return v;
}

/*
 * Optimized RGGB10 Bayer -> RGB888 + I420
 *
 * Bayer pattern:
 *
 *     R G R G
 *     G B G B
 *     R G R G
 *     G B G B
 *
 * Input:
 *
 *     raw:
 *         width * height uint16_t pixels
 *
 *     Each pixel contains a 10-bit RAW value.
 *
 * Output:
 *
 *     rgb:
 *         width * height * 3 bytes
 *
 *     i420:
 *
 *         Y: width * height
 *         U: width/2 * height/2
 *         V: width/2 * height/2
 *
 *         Total:
 *
 *             width * height * 3 / 2
 *
 * Requirements:
 *
 *     width  must be even
 *     height must be even
 */
extern "C"
void aloe_rggb10_to_rgb888_i420_v2( int width, int height, const uint16_t *raw, 
		uint8_t *i420, uint8_t *rgb) {
    if (!raw || width <= 0 || height <= 0)
        return;

    if ((width & 1) || (height & 1))
        return;

    uint8_t *y_plane = NULL;
    uint8_t *u_plane = NULL;
    uint8_t *v_plane = NULL;

    const int uv_width = width >> 1;

    if (i420) {
        const size_t y_size = (size_t)width * height;
        const size_t uv_size = (size_t)uv_width * (height >> 1);

        y_plane = i420;
        u_plane = i420 + y_size;
        v_plane = u_plane + uv_size;
    }

    /*
     * Process one 2x2 Bayer block at a time.
     *
     * Each block:
     *
     *     R G
     *     G B
     *
     * We calculate the RGB values of all four pixels.
     *
     * Then:
     *
     *     Y is generated for each pixel
     *
     *     U/V are generated from the average RGB
     *     of the 2x2 block
     */

    for (int y = 0; y < height; y += 2) {

        const int y0 = y;
        const int y1 = (y + 1 < height) ? y + 1 : y;

        /*
         * Row pointers with edge replication.
         */
        const uint16_t *row_m1 =
            (y0 > 0) ? raw + (size_t)(y0 - 1) * width
                     : raw + (size_t)y0 * width;

        const uint16_t *row_0 =
            raw + (size_t)y0 * width;

        const uint16_t *row_1 =
            raw + (size_t)y1 * width;

        const uint16_t *row_p2 =
            (y + 2 < height) ? raw + (size_t)(y + 2) * width
                             : row_1;

        uint8_t *y_row_0 =
            y_plane ? y_plane + (size_t)y0 * width : NULL;

        uint8_t *y_row_1 =
            y_plane ? y_plane + (size_t)y1 * width : NULL;

        uint8_t *rgb_row_0 =
            rgb ? rgb + (size_t)y0 * width * 3 : NULL;

        uint8_t *rgb_row_1 =
            rgb ? rgb + (size_t)y1 * width * 3 : NULL;

        uint8_t *u_row =
            u_plane ? u_plane + (size_t)(y >> 1) * uv_width : NULL;

        uint8_t *v_row =
            v_plane ? v_plane + (size_t)(y >> 1) * uv_width : NULL;

        for (int x = 0; x < width; x += 2) {

            /*
             * ----------------------------------------------------
             * Bayer block:
             *
             *       x       x+1
             *
             * y       R        G
             *
             * y+1     G        B
             *
             * ----------------------------------------------------
             */

            const int xm1 = (x > 0) ? x - 1 : x;
            const int xp1 = (x + 1 < width) ? x + 1 : x;
            const int xp2 = (x + 2 < width) ? x + 2 : xp1;

            /*
             * ----------------------------------------------------
             * Pixel 0: R at (x, y)
             * ----------------------------------------------------
             */

            int r0 = row_0[x];

            int g0 =
                row_0[xm1] +
                row_0[xp1] +
                row_m1[x] +
                row_1[x];

            g0 >>= 2;

            int b0 =
                row_m1[xm1] +
                row_m1[xp1] +
                row_1[xm1] +
                row_1[xp1];

            b0 >>= 2;

            /*
             * ----------------------------------------------------
             * Pixel 1: G at (x+1, y)
             * ----------------------------------------------------
             */

            int g1 = row_0[xp1];

            int r1 =
                row_0[x] +
                row_0[xp2];

            r1 >>= 1;

            int b1 =
                row_m1[xp1] +
                row_1[xp1];

            b1 >>= 1;

            /*
             * ----------------------------------------------------
             * Pixel 2: G at (x, y+1)
             * ----------------------------------------------------
             */

            int g2 = row_1[x];

            int r2 =
                row_0[x] +
                row_p2[x];

            r2 >>= 1;

            int b2 =
                row_1[xm1] +
                row_1[xp1];

            b2 >>= 1;

            /*
             * ----------------------------------------------------
             * Pixel 3: B at (x+1, y+1)
             * ----------------------------------------------------
             */

            int b3 = row_1[xp1];

            int g3 =
                row_1[x] +
                row_1[xp2] +
                row_0[xp1] +
                row_p2[xp1];

            g3 >>= 2;

            int r3 =
                row_0[x] +
                row_0[xp2] +
                row_p2[x] +
                row_p2[xp2];

            r3 >>= 2;

            /*
             * ----------------------------------------------------
             * Convert 10-bit RGB to 8-bit RGB
             * ----------------------------------------------------
             */

            const int r0_8 = (r0 + 2) >> 2;
            const int g0_8 = (g0 + 2) >> 2;
            const int b0_8 = (b0 + 2) >> 2;

            const int r1_8 = (r1 + 2) >> 2;
            const int g1_8 = (g1 + 2) >> 2;
            const int b1_8 = (b1 + 2) >> 2;

            const int r2_8 = (r2 + 2) >> 2;
            const int g2_8 = (g2 + 2) >> 2;
            const int b2_8 = (b2 + 2) >> 2;

            const int r3_8 = (r3 + 2) >> 2;
            const int g3_8 = (g3 + 2) >> 2;
            const int b3_8 = (b3 + 2) >> 2;

            /*
             * ----------------------------------------------------
             * RGB output
             * ----------------------------------------------------
             */

            if (rgb) {
                uint8_t *p0 = rgb_row_0 + (size_t)x * 3;
                uint8_t *p1 = p0 + 3;

                uint8_t *p2 = rgb_row_1 + (size_t)x * 3;
                uint8_t *p3 = p2 + 3;

                p0[0] = (uint8_t)r0_8;
                p0[1] = (uint8_t)g0_8;
                p0[2] = (uint8_t)b0_8;

                p1[0] = (uint8_t)r1_8;
                p1[1] = (uint8_t)g1_8;
                p1[2] = (uint8_t)b1_8;

                p2[0] = (uint8_t)r2_8;
                p2[1] = (uint8_t)g2_8;
                p2[2] = (uint8_t)b2_8;

                p3[0] = (uint8_t)r3_8;
                p3[1] = (uint8_t)g3_8;
                p3[2] = (uint8_t)b3_8;
            }

            /*
             * ----------------------------------------------------
             * Y output
             * ----------------------------------------------------
             */

            if (i420) {

                y_row_0[x] =
                    clamp_u8_int(
                        ((66 * r0_8 +
                          129 * g0_8 +
                          25 * b0_8 +
                          128) >> 8) + 16);

                y_row_0[x + 1] =
                    clamp_u8_int(
                        ((66 * r1_8 +
                          129 * g1_8 +
                          25 * b1_8 +
                          128) >> 8) + 16);

                y_row_1[x] =
                    clamp_u8_int(
                        ((66 * r2_8 +
                          129 * g2_8 +
                          25 * b2_8 +
                          128) >> 8) + 16);

                y_row_1[x + 1] =
                    clamp_u8_int(
                        ((66 * r3_8 +
                          129 * g3_8 +
                          25 * b3_8 +
                          128) >> 8) + 16);
            }

            /*
             * ----------------------------------------------------
             * U/V output
             * ----------------------------------------------------
             *
             * Average the 2x2 RGB block.
             *
             * Important:
             *
             *     Average RGB first
             *     Then convert to U/V
             *
             * This is equivalent to the original implementation.
             */

            if (i420) {

                const int r_avg =
                    (r0_8 + r1_8 + r2_8 + r3_8) >> 2;

                const int g_avg =
                    (g0_8 + g1_8 + g2_8 + g3_8) >> 2;

                const int b_avg =
                    (b0_8 + b1_8 + b2_8 + b3_8) >> 2;

                const int U =
                    ((-38 * r_avg -
                       74 * g_avg +
                      112 * b_avg +
                      128) >> 8) + 128;

                const int V =
                    ((112 * r_avg -
                       94 * g_avg -
                       18 * b_avg +
                      128) >> 8) + 128;

                const int uv_x = x >> 1;

                u_row[uv_x] = clamp_u8_int(U);
                v_row[uv_x] = clamp_u8_int(V);
            }
        }
    }
}

