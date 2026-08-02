/*
 *
 */
#include <chrono>

#include <stdio.h>
#include <stdint.h>
// #include <arm_neon.h>
#include <fcntl.h>
#include <unistd.h>
#include <aloe/util_img.h>

#include "priv.h"


//#define log_m(_lvl, _fmt, _args...) printf("[" _lvl "][%s][#%d] " _fmt, __func__, __LINE__, ##_args)
//#define log_d(_args...) log_m("Debug", _args)
//#define log_e(_args...) log_m("ERROR", _args)

//#define dump_argv(_argc, _argv) for (int i = 0; i < (_argc); i++) { \
//	log_d("argv[%d/%d]: %s\n", i + 1, (_argc), (_argv)[i]); \
//}

static int tester_bmp2(void*, int argc, char **argv) {
	int ret = -1;
	int width = 3280, height = 2464;
	void *mem = NULL;
	aloe_buf_t fb_rgb = {};

	if (argc < 2) {
		log_e("usage: %s <output_file>\n", argv[0]);
		goto finally;
	}

	if ((mem = (void*)malloc(
			width * height * 3)) == NULL) {
		log_e("malloc failed\n");
		goto finally;
	}

	fb_rgb.data = mem;
	fb_rgb.cap = width * height * 3;

	// output colorbar
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			uint8_t *rgb_pos = (uint8_t*)fb_rgb.data + y * width * 3 + x * 3;
			rgb_pos[0] = x * 255 / (width - 1);
			rgb_pos[1] = y * 255 / (height - 1);
			rgb_pos[2] = 0;
		}
	}
	aloe_bmp_save(argv[1], width, height, (uint8_t*)fb_rgb.data);
	ret = 0;

finally:
	if (mem) free(mem);
	return ret;
}

static int clamp_u8(int value) {
	return value > 255 ? 255 : value < 0 ? 0 : value;
}

static void clamp3_u8(int *r, int *g, int *b) {
	*r = clamp_u8(*r);
	*g = clamp_u8(*g);
	*b = clamp_u8(*b);
}

static void rgb_yuv(int r, int g, int b, uint8_t *y, uint8_t *u, uint8_t *v) {
	int out_y, out_u, out_v;
	out_y = clamp_u8((( 66 * r + 129 * g +  25 * b + 128) / 256) +  16);
	out_u = clamp_u8(((-38 * r -  74 * g + 112 * b + 128) / 256) + 128);
	out_v = clamp_u8(((112 * r -  94 * g -  18 * b + 128) / 256) + 128);
	if (y) *y = out_y;
	if (u) *u = out_u;
	if (v) *v = out_v;
}

/*
 * I420
 *
 * Y11(uv11)   Y12(uv11)   Y13(uv12)   Y14(uv12)
 * Y21(uv11)   Y22(uv11)   Y23(uv12)   Y24(uv12)
 * Y31(uv21)   Y32(uv21)   Y33(uv22)   Y34(uv22)
 * Y41(uv21)   Y42(uv21)   Y43(uv22)   Y44(uv22)
 * u11   u12
 * u21   u22
 * v11   v12
 * v21   v22
 *
 * Color RGB to YUV
 * 
 * Y = (( 66 * R + 129 * G +  25 * B + 128) / 256) +  16;
 * U = ((-38 * R -  74 * G + 112 * B + 128) / 256) + 128;
 * V = ((112 * R -  94 * G -  18 * B + 128) / 256) + 128;
 *
 * Y  =     ( 0.257 * R) + (0.504 * G) + (0.098 * B) +  16
 * Cb = U = (-0.148 * R) - (0.291 * G) + (0.439 * B) + 128
 * Cr = V = ( 0.439 * R) - (0.368 * G) - (0.071 * B) + 128
 *
 * Color YUV to RGB
 * 
 * R = 1.164 * (Y - 16) + 1.596 * (V - 128)
 * G = 1.164 * (Y - 16) - 0.813 * (V - 128) - 0.391 * (U - 128)
 * B = 1.164 * (Y - 16)                     + 2.018 * (U - 128)
 */

