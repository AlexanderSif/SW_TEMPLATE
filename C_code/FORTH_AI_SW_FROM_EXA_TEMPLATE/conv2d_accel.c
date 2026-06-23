#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "xconv2d_accel.h"
#include "libubuf/libubuf.h"

/* ============================================================
 * User parameters
 * ============================================================ */
#define IMG_DIM   16
#define IN_CH      2
#define OUT_CH     2

#define PAD_TOP    1
#define PAD_RIGHT  1
#define PAD_LEFT   1
#define PAD_BOTTOM 1

#define FLOATS_PER_BEAT 16
#define WORDS_PER_BEAT  16
#define BEAT_BYTES      64


/* ============================================================
 * Derived constants
 * ============================================================ */
#define BEATS_PER_ROW   ((IMG_DIM + FLOATS_PER_BEAT - 1) / FLOATS_PER_BEAT)
#define IMG_BEATS       (IN_CH * IMG_DIM * BEATS_PER_ROW)
#define TOTAL_W_FLOATS  (OUT_CH * IN_CH * 9)
#define W_BEATS         ((TOTAL_W_FLOATS + FLOATS_PER_BEAT - 1) / FLOATS_PER_BEAT)
#define BIAS_BEATS      ((OUT_CH + FLOATS_PER_BEAT - 1) / FLOATS_PER_BEAT)

#define TOTAL_IN_BEATS  (1 + IMG_BEATS + W_BEATS + BIAS_BEATS)
#define TOTAL_OUT_BEATS (IN_CH * IMG_DIM * BEATS_PER_ROW)

static inline uint32_t float_to_bits(float f)
{
    union {
        float f;
        uint32_t u;
    } x;
    x.f = f;
    return x.u;
}

static inline float bits_to_float(uint32_t u)
{
    union {
        float f;
        uint32_t u;
    } x;
    x.u = u;
    return x.f;
}

static inline void beat_set_float(uint32_t *beat, int idx, float value)
{
    beat[idx] = float_to_bits(value);
}

static inline float beat_get_float(const uint32_t *beat, int idx)
{
    return bits_to_float(beat[idx]);
}

/* ============================================================
 * Fill DDR input buffer
 * ============================================================ */
static void build_input_buffer(uint32_t *buf)
{
    memset(buf, 0, TOTAL_IN_BEATS * BEAT_BYTES);

    /* --------------------------------------------
     * Beat 0: header
     * [img_dim | in_ch | out_ch | pad_top | pad_right | pad_left | pad_bottom]
     * -------------------------------------------- */
    uint32_t *hdr = buf + 0 * WORDS_PER_BEAT;
    hdr[0] = (uint32_t)IMG_DIM;
    hdr[1] = (uint32_t)IN_CH;
    hdr[2] = (uint32_t)OUT_CH;
    hdr[3] = (uint32_t)PAD_TOP;
    hdr[4] = (uint32_t)PAD_RIGHT;
    hdr[5] = (uint32_t)PAD_LEFT;
    hdr[6] = (uint32_t)PAD_BOTTOM;

    /* --------------------------------------------
     * Image beats: fill image with known values
     * You can use all 1.0f, but distinct values are
     * better for checking passthrough behavior.
     * -------------------------------------------- */
    for (int ic = 0; ic < IN_CH; ic++) {
        for (int r = 0; r < IMG_DIM; r++) {
            int beat_abs = 1
                         + ic * IMG_DIM * BEATS_PER_ROW
                         + r  * BEATS_PER_ROW;

            uint32_t *beat = buf + beat_abs * WORDS_PER_BEAT;

            for (int k = 0; k < FLOATS_PER_BEAT; k++) {
                int col = k;
                float val = 0.0f;

                if (col < IMG_DIM) {
                    /* unique deterministic test pattern */
                    val = (float)(1000 * ic + 100 * r + col + 1);
                }

                beat_set_float(beat, k, val);
            }
        }
    }

    /* --------------------------------------------
     * Weights: all 1.0f
     * -------------------------------------------- */
    {
        int w_offset_abs = 1 + IMG_BEATS;
        int float_idx = 0;

        for (int b = 0; b < W_BEATS; b++) {
            uint32_t *beat = buf + (w_offset_abs + b) * WORDS_PER_BEAT;

            for (int k = 0; k < FLOATS_PER_BEAT; k++) {
                float val = (float_idx < TOTAL_W_FLOATS) ? 1.0f : 0.0f;
                beat_set_float(beat, k, val);
                float_idx++;
            }
        }
    }

    /* --------------------------------------------
     * Bias: all 1.0f
     * -------------------------------------------- */
    {
        int bias_offset_abs = 1 + IMG_BEATS + W_BEATS;

        for (int b = 0; b < BIAS_BEATS; b++) {
            uint32_t *beat = buf + (bias_offset_abs + b) * WORDS_PER_BEAT;

            for (int k = 0; k < FLOATS_PER_BEAT; k++) {
                int oc = b * FLOATS_PER_BEAT + k;
                float val = (oc < OUT_CH) ? 1.0f : 0.0f;
                beat_set_float(beat, k, val);
            }
        }
    }
}

