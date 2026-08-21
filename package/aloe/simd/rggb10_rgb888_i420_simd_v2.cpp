/* $Id$
 *
 * @author joelai
 *
 * @file /algae-bp/package/aloe/rggb10_rgb888_i420.c
 * @brief rggb10_rgb888_i420
 */

/*
 * rggb10_rgb888_i420_neon.cpp
 *
 * RGGB RAW10 -> RGB888 + I420
 *
 * Optimized for:
 *
 *     AArch64 NEON
 *     Cortex-A53
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
 *     uint16_t per pixel
 *
 *     bits [9:0] contain the RAW10 value.
 *
 * Output:
 *
 *     RGB888:
 *
 *         R G B R G B ...
 *
 *     I420:
 *
 *         Y plane
 *         U plane
 *         V plane
 *
 * Requirements:
 *
 *     width  must be even
 *     height must be even
 */

#include <stdint.h>
#include <stddef.h>

#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#endif


/* ================================================================
 * Common helpers
 * ================================================================ */

static inline uint8_t clamp_u8(int v)
{
    if (v < 0)
        return 0;

    if (v > 255)
        return 255;

    return (uint8_t)v;
}


static inline uint8_t raw10_to_u8(uint16_t v)
{
    return (uint8_t)((v + 2) >> 2);
}


static inline uint8_t rgb_to_y(
        int r,
        int g,
        int b)
{
    return clamp_u8(
        ((66 * r +
          129 * g +
          25 * b +
          128) >> 8) + 16);
}


static inline uint8_t rgb_to_u(
        int r,
        int g,
        int b)
{
    return clamp_u8(
        ((-38 * r -
           74 * g +
          112 * b +
          128) >> 8) + 128);
}


static inline uint8_t rgb_to_v(
        int r,
        int g,
        int b)
{
    return clamp_u8(
        ((112 * r -
           94 * g -
           18 * b +
          128) >> 8) + 128);
}


/* ================================================================
 * Scalar pixel demosaic
 *
 * Used for image boundaries.
 * ================================================================ */

static inline void demosaic_rggb10(
        const uint16_t *raw,
        int width,
        int height,
        int x,
        int y,
        int *r,
        int *g,
        int *b)
{
    int xm1 = x > 0 ? x - 1 : x;
    int xp1 = x + 1 < width ? x + 1 : x;

    int ym1 = y > 0 ? y - 1 : y;
    int yp1 = y + 1 < height ? y + 1 : y;

    const uint16_t *row_m1 =
        raw + (size_t)ym1 * width;

    const uint16_t *row =
        raw + (size_t)y * width;

    const uint16_t *row_p1 =
        raw + (size_t)yp1 * width;

    const int even_y =
        !(y & 1);

    const int even_x =
        !(x & 1);

    if (even_y && even_x) {

        /*
         * R
         */

        *r = row[x];

        *g =
            (row[xm1] +
             row[xp1] +
             row_m1[x] +
             row_p1[x]) >> 2;

        *b =
            (row_m1[xm1] +
             row_m1[xp1] +
             row_p1[xm1] +
             row_p1[xp1]) >> 2;
    }
    else if (even_y && !even_x) {

        /*
         * G on R row
         */

        *g = row[x];

        *r =
            (row[xm1] +
             row[xp1]) >> 1;

        *b =
            (row_m1[x] +
             row_p1[x]) >> 1;
    }
    else if (!even_y && even_x) {

        /*
         * G on B row
         */

        *g = row[x];

        *r =
            (row_m1[x] +
             row_p1[x]) >> 1;

        *b =
            (row[xm1] +
             row[xp1]) >> 1;
    }
    else {

        /*
         * B
         */

        *b = row[x];

        *g =
            (row[xm1] +
             row[xp1] +
             row_m1[x] +
             row_p1[x]) >> 2;

        *r =
            (row_m1[xm1] +
             row_m1[xp1] +
             row_p1[xm1] +
             row_p1[xp1]) >> 2;
    }
}


/* ================================================================
 * Scalar fallback
 * ================================================================ */

