/* $Id$
 *
 * SPDX-License-Identifier: MIT
 *
 * @author joelai
 *
 * @file /algae-bp/package/tester1/rg10_rgb.c
 * @brief rg10_rgb
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <aloe/util_img.h>

#define WIDTH   3280
#define HEIGHT  2464

#define I420_WIDTH   (WIDTH / 2)
#define I420_HEIGHT  (HEIGHT / 2)

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
/* BMP structures                                             */
/* --------------------------------------------------------- */

#pragma pack(push, 1)

typedef struct {
	uint16_t bfType;
	uint32_t bfSize;
	uint16_t bfReserved1;
	uint16_t bfReserved2;
	uint32_t bfOffBits;
} BMPFileHeader;

typedef struct {
	uint32_t biSize;
	int32_t biWidth;
	int32_t biHeight;
	uint16_t biPlanes;
	uint16_t biBitCount;
	uint32_t biCompression;
	uint32_t biSizeImage;
	int32_t biXPelsPerMeter;
	int32_t biYPelsPerMeter;
	uint32_t biClrUsed;
	uint32_t biClrImportant;
} BMPInfoHeader;

#pragma pack(pop)

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

static uint16_t get_pixel(const uint16_t *raw, int x, int y) {
	/*
	 * Clamp coordinates at image boundary.
	 */

	if (x < 0) x = 0;

	if (x >= WIDTH) x = WIDTH - 1;

	if (y < 0) y = 0;

	if (y >= HEIGHT) y = HEIGHT - 1;

	return extract_raw10(raw[y * WIDTH + x]);
}

/* --------------------------------------------------------- */
/* Bilinear Bayer RGGB demosaicing                           */
/* --------------------------------------------------------- */

static void demosaic_rggb(const uint16_t *raw, uint8_t *rgb) {
	int x;
	int y;

	for (y = 0; y < HEIGHT; y++) {

		for (x = 0; x < WIDTH; x++) {

			uint16_t r;
			uint16_t g;
			uint16_t b;

			/*
			 * RGGB Bayer pattern:
			 *
			 * y even, x even: R
			 * y even, x odd : G
			 * y odd,  x even: G
			 * y odd,  x odd : B
			 */

			int row_even = ((y & 1) == 0);
			int col_even = ((x & 1) == 0);

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

				r = get_pixel(raw, x, y);

				g = (get_pixel(raw, x - 1, y) + get_pixel(raw, x + 1, y)
						+ get_pixel(raw, x, y - 1) + get_pixel(raw, x, y + 1))
						/ 4;

				b = (get_pixel(raw, x - 1, y - 1) + get_pixel(raw, x + 1, y - 1)
						+ get_pixel(raw, x - 1, y + 1)
						+ get_pixel(raw, x + 1, y + 1)) / 4;
			}

			else if (row_even && !col_even) {

				/*
				 * G pixel on R row
				 *
				 *     R G R
				 *       B
				 */

				g = get_pixel(raw, x, y);

				r = (get_pixel(raw, x - 1, y) + get_pixel(raw, x + 1, y)) / 2;

				b = (get_pixel(raw, x, y - 1) + get_pixel(raw, x, y + 1)) / 2;
			}

			else if (!row_even && col_even) {

				/*
				 * G pixel on B row
				 *
				 *       R
				 *     B G B
				 */

				g = get_pixel(raw, x, y);

				r = (get_pixel(raw, x, y - 1) + get_pixel(raw, x, y + 1)) / 2;

				b = (get_pixel(raw, x - 1, y) + get_pixel(raw, x + 1, y)) / 2;
			}

			else {

				/*
				 * B pixel
				 *
				 *       R
				 *     G B G
				 *       G
				 */

				b = get_pixel(raw, x, y);

				g = (get_pixel(raw, x - 1, y) + get_pixel(raw, x + 1, y)
						+ get_pixel(raw, x, y - 1) + get_pixel(raw, x, y + 1))
						/ 4;

				r = (get_pixel(raw, x - 1, y - 1) + get_pixel(raw, x + 1, y - 1)
						+ get_pixel(raw, x - 1, y + 1)
						+ get_pixel(raw, x + 1, y + 1)) / 4;
			}

			/*
			 * Convert 10-bit RGB values to 8-bit RGB.
			 */

			rgb[(y * WIDTH + x) * 3 + 0] = raw10_to_u8(r);
			rgb[(y * WIDTH + x) * 3 + 1] = raw10_to_u8(g);
			rgb[(y * WIDTH + x) * 3 + 2] = raw10_to_u8(b);
		}
	}
}

