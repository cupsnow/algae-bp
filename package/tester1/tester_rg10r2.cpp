#include <stdio.h>
#include <stdint.h>
// #include <arm_neon.h>
#include <fcntl.h>
#include <unistd.h>
#include <aloe/util_img.h>

#include "priv.h"

#define log_m(_lvl, _fmt, _args...) printf("[" _lvl "][%s][#%d] " _fmt, __func__, __LINE__, ##_args)
#define log_d(_args...) log_m("Debug", _args)
#define log_e(_args...) log_m("ERROR", _args)

#define dump_argv(_argc, _argv) for (int i = 0; i < (_argc); i++) { \
	log_d("argv[%d/%d]: %s\n", i + 1, (_argc), (_argv)[i]); \
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

/** RG10 to RGB888 and I420.
 *
 * Bayer pattern: RGGB, each pixel 10bits, occupied in lower of 16 bits
 *
 * R G R G ...
 * G B G B ...
 * R G R G ...
 * G B G B ...
 *
 */
static void rg10_rgb_i420_v3(int width, int height, const void *rg10, void *rgb, 
		void *i420) {
	uint8_t *y_plane, *u_plane;
	int out_w = width / 2;
	int out_h = height / 2;
	int uv_size = (out_h / 2) * (out_w / 2);
	int y, out_y;

	if (i420) {
		y_plane = (uint8_t *)i420;
		u_plane = y_plane + out_w * out_h;
	}

	for (y = 0, out_y = 0; y < height; y += 8, out_y += 4) {
		/*
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
		 * Y11(uv11) Y12(uv11)
		 * Y21(uv11) Y22(uv11)
		 * u11
		 * v11

		 * Y = (( 66 * R + 129 * G +  25 * B + 128) / 256) +  16;
		 * U = ((-38 * R -  74 * G + 112 * B + 128) / 256) + 128;
		 * V = ((112 * R -  94 * G -  18 * B + 128) / 256) + 128;

		 * Y  =      (0.257 * R) + (0.504 * G) + (0.098 * B) + 16
		 * Cb = U = -(0.148 * R) - (0.291 * G) + (0.439 * B) + 128
		 * Cr = V =  (0.439 * R) - (0.368 * G) - (0.071 * B) + 128

		 * R = 1.164(Y - 16) + 1.596(V - 128)
		 * G = 1.164(Y - 16) - 0.813(V - 128) - 0.391(U - 128)
		 * B = 1.164(Y - 16)                   + 2.018(U - 128)

		 */
		//
		int x, out_x;
		uint16_t *rg10_row = (uint16_t *)rg10 + width * y;
		uint8_t *y_row, *u_row, *v_row, *rgb_row;

		log_d("row %d/%d\n", y, height);

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

			int r11 = rg10_pos[0];
			int g11 = rg10_pos[1];
			int b11 = rg10_pos[width + 1];

			int r12 = rg10_pos[4];
			int g12 = rg10_pos[5];
			int b12 = rg10_pos[width + 5];

			int r21 = rg10_pos[width * 4];
			int g21 = rg10_pos[width * 4 + 1];
			int b21 = rg10_pos[width * 4 + width + 1];

			int r22 = rg10_pos[width * 4 + 4];
			int g22 = rg10_pos[width * 4 + 5];
			int b22 = rg10_pos[width * 4 + width + 5];

			if (rgb) {
				uint8_t *rgb_pos = rgb_row + out_x * 3;

				rgb_pos[0] = r11;
				rgb_pos[1] = g11;
				rgb_pos[2] = b11;

				rgb_pos[3] = r12;
				rgb_pos[4] = g12;
				rgb_pos[5] = b12;

				rgb_pos[out_w * 3] = r21;
				rgb_pos[out_w * 3 + 1] = g21;
				rgb_pos[out_w * 3 + 2] = b21;

				rgb_pos[out_w * 3 + 3] = r22;
				rgb_pos[out_w * 3 + 4] = g22;
				rgb_pos[out_w * 3 + 5] = b22;
			}

			if (i420) {
				uint8_t *y_pos = y_row + out_x;
				uint8_t *u_pos = u_row + out_x / 2;
				uint8_t *v_pos = u_pos + uv_size;

				rgb_yuv(r11, g11, b11, y_pos,                 u_pos, v_pos);
				rgb_yuv(r12, g12, b12, y_pos + 1,             NULL, NULL);
				rgb_yuv(r21, g21, b21, y_pos + out_w * 3,     NULL, NULL);
				rgb_yuv(r22, g22, b22, y_pos + out_w * 3 + 3, NULL, NULL);
			}
		}
	}
}

int main(int argc, char **argv) {
	int fd = -1, ret = -1, r;
	int width = 3280, height = 2464;
	aloe_buf_t buf = {}, fb_rgb = {}, fb_i420 = {};

	// dump_argv(argc, argv);

	if (argc < 3) {
		log_e("usage: %s <input_file> <output_file>\n", argv[0]);
		goto finally;
	}

	if ((fd = open(argv[1], O_RDONLY)) < 0) {
		log_e("open %s failed\n", argv[1]);
		goto finally;
	}

	if ((buf.data = (void*)malloc(buf.cap = width * height * 2 * 2)) == NULL) {
		log_e("malloc failed\n");
		goto finally;
	}
	if ((fb_rgb.data = (void*)malloc(fb_rgb.cap = width * height * 2 * 2)) == NULL) {
		log_e("malloc failed\n");
		goto finally;
	}
	if ((fb_i420.data = (void*)malloc(fb_i420.cap = width * height * 2 * 2)) == NULL) {
		log_e("malloc failed\n");
		goto finally;
	}
	if ((r = aloe_bio_read(fd, buf.data, buf.cap)) != width * height * 2) {
		log_e("read failed, %d != %d\n", r, width * height * 2);
		goto finally;
	}
	close(fd);
	fd = -1;
	log_d("convert RG10 to RGB888\n");
	rg10_rgb_i420_v3(width, height, buf.data, fb_rgb.data, NULL);
	log_d("save RGB888 to %s\n", argv[2]);
	aloe_bmp_save(argv[2], width, height, (uint8_t*)fb_rgb.data);
	ret = 0;
finally:
	if (fd != -1) close(fd);
	if (buf.data) free(buf.data);
	if (fb_rgb.data) free(fb_rgb.data);
	if (fb_i420.data) free(fb_i420.data);
	return ret;
}