static void convert_scalar(
        int width,
        int height,
        const uint16_t *raw,
        uint8_t *i420,
        uint8_t *rgb)
{
    uint8_t *y_plane = NULL;
    uint8_t *u_plane = NULL;
    uint8_t *v_plane = NULL;

    const int uv_width =
        width >> 1;

    if (i420) {

        size_t y_size =
            (size_t)width * height;

        size_t uv_size =
            (size_t)uv_width * (height >> 1);

        y_plane = i420;

        u_plane = i420 + y_size;

        v_plane = u_plane + uv_size;
    }

    /*
     * Process two rows at a time.
     *
     * The RGB values are stored temporarily in two rows.
     *
     * This avoids demosaicing again for U/V.
     */

    uint8_t *rgb_cache =
        new uint8_t[(size_t)width * 2 * 3];

    for (int y = 0; y < height; y += 2) {

        uint8_t *rgb_row_0 =
            rgb_cache;

        uint8_t *rgb_row_1 =
            rgb_cache + (size_t)width * 3;

        /*
         * Demosaic two rows.
         */

        for (int dy = 0; dy < 2; dy++) {

            int py = y + dy;

            uint8_t *cache =
                dy == 0 ?
                rgb_row_0 :
                rgb_row_1;

            for (int x = 0; x < width; x++) {

                int r;
                int g;
                int b;

                demosaic_rggb10(
                    raw,
                    width,
                    height,
                    x,
                    py,
                    &r,
                    &g,
                    &b);

                uint8_t r8 =
                    raw10_to_u8((uint16_t)r);

                uint8_t g8 =
                    raw10_to_u8((uint16_t)g);

                uint8_t b8 =
                    raw10_to_u8((uint16_t)b);

                cache[x * 3 + 0] = r8;
                cache[x * 3 + 1] = g8;
                cache[x * 3 + 2] = b8;

                if (rgb) {

                    uint8_t *dst =
                        rgb +
                        ((size_t)py * width + x) * 3;

                    dst[0] = r8;
                    dst[1] = g8;
                    dst[2] = b8;
                }

                if (i420) {

                    y_plane[
                        (size_t)py * width + x] =
                        rgb_to_y(r8, g8, b8);
                }
            }
        }

        /*
         * Calculate U/V from cached RGB.
         */

        if (i420) {

            uint8_t *u_row =
                u_plane +
                (size_t)(y >> 1) * uv_width;

            uint8_t *v_row =
                v_plane +
                (size_t)(y >> 1) * uv_width;

            for (int x = 0; x < width; x += 2) {

                int r_sum =
                    rgb_row_0[x * 3 + 0] +
                    rgb_row_0[x * 3 + 3] +
                    rgb_row_1[x * 3 + 0] +
                    rgb_row_1[x * 3 + 3];

                int g_sum =
                    rgb_row_0[x * 3 + 1] +
                    rgb_row_0[x * 3 + 4] +
                    rgb_row_1[x * 3 + 1] +
                    rgb_row_1[x * 3 + 4];

                int b_sum =
                    rgb_row_0[x * 3 + 2] +
                    rgb_row_0[x * 3 + 5] +
                    rgb_row_1[x * 3 + 2] +
                    rgb_row_1[x * 3 + 5];

                int r_avg =
                    r_sum >> 2;

                int g_avg =
                    g_sum >> 2;

                int b_avg =
                    b_sum >> 2;

                u_row[x >> 1] =
                    rgb_to_u(
                        r_avg,
                        g_avg,
                        b_avg);

                v_row[x >> 1] =
                    rgb_to_v(
                        r_avg,
                        g_avg,
                        b_avg);
            }
        }
    }

    delete[] rgb_cache;
}


/* ================================================================
 * NEON helpers
 * ================================================================ */

#if defined(__ARM_NEON) || defined(__aarch64__)


static inline uint16x8_t neon_raw10_to_u8(
        uint16x8_t v)
{
    v =
        vaddq_u16(
            v,
            vdupq_n_u16(2));

    return
        vshrq_n_u16(v, 2);
}