/** Convert RG10 to RGB888 and I420.
 *
 * Bayer pattern: RGGB, each pixel 10bits, occupied in lower of 16 bits
 *
 * R G R G ...
 * G B G B ...
 * R G R G ...
 * G B G B ...
 *
 * Input RG10 process 8 x 8 pixels block
 *
 * ----+-----+-----+----
 * R G | R G | R G | R G
 * G B | G B | G B | G B
 * ----+-----+-----+----
 * R G | R G | R G | R G
 * G B | G B | G B | G B
 * ----+-----+-----+----
 * R G | R G | R G | R G
 * G B | G B | G B | G B
 * ----+-----+-----+----
 * R G | R G | R G | R G
 * G B | G B | G B | G B
 *
 * Output RGB/I420 4 x 4 pixels block
 *
 * Y11(uv11)   Y12(uv11)   Y13(uv12)   Y14(uv12)
 * Y21(uv11)   Y22(uv11)   Y23(uv12)   Y24(uv12)
 * Y31(uv21)   Y32(uv21)   Y33(uv22)   Y34(uv22)
 * Y41(uv21)   Y42(uv21)   Y43(uv22)   Y44(uv22)
 * u11   u12
 * u21   u22
 * v11   v12
 * v21   v22
 *
 */
static void rg10_rgb_i420_v3(int width, int height, const void *rg10, void *rgb,
		void *i420) {
	uint8_t *y_plane, *u_plane;
	int out_w = width / 2;
	int out_h = height / 2;
	int uv_size = (out_h / 2) * (out_w / 2);
	int y, out_y;

	if ((width & 8) || (height & 8)) {
		log_e("invalid arguments\n");
		return;
	}

	if (i420) {
		y_plane = (uint8_t *)i420;
		u_plane = y_plane + out_w * out_h;
	}

	for (y = 0, out_y = 0; y < height; y += 8, out_y += 4) {
		//
		int x, out_x;
		uint16_t *rg10_row = (uint16_t *)rg10 + width * y;
		uint8_t *y_row, *u_row, *v_row, *rgb_row;

//		log_d("row %d/%d\n", y, height - 1);

		if (i420) {
			y_row = y_plane + out_w * out_y;
			u_row = u_plane + (out_w / 2) * (out_y / 2);
			v_row = u_row + uv_size;
		}

		if (rgb) {
			rgb_row = (uint8_t *)rgb + out_w * out_y * 3;
		}

		for (x = 0, out_x = 0; x < width; x += 8, out_x += 4) {
			uint16_t *rg10_pos = rg10_row + x;
			typedef struct {
				int r, g, b;
			} rgb_t;
			rgb_t rgb_val[4][4] = {};

#define rg10_fetch(_y, _x) \
		rgb_val[_y][_x].r = rg10_pos[((_y) * 2) * width + ((_x) * 2)            ]; \
		rgb_val[_y][_x].g = rg10_pos[((_y) * 2) * width + ((_x) * 2)         + 1]; \
		rgb_val[_y][_x].b = rg10_pos[((_y) * 2) * width + ((_x) * 2) + width + 1];

			rg10_fetch(0, 0); rg10_fetch(0, 1); rg10_fetch(0, 2); rg10_fetch(0, 3);
			rg10_fetch(1, 0); rg10_fetch(1, 1); rg10_fetch(1, 2); rg10_fetch(1, 3);
			rg10_fetch(2, 0); rg10_fetch(2, 1); rg10_fetch(2, 2); rg10_fetch(2, 3);
			rg10_fetch(3, 0); rg10_fetch(3, 1); rg10_fetch(3, 2); rg10_fetch(3, 3);

			if (rgb) {
				typedef struct {
					uint8_t r, g, b;
				} rgb24_t;
				rgb24_t *rgb_pos = (rgb24_t*)rgb_row + out_x;

#define rgb_put(_y, _x) \
		rgb_pos[(_y) * out_w + (_x)].r = rgb_val[_y][_x].r / 4; \
		rgb_pos[(_y) * out_w + (_x)].g = rgb_val[_y][_x].g / 4; \
		rgb_pos[(_y) * out_w + (_x)].b = rgb_val[_y][_x].b / 4;

				rgb_put(0, 0); rgb_put(0, 1); rgb_put(0, 2); rgb_put(0, 3);
				rgb_put(1, 0); rgb_put(1, 1); rgb_put(1, 2); rgb_put(1, 3);
				rgb_put(2, 0); rgb_put(2, 1); rgb_put(2, 2); rgb_put(2, 3);
				rgb_put(3, 0); rgb_put(3, 1); rgb_put(3, 2); rgb_put(3, 3);
			}
#if 1
			if (i420) {
				uint8_t *y_pos = y_row + out_x;
				uint8_t *u_pos = u_row + out_x / 2;
				uint8_t *v_pos = u_pos + uv_size;

#if 1
#  define rgb_y(_r, _g, _b) clamp_u8(( 0.257 * (_r) + 0.504 * (_g) + 0.098 * (_b)) +  16);
#  define rgb_u(_r, _g, _b) clamp_u8((-0.148 * (_r) - 0.291 * (_g) + 0.439 * (_b)) + 128);
#  define rgb_v(_r, _g, _b) clamp_u8(( 0.439 * (_r) - 0.368 * (_g) - 0.071 * (_b)) + 128);
#else
#  define rgb_y(_r, _g, _b) clamp_u8((( 66 * (_r) + 129 * (_g) +  25 * (_b) + 128) / 256) +  16);
#  define rgb_u(_r, _g, _b) clamp_u8(((-38 * (_r) -  74 * (_g) + 112 * (_b) + 128) / 256) + 128);
#  define rgb_v(_r, _g, _b) clamp_u8(((112 * (_r) -  94 * (_g) -  18 * (_b) + 128) / 256) + 128);
#endif

#define yuv_put_y(_y, _x) y_pos[(_y) * out_w + (_x)] = \
	rgb_y(rgb_val[_y][_x].r, rgb_val[_y][_x].g, rgb_val[_y][_x].b);
#define yuv_put_u(_y, _x) u_pos[(_y) * out_w / 2 + (_x) / 2] = \
	rgb_u(rgb_val[_y][_x].r, rgb_val[_y][_x].g, rgb_val[_y][_x].b);
#define yuv_put_v(_y, _x) v_pos[(_y) * out_w / 2 + (_x) / 2] = \
	rgb_u(rgb_val[_y][_x].r, rgb_val[_y][_x].g, rgb_val[_y][_x].b);

				yuv_put_y(0, 0); yuv_put_y(0, 1); yuv_put_y(0, 2); yuv_put_y(0, 3);
				yuv_put_y(1, 0); yuv_put_y(1, 1); yuv_put_y(1, 2); yuv_put_y(1, 3);
				yuv_put_y(2, 0); yuv_put_y(2, 1); yuv_put_y(2, 2); yuv_put_y(2, 3);
				yuv_put_y(3, 0); yuv_put_y(3, 1); yuv_put_y(3, 2); yuv_put_y(3, 3);

				yuv_put_u(0, 0); yuv_put_u(0, 2);
				yuv_put_u(2, 0); yuv_put_u(2, 2);

				yuv_put_v(0, 0); yuv_put_v(0, 2);
				yuv_put_v(2, 0); yuv_put_v(2, 2);
			}
#endif
		}
	}
#undef rg10_fetch
#undef rgb_put
#undef rgb_y
#undef rgb_u
#undef rgb_v
#undef yuv_put_y
#undef yuv_put_u
#undef yuv_put_v
}