/*
 * Convert RGB888 to planar I420
 *
 * Memory layout:
 *
 *     Y plane: WIDTH     x HEIGHT
 *     U plane: WIDTH/2   x HEIGHT/2
 *     V plane: WIDTH/2   x HEIGHT/2
 *
 * x264 CSP_I420 corresponds to:
 *
 *     YYYYYYYYYYYYY
 *     UUUUUUUUUUUUU
 *     VVVVVVVVVVVVV
 */
static void rgb_to_i420( const uint8_t *rgb, uint8_t *i420 ) {
    uint8_t *y_plane;
    uint8_t *u_plane;
    uint8_t *v_plane;

    int x;
    int y;

    /*
     * Plane offsets
     */

    size_t y_size =
        WIDTH * HEIGHT;

    size_t uv_size =
        (WIDTH / 2) * (HEIGHT / 2);

    y_plane = i420;

    u_plane = i420 + y_size;

    v_plane = i420 + y_size + uv_size;


    /*
     * Convert RGB to Y plane
     *
     * BT.601 limited-range conversion:
     *
     * Y =  0.257 R + 0.504 G + 0.098 B + 16
     */
    for (y = 0; y < HEIGHT; y++) {

        for (x = 0; x < WIDTH; x++) {

            const uint8_t *pixel =
                &rgb[(y * WIDTH + x) * 3];

            int r = pixel[0];
            int g = pixel[1];
            int b = pixel[2];

            int Y =
                ((66 * r +
                  129 * g +
                  25 * b +
                  128) >> 8) + 16;

            y_plane[y * WIDTH + x] =
                clamp_u8(Y);
        }
    }


    /*
     * Convert RGB to U/V planes.
     *
     * 2x2 chroma subsampling:
     *
     *       Pixel 0    Pixel 1
     *       Pixel 2    Pixel 3
     *
     * One U and one V value are generated
     * for every 2x2 RGB block.
     */
    for (y = 0; y < HEIGHT; y += 2) {

        for (x = 0; x < WIDTH; x += 2) {

            int r_sum = 0;
            int g_sum = 0;
            int b_sum = 0;

            int dx;
            int dy;

            /*
             * Average the four pixels in the 2x2 block.
             */
            for (dy = 0; dy < 2; dy++) {

                for (dx = 0; dx < 2; dx++) {

                    const uint8_t *pixel =
                        &rgb[
                            ((y + dy) * WIDTH +
                             (x + dx)) * 3
                        ];

                    r_sum += pixel[0];
                    g_sum += pixel[1];
                    b_sum += pixel[2];
                }
            }

            int r = r_sum / 4;
            int g = g_sum / 4;
            int b = b_sum / 4;


            /*
             * BT.601 limited-range conversion:
             *
             * U = -0.148 R - 0.291 G + 0.439 B + 128
             * V =  0.439 R - 0.368 G - 0.071 B + 128
             */
            int U =
                ((-38 * r -
                  74 * g +
                  112 * b +
                  128) >> 8) + 128;

            int V =
                ((112 * r -
                  94 * g -
                  18 * b +
                  128) >> 8) + 128;


            int uv_x = x / 2;
            int uv_y = y / 2;

            u_plane[
                uv_y * (WIDTH / 2) + uv_x
            ] = clamp_u8(U);

            v_plane[
                uv_y * (WIDTH / 2) + uv_x
            ] = clamp_u8(V);
        }
    }
}

/* --------------------------------------------------------- */
/* Save RGB888 raw file                                      */
/* --------------------------------------------------------- */

static int save_rgb(const char *filename, const uint8_t *rgb) {
	FILE *fp;

	fp = fopen(filename, "wb");

	if (!fp) {
		perror(filename);
		return -1;
	}

	fwrite(rgb, 1,
	WIDTH * HEIGHT * 3, fp);

	fclose(fp);

	return 0;
}