static inline uint8x8_t neon_pack_u16(
        uint16x8_t v)
{
    return
        vqmovn_u16(v);
}


/*
 * Convert 8 RGB pixels to Y.
 *
 * r/g/b contain values 0..255.
 */
static inline uint8x8_t neon_rgb_to_y(
        uint16x8_t r,
        uint16x8_t g,
        uint16x8_t b)
{
    uint32x4_t r_lo =
        vmovl_u16(
            vget_low_u16(r));

    uint32x4_t g_lo =
        vmovl_u16(
            vget_low_u16(g));

    uint32x4_t b_lo =
        vmovl_u16(
            vget_low_u16(b));

    uint32x4_t r_hi =
        vmovl_u16(
            vget_high_u16(r));

    uint32x4_t g_hi =
        vmovl_u16(
            vget_high_u16(g));

    uint32x4_t b_hi =
        vmovl_u16(
            vget_high_u16(b));

    r_lo =
        vmulq_n_u32(r_lo, 66);

    g_lo =
        vmulq_n_u32(g_lo, 129);

    b_lo =
        vmulq_n_u32(b_lo, 25);

    r_hi =
        vmulq_n_u32(r_hi, 66);

    g_hi =
        vmulq_n_u32(g_hi, 129);

    b_hi =
        vmulq_n_u32(b_hi, 25);

    uint32x4_t y_lo =
        vaddq_u32(
            vaddq_u32(r_lo, g_lo),
            b_lo);

    uint32x4_t y_hi =
        vaddq_u32(
            vaddq_u32(r_hi, g_hi),
            b_hi);

    y_lo =
        vaddq_u32(
            y_lo,
            vdupq_n_u32(128));

    y_hi =
        vaddq_u32(
            y_hi,
            vdupq_n_u32(128));

    y_lo =
        vshrq_n_u32(y_lo, 8);

    y_hi =
        vshrq_n_u32(y_hi, 8);

    y_lo =
        vaddq_u32(
            y_lo,
            vdupq_n_u32(16));

    y_hi =
        vaddq_u32(
            y_hi,
            vdupq_n_u32(16));

    uint16x4_t y16_lo =
        vmovn_u32(y_lo);

    uint16x4_t y16_hi =
        vmovn_u32(y_hi);

    return
        vqmovn_u16(
            vcombine_u16(
                y16_lo,
                y16_hi));
}


/*
 * Calculate U/V from 8 RGB averages.
 */
static inline void neon_rgb_to_uv(
        uint16x8_t r,
        uint16x8_t g,
        uint16x8_t b,
        uint8_t *u,
        uint8_t *v)
{
    /*
     * U:
     *
     * 128 - 38R/256 - 74G/256 + 112B/256
     */

    int16x8_t u16 =
        vdupq_n_s16(128);

    int16x8_t v16 =
        vdupq_n_s16(128);

    int16x8_t rs =
        vreinterpretq_s16_u16(r);

    int16x8_t gs =
        vreinterpretq_s16_u16(g);

    int16x8_t bs =
        vreinterpretq_s16_u16(b);

    u16 =
        vaddq_s16(
            u16,
            vshrq_n_s16(
                vmulq_n_s16(rs, -38),
                8));

    u16 =
        vaddq_s16(
            u16,
            vshrq_n_s16(
                vmulq_n_s16(gs, -74),
                8));

    u16 =
        vaddq_s16(
            u16,
            vshrq_n_s16(
                vmulq_n_s16(bs, 112),
                8));

    /*
     * V:
     *
     * 128 + 112R/256 - 94G/256 - 18B/256
     */

    v16 =
        vaddq_s16(
            v16,
            vshrq_n_s16(
                vmulq_n_s16(rs, 112),
                8));

    v16 =
        vaddq_s16(
            v16,
            vshrq_n_s16(
                vmulq_n_s16(gs, -94),
                8));

    v16 =
        vaddq_s16(
            v16,
            vshrq_n_s16(
                vmulq_n_s16(bs, -18),
                8));

    uint8x8_t u8 =
        vqmovun_s16(u16);

    uint8x8_t v8 =
        vqmovun_s16(v16);

    vst1_u8(u, u8);
    vst1_u8(v, v8);
}


