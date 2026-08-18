/*
 *
 */
#include <chrono>

#include <stdio.h>
#include <stdint.h>
// #include <arm_neon.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
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

enum {
	opt_key_reflags = 0x201,
	opt_key_max
};

static const char opt_short[] = "h";
static struct option opt_long[] = {
	{"help", no_argument, NULL, 'h'},
	{0},
};

static void show_usage(const char *argv0) {
	printf(
"Usage:\n"
"  %s <width> <height> <input RG10> <output prefix>"
"\n", argv0 ? argv0 : "program");
}

// 3280 2464 sample-rg10.raw test2.bmp
static int tester_rg10bmp2(void*, int argc, char **argv) {
	int ret = -1, r, i, opt_op, opt_idx;
	int fd = -1, width = 3280, height = 2464, output_prefix_len = 0;
	int rgb_width, rgb_height;
	const char *input_file = NULL, *output_prefix = NULL;
	void *mem = NULL;
	aloe_buf_t buf = {}, fb_rgb = {}, fb_i420 = {}, fb_outpath = {};
	std::chrono::steady_clock::time_point t1;
	std::chrono::milliseconds td1;

	// dump_argv(argc, argv);

	optind = 0;
	while ((opt_op = getopt_long(argc, (char* const*)argv, opt_short, opt_long,
			&opt_idx)) != -1) {
		if (opt_op == 'h') {
			show_usage(argv[0]);
			goto finally;
		}
	}

//	dump_argv(argc - optind, &argv[optind]);
//	for (i = optind; i < argc; i++) {
//		log_d("non-option argv[%d]: %s\n", i, argv[i]);
//	}

	if (argc - optind < 3) {
		show_usage(argv[0]);
		goto finally;
	}

	width = strtol(argv[optind], NULL, 10);
	height = strtol(argv[optind + 1], NULL, 10);
	input_file = argv[optind + 2];
	if (argc - optind >= 4 && (output_prefix = argv[optind + 3])) {
		output_prefix_len = strlen(output_prefix);
	}

	rgb_width = width / 2;
	rgb_height = height / 2;

	if ((fd = open(input_file, O_RDONLY)) < 0) {
		log_e("open %s failed\n", input_file);
		goto finally;
	}

	if ((mem = (void*)calloc(1,
			width * height * 2 /* rg10 */
			+ rgb_width * rgb_height * 3 /* rgb, 3 */
			+ rgb_width * rgb_height * 2 /* i420, 2 */
			+ output_prefix_len + 32)) == NULL) {
		log_e("malloc failed\n");
		goto finally;
	}

	buf.data = mem;
	buf.cap = width * height * 2;

	fb_rgb.data = (char*)buf.data + buf.cap;
	fb_rgb.cap = rgb_width * rgb_height * 3;

	fb_i420.data = (char*)fb_rgb.data + fb_rgb.cap;
	fb_i420.cap = rgb_width * rgb_height * 2;

	if (output_prefix_len > 0) {
		fb_outpath.data = (char*)fb_i420.data + fb_i420.cap;
		fb_outpath.cap = output_prefix_len + 32;
	}

	if ((r = aloe_bio_read(fd, buf.data, buf.cap)) != width * height * 2) {
		log_e("read failed, %d != %d\n", r, width * height * 2);
		goto finally;
	}
	close(fd);
	fd = -1;
	log_d("read %d x %d RG10 from %s\n", width, height, input_file);

	log_d("convert RG10 to RGB888 and I420\n");
	t1 = std::chrono::steady_clock::now();
	aloe_rg10_rgb8_i420_v4(width, height, width * 2, buf.data, fb_rgb.data, fb_i420.data);
	td1 = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - t1);
	log_d("convert RG10 to RGB888 and I420, cost %llu ms\n",
			(unsigned long long) td1.count());

	if (output_prefix_len > 0) {
		log_d("save RGB888 to %s.bmp\n", output_prefix);
		if (((r = snprintf((char*)fb_outpath.data, fb_outpath.cap, "%s.bmp",
				output_prefix)) <= 0) || (r >= fb_outpath.cap)) {
			log_e("insufficient buffer for output path\n");
			goto finally;
		}
		aloe_bmp_save((char*)fb_outpath.data, rgb_width, rgb_height,
				(uint8_t*)fb_rgb.data);

		log_d("save i420 to %s.i420\n", output_prefix);
		if (((r = snprintf((char*)fb_outpath.data, fb_outpath.cap, "%s.i420",
				output_prefix)) <= 0) || (r >= fb_outpath.cap)) {
			log_e("insufficient buffer for output path\n");
			goto finally;
		}
		i = rgb_width * rgb_height
				+ rgb_width * rgb_height / 4
				+ rgb_width * rgb_height / 4;
		if ((aloe_bio_write_fn((char*)fb_outpath.data, fb_i420.data, i, 0)) != i) {
			log_e("failed save to %s\n", (char*)fb_outpath.data);
			goto finally;
		}
	}

	if (output_prefix_len > 0) {
		log_d("convert I420 to RGB888\n");
		t1 = std::chrono::steady_clock::now();
		aloe_i420_rgb8(rgb_width, rgb_height, fb_i420.data, fb_rgb.data);
		td1 = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - t1);
		log_d("convert I420 to RGB888, cost %llu ms\n",
				(unsigned long long) td1.count());

		log_d("save RGB888 to %s.i420.bmp\n", output_prefix);
		if (((r = snprintf((char*)fb_outpath.data, fb_outpath.cap, "%s.i420.bmp",
				output_prefix)) <= 0) || (r >= fb_outpath.cap)) {
			log_e("insufficient buffer for output path\n");
			goto finally;
		}
		aloe_bmp_save((char*)fb_outpath.data, rgb_width, rgb_height,
				(uint8_t*)fb_rgb.data);
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
