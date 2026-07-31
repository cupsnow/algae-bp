#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "log.h"
#include <aloe/sys.h>

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

extern "C"
int aloe_bmp_save(const char *filename, int width, int height, const uint8_t *rgb) {
	int r;
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

	int row_size = width * 3;

	int padding = (4 - (row_size % 4)) % 4;

	int bmp_row_size = row_size + padding;

	uint32_t image_size = bmp_row_size * height;

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
	width;

	/*
	 * Positive height means bottom-up BMP.
	 */

	info_header.biHeight =
	height;

	info_header.biPlanes = 1;

	info_header.biBitCount = 24;

	info_header.biCompression = 0; /* BI_RGB */

	info_header.biSizeImage = image_size;

	fp = fopen(filename, "wb");

	if (!fp) {
		r = errno;
		aloe_log_e("%s\n", strerror(r));
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

	for (int y = height - 1; y >= 0; y--) {

		for (int x = 0; x < width; x++) {

			const uint8_t *pixel = &rgb[(y * width + x) * 3];

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