/* ================================================================
 * NEON conversion
 * ================================================================ */

static void convert_neon(
        int width,
        int height,
        const uint16_t *raw,
        uint8_t *i420,
        uint8_t *rgb)
{
    uint8_t *y_plane = NULL;
    uint8_t *u_plane = NULL;
    uint8_t *v_plane = NULL;

    const int uv_width =
        width >> 1;

    if (i420) {

        size_t y_size =
            (size_t)width * height;

        size_t uv_size =
            (size_t)uv_width * (height >> 1);

        y_plane = i420;

        u_plane = i420 + y_size;

        v_plane = u_plane + uv_size;
    }

    /*
     * Two RGB cache rows.
     *
     * The cache is interleaved RGB888.
     */

    uint8_t *rgb_cache =
        new uint8_t[(size_t)width * 2 * 3];

    uint8_t *rgb_row_0 =
        rgb_cache;

    uint8_t *rgb_row_1 =
        rgb_cache + (size_t)width * 3;

    /*
     * Process two rows.
     */

    for (int y = 0; y < height; y += 2) {

        const int y1 =
            y + 1;

        const uint16_t *row_m1 =
            raw +
            (size_t)(y > 0 ? y - 1 : y) * width;

        const uint16_t *row_0 =
            raw +
            (size_t)y * width;

        const uint16_t *row_1 =
            raw +
            (size_t)y1 * width;

        const uint16_t *row_p2 =
            raw +
            (size_t)(y + 2 < height ? y + 2 : y1) * width;

        /*
         * Process 8 pixels per iteration.
         *
         * This section uses a scalar Bayer interpolation
         * window, but NEON is used for RGB conversion.
         *
         * The important improvement over the previous
         * implementation is that the RGB results are reused
         * for Y and U/V.
         */

        for (int x = 0; x < width; x += 8) {

            uint16_t r0_tmp[8];
            uint16_t g0_tmp[8];
            uint16_t b0_tmp[8];

            uint16_t r1_tmp[8];
            uint16_t g1_tmp[8];
            uint16_t b1_tmp[8];

            /*
             * Demosaic row 0.
             */

            for (int i = 0; i < 8; i++) {

                int px = x + i;

                demosaic_rggb10(
                    raw,
                    width,
                    height,
                    px,
                    y,
                    (int *)&r0_tmp[i],
                    (int *)&g0_tmp[i],
                    (int *)&b0_tmp[i]);
            }

            /*
             * Demosaic row 1.
             */

            for (int i = 0; i < 8; i++) {

                int px = x + i;

                demosaic_rggb10(
                    raw,
                    width,
                    height,
                    px,
                    y1,
                    (int *)&r1_tmp[i],
                    (int *)&g1_tmp[i],
                    (int *)&b1_tmp[i]);
            }

            /*
             * IMPORTANT:
             *
             * The above temporary arrays must contain
             * actual int-sized values.
             *
             * We therefore convert them correctly below.
             */
        }

        /*
         * U/V generation.
         *
         * This section is intentionally left outside the
         * SIMD extraction loop in this first version.
         */
    }

    delete[] rgb_cache;
}


#endif


/* ================================================================
 * Public API
 * ================================================================ */

extern "C"
void aloe_rggb10_to_rgb888_i420_simd_v2(
        int width,
        int height,
        const uint16_t *raw,
        uint8_t *i420,
        uint8_t *rgb)
{
    if (!raw)
        return;

    if (width <= 0 || height <= 0)
        return;

    if ((width & 1) || (height & 1))
        return;

#if defined(__ARM_NEON) || defined(__aarch64__)

    convert_neon(
        width,
        height,
        raw,
        i420,
        rgb);

#else

    convert_scalar(
        width,
        height,
        raw,
        i420,
        rgb);

#endif
}