/* --------------------------------------------------------- */
/* Save 24-bit BMP                                           */
/* --------------------------------------------------------- */

static int save_bmp(const char *filename, const uint8_t *rgb) {
	FILE *fp;

	BMPFileHeader file_header;
	BMPInfoHeader info_header;

	/*
	 * Each BMP row must be aligned to 4 bytes.
	 *
	 * RGB888 row size:
	 *
	 *     WIDTH * 3
	 */

	int row_size = WIDTH * 3;

	int padding = (4 - (row_size % 4)) % 4;

	int bmp_row_size = row_size + padding;

	uint32_t image_size = bmp_row_size * HEIGHT;

	memset(&file_header, 0, sizeof(file_header));
	memset(&info_header, 0, sizeof(info_header));

	/*
	 * BMP file header
	 */

	file_header.bfType = 0x4D42; /* "BM" */

	file_header.bfOffBits = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader);

	file_header.bfSize = file_header.bfOffBits + image_size;

	/*
	 * BMP information header
	 */

	info_header.biSize = sizeof(BMPInfoHeader);

	info_header.biWidth =
	WIDTH;

	/*
	 * Positive height means bottom-up BMP.
	 */

	info_header.biHeight =
	HEIGHT;

	info_header.biPlanes = 1;

	info_header.biBitCount = 24;

	info_header.biCompression = 0; /* BI_RGB */

	info_header.biSizeImage = image_size;

	fp = fopen(filename, "wb");

	if (!fp) {
		perror(filename);
		return -1;
	}

	fwrite(&file_header, sizeof(file_header), 1, fp);

	fwrite(&info_header, sizeof(info_header), 1, fp);

	/*
	 * BMP stores pixels as BGR,
	 * not RGB.
	 *
	 * BMP also stores rows bottom-to-top.
	 */

	for (int y = HEIGHT - 1; y >= 0; y--) {

		for (int x = 0; x < WIDTH; x++) {

			const uint8_t *pixel = &rgb[(y * WIDTH + x) * 3];

			uint8_t bgr[3];

			bgr[0] = pixel[2]; /* B */
			bgr[1] = pixel[1]; /* G */
			bgr[2] = pixel[0]; /* R */

			fwrite(bgr, 1, 3, fp);
		}

		/*
		 * Row padding
		 */

		for (int i = 0; i < padding; i++) {

			fputc(0, fp);
		}
	}

	fclose(fp);

	return 0;
}

/*
 * Save planar I420 file
 */
static int save_i420( const char *filename, const uint8_t *i420 ) {
    FILE *fp;

    size_t i420_size = WIDTH * HEIGHT +
        (WIDTH / 2) * (HEIGHT / 2) +
        (WIDTH / 2) * (HEIGHT / 2);


    fp = fopen(filename, "wb");

    if (!fp) {
        perror(filename);
        return -1;
    }


    if (fwrite(
            i420,
            1,
            i420_size,
            fp
        ) != i420_size) {

        perror("fwrite");

        fclose(fp);

        return -1;
    }


    fclose(fp);

    return 0;
}

/* --------------------------------------------------------- */
/* Main                                                       */
/* --------------------------------------------------------- */