/** Convert I420 to RGB888.
 * 
 * I420
 *
 * Y11(uv11)   Y12(uv11)   Y13(uv12)   Y14(uv12)
 * Y21(uv11)   Y22(uv11)   Y23(uv12)   Y24(uv12)
 * Y31(uv21)   Y32(uv21)   Y33(uv22)   Y34(uv22)
 * Y41(uv21)   Y42(uv21)   Y43(uv22)   Y44(uv22)
 * u11   u12
 * u21   u22
 * v11   v12
 * v21   v22
 */
static void i420_rgb(int width, int height, const void *i420, void *rgb) {
	int y;
	int uv_size = (height / 2) * (width / 2);
	uint8_t *y_plane = (uint8_t*)i420;
	uint8_t *u_plane = y_plane + width * height;

	for (y = 0; y < height; y+=2) {
		typedef struct {
			uint8_t r, g, b;
		} rgb24_t;
		int x;
		rgb24_t *rgb_row = (rgb24_t*)rgb + y * width;
		uint8_t *y_row = y_plane + width * y;
		uint8_t *u_row = u_plane + (width / 2) * (y / 2);
		uint8_t *v_row = u_row + uv_size;

//		log_d("row %d/%d\n", y, height - 1);

		for (x = 0; x < width; x+=2) {
			uint8_t cy[2][2], u, v;
			rgb24_t *rgb_pos = rgb_row + x;

#define yuv_r(_cy, _u, _v) clamp_u8(1.164 * ((_cy) - 16) + 1.596 * ((_v) - 128)                    )
#define yuv_g(_cy, _u, _v) clamp_u8(1.164 * ((_cy) - 16) - 0.813 * ((_v) - 128) - 0.391 * ((_u) - 128))
#define yuv_b(_cy, _u, _v) clamp_u8(1.164 * ((_cy) - 16)                        + 2.018 * ((_u) - 128))

			cy[0][0] = y_row[x];
			cy[0][1] = y_row[x + 1];
			cy[1][0] = y_row[width + x];
			cy[1][1] = y_row[width + x + 1];
			u = u_row[x / 2];
			v = v_row[x / 2];
			
#define rgb_put(_y, _x) \
		rgb_row[(_y) * width + (_x)].r = yuv_r(cy[_y][_x], u, v); \
		rgb_row[(_y) * width + (_x)].g = yuv_g(cy[_y][_x], u, v); \
		rgb_row[(_y) * width + (_x)].b = yuv_b(cy[_y][_x], u, v);

			rgb_put(0, 0); rgb_put(0, 1);
			rgb_put(1, 0); rgb_put(1, 1);
		}
	}
}

