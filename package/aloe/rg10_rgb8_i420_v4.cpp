/* $Id$
 *
 * @author joelai
 *
 * @file /algae-bp/package/aloe/rg10_rgb8_i420_v2.cpp
 * @brief rg10_rgb8_i420_v2
 */

#include <stdlib.h>

#if defined(__aarch64__) || defined(__ARM_NEON) || defined(__ARM_NEON__)
#define ALOE_HAVE_NEON 1
#include <arm_neon.h>
#endif

#if defined(__SSE2__)
#define ALOE_HAVE_SSE2 1
#include <emmintrin.h>
#endif

#include "log.h"
#include <aloe/util_img.h>
#include <aloe/sys.h>

static inline uint8_t rg10_u8(unsigned value) {
	return (value >> 2) & 0xff;
}

static inline int clamp_u8(int value) {
	return value > 255 ? 255 : value < 0 ? 0 : value;
}

/* Convert eight RGB triples to their BT.601 limited-range luma values. */
#if defined(ALOE_HAVE_NEON)
static inline void rgb_y_neon_8(uint8_t *dst, const uint8_t *r,
		const uint8_t *g, const uint8_t *b) {
	const uint16x8_t bias = vdupq_n_u16(128);
	const uint16x8_t offset = vdupq_n_u16(16);
	const uint8x8_t y_r = vdup_n_u8(66), y_g = vdup_n_u8(129), y_b = vdup_n_u8(25);
	uint16x8_t sum = vmlal_u8(vmull_u8(vld1_u8(r), y_r), vld1_u8(g), y_g);
	sum = vmlal_u8(sum, vld1_u8(b), y_b);
	vst1_u8(dst, vmovn_u16(vaddq_u16(vshrq_n_u16(vaddq_u16(sum, bias), 8), offset)));
}
#elif defined(ALOE_HAVE_SSE2)
static inline void rgb_y_sse2_8(uint8_t *dst, const uint8_t *r,
		const uint8_t *g, const uint8_t *b) {
	const __m128i zero = _mm_setzero_si128();
	const __m128i y_r = _mm_set1_epi16(66), y_g = _mm_set1_epi16(129), y_b = _mm_set1_epi16(25);
	const __m128i vr = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(const void *)r), zero);
	const __m128i vg = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(const void *)g), zero);
	const __m128i vb = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)(const void *)b), zero);
	__m128i sum = _mm_add_epi16(_mm_mullo_epi16(vr, y_r), _mm_mullo_epi16(vg, y_g));
	sum = _mm_add_epi16(sum, _mm_mullo_epi16(vb, y_b));
	sum = _mm_add_epi16(sum, _mm_set1_epi16(128));
	sum = _mm_add_epi16(_mm_srli_epi16(sum, 8), _mm_set1_epi16(16));
	_mm_storel_epi64((__m128i *)(void *)dst, _mm_packus_epi16(sum, zero));
}
#endif

static inline void clamp3_u8(int *r, int *g, int *b) {
	*r = clamp_u8(*r);
	*g = clamp_u8(*g);
	*b = clamp_u8(*b);
}

static inline void rgb_yuv(int r, int g, int b, uint8_t *y, uint8_t *u, uint8_t *v) {
	int out_y, out_u, out_v;
	out_y = clamp_u8((( 66 * r + 129 * g +  25 * b + 128) / 256) +  16);
	out_u = clamp_u8(((-38 * r -  74 * g + 112 * b + 128) / 256) + 128);
	out_v = clamp_u8(((112 * r -  94 * g -  18 * b + 128) / 256) + 128);
	if (y) *y = out_y;
	if (u) *u = out_u;
	if (v) *v = out_v;
}

typedef int v4si __attribute__ ((vector_size (16)));

static v4si rgb_y_v4si_coeff = {66, 129, 25, 0};
static v4si rgb_u_v4si_coeff = {-38, -74, 112, 0};
static v4si rgb_v_v4si_coeff = {112, -94, -18, 0};