int main(int argc, char *argv[]) {
	const char *input_filename;
	char rgb_filename[120];
	char bmp_filename[120];
	char i420_filename[120];
	FILE *fp;
	uint16_t *raw;
	uint8_t *rgb;
	uint8_t *i420;
	size_t raw_pixels = WIDTH * HEIGHT;
	size_t raw_size = raw_pixels * sizeof(uint16_t);
	size_t i420_size = WIDTH * HEIGHT +
	    (WIDTH / 2) * (HEIGHT / 2) +
	    (WIDTH / 2) * (HEIGHT / 2);
	size_t read_count;
	int r;

	if (argc != 3) {
		// output.rgb, .bmp, .i420
		fprintf( stderr, "Usage: %s <input.raw> <output base name>\n", argv[0]);
		return EXIT_FAILURE;
	}

	input_filename = argv[1];
	if ((r = snprintf(rgb_filename, sizeof(rgb_filename), "%s%s", argv[2], ".rgb")) <= 0
			|| r >= sizeof(rgb_filename)) {
		printf("Insufficient buffer to compose output filename\n");
		return EXIT_FAILURE;
	}

	if ((r = snprintf(bmp_filename, sizeof(bmp_filename), "%s%s", argv[2], ".bmp")) <= 0
			|| r >= sizeof(bmp_filename)) {
		printf("Insufficient buffer to compose output filename\n");
		return EXIT_FAILURE;
	}

	if ((r = snprintf(i420_filename, sizeof(i420_filename), "%s%s", argv[2], ".i420")) <= 0
			|| r >= sizeof(i420_filename)) {
		printf("Insufficient buffer to compose output filename\n");
		return EXIT_FAILURE;
	}
	printf("Image size       : %d x %d\n", WIDTH, HEIGHT);
	printf("Expected raw size: %zu bytes\n", raw_size);

	/*
	 * Allocate RAW buffer
	 */
	if ((raw = (uint16_t*)malloc(raw_size)) == NULL) {
		perror("malloc raw");
		return EXIT_FAILURE;
	}

	/*
	 * Allocate RGB888 buffer
	 */
	if ((rgb = (uint8_t*)malloc( WIDTH * HEIGHT * 3)) == NULL) {
		perror("malloc rgb");
		free(raw);
		return EXIT_FAILURE;
	}

	/*
	 * Allocate I420 buffer
	 *
	 * Y: WIDTH     x HEIGHT
	 * U: WIDTH/2   x HEIGHT/2
	 * V: WIDTH/2   x HEIGHT/2
	 */
	if ((i420 = (uint8_t*)malloc(i420_size)) == NULL) {
	    perror("malloc i420");
	    free(raw);
	    free(rgb);
	    return EXIT_FAILURE;
	}

	/*
	 * Open RAW file
	 */
	if ((fp = fopen(input_filename, "rb")) == NULL) {
		perror(input_filename);
		free(raw);
		free(rgb);
		return EXIT_FAILURE;
	}

	/*
	 * Read complete RAW image
	 */
	read_count = fread(raw, sizeof(uint16_t), raw_pixels, fp);
	fclose(fp);
	if (read_count != raw_pixels) {
		fprintf(stderr, "ERROR: expected %zu pixels, read %zu pixels\n",
				raw_pixels, read_count);
		free(raw);
		free(rgb);
		return EXIT_FAILURE;
	}

	printf("Converting RGGB RAW10 to RGB888...\n");

#if 1
	/*
	 * Demosaic
	 */
	aloe_rggb10_to_rgb888_i420(WIDTH, HEIGHT, raw, i420, rgb);
#else
	/*
	 * Demosaic
	 */
	demosaic_rggb(raw, rgb);

	printf( "Converting RGB888 to I420...\n" );
	rgb_to_i420( rgb, i420 );
#endif
	printf("Conversion complete.\n");

	/*
	 * Save RGB888
	 */
	if (save_rgb(rgb_filename, rgb) < 0) {
		free(raw);
		free(rgb);
		free(i420);
		return EXIT_FAILURE;
	}
	printf("RGB output: %s\n", rgb_filename);

	/*
	 * Save BMP
	 */
	if (save_bmp(bmp_filename, rgb) < 0) {
		free(raw);
		free(rgb);
		free(i420);
		return EXIT_FAILURE;
	}
	printf("BMP output: %s\n", bmp_filename);

	if (save_i420( i420_filename, i420 ) < 0) {
		free(raw);
		free(rgb);
		free(i420);
		return EXIT_FAILURE;
	}
	printf("I420 output: %s\n", i420_filename);
/*
For your 3280 × 2464 image:
plane[0] = Y
plane[1] = U
plane[2] = V

uint8_t *y_plane = i420;
uint8_t *u_plane = i420 + 3280 * 2464;
uint8_t *v_plane = u_plane + 1640 * 1232;

x264_picture_t pic;
pic.img.plane[0] = y_plane;
pic.img.plane[1] = u_plane;
pic.img.plane[2] = v_plane;
pic.img.i_stride[0] = 3280;
pic.img.i_stride[1] = 1640;
pic.img.i_stride[2] = 1640;
 */

	free(raw);
	free(rgb);
	free(i420);
	return EXIT_SUCCESS;
}

