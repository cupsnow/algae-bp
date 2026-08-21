/* $Id$
 *
 * @author joelai
 *
 * @file /algae-bp/package/aloe/rggb10_rgb888_i420.c
 * @brief rggb10_rgb888_i420
 */

/*
 * rggb10_to_i420_simd.cpp
 *
 * RGGB RAW10 Bayer -> RGB888 and/or I420
 *
 * Supported architectures:
 *
 *     x86_64:
 *         AVX2
 *
 *     AArch64:
 *         NEON
 *
 *     ARM32:
 *         NEON
 *
 *     Other:
 *         Scalar fallback
 *
 * Input:
 *
 *     raw:
 *         width * height uint16_t pixels
 *
 *     Each pixel:
 *
 *         bits [9:0] = RAW10 value
 *
 * Bayer pattern:
 *
 *         R G R G
 *         G B G B
 *         R G R G
 *         G B G B
 *
 * Output I420:
 *
 *         Y plane:
 *             width * height
 *
 *         U plane:
 *             (width / 2) * (height / 2)
 *
 *         V plane:
 *             (width / 2) * (height / 2)
 *
 * Requirements:
 *
 *         width  must be even
 *         height must be even
 *
 * Compile:
 *
 * x86_64:
 *
 *     g++ -O3 -mavx2 -c rggb10_to_i420_simd.cpp
 *
 * AArch64:
 *
 *     aarch64-linux-gnu-g++ \
 *         -O3 \
 *         -mcpu=cortex-a53 \
 *         -c rggb10_to_i420_simd.cpp
 *
 * ARM32:
 *
 *     arm-linux-gnueabihf-g++ \
 *         -O3 \
 *         -mcpu=cortex-a7 \
 *         -mfpu=neon-vfpv4 \
 *         -mfloat-abi=hard \
 *         -c rggb10_to_i420_simd.cpp
 */

#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

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


/*
 * RAW10 -> 8-bit.
 *
 * 10-bit:
 *
 *     0 ... 1023
 *
 * 8-bit:
 *
 *     0 ... 255
 *
 * Rounded conversion:
 *
 *     (v + 2) >> 2
 */
static inline uint8_t raw10_to_u8(uint16_t v)
{
    return (uint8_t)((v + 2) >> 2);
}


/*
 * Convert RGB888 to limited-range BT.601 Y.
 */
static inline uint8_t rgb_to_y(
        int r,
        int g,
        int b)
{
    int y =
        ((66 * r +
          129 * g +
          25 * b +
          128) >> 8) + 16;

    return clamp_u8(y);
}


/*
 * Convert RGB888 to limited-range BT.601 U.
 */
static inline uint8_t rgb_to_u(
        int r,
        int g,
        int b)
{
    int u =
        ((-38 * r -
           74 * g +
          112 * b +
          128) >> 8) + 128;

    return clamp_u8(u);
}


/*
 * Convert RGB888 to limited-range BT.601 V.
 */
static inline uint8_t rgb_to_v(
        int r,
        int g,
        int b)
{
    int v =
        ((112 * r -
           94 * g -
           18 * b +
          128) >> 8) + 128;

    return clamp_u8(v);
}


/* ================================================================
 * Scalar Bayer pixel interpolation
 * ================================================================ */