static inline uint8_t rgb_y_vec(int r, int g, int b) {
	v4si v_rgb = {r, g, b, 0};
	v4si v_prod = v_rgb * rgb_y_v4si_coeff;
	return clamp_u8(((v_prod[0] + v_prod[1] + v_prod[2] + 128) / 256) + 16);
}

static inline uint8_t rgb_u_vec(int r, int g, int b) {
	v4si v_rgb = {r, g, b, 0};
	v4si v_prod = v_rgb * rgb_u_v4si_coeff;
	return clamp_u8(((v_prod[0] + v_prod[1] + v_prod[2] + 128) / 256) + 128);
}

static inline uint8_t rgb_v_vec(int r, int g, int b) {
	v4si v_rgb = {r, g, b, 0};
	v4si v_prod = v_rgb * rgb_v_v4si_coeff;
	return clamp_u8(((v_prod[0] + v_prod[1] + v_prod[2] + 128) / 256) + 128);
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
extern "C"
void aloe_rg10_rgb8_i420_v4(int width, int height, int stride,
		const void *rg10, void *rgb, void *i420) {
	uint8_t *y_plane, *u_plane, *v_plane;
	int out_w = width / 2, out_h = height / 2;
	int uv_size = (out_h / 2) * (out_w / 2);
	int y, out_y;

	if (width < 8 || height < 8 || width % 8 || height % 8) {
		aloe_log_e("invalid arguments\n");
		return;
	}

	if (i420) {
		y_plane = (uint8_t *)i420;
		u_plane = y_plane + out_w * out_h;
		v_plane = u_plane + uv_size;
	}

#define rg10_stride_row(_p, _y) ((uint16_t*)((uint8_t*)(_p) + (_y) * stride))

	for (y = 0, out_y = 0; y < height; y += 8, out_y += 4) {
		int x, out_x;
		uint16_t *rg10_row = rg10_stride_row(rg10, y);
		uint8_t *y_row, *u_row, *v_row, *rgb_row;

		if (i420) {
			y_row = y_plane + out_w * out_y;
			u_row = u_plane + (out_w / 2) * (out_y / 2);
			v_row = v_plane + (out_w / 2) * (out_y / 2);
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

#if 1
#  define rg10_fetch_g(_y, _x) ( \
		rg10_stride_row(rg10_pos, (_y) * 2)[(_x) * 2 + 1] \
		+ rg10_stride_row(rg10_pos, (_y) * 2 + 1)[(_x) * 2]) / 2 / 4
#else
#  define rg10_fetch_g(_y, _x) rg10_stride_row(rg10_pos, (_y) * 2)[(_x) * 2 + 1] / 4
#endif

#define rg10_fetch(_y, _x) \
		rgb_val[_y][_x].r = rg10_stride_row(rg10_pos, (_y) * 2)[(_x) * 2] / 4; \
		rgb_val[_y][_x].g = rg10_fetch_g(_y, _x); \
		rgb_val[_y][_x].b = rg10_stride_row(rg10_pos, (_y) * 2 + 1)[(_x) * 2 + 1] / 4;

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
		rgb_pos[(_y) * out_w + (_x)].r = rgb_val[_y][_x].r; \
		rgb_pos[(_y) * out_w + (_x)].g = rgb_val[_y][_x].g; \
		rgb_pos[(_y) * out_w + (_x)].b = rgb_val[_y][_x].b;

				rgb_put(0, 0); rgb_put(0, 1); rgb_put(0, 2); rgb_put(0, 3);
				rgb_put(1, 0); rgb_put(1, 1); rgb_put(1, 2); rgb_put(1, 3);
				rgb_put(2, 0); rgb_put(2, 1); rgb_put(2, 2); rgb_put(2, 3);
				rgb_put(3, 0); rgb_put(3, 1); rgb_put(3, 2); rgb_put(3, 3);
			}
#if 1 // i420
			if (i420) {
				uint8_t *y_pos = y_row + out_x;
				uint8_t *u_pos = u_row + out_x / 2;
				uint8_t *v_pos = v_row + out_x / 2;

#  if 1 // use vector
#    define rgb_y(_r, _g, _b) rgb_y_vec((_r), (_g), (_b))
#    define rgb_u(_r, _g, _b) rgb_u_vec((_r), (_g), (_b))
#    define rgb_v(_r, _g, _b) rgb_v_vec((_r), (_g), (_b))
#  elif 1
#    define rgb_y(_r, _g, _b) clamp_u8((( 66 * (_r) + 129 * (_g) +  25 * (_b) + 128) / 256) +  16);
#    define rgb_u(_r, _g, _b) clamp_u8(((-38 * (_r) -  74 * (_g) + 112 * (_b) + 128) / 256) + 128);
#    define rgb_v(_r, _g, _b) clamp_u8(((112 * (_r) -  94 * (_g) -  18 * (_b) + 128) / 256) + 128);
#  elif 1
#    define rgb_y(_r, _g, _b) clamp_u8(( 0.257 * (_r) + 0.504 * (_g) + 0.098 * (_b)) +  16);
#    define rgb_u(_r, _g, _b) clamp_u8((-0.148 * (_r) - 0.291 * (_g) + 0.439 * (_b)) + 128);
#    define rgb_v(_r, _g, _b) clamp_u8(( 0.439 * (_r) - 0.368 * (_g) - 0.071 * (_b)) + 128);
#  endif

#  if 0
#    define rgb_r(_y, _x) (rgb_val[_y + 0][_x + 0].r \
		+ rgb_val[_y + 0][_x + 1].r \
		+ rgb_val[_y + 1][_x + 0].r \
		+ rgb_val[_y + 1][_x + 1].r) >> 2
#    define rgb_g(_y, _x) (rgb_val[_y + 0][_x + 0].g \
		+ rgb_val[_y + 0][_x + 1].g \
		+ rgb_val[_y + 1][_x + 0].g \
		+ rgb_val[_y + 1][_x + 1].g) >> 2
#    define rgb_b(_y, _x) (rgb_val[_y + 0][_x + 0].b \
		+ rgb_val[_y + 0][_x + 1].b \
		+ rgb_val[_y + 1][_x + 0].b \
		+ rgb_val[_y + 1][_x + 1].b) >> 2
