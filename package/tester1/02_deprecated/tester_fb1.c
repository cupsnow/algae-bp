/* $Id$
 *
 * Copyright 2024, Dexatek Technology Ltd.
 * This is proprietary information of Dexatek Technology Ltd.
 * All Rights Reserved. Reproduction of this documentation or the
 * accompanying programs in any manner whatsoever without the written
 * permission of Dexatek Technology Ltd. is strictly forbidden.
 *
 * @author joelai
 */

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include "tester_main.h"

typedef struct {
	int fd;
	struct fb_fix_screeninfo fbfinfo;
	struct fb_var_screeninfo fbvinfo;
	void *fbmem;
	size_t fbmem_sz, stride;
} lfb_t;

typedef struct __attribute__((packed)) tagBITMAPFILEHEADER {
  uint16_t  bfType;
  uint32_t bfSize;
  uint16_t  bfReserved1;
  uint16_t  bfReserved2;
  uint32_t bfOffBits;
} BITMAPFILEHEADER_t;

#define BMP_TYPE_BM ('B' | 'M' << 8)
#define FOURCC(_a, _b, _c, _d) ( \
		((_a) & 0xff) \
		| ((_b) & 0xff) << 8 \
		| ((_c) & 0xff) << 16 \
		| ((_d) & 0xff) << 24)
#define FOURCC_YUV2 FOURCC('Y', 'U', 'V', '2')
#define BMP_BI_RGB 0
#define BMP_BI_BITFIELDS 3

typedef struct __attribute__((packed)) tagBITMAPINFOHEADER {
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
} BITMAPINFOHEADER_t;

typedef struct {
	aloe_mmap_t mm;
	void *preload, *bdata;
	const BITMAPFILEHEADER_t *fhdr;
	const BITMAPINFOHEADER_t *bhdr;
	size_t stride;
} bmp_t;

#define FBDEV_DEF "/dev/fb0"

#define BMP_PRELOAD_AUTO (1048576 * 5)

static unsigned long opt_defer_close = 0;
static int opt_bmp_preload = -1;
static int opt_bmp_height = -1;

static lfb_t* lfb_reset(lfb_t *lfb) {
	lfb->fd = -1;
	lfb->fbmem = MAP_FAILED;
	return lfb;
}

static void lfb_close(int fd, lfb_t *lfb) {
	int r;

	if (lfb) {
		if (lfb->fbmem && lfb->fbmem != MAP_FAILED) {
			if ((r = munmap(lfb->fbmem, lfb->fbmem_sz)) != 0) {
				r = errno;
				log_e("Failed munmap for framebuffer %s\n", strerror(r));
			}
		}
		lfb->fbmem = MAP_FAILED;

		if (lfb->fd != -1) {
			if (lfb->fd == fd) fd = -1;
			fd_gc(lfb->fd);
		}
	}
	fd_gc(fd);
}

static int lfb_open(const char *dev, lfb_t *lfb) {
	int r, fd = -1;
	size_t fbsz;

	if ((fd = open(dev, O_RDWR)) == -1) {
		r = errno;
		log_e("Failed open framebuffer %s\n", strerror(r));
		goto finally;
	}
	if (lfb) {
	    if (ioctl(fd, FBIOGET_FSCREENINFO, &lfb->fbfinfo)) {
			r = errno;
			log_e("Failed FBIOGET_FSCREENINFO %s\n", strerror(r));
			goto finally;
		}
	    if (ioctl(fd, FBIOGET_VSCREENINFO, &lfb->fbvinfo)) {
			r = errno;
			log_e("Failed FBIOGET_VSCREENINFO %s\n", strerror(r));
			goto finally;
		}
	    // lfb->fbvinfo.xres * lfb->fbvinfo.yres * lfb->fbvinfo.bits_per_pixel / 8;
		lfb->fbmem_sz = lfb->fbfinfo.smem_len;
		if ((lfb->fbmem = mmap(0, lfb->fbmem_sz, PROT_READ | PROT_WRITE,
				MAP_SHARED, fd, 0)) == MAP_FAILED) {
			r = errno;
			log_e("Failed mmap for framebuffer %s\n", strerror(r));
			goto finally;
		}
		lfb->fd = fd;

		lfb->stride = (lfb->fbvinfo.bits_per_pixel + 7) / 8 * lfb->fbvinfo.xres;
		if (lfb->fbvinfo.colorspace) {
			log_d("fb fourcc: %c%c%c%c\n",
					lfb->fbvinfo.colorspace & 0xff,
					(lfb->fbvinfo.colorspace >> 8) & 0xff,
					(lfb->fbvinfo.colorspace >> 16) & 0xff,
					(lfb->fbvinfo.colorspace >> 24) & 0xff);
		}
	}
	r = 0;
finally:
	if (r != 0) {
		fd_gc(fd);
		return -1;
	}
	return fd;
}