/* ============================================================
 * Dump header for debugging
 * ============================================================ */
static void dump_header(const uint32_t *buf)
{
    const uint32_t *hdr = buf;

    printf("Header contents:\n");
    printf("  img_dim    = %u\n", hdr[0]);
    printf("  in_ch      = %u\n", hdr[1]);
    printf("  out_ch     = %u\n", hdr[2]);
    printf("  pad_top    = %u\n", hdr[3]);
    printf("  pad_right  = %u\n", hdr[4]);
    printf("  pad_left   = %u\n", hdr[5]);
    printf("  pad_bottom = %u\n", hdr[6]);
}

/* ============================================================
 * Dump input image
 * ============================================================ */
static void dump_input_image(const uint32_t *buf)
{
    printf("\nInput image stored in DDR:\n");

    for (int ic = 0; ic < IN_CH; ic++) {
        printf("Channel %d:\n", ic);
        for (int r = 0; r < IMG_DIM; r++) {
            int beat_abs = 1
                         + ic * IMG_DIM * BEATS_PER_ROW
                         + r  * BEATS_PER_ROW;

            const uint32_t *beat = buf + beat_abs * WORDS_PER_BEAT;

            printf("  row %2d : ", r);
            for (int c = 0; c < IMG_DIM; c++) {
                float v = beat_get_float(beat, c);
                printf("%7.1f ", v);
            }
            printf("\n");
        }
    }
}

/* ============================================================
 * Dump output image
 * ============================================================ */
static void dump_output_image(const uint32_t *out_buf)
{
    printf("\nOutput image returned by HLS IP:\n");

    for (int ic = 0; ic < IN_CH; ic++) {
        printf("Channel %d:\n", ic);
        for (int r = 0; r < IMG_DIM; r++) {
            int beat_abs = ic * IMG_DIM * BEATS_PER_ROW
                         + r  * BEATS_PER_ROW;

            const uint32_t *beat = out_buf + beat_abs * WORDS_PER_BEAT;

            printf("  row %2d : ", r);
            for (int c = 0; c < IMG_DIM; c++) {
                float v = beat_get_float(beat, c);
                printf("%7.1f ", v);
            }
            printf("\n");
        }
    }
}

/* ============================================================
 * Verify output == input image
 * ============================================================ */
static int verify_output_against_input(const uint32_t *in_buf, const uint32_t *out_buf)
{
    int errors = 0;
    int checked = 0;

    for (int ic = 0; ic < IN_CH; ic++) {
        for (int r = 0; r < IMG_DIM; r++) {
            int in_beat_abs = 1
                            + ic * IMG_DIM * BEATS_PER_ROW
                            + r  * BEATS_PER_ROW;

            int out_beat_abs = ic * IMG_DIM * BEATS_PER_ROW
                             + r  * BEATS_PER_ROW;

            const uint32_t *in_beat  = in_buf  + in_beat_abs  * WORDS_PER_BEAT;
            const uint32_t *out_beat = out_buf + out_beat_abs * WORDS_PER_BEAT;

            for (int c = 0; c < IMG_DIM; c++) {
                float expected = beat_get_float(in_beat, c);
                float got      = beat_get_float(out_beat, c);
                checked++;

                if (fabsf(got - expected) > 1e-5f) {
                    printf("[FAIL] ic=%d r=%d c=%d expected=%.6f got=%.6f\n",
                           ic, r, c, expected, got);
                    errors++;
                }
            }
        }
    }

    printf("\nVerification summary:\n");
    printf("  Checked values : %d\n", checked);
    printf("  Errors         : %d\n", errors);

    return errors;
}