// 3280 2464 sample-rg10.raw test2.bmp
static int tester_rg10bmp2(void*, int argc, char **argv) {
	int fd = -1, ret = -1, r;
	int width = 3280, height = 2464;
	const char *input_file = NULL, *output_file = NULL, *output2_file = NULL;
	void *mem = NULL;
	aloe_buf_t buf = {}, fb_rgb = {}, fb_i420 = {};
	std::chrono::steady_clock::time_point t1;
	std::chrono::milliseconds td1;

	// dump_argv(argc, argv);

	if (argc < 5) {
		log_e("usage: %s <width> <height> <input_file> <output_file> [output2_file]\n", argv[0]);
		goto finally;
	}
	width = strtol(argv[1], NULL, 10);
	height = strtol(argv[2], NULL, 10);
	input_file = argv[3];
	output_file = argv[4];
	output2_file = argc >= 6 ? argv[5] : NULL;

	if ((fd = open(input_file, O_RDONLY)) < 0) {
		log_e("open %s failed\n", input_file);
		goto finally;
	}

	if ((mem = (void*)malloc(
			width * height * 2
			+ width * height * 3
			+ width * height * 2)) == NULL) {
		log_e("malloc failed\n");
		goto finally;
	}

	buf.data = mem;
	buf.cap = width * height * 2;

	fb_rgb.data = (char*)buf.data + buf.cap;
	fb_rgb.cap = width * height * 3;

	fb_i420.data = (char*)fb_rgb.data + fb_rgb.cap;
	fb_i420.cap = width * height * 2;

	if ((r = aloe_bio_read(fd, buf.data, buf.cap)) != width * height * 2) {
		log_e("read failed, %d != %d\n", r, width * height * 2);
		goto finally;
	}
	close(fd);
	fd = -1;
	log_d("read %d x %d RG10 from %s\n", width, height, input_file);

	log_d("convert RG10 to RGB888 and I420\n");
	t1 = std::chrono::steady_clock::now();
	rg10_rgb_i420_v3(width, height, buf.data, fb_rgb.data, fb_i420.data);
	td1 = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - t1);
	log_d("save RGB888 to %s, cost %llu ms\n", output_file,
			(unsigned long long) td1.count());
	aloe_bmp_save(output_file, width / 2, height / 2, (uint8_t*)fb_rgb.data);

	if (output2_file) {
		log_d("convert I420 to RGB888\n");
		t1 = std::chrono::steady_clock::now();
		i420_rgb(width / 2, height / 2, fb_i420.data, fb_rgb.data);
		td1 = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - t1);

		log_d("save RGB888 to %s, cost %llu ms\n", output2_file,
				(unsigned long long) td1.count());
		aloe_bmp_save(output2_file, width / 2, height / 2, (uint8_t*)fb_rgb.data);
	}
	ret = 0;
finally:
	if (fd != -1) close(fd);
	if (mem) free(mem);
	return ret;
}

int main(int argc, char **argv) {
	int ret = -1, r;

	// tester_bmp2(NULL, argc, argv);
	tester_rg10bmp2(NULL, argc, argv);

	ret = 0;
finally:
	return ret;
}