static bmp_t* bmp_reset(bmp_t *bmp) {
	aloe_mmap_reset(&bmp->mm);
	bmp->preload = NULL;
	return bmp;
}

static void bmp_close(bmp_t *bmp) {
	if (bmp) {
		aloe_munmap(&bmp->mm);
		if (bmp->preload) {
			mem_gc(bmp->preload);
		}
	}
}

static int bmp_open(const char *dev, bmp_t *bmp, char preload) {
	int r;
	size_t bmpsz;
	struct stat st;

	if (preload != 0) {
		if ((r = stat(dev, &st)) != 0) {
			r = errno;
			log_e("Failed get file stat %s, %s\n", dev, strerror(r));
			goto finally;
		}
		if (preload < 0 && st.st_size < BMP_PRELOAD_AUTO) {
			preload = 1;
		}
	}

	if (preload) {
		size_t preload_sz;

		log_d("BMP preload\n");
		if (!(bmp->preload = malloc(preload_sz = st.st_size + 32))) {
			r = errno;
			log_e("Failed alloc for bmp\n");
			goto finally;
		}
		if (read_file(dev, bmp->preload, preload_sz) != st.st_size) {
			r = -1;
			log_e("Failed preload bmp\n");
			goto finally;
		}
		bmp->fhdr = (BITMAPFILEHEADER_t*)bmp->preload;
	} else {
		if (aloe_mmap_file(dev, aloe_mmap_reset(&bmp->mm)) != 0) {
			r = errno;
			log_e("Failed open %s, %s\n", dev, strerror(r));
			goto finally;
		}
		bmp->fhdr = (BITMAPFILEHEADER_t*)bmp->mm.fmem;
	}
	bmp->bhdr = (BITMAPINFOHEADER_t*)(bmp->fhdr + 1);
	if (bmp->fhdr->bfType != BMP_TYPE_BM
			|| (bmp->bhdr->biCompression != BMP_BI_RGB
					&& bmp->bhdr->biCompression != BMP_BI_BITFIELDS)
			|| bmp->bhdr->biClrUsed != 0
			|| bmp->bhdr->biBitCount != 32) {
		r = EINVAL;
		log_e("Suport only uncompressed ms-bmp, no color table, 32 bits per pixel\n");
		goto finally;
	}
	bmp->stride = ((((abs(bmp->bhdr->biWidth) * bmp->bhdr->biBitCount) + 31) & ~31) >> 3);
	bmp->bdata = (char*)bmp->bhdr + bmp->bhdr->biSize;
	r = 0;
finally:
	if (r != 0) {
		bmp_close(bmp);
		return -1;
	}
	return 0;
}

static int lfb_draw_bmp(lfb_t *lfb, bmp_t *bmp) {
	int i;
	const uint8_t *bmp_row;
	uint8_t *lfb_row;
	size_t row_sz = min(bmp->stride, lfb->stride), bmp_stride, bmp_height;

	if (opt_bmp_height > 0) {
		bmp_height = min(abs(bmp->bhdr->biHeight), opt_bmp_height);
	} else {
		bmp_height = abs(bmp->bhdr->biHeight);
	}
	if (bmp->bhdr->biHeight > 0) {
		// bottom-up
		bmp_row = (char*)bmp->bdata + bmp->stride * bmp->bhdr->biHeight - bmp->stride;
		bmp_stride = -bmp->stride;
	} else {
		// top-down
		bmp_row = (char*)bmp->bdata;
		bmp_stride = bmp->stride;
	}

	for (i = 0, lfb_row = lfb->fbmem;
			i < bmp_height && i < lfb->fbvinfo.yres;
			i++, lfb_row += lfb->stride, bmp_row += bmp_stride) {
		memcpy(lfb_row, bmp_row, row_sz);
	}
	return 0;
}