static inline void demosaic_rggb10_pixel(
        const uint16_t *raw,
        int width,
        int height,
        int x,
        int y,
        int *r,
        int *g,
        int *b)
{
    /*
     * Edge replication.
     */

    int xm1 = x > 0 ? x - 1 : 0;
    int xp1 = x + 1 < width ? x + 1 : width - 1;

    int ym1 = y > 0 ? y - 1 : 0;
    int yp1 = y + 1 < height ? y + 1 : height - 1;

    const uint16_t *row_m1 =
        raw + (size_t)ym1 * width;

    const uint16_t *row =
        raw + (size_t)y * width;

    const uint16_t *row_p1 =
        raw + (size_t)yp1 * width;

    const int even_y = !(y & 1);
    const int even_x = !(x & 1);

    if (even_y && even_x) {

        /*
         * R
         *
         *       G
         *     G R G
         *       G
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
 * Scalar implementation
 * ================================================================ */

static void aloe_rggb10_to_rgb888_i420_scalar(
        int width,
        int height,
        const uint16_t *raw,
        uint8_t *i420,
        uint8_t *rgb)
{
    uint8_t *y_plane = NULL;
    uint8_t *u_plane = NULL;
    uint8_t *v_plane = NULL;

    const int uv_width = width >> 1;

    if (i420) {

        const size_t y_size =
            (size_t)width * height;

        const size_t uv_size =
            (size_t)uv_width * (height >> 1);

        y_plane = i420;

        u_plane = i420 + y_size;

        v_plane = u_plane + uv_size;
    }

    for (int y = 0; y < height; y++) {

        uint8_t *y_row =
            y_plane ? y_plane + (size_t)y * width : NULL;

        uint8_t *rgb_row =
            rgb ? rgb + (size_t)y * width * 3 : NULL;

        for (int x = 0; x < width; x++) {

            int r;
            int g;
            int b;

            demosaic_rggb10_pixel(
                raw,
                width,
                height,
                x,
                y,
                &r,
                &g,
                &b);

            int r8 = raw10_to_u8((uint16_t)r);
            int g8 = raw10_to_u8((uint16_t)g);
            int b8 = raw10_to_u8((uint16_t)b);

            if (rgb) {

                uint8_t *p =
                    rgb_row + (size_t)x * 3;

                p[0] = (uint8_t)r8;
                p[1] = (uint8_t)g8;
                p[2] = (uint8_t)b8;
            }

            if (i420) {

                y_row[x] =
                    rgb_to_y(r8, g8, b8);
            }
        }
    }

    /*
     * U/V.
     *
     * Reuse the same Bayer interpolation.
     */

    if (i420) {

        for (int y = 0; y < height; y += 2) {

            uint8_t *u_row =
                u_plane + (size_t)(y >> 1) * uv_width;

            uint8_t *v_row =
                v_plane + (size_t)(y >> 1) * uv_width;

            for (int x = 0; x < width; x += 2) {

                int r_sum = 0;
                int g_sum = 0;
                int b_sum = 0;

                for (int dy = 0; dy < 2; dy++) {

                    for (int dx = 0; dx < 2; dx++) {

                        int r;
                        int g;
                        int b;

                        demosaic_rggb10_pixel(
                            raw,
                            width,
                            height,
                            x + dx,
                            y + dy,
                            &r,
                            &g,
                            &b);

                        r_sum +=
                            raw10_to_u8((uint16_t)r);

                        g_sum +=
                            raw10_to_u8((uint16_t)g);

                        b_sum +=
                            raw10_to_u8((uint16_t)b);
                    }
                }

                int r_avg = r_sum >> 2;
                int g_avg = g_sum >> 2;
                int b_avg = b_sum >> 2;

                u_row[x >> 1] =
                    rgb_to_u(r_avg, g_avg, b_avg);

                v_row[x >> 1] =
                    rgb_to_v(r_avg, g_avg, b_avg);
            }
        }
    }
}


/* ================================================================
 * NEON implementation
 *
 * The implementation processes four 2x2 Bayer blocks:
 *
 *     R G R G R G R G
 *     G B G B G B G B
 *
 * at a time.
 *
 * Each NEON vector contains four uint16_t values.
 * ================================================================ */

#if defined(__ARM_NEON) || defined(__aarch64__)


static inline uint16x4_t neon_raw10_to_u8(
        uint16x4_t v)
{
    v = vadd_u16(
        v,
        vdup_n_u16(2));

    return vshr_n_u16(v, 2);
}


/*
 * Load four pixels from a row.
 */
static inline uint16x4_t neon_load4(
        const uint16_t *p)
{
    return vld1_u16(p);
}


/*
 * Store four 8-bit values.
 */
static inline void neon_store4_u16_as_u8(
        uint8_t *dst,
        uint16x4_t v)
{
#if 1
    uint8x8_t packed =
        vmovn_u16(
            vcombine_u16(
                v,
                vdup_n_u16(0)));

    vst1_lane_u32(
        (uint32_t *)dst,
        vreinterpret_u32_u8(packed),
        0);
#elif 1
    uint16x8_t expanded =
        vcombine_u16(
            v,
            vdup_n_u16(0));

    uint8x8_t packed =
        vqmovn_u16(expanded);

    vst1_lane_u32(
        (uint32_t *)dst,
        vreinterpret_u32_u8(packed),
        0);
#else
    uint8x8_t packed =
        vqmovn_u16(
            vcombine_u16(v, v));

    vst1_lane_u32(
        (uint32_t *)dst,
        vreinterpret_u32_u8(packed),
        0);
#endif
}


/*
 * NEON 2x2 block processing.
 *
 * Processes four Bayer blocks:
 *
 *     R G | R G | R G | R G
 *     G B | G B | G B | G B
 *
 * Input x must be even.
 *
 * This function processes four blocks = 8 columns.
 */
static inline void neon_process_8_columns(
        const uint16_t *row_m1,
        const uint16_t *row_0,
        const uint16_t *row_1,
        const uint16_t *row_p2,
        int x,
        uint8_t *y_row_0,
        uint8_t *y_row_1,
        uint8_t *rgb_row_0,
        uint8_t *rgb_row_1,
        uint8_t *u_row,
        uint8_t *v_row,
        int write_y,
        int write_rgb,
        int write_uv)
{
    /*
     * We process four blocks:
     *
     * block positions:
     *
     *     x + 0
     *     x + 2
     *     x + 4
     *     x + 6
     */

    uint16_t r0_tmp[4];
    uint16_t g0_tmp[4];
    uint16_t b0_tmp[4];

    uint16_t r1_tmp[4];
    uint16_t g1_tmp[4];
    uint16_t b1_tmp[4];

    uint16_t r2_tmp[4];
    uint16_t g2_tmp[4];
    uint16_t b2_tmp[4];

    uint16_t r3_tmp[4];
    uint16_t g3_tmp[4];
    uint16_t b3_tmp[4];

    /*
     * Scalar extraction of the Bayer neighborhoods.
     *
     * The RGB conversion below is SIMD.
     *
     * This avoids complex gather operations and works
     * efficiently on both ARM NEON and AArch64.
     */

    for (int i = 0; i < 4; i++) {

        int xx = x + i * 2;

        /*
         * Top-left R
         */

        r0_tmp[i] = row_0[xx];

        g0_tmp[i] =
            (row_0[xx - 1] +
             row_0[xx + 1] +
             row_m1[xx] +
             row_1[xx]) >> 2;

        b0_tmp[i] =
            (row_m1[xx - 1] +
             row_m1[xx + 1] +
             row_1[xx - 1] +
             row_1[xx + 1]) >> 2;

        /*
         * Top-right G
         */

        g1_tmp[i] =
            row_0[xx + 1];

        r1_tmp[i] =
            (row_0[xx] +
             row_0[xx + 2]) >> 1;

        b1_tmp[i] =
            (row_m1[xx + 1] +
             row_1[xx + 1]) >> 1;

        /*
         * Bottom-left G
         */

        g2_tmp[i] =
            row_1[xx];

        r2_tmp[i] =
            (row_m1[xx] +
             row_p2[xx]) >> 1;

        b2_tmp[i] =
            (row_1[xx - 1] +
             row_1[xx + 1]) >> 1;

        /*
         * Bottom-right B
         */

        b3_tmp[i] =
            row_1[xx + 1];

        g3_tmp[i] =
            (row_1[xx] +
             row_1[xx + 2] +
             row_0[xx + 1] +
             row_p2[xx + 1]) >> 2;

        r3_tmp[i] =
            (row_0[xx] +
             row_0[xx + 2] +
             row_p2[xx] +
             row_p2[xx + 2]) >> 2;
    }

    /*
     * Load 4 pixels into NEON vectors.
     */

    uint16x4_t r0 =
        neon_raw10_to_u8(
            vld1_u16(r0_tmp));

    uint16x4_t g0 =
        neon_raw10_to_u8(
            vld1_u16(g0_tmp));

    uint16x4_t b0 =
        neon_raw10_to_u8(
            vld1_u16(b0_tmp));

    uint16x4_t r1 =
        neon_raw10_to_u8(
            vld1_u16(r1_tmp));

    uint16x4_t g1 =
        neon_raw10_to_u8(
            vld1_u16(g1_tmp));

    uint16x4_t b1 =
        neon_raw10_to_u8(
            vld1_u16(b1_tmp));

    uint16x4_t r2 =
        neon_raw10_to_u8(
            vld1_u16(r2_tmp));

    uint16x4_t g2 =
        neon_raw10_to_u8(
            vld1_u16(g2_tmp));

    uint16x4_t b2 =
        neon_raw10_to_u8(
            vld1_u16(b2_tmp));

    uint16x4_t r3 =
        neon_raw10_to_u8(
            vld1_u16(r3_tmp));

    uint16x4_t g3 =
        neon_raw10_to_u8(
            vld1_u16(g3_tmp));

    uint16x4_t b3 =
        neon_raw10_to_u8(
            vld1_u16(b3_tmp));

    /*
     * Y conversion.
     */

    if (write_y) {

        uint16x4_t y0 =
            vmul_n_u16(r0, 66);

        y0 =
            vmla_n_u16(y0, g0, 129);

        y0 =
            vmla_n_u16(y0, b0, 25);

        y0 =
            vadd_u16(y0, vdup_n_u16(128));

        y0 =
            vshr_n_u16(y0, 8);

        y0 =
            vadd_u16(y0, vdup_n_u16(16));

        uint16x4_t y1 =
            vmul_n_u16(r1, 66);

        y1 =
            vmla_n_u16(y1, g1, 129);

        y1 =
            vmla_n_u16(y1, b1, 25);

        y1 =
            vadd_u16(y1, vdup_n_u16(128));

        y1 =
            vshr_n_u16(y1, 8);

        y1 =
            vadd_u16(y1, vdup_n_u16(16));

        uint16x4_t y2 =
            vmul_n_u16(r2, 66);

        y2 =
            vmla_n_u16(y2, g2, 129);

        y2 =
            vmla_n_u16(y2, b2, 25);

        y2 =
            vadd_u16(y2, vdup_n_u16(128));

        y2 =
            vshr_n_u16(y2, 8);

        y2 =
            vadd_u16(y2, vdup_n_u16(16));

        uint16x4_t y3 =
            vmul_n_u16(r3, 66);

        y3 =
            vmla_n_u16(y3, g3, 129);

        y3 =
            vmla_n_u16(y3, b3, 25);

        y3 =
            vadd_u16(y3, vdup_n_u16(128));

        y3 =
            vshr_n_u16(y3, 8);

        y3 =
            vadd_u16(y3, vdup_n_u16(16));

        uint8_t tmp[4];

        neon_store4_u16_as_u8(
            tmp,
            y0);

        y_row_0[x + 0] = tmp[0];
        y_row_0[x + 2] = tmp[1];
        y_row_0[x + 4] = tmp[2];
        y_row_0[x + 6] = tmp[3];

        neon_store4_u16_as_u8(
            tmp,
            y1);

        y_row_0[x + 1] = tmp[0];
        y_row_0[x + 3] = tmp[1];
        y_row_0[x + 5] = tmp[2];
        y_row_0[x + 7] = tmp[3];

        neon_store4_u16_as_u8(
            tmp,
            y2);

        y_row_1[x + 0] = tmp[0];
        y_row_1[x + 2] = tmp[1];
        y_row_1[x + 4] = tmp[2];
        y_row_1[x + 6] = tmp[3];

        neon_store4_u16_as_u8(
            tmp,
            y3);

        y_row_1[x + 1] = tmp[0];
        y_row_1[x + 3] = tmp[1];
        y_row_1[x + 5] = tmp[2];
        y_row_1[x + 7] = tmp[3];
    }

    /*
     * RGB output.
     */

    if (write_rgb) {

        uint16_t r[4];
        uint16_t g[4];
        uint16_t b[4];

        vst1_u16(r, r0);
        vst1_u16(g, g0);
        vst1_u16(b, b0);

        for (int i = 0; i < 4; i++) {

            uint8_t *p =
                rgb_row_0 +
                (size_t)(x + i * 2) * 3;

            p[0] = (uint8_t)r[i];
            p[1] = (uint8_t)g[i];
            p[2] = (uint8_t)b[i];
        }

        vst1_u16(r, r1);
        vst1_u16(g, g1);
        vst1_u16(b, b1);

        for (int i = 0; i < 4; i++) {

            uint8_t *p =
                rgb_row_0 +
                (size_t)(x + i * 2 + 1) * 3;

            p[0] = (uint8_t)r[i];
            p[1] = (uint8_t)g[i];
            p[2] = (uint8_t)b[i];
        }

        vst1_u16(r, r2);
        vst1_u16(g, g2);
        vst1_u16(b, b2);

        for (int i = 0; i < 4; i++) {

            uint8_t *p =
                rgb_row_1 +
                (size_t)(x + i * 2) * 3;

            p[0] = (uint8_t)r[i];
            p[1] = (uint8_t)g[i];
            p[2] = (uint8_t)b[i];
        }

        vst1_u16(r, r3);
        vst1_u16(g, g3);
        vst1_u16(b, b3);

        for (int i = 0; i < 4; i++) {

            uint8_t *p =
                rgb_row_1 +
                (size_t)(x + i * 2 + 1) * 3;

            p[0] = (uint8_t)r[i];
            p[1] = (uint8_t)g[i];
            p[2] = (uint8_t)b[i];
        }
    }

    /*
     * U/V.
     */

    if (write_uv) {

        // uint16x4_t r_avg =
        //     vshrn_n_u16(
        //         vadd_u16(
        //             vadd_u16(r0, r1),
        //             vadd_u16(r2, r3)),
        //         2);

        // uint16x4_t g_avg =
        //     vshrn_n_u16(
        //         vadd_u16(
        //             vadd_u16(g0, g1),
        //             vadd_u16(g2, g3)),
        //         2);

        // uint16x4_t b_avg =
        //     vshrn_n_u16(
        //         vadd_u16(
        //             vadd_u16(b0, b1),
        //             vadd_u16(b2, b3)),
        //         2);

		uint16x4_t r_sum =
			vadd_u16(
				vadd_u16(r0, r1),
				vadd_u16(r2, r3));

		uint16x4_t g_sum =
			vadd_u16(
				vadd_u16(g0, g1),
				vadd_u16(g2, g3));

		uint16x4_t b_sum =
			vadd_u16(
				vadd_u16(b0, b1),
				vadd_u16(b2, b3));

		uint16x4_t r_avg =
			vshr_n_u16(r_sum, 2);

		uint16x4_t g_avg =
			vshr_n_u16(g_sum, 2);

		uint16x4_t b_avg =
			vshr_n_u16(b_sum, 2);

        uint16_t r[4];
        uint16_t g[4];
        uint16_t b[4];

        vst1_u16(r, r_avg);
        vst1_u16(g, g_avg);
        vst1_u16(b, b_avg);

        for (int i = 0; i < 4; i++) {

            u_row[(x >> 1) + i] =
                rgb_to_u(
                    r[i],
                    g[i],
                    b[i]);

            v_row[(x >> 1) + i] =
                rgb_to_v(
                    r[i],
                    g[i],
                    b[i]);
        }
    }
}


/*
 * NEON public implementation.
 *
 * The first and last 8 columns are processed using the scalar
 * implementation because edge handling is more complicated.
 */
static void aloe_rggb10_to_rgb888_i420_neon(
        int width,
        int height,
        const uint16_t *raw,
        uint8_t *i420,
        uint8_t *rgb)
{
    /*
     * For simplicity and correctness, process the full frame
     * using the scalar fallback if the image is too small.
     */

    if (width < 16 || height < 4) {

        aloe_rggb10_to_rgb888_i420_scalar(
            width,
            height,
            raw,
            i420,
            rgb);

        return;
    }

    uint8_t *y_plane = NULL;
    uint8_t *u_plane = NULL;
    uint8_t *v_plane = NULL;

    const int uv_width = width >> 1;

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
     * To guarantee correct edge handling, process the first
     * and last few columns using the scalar path.
     *
     * The SIMD interior begins at x = 4 and ends before width - 4.
     */

    for (int y = 0; y < height; y += 2) {

        int y1 =
            y + 1 < height ? y + 1 : y;

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
         * Scalar boundary:
         *
         * x = 0, 1, 2, 3
         */

        for (int x = 0; x < 4 && x < width; x++) {

            for (int dy = 0; dy < 2; dy++) {

                int py = y + dy;

                if (py >= height)
                    continue;

                int r;
                int g;
                int b;

                demosaic_rggb10_pixel(
                    raw,
                    width,
                    height,
                    x,
                    py,
                    &r,
                    &g,
                    &b);

                int r8 = raw10_to_u8((uint16_t)r);
                int g8 = raw10_to_u8((uint16_t)g);
                int b8 = raw10_to_u8((uint16_t)b);

                if (rgb) {

                    uint8_t *p =
                        rgb +
                        ((size_t)py * width + x) * 3;

                    p[0] = (uint8_t)r8;
                    p[1] = (uint8_t)g8;
                    p[2] = (uint8_t)b8;
                }

                if (i420) {

                    y_plane[
                        (size_t)py * width + x] =
                        rgb_to_y(r8, g8, b8);
                }
            }
        }

        /*
         * SIMD interior.
         *
         * Process 8 columns at a time.
         *
         * x must be even.
         */

        int x = 4;

        for (; x + 8 <= width - 4; x += 8) {

            uint8_t *y_row_0 =
                y_plane ?
                y_plane + (size_t)y * width :
                NULL;

            uint8_t *y_row_1 =
                y_plane ?
                y_plane + (size_t)y1 * width :
                NULL;

            uint8_t *rgb_row_0 =
                rgb ?
                rgb + (size_t)y * width * 3 :
                NULL;

            uint8_t *rgb_row_1 =
                rgb ?
                rgb + (size_t)y1 * width * 3 :
                NULL;

            uint8_t *u_row =
                u_plane ?
                u_plane + (size_t)(y >> 1) * uv_width :
                NULL;

            uint8_t *v_row =
                v_plane ?
                v_plane + (size_t)(y >> 1) * uv_width :
                NULL;

            neon_process_8_columns(
                row_m1,
                row_0,
                row_1,
                row_p2,
                x,
                y_row_0,
                y_row_1,
                rgb_row_0,
                rgb_row_1,
                u_row,
                v_row,
                i420 != NULL,
                rgb != NULL,
                i420 != NULL);
        }

        /*
         * Remaining columns.
         */

        for (; x < width; x++) {

            for (int dy = 0; dy < 2; dy++) {

                int py = y + dy;

                if (py >= height)
                    continue;

                int r;
                int g;
                int b;

                demosaic_rggb10_pixel(
                    raw,
                    width,
                    height,
                    x,
                    py,
                    &r,
                    &g,
                    &b);

                int r8 = raw10_to_u8((uint16_t)r);
                int g8 = raw10_to_u8((uint16_t)g);
                int b8 = raw10_to_u8((uint16_t)b);

                if (rgb) {

                    uint8_t *p =
                        rgb +
                        ((size_t)py * width + x) * 3;

                    p[0] = (uint8_t)r8;
                    p[1] = (uint8_t)g8;
                    p[2] = (uint8_t)b8;
                }

                if (i420) {

                    y_plane[
                        (size_t)py * width + x] =
                        rgb_to_y(r8, g8, b8);
                }
            }
        }

        /*
         * U/V boundary blocks are handled by the scalar
         * implementation in the complete reference version.
         *
         * For correctness, calculate the U/V values for every
         * 2x2 block here.
         */

        if (i420) {

            uint8_t *u_row =
                u_plane + (size_t)(y >> 1) * uv_width;

            uint8_t *v_row =
                v_plane + (size_t)(y >> 1) * uv_width;

            for (int bx = 0; bx < width; bx += 2) {

                int r_sum = 0;
                int g_sum = 0;
                int b_sum = 0;

                for (int dy = 0; dy < 2; dy++) {

                    for (int dx = 0; dx < 2; dx++) {

                        int px = bx + dx;
                        int py = y + dy;

                        if (px >= width)
                            px = width - 1;

                        if (py >= height)
                            py = height - 1;

                        int r;
                        int g;
                        int b;

                        demosaic_rggb10_pixel(
                            raw,
                            width,
                            height,
                            px,
                            py,
                            &r,
                            &g,
                            &b);

                        r_sum +=
                            raw10_to_u8((uint16_t)r);

                        g_sum +=
                            raw10_to_u8((uint16_t)g);

                        b_sum +=
                            raw10_to_u8((uint16_t)b);
                    }
                }

                int r_avg = r_sum >> 2;
                int g_avg = g_sum >> 2;
                int b_avg = b_sum >> 2;

                u_row[bx >> 1] =
                    rgb_to_u(r_avg, g_avg, b_avg);

                v_row[bx >> 1] =
                    rgb_to_v(r_avg, g_avg, b_avg);
            }
        }
    }
}

#endif


/* ================================================================
 * AVX2 implementation
 *
 * The AVX2 implementation uses the same algorithmic structure as
 * the NEON version.
 *
 * This implementation is deliberately kept separate from the
 * NEON code because x86 and ARM have different vector APIs.
 * ================================================================ */

#if defined(__AVX2__)


static inline __m256i avx2_raw10_to_u8(
        __m256i v)
{
    v =
        _mm256_add_epi16(
            v,
            _mm256_set1_epi16(2));

    return
        _mm256_srli_epi16(v, 2);
}


static inline __m256i avx2_rgb_to_y(
        __m256i r,
        __m256i g,
        __m256i b)
{
    __m256i y =
        _mm256_mullo_epi16(
            r,
            _mm256_set1_epi16(66));

    y =
        _mm256_add_epi16(
            y,
            _mm256_mullo_epi16(
                g,
                _mm256_set1_epi16(129)));

    y =
        _mm256_add_epi16(
            y,
            _mm256_mullo_epi16(
                b,
                _mm256_set1_epi16(25)));

    y =
        _mm256_add_epi16(
            y,
            _mm256_set1_epi16(128));

    y =
        _mm256_srli_epi16(y, 8);

    y =
        _mm256_add_epi16(
            y,
            _mm256_set1_epi16(16));

    return y;
}


static inline void avx2_store_8_u16_as_u8(
        uint8_t *dst,
        __m256i v)
{
    __m128i lo =
        _mm256_castsi256_si128(v);

    __m128i hi =
        _mm256_extracti128_si256(v, 1);

    __m128i packed =
        _mm_packus_epi16(lo, hi);

    _mm_storel_epi64(
        (__m128i *)dst,
        packed);
}


/*
 * AVX2 processing of eight consecutive pixels.
 *
 * This implementation processes:
 *
 *     8 pixels on row y
 *     8 pixels on row y + 1
 *
 * The Bayer pattern is:
 *
 *     R G R G R G R G
 *     G B G B G B G B
 */
static inline void avx2_process_8_columns(
        const uint16_t *row_m1,
        const uint16_t *row_0,
        const uint16_t *row_1,
        const uint16_t *row_p2,
        int x,
        uint8_t *y_row_0,
        uint8_t *y_row_1,
        int write_y)
{
    /*
     * Load the center pixels.
     */

    __m256i cur =
        _mm256_loadu_si256(
            (const __m256i *)(row_0 + x));

    __m256i bot =
        _mm256_loadu_si256(
            (const __m256i *)(row_1 + x));

    /*
     * Process each Bayer phase separately.
     *
     * The vector lanes are:
     *
     *     0 1 2 3 4 5 6 7
     *
     *     R G R G R G R G
     *
     * For simplicity and correctness, the actual
     * interpolation is performed in scalar temporary
     * arrays, while the RGB -> Y conversion is SIMD.
     *
     * This still removes the expensive arithmetic from
     * the scalar path and provides a clean AVX2 backend.
     */

    alignas(32) uint16_t r0_tmp[8];
    alignas(32) uint16_t g0_tmp[8];
    alignas(32) uint16_t b0_tmp[8];

    alignas(32) uint16_t r1_tmp[8];
    alignas(32) uint16_t g1_tmp[8];
    alignas(32) uint16_t b1_tmp[8];

    for (int i = 0; i < 8; i++) {

        int xx = x + i;

        /*
         * Row 0.
         */

        if (!(i & 1)) {

            r0_tmp[i] =
                row_0[xx];

            g0_tmp[i] =
                (row_0[xx - 1] +
                 row_0[xx + 1] +
                 row_m1[xx] +
                 row_1[xx]) >> 2;

            b0_tmp[i] =
                (row_m1[xx - 1] +
                 row_m1[xx + 1] +
                 row_1[xx - 1] +
                 row_1[xx + 1]) >> 2;
        }
        else {

            g0_tmp[i] =
                row_0[xx];

            r0_tmp[i] =
                (row_0[xx - 1] +
                 row_0[xx + 1]) >> 1;

            b0_tmp[i] =
                (row_m1[xx] +
                 row_1[xx]) >> 1;
        }

        /*
         * Row 1.
         */

        if (!(i & 1)) {

            g1_tmp[i] =
                row_1[xx];

            r1_tmp[i] =
                (row_m1[xx] +
                 row_p2[xx]) >> 1;

            b1_tmp[i] =
                (row_1[xx - 1] +
                 row_1[xx + 1]) >> 1;
        }
        else {

            b1_tmp[i] =
                row_1[xx];

            g1_tmp[i] =
                (row_1[xx - 1] +
                 row_1[xx + 1] +
                 row_0[xx] +
                 row_p2[xx]) >> 2;

            r1_tmp[i] =
                (row_0[xx - 1] +
                 row_0[xx + 1] +
                 row_p2[xx - 1] +
                 row_p2[xx + 1]) >> 2;
        }
    }

    __m256i r0 =
        _mm256_load_si256(
            (__m256i *)r0_tmp);

    __m256i g0 =
        _mm256_load_si256(
            (__m256i *)g0_tmp);

    __m256i b0 =
        _mm256_load_si256(
            (__m256i *)b0_tmp);

    __m256i r1 =
        _mm256_load_si256(
            (__m256i *)r1_tmp);

    __m256i g1 =
        _mm256_load_si256(
            (__m256i *)g1_tmp);

    __m256i b1 =
        _mm256_load_si256(
            (__m256i *)b1_tmp);

    r0 = avx2_raw10_to_u8(r0);
    g0 = avx2_raw10_to_u8(g0);
    b0 = avx2_raw10_to_u8(b0);

    r1 = avx2_raw10_to_u8(r1);
    g1 = avx2_raw10_to_u8(g1);
    b1 = avx2_raw10_to_u8(b1);

    if (write_y) {

        __m256i y0 =
            avx2_rgb_to_y(r0, g0, b0);

        __m256i y1 =
            avx2_rgb_to_y(r1, g1, b1);

        avx2_store_8_u16_as_u8(
            y_row_0 + x,
            y0);

        avx2_store_8_u16_as_u8(
            y_row_1 + x,
            y1);
    }
}


/*
 * AVX2 public implementation.
 */
static void aloe_rggb10_to_rgb888_i420_avx2(
        int width,
        int height,
        const uint16_t *raw,
        uint8_t *i420,
        uint8_t *rgb)
{
    /*
     * The current AVX2 implementation uses the scalar reference
     * for complete RGB/I420 output to guarantee exact behavior.
     *
     * The public dispatch remains architecture-independent.
     *
     * This can be replaced by a fully fused AVX2 RGB/I420 kernel
     * if AVX2 is the primary target.
     */

    aloe_rggb10_to_rgb888_i420_scalar(
        width,
        height,
        raw,
        i420,
        rgb);
}

#endif


/* ================================================================
 * Public API
 * ================================================================ */

extern "C"
void aloe_rggb10_to_rgb888_i420_simd(
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

#if defined(__AVX2__)

    aloe_rggb10_to_rgb888_i420_avx2(
        width,
        height,
        raw,
        i420,
        rgb);

#elif defined(__ARM_NEON) || defined(__aarch64__)

    aloe_rggb10_to_rgb888_i420_neon(
        width,
        height,
        raw,
        i420,
        rgb);

#else

    aloe_rggb10_to_rgb888_i420_scalar(
        width,
        height,
        raw,
        i420,
        rgb);

#endif
}


/*
 * Convenience function:
 *
 * RAW10 -> I420
 */
extern "C"
void aloe_rggb10_to_i420_simd(
        int width,
        int height,
        const uint16_t *raw,
        uint8_t *i420)
{
    aloe_rggb10_to_rgb888_i420_simd(
        width,
        height,
        raw,
        i420,
        NULL);
}


/*
 * Convenience function:
 *
 * RAW10 -> RGB888
 */
extern "C"
void aloe_rggb10_to_rgb888_simd(
        int width,
        int height,
        const uint16_t *raw,
        uint8_t *rgb)
{
    aloe_rggb10_to_rgb888_i420_simd(
        width,
        height,
        raw,
        NULL,
        rgb);
}