#  else
#    define rgb_r(_y, _x) rgb_val[_y][_x].r
#    define rgb_g(_y, _x) rgb_val[_y][_x].g
#    define rgb_b(_y, _x) rgb_val[_y][_x].b
#  endif

#  define yuv_put_y(_y, _x) y_pos[(_y) * out_w + (_x)] = \
	rgb_y(rgb_r(_y,_x), rgb_g(_y,_x), rgb_b(_y,_x));

#  define yuv_put_u(_y, _x) u_pos[((_y) / 2) * (out_w / 2) + ((_x) / 2)] = \
	rgb_u(rgb_r(_y,_x), rgb_g(_y,_x), rgb_b(_y,_x));

#  define yuv_put_v(_y, _x) v_pos[((_y) / 2) * (out_w / 2) + ((_x) / 2)] = \
	rgb_v(rgb_r(_y,_x), rgb_g(_y,_x), rgb_b(_y,_x));

				yuv_put_y(0, 0); yuv_put_y(0, 1); yuv_put_y(0, 2); yuv_put_y(0, 3);
				yuv_put_y(1, 0); yuv_put_y(1, 1); yuv_put_y(1, 2); yuv_put_y(1, 3);
				yuv_put_y(2, 0); yuv_put_y(2, 1); yuv_put_y(2, 2); yuv_put_y(2, 3);
				yuv_put_y(3, 0); yuv_put_y(3, 1); yuv_put_y(3, 2); yuv_put_y(3, 3);

				yuv_put_u(0, 0); yuv_put_u(0, 2);
				yuv_put_u(2, 0); yuv_put_u(2, 2);

				yuv_put_v(0, 0); yuv_put_v(0, 2);
				yuv_put_v(2, 0); yuv_put_v(2, 2);
			} // i420