#define _BMP_RESOLUTION_INFO_ADDR_ (0x0a)
#define _BMP_RESOLUTION_INFO_SIZE_ (7)
#define _BMP_INFO_BYTE_PER_DATA_ (4)
int img_line_width_byte = 0;
int img_pixel_width_byte = 0;
int fb_x_offset_byte_left = 0;
int fb_x_offset_byte_right = 0;
int fb_y_offset_line = 0;

void lcd_put_pixel(unsigned int x, unsigned int y, unsigned int color,
		lfb_t *lfb) {
	unsigned char *pen_8 = (unsigned char*)lfb->fbmem + fb_x_offset_byte_left
			+ fb_y_offset_line * lfb->fbvinfo.xres * img_pixel_width_byte
			+ y * (img_line_width_byte + fb_x_offset_byte_right
					+ fb_x_offset_byte_left) + x * img_pixel_width_byte;
	unsigned int *pen_32 = (unsigned int*)pen_8;
	*pen_32 = color;
}

int draw_bmp(const char *opt_bmp, lfb_t *lfb) {
	int r;
	FILE *image = NULL;
	unsigned int header[_BMP_RESOLUTION_INFO_SIZE_];
	unsigned int hdr_sz = 0;
	unsigned char BMP_bottom_up = 1;
	int img_width_pixel = 0;
	int img_height_pixel = 0;
	unsigned int img_bits_per_pixel = 0;
	unsigned int img_sz_pixel = 0;
	unsigned int img_x_pixel = 0;
	unsigned int img_y_pixel = 0;
	unsigned int *buffer = NULL;

	if (!(image = fopen(opt_bmp, "rb"))) {
		log_e("Failed open %s", opt_bmp);
		r = -1;
		goto finally;
	}

	fseek(image, _BMP_RESOLUTION_INFO_ADDR_, SEEK_SET); // Header sz position
	fread(header, _BMP_INFO_BYTE_PER_DATA_, _BMP_RESOLUTION_INFO_SIZE_, image);
	hdr_sz = header[0];
	img_width_pixel = header[2];
	img_height_pixel = header[3];
	img_bits_per_pixel = (header[4] >> 16);

	if (img_height_pixel < 0) {
		BMP_bottom_up = 0; // Top down
		img_height_pixel = ~img_height_pixel + 1;
	}
	img_pixel_width_byte = img_bits_per_pixel / 8;
	img_line_width_byte = img_width_pixel * img_pixel_width_byte;
	fb_x_offset_byte_left = ((lfb->fbvinfo.xres - img_width_pixel) / 2)
			* img_pixel_width_byte;
	fb_x_offset_byte_right = (lfb->fbvinfo.xres - img_width_pixel)
			* img_pixel_width_byte - fb_x_offset_byte_left;
	fb_y_offset_line = (lfb->fbvinfo.yres - img_height_pixel) / 2;
	img_sz_pixel = img_width_pixel * img_height_pixel;

	log_d("%s %dx%d, draw (%d, %d)\n", opt_bmp,
			img_width_pixel, img_height_pixel,
			fb_x_offset_byte_left, fb_y_offset_line);

	if (!(buffer = malloc(img_sz_pixel * sizeof(*buffer)))) {
		log_e("Failed alloc image buffer\n");
		r = -1;
		goto finally;
	}

	fseek(image, hdr_sz, SEEK_SET); // Header sz position
    fread(buffer, img_pixel_width_byte, img_sz_pixel, image);

	for (unsigned int i = 0; i < img_sz_pixel; i++) {
		img_x_pixel = i % img_width_pixel;
		img_y_pixel = BMP_bottom_up == 1 ?
				((img_height_pixel - 1) - i / img_width_pixel) :
				i / img_width_pixel;
		lcd_put_pixel(img_x_pixel, img_y_pixel, buffer[i], lfb);
	}

	log_d("fb yoffset %d\n", lfb->fbvinfo.yoffset);
	if ((ioctl(lfb->fd, FBIOPAN_DISPLAY, &lfb->fbvinfo)) != 0) {
		r = errno;
		log_e("FBIOPAN_DISPLAY, %s\n", strerror(r));
	}

	r = 0;
	if ((ioctl(lfb->fd, FBIO_WAITFORVSYNC, &r)) != 0) {
		r = errno;
		log_e("FBIO_WAITFORVSYNC, %s\n", strerror(r));
	}
	usleep(opt_defer_close);
finally:
	if (image) fclose(image);
	if (buffer) free(buffer);
	return r;
}

