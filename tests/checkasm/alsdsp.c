/*
 * Copyright (c) 2015 James Almer
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <string.h>
#include "checkasm.h"
#include "libavcodec/alsdsp.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

#define COEFFS_NUM 8
#define BUF_SIZE (8*8*4)

#define randomize_buffers()                    \
    do {                                       \
        int i;                                 \
        for (i = 0; i < COEFFS_NUM*2; i += 4) {  \
                uint32_t r = rnd();            \
                AV_WN32A(&ref_coeffs[i], r);    \
                AV_WN32A(&new_coeffs[i], r);    \
                r = rnd();                     \
                AV_WN32A(&ref_samples[i], r);   \
                AV_WN32A(&new_samples[i], r);   \
        }                                      \
    } while (0)


void checkasm_check_alsdsp(void)
{
    LOCAL_ALIGNED_16(uint32_t, ref_samples, [1024]);
    LOCAL_ALIGNED_16(uint32_t, ref_coeffs,  [1024]);
    LOCAL_ALIGNED_16(uint32_t, new_samples, [1024]);
    LOCAL_ALIGNED_16(uint32_t, new_coeffs,  [1024]);
/*
	int32_t ref_samples[1024] = {0,};
	int32_t ref_coeffs [1024] = {0,};
	int32_t new_samples[1024] = {0,};
	int32_t new_coeffs [1024] = {0,};
	*/
    ALSDSPContext dsp;
    ff_alsdsp_init(&dsp);

    if (check_func(dsp.reconstruct, "als_reconstruct")) {
	declare_func(void, int32_t *samples, int32_t *coeffs, unsigned int opt_order);
int32_t *s, *c;
unsigned int o = 9;
//	randomize_buffers();
        for (int i = 0; i < o+1; i++) {
		ref_samples[i] = i;
		ref_coeffs [i] = i;
		new_samples[i] = i;
		new_coeffs [i] = i;
	}

	s = (int32_t*)(ref_samples + o);
	c = (int32_t*)(ref_coeffs + o);
	call_ref(s, c, o);

	s = (int32_t*)(new_samples + o);
	c = (int32_t*)(new_coeffs + o);
	call_new(s, c, o);

	if (memcmp(ref_samples, new_samples, o+1) || memcmp(ref_coeffs, new_coeffs, o+1))
            fail();
	bench_new(new_samples, new_coeffs, o);

    }
    else av_log(NULL, AV_LOG_INFO, "!check_func\n");
    report("reconstruct"); // gets called
}
/*
#define BUF_SIZE 256
#define MAX_CHANNELS 8

#define randomize_buffers()                                 \
    do {                                                    \
        int i, j;                                           \
        for (i = 0; i < BUF_SIZE; i += 4) {                 \
            for (j = 0; j < channels; j++) {                \
                uint32_t r = rnd() & (1 << (bits - 2)) - 1; \
                AV_WN32A(ref_src[j] + i, r);                \
                AV_WN32A(new_src[j] + i, r);                \
            }                                               \
        }                                                   \
    } while (0)

static void check_decorrelate(uint8_t **ref_dst, uint8_t **ref_src, uint8_t **new_dst, uint8_t **new_src,
                              int channels, int bits) {
    declare_func(void, uint8_t **out, int32_t **in, int channels, int len, int shift);

    randomize_buffers();
    call_ref(ref_dst, (int32_t **)ref_src, channels, BUF_SIZE / sizeof(int32_t), 8);
    call_new(new_dst, (int32_t **)new_src, channels, BUF_SIZE / sizeof(int32_t), 8);
    if (memcmp(*ref_dst, *new_dst, bits == 16 ? BUF_SIZE * (channels/2) : BUF_SIZE * channels) ||
        memcmp(*ref_src, *new_src, BUF_SIZE * channels))
        fail();
    bench_new(new_dst, (int32_t **)new_src, channels, BUF_SIZE / sizeof(int32_t), 8);
}

void checkasm_check_flacdsp(void)
{
    LOCAL_ALIGNED_16(uint8_t, ref_dst, [BUF_SIZE*MAX_CHANNELS]);
    LOCAL_ALIGNED_16(uint8_t, ref_buf, [BUF_SIZE*MAX_CHANNELS]);
    LOCAL_ALIGNED_16(uint8_t, new_dst, [BUF_SIZE*MAX_CHANNELS]);
    LOCAL_ALIGNED_16(uint8_t, new_buf, [BUF_SIZE*MAX_CHANNELS]);
    uint8_t *ref_src[] = { &ref_buf[BUF_SIZE*0], &ref_buf[BUF_SIZE*1], &ref_buf[BUF_SIZE*2], &ref_buf[BUF_SIZE*3],
                           &ref_buf[BUF_SIZE*4], &ref_buf[BUF_SIZE*5], &ref_buf[BUF_SIZE*6], &ref_buf[BUF_SIZE*7] };
    uint8_t *new_src[] = { &new_buf[BUF_SIZE*0], &new_buf[BUF_SIZE*1], &new_buf[BUF_SIZE*2], &new_buf[BUF_SIZE*3],
                           &new_buf[BUF_SIZE*4], &new_buf[BUF_SIZE*5], &new_buf[BUF_SIZE*6], &new_buf[BUF_SIZE*7] };
    static const char * const names[3] = { "ls", "rs", "ms" };
    static const struct {
        enum AVSampleFormat fmt;
        int bits;
    } fmts[] = {
        { AV_SAMPLE_FMT_S16, 16 },
        { AV_SAMPLE_FMT_S32, 32 },
    };
    FLACDSPContext h;
    int i, j;

    for (i = 0; i < 2; i++) {
        ff_flacdsp_init(&h, fmts[i].fmt, 2, 0);
        for (j = 0; j < 3; j++)
            if (check_func(h.decorrelate[j], "flac_decorrelate_%s_%d", names[j], fmts[i].bits))
                check_decorrelate(&ref_dst, ref_src, &new_dst, new_src, 2, fmts[i].bits);
        for (j = 2; j <= MAX_CHANNELS; j += 2) {
            ff_flacdsp_init(&h, fmts[i].fmt, j, 0);
            if (check_func(h.decorrelate[0], "flac_decorrelate_indep%d_%d", j, fmts[i].bits))
                check_decorrelate(&ref_dst, ref_src, &new_dst, new_src, j, fmts[i].bits);
        }
    }

    report("decorrelate");
}
*/
