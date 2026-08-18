/* $Id$
 *
 * Copyright 2024, Dexatek Technology Ltd.
 * This is proprietary information of Dexatek Technology Ltd.
 * All Rights Reserved. Reproduction of this documentation or the
 * accompanying programs in any manner whatsoever without the written
 * permission of Dexatek Technology Ltd. is strictly forbidden.
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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* UTIL_IMG_H_ */