static const char opt_short[] = "hc:d:";
enum {
	opt_key_null = 0x200,
	opt_key_defer_close,
	opt_key_bmp_preload,
	opt_key_bmp_height,
};
static struct option opt_long[] = {
	{"help", no_argument, NULL, 'h'},
	{"color", required_argument, NULL, 'c'},
	{"fbdev", required_argument, NULL, 'd'},
	{"defer_close", required_argument, NULL, opt_key_defer_close},
	{"bmp_preload", optional_argument, NULL, opt_key_bmp_preload},
	{"bmp_height", required_argument, NULL, opt_key_bmp_height},
	{NULL}
};

static void show_help(const char *prog) {

	fprintf(stdout,
"COMMAND\n"
"    %s [OPTIONS] [BMP FILE]\n"
"\n"
"OPTIONS\n"
"    -h, --help           Show help\n"
"    -c,--color=HEXCOLOR  Fill hex color\n"
"    -d,--fbdev=FBDEV     Framebuffer device [default: %s]\n"
"    --defer_close=US     Delay before close [default: %lu]\n"
"    --bmp_preload=[0|1]  Preload BMP [default: Auto]\n"
"    --bmp_height=NUM     BMP draw height [default: Auto]\n"
"\n"
"DESCRIPTION\n"
"  BMP preload auto enable when bmp file size under %d bytes\n"
"\n", (prog ? prog : "APPLICATION"), FBDEV_DEF, opt_defer_close,
		BMP_PRELOAD_AUTO);
}

static int run(int level, int argc, const char **argv) {
	int r;
	lfb_t _lfb, *lfb = NULL;
	bmp_t _bmp, *bmp = NULL;
	int opt_op, opt_idx, opt_color = -1;
	const char *opt_fbdev = FBDEV_DEF, *opt_bmp = NULL;

	optind = 0;
	while ((opt_op = getopt_long(argc, (char *const*)argv, opt_short, opt_long,
			&opt_idx)) != -1) {
		if (opt_op == 'h') {
			show_help(argc > 0 ? argv[0] : NULL);
			return 1;
		}
		if (opt_op == 'c') {
			opt_color = strtol(optarg, NULL, 0);
			continue;
		}
		if (opt_op == 'd') {
			opt_fbdev = optarg;
			continue;
		}
		if (opt_op == opt_key_defer_close) {
			opt_defer_close = strtol(optarg, NULL, 0);
			continue;
		}
		if (opt_op == opt_key_bmp_preload) {
			if (optarg) {
				opt_bmp_preload = strtol(optarg, NULL, 0);
			} else {
				opt_bmp_preload = 1;
			}
			continue;
		}
		if (opt_op == opt_key_bmp_height) {
			opt_bmp_height = strtol(optarg, NULL, 0);
		}
	}

	for (r = optind; r < argc; r++) {
		log_d("argv[%d/%d]: %s\n", r + 1, argc, argv[r]);
	}
	if (optind < argc) {
		opt_bmp = argv[optind];
	}

	if (lfb_open(opt_fbdev, lfb_reset(&_lfb)) == -1) {
		log_e("Failed open fbdev\n");
		r = -1;
		goto finally;
	}
	lfb = &_lfb;

	if (opt_color != -1) {
		if (opt_color >= 0 && opt_color <= 255) {
			memset(lfb->fbmem, opt_color, lfb->fbmem_sz);
		} else {
			log_e("Not implement fill RGB color\n");
		}
	}

	if (opt_bmp) {
//		draw_bmp(opt_bmp, lfb);
		if ((r = bmp_open(opt_bmp, bmp_reset(&_bmp), opt_bmp_preload)) != 0) {
			log_e("Failed open %s\n", opt_bmp);
			goto finally;
		}
		bmp = &_bmp;
		lfb_draw_bmp(lfb, bmp);
	}

	r = 0;
finally:
	if (lfb) lfb_close(lfb->fd, lfb);
	if (bmp) bmp_close(bmp);
	return r;
}

static tester_test_t TESTER_SECTION_ATTR(_tester_section) test_case = {
	.name = "Test template1",
	.run = &run,
};

__attribute__((weak))
int main(int argc, const char **argv);
int main(int argc, const char **argv) {
	return (*test_case.run)(1, argc, argv);
}
