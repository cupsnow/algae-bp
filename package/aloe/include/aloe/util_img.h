/* $Id$
 *
 * @author joelai
 *
 * @file /algae-bp/package/aloe/include/aloe/util_img.h
 * @brief util_img
 */

#ifndef UTIL_IMG_H_
#define UTIL_IMG_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void aloe_rggb10_to_rgb888_i420(int width, int height, const uint16_t *raw,
		uint8_t *i420, uint8_t *rgb);
void aloe_rggb10_to_rgb888_i420_v2( int width, int height, const uint16_t *raw, 
		uint8_t *i420, uint8_t *rgb);

void aloe_rggb10_to_rgb888_i420_simd( int width, int height, const uint16_t *raw,
		uint8_t *i420, uint8_t *rgb);
void aloe_rggb10_to_i420_simd( int width, int height, const uint16_t *raw,
		uint8_t *i420);
void aloe_rggb10_to_rgb888_simd( int width, int height, const uint16_t *raw,
		uint8_t *rgb);

void aloe_rggb10_to_rgb888_i420_simd_v2( int width, int height, const uint16_t *raw,
		uint8_t *i420, uint8_t *rgb);

int aloe_bmp_save(const char *filename, int width, int height, const uint8_t *rgb);

void aloe_rg10_rgb8_i420_v4(int width, int height, int stride, const void *rg10,
		void *rgb, void *i420);
void aloe_i420_rgb8(int width, int height, const void *i420, void *rgb);

void aloe_rg10_rgb8_i420_v5(int width, int height, int stride, const void *rg10, 
		void *rgb, void *i420);

/* Quarter-size RGB888 / I420; either output may be NULL.
 * RGB gains multiply extracted 8-bit channels before YUV conversion, with
 * rounding and saturation to [0, 255]. Each gain must be finite in [0, 16].
 * Invalid gains leave outputs untouched. Use 1.0f for v5-compatible output.
 * Signature changed: callers must supply all three gains and rebuild. */
void aloe_rg10_rgb8_i420_v6(int width, int height, int stride, const void *rg10,
		void *rgb, void *i420, float r_gain, float g_gain, float b_gain);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* UTIL_IMG_H_ */