#endif // i420
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

/** Convert RG10 to RGB888 and I420.
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
 * Output RGB/I420 2 x 2 pixels block
 *
 * Y11(uv11)   Y12(uv11)
 * Y21(uv11)   Y22(uv11)
 * u11
 * v11
 *
 * Y = ( 66 * R + 129 * G +  25 * B + 128) / 256 +  16;
 * U = (-38 * R -  74 * G + 112 * B + 128) / 256 + 128;
 * V = (112 * R -  94 * G -  18 * B + 128) / 256 + 128;
 *
 */
extern "C"
void aloe_rg10_rgb8_i420_v5(int width, int height, int stride,
		const void *rg10, void *rgb, void *i420) {
	const int scale_factor = 4;
	const int out_w = width / scale_factor;
	const int out_h = height / scale_factor;
	const int rgb_stride = out_w * 3;
	const uint8_t *const src = (const uint8_t *)rg10;
	uint8_t *const rgb_base = (uint8_t *)rgb;
	uint8_t *const y_plane = (uint8_t *)i420;
	uint8_t *const u_plane = i420 ? y_plane + out_w * out_h : NULL;
	uint8_t *const v_plane = i420 ? u_plane + out_w * out_h / 4 : NULL;

	if (width < 8 || height < 8 || width % 8 || height % 8) {
		aloe_log_e("invalid arguments\n");
		return;
	}

	for (int out_y = 0; out_y < out_h; out_y += 2) {
		const uint16_t *const row0 = (const uint16_t *)(src + (out_y * 4) * stride);
		const uint16_t *const row1 = (const uint16_t *)(src + (out_y * 4 + 1) * stride);
		const uint16_t *const row4 = (const uint16_t *)(src + (out_y * 4 + 4) * stride);
		const uint16_t *const row5 = (const uint16_t *)(src + (out_y * 4 + 5) * stride);
		uint8_t *const rgb0 = rgb ? rgb_base + out_y * rgb_stride : NULL;
		uint8_t *const rgb1 = rgb ? rgb0 + rgb_stride : NULL;
		uint8_t *const y0 = i420 ? y_plane + out_y * out_w : NULL;
		uint8_t *const y1 = i420 ? y0 + out_w : NULL;
		uint8_t *const u = i420 ? u_plane + (out_y / 2) * (out_w / 2) : NULL;
		uint8_t *const v = i420 ? v_plane + (out_y / 2) * (out_w / 2) : NULL;
		int out_x = 0;

#if defined(ALOE_HAVE_NEON) || defined(ALOE_HAVE_SSE2)
		/* Eight RGB pixels consume a 32-pixel source span. */
		for (; out_x + 8 <= out_w; out_x += 8) {
			uint8_t r0[8], g0[8], b0[8], r1[8], g1[8], b1[8];
			const int x = out_x * scale_factor;
			for (int i = 0; i < 8; ++i) {
				const int sx = x + i * scale_factor;
				r0[i] = rg10_u8(row0[sx]);     g0[i] = rg10_u8(row0[sx + 1]); b0[i] = rg10_u8(row1[sx + 1]);
				r1[i] = rg10_u8(row4[sx]);     g1[i] = rg10_u8(row4[sx + 1]); b1[i] = rg10_u8(row5[sx + 1]);
			}
			if (rgb) for (int i = 0; i < 8; ++i) {
				rgb0[(out_x + i) * 3] = r0[i]; rgb0[(out_x + i) * 3 + 1] = g0[i]; rgb0[(out_x + i) * 3 + 2] = b0[i];
				rgb1[(out_x + i) * 3] = r1[i]; rgb1[(out_x + i) * 3 + 1] = g1[i]; rgb1[(out_x + i) * 3 + 2] = b1[i];
			}
			if (i420) {
#if defined(ALOE_HAVE_NEON)
				rgb_y_neon_8(y0 + out_x, r0, g0, b0);
				rgb_y_neon_8(y1 + out_x, r1, g1, b1);
#else
				rgb_y_sse2_8(y0 + out_x, r0, g0, b0);
				rgb_y_sse2_8(y1 + out_x, r1, g1, b1);
#endif
				for (int i = 1; i < 8; i += 2) {
					u[(out_x + i) / 2] = clamp_u8((-38 * r1[i] - 74 * g1[i] + 112 * b1[i] + 128) / 256 + 128);
					v[(out_x + i) / 2] = clamp_u8((112 * r1[i] - 94 * g1[i] - 18 * b1[i] + 128) / 256 + 128);
				}
			}
		}
#endif
		for (; out_x < out_w; ++out_x) {
			const int x = out_x * scale_factor;
			const uint8_t r0 = rg10_u8(row0[x]), g0 = rg10_u8(row0[x + 1]), b0 = rg10_u8(row1[x + 1]);
			const uint8_t r1 = rg10_u8(row4[x]), g1 = rg10_u8(row4[x + 1]), b1 = rg10_u8(row5[x + 1]);
			if (rgb) {
				rgb0[out_x * 3] = r0; rgb0[out_x * 3 + 1] = g0; rgb0[out_x * 3 + 2] = b0;
				rgb1[out_x * 3] = r1; rgb1[out_x * 3 + 1] = g1; rgb1[out_x * 3 + 2] = b1;
			}
			if (i420) {
				y0[out_x] = (66 * r0 + 129 * g0 + 25 * b0 + 128) / 256 + 16;
				y1[out_x] = (66 * r1 + 129 * g1 + 25 * b1 + 128) / 256 + 16;
				if (out_x & 1) {
					u[out_x / 2] = clamp_u8((-38 * r1 - 74 * g1 + 112 * b1 + 128) / 256 + 128);
					v[out_x / 2] = clamp_u8((112 * r1 - 94 * g1 - 18 * b1 + 128) / 256 + 128);
				}
			}
		}
	}
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
extern "C"
void aloe_i420_rgb8(int width, int height, const void *i420, void *rgb) {
	int y;
	int uv_size = (height / 2) * (width / 2);
	uint8_t *y_plane = (uint8_t*)i420;
	uint8_t *u_plane = y_plane + width * height;
	uint8_t *v_plane = u_plane + uv_size;

	if (width < 2 || height < 2 || width % 2 || height % 2) {
		aloe_log_e("invalid arguments\n");
		return;
	}

#if 0
	memset(y_plane, 0x80, width * height);
	memset(u_plane, 0x80, uv_size);
	memset(v_plane, 0x80, uv_size);
#endif
	for (y = 0; y < height; y+=2) {
		typedef struct {
			uint8_t r, g, b;
		} rgb24_t;
		int x;
		rgb24_t *rgb_row = (rgb24_t*)rgb + y * width;
		uint8_t *y_row = y_plane + width * y;
		uint8_t *u_row = u_plane + (width / 2) * (y / 2);
		uint8_t *v_row = v_plane + (width / 2) * (y / 2);

//		aloe_log_d("row %d/%d\n", y, height - 1);

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
		rgb_pos[(_y) * width + (_x)].r = yuv_r(cy[_y][_x], u, v); \
		rgb_pos[(_y) * width + (_x)].g = yuv_g(cy[_y][_x], u, v); \
		rgb_pos[(_y) * width + (_x)].b = yuv_b(cy[_y][_x], u, v);

			rgb_put(0, 0); rgb_put(0, 1);
			rgb_put(1, 0); rgb_put(1, 1);
		}
	}
}