/* ============================================================
 * Main
 * ============================================================ */
int main(void)
{
    struct timeval t0, t1;
    double dt_ms;
    int ret;

    printf("=== conv2d_accel DDR host test ===\n");
    printf("IMG_DIM=%d IN_CH=%d OUT_CH=%d\n", IMG_DIM, IN_CH, OUT_CH);
    printf("Padding: top=%d right=%d left=%d bottom=%d\n",
           PAD_TOP, PAD_RIGHT, PAD_LEFT, PAD_BOTTOM);
    printf("BEATS_PER_ROW=%d IMG_BEATS=%d W_BEATS=%d BIAS_BEATS=%d TOTAL_IN_BEATS=%d TOTAL_OUT_BEATS=%d\n",
           BEATS_PER_ROW, IMG_BEATS, W_BEATS, BIAS_BEATS, TOTAL_IN_BEATS, TOTAL_OUT_BEATS);

    size_t in_size_bytes  = (size_t)TOTAL_IN_BEATS  * BEAT_BYTES;
    size_t out_size_bytes = (size_t)TOTAL_OUT_BEATS * BEAT_BYTES;

    printf("Allocating DDR input buffer : %zu bytes\n", in_size_bytes);
    printf("Allocating DDR output buffer: %zu bytes\n", out_size_bytes);

    uint32_t *in_buf  = (uint32_t *)ubuf_create(in_size_bytes, 1);
    uint32_t *out_buf = (uint32_t *)ubuf_create(out_size_bytes, 1);

    assert(in_buf  != NULL);
    assert(out_buf != NULL);

    memset(out_buf, 0, out_size_bytes);

    uint64_t in_phys  = ubuf_get_phys(in_buf);
    uint64_t out_phys = ubuf_get_phys(out_buf);

    assert(in_phys  != 0);
    assert(out_phys != 0);

    printf("CPU physical input  address: 0x%016llx\n", (unsigned long long)in_phys);
    printf("CPU physical output address: 0x%016llx\n", (unsigned long long)out_phys);

    build_input_buffer(in_buf);
    dump_header(in_buf);
    dump_input_image(in_buf);

    ret = ubuf_sync();
    assert(ret == 0);

    XConv2d_accel ip;
    ret = XConv2d_accel_Initialize(&ip, "conv2d_accel");
    assert(ret == XST_SUCCESS);

    printf("\nStarting IP...\n");

    gettimeofday(&t0, NULL);

	XConv2d_accel_Set_master_axi(&ip,  in_phys);
	XConv2d_accel_Set_output_mem(&ip, out_phys);
    XConv2d_accel_Start(&ip);

    while (XConv2d_accel_IsDone(&ip) == 0) {
        /* polling */
    }

    ret = ubuf_sync();
    assert(ret == 0);

    gettimeofday(&t1, NULL);
    dt_ms = (t1.tv_sec - t0.tv_sec) * 1000.0 +
            (t1.tv_usec - t0.tv_usec) / 1000.0;

    printf("IP finished in %.3f ms\n", dt_ms);

    dump_output_image(out_buf);

    int errors = verify_output_against_input(in_buf, out_buf);

    ret = XConv2d_accel_Release(&ip);
    assert(ret == XST_SUCCESS);

    ret = ubuf_destroy(in_buf);
    assert(ret == 0);

    ret = ubuf_destroy(out_buf);
    assert(ret == 0);

    if (errors == 0) {
        printf("\n[PASS] Output matches input image.\n");
        return 0;
    } else {
        printf("\n[FAIL] Output does not match input image.\n");
        return 1;
    }
}
