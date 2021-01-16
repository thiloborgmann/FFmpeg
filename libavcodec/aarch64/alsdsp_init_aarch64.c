/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"

#include "libavutil/aarch64/cpu.h"
#include "libavcodec/alsdsp.h"
/*
void ff_ps_add_squares_neon(float *dst, const float (*src)[2], int n);
void ff_ps_mul_pair_single_neon(float (*dst)[2], float (*src0)[2],
                                float *src1, int n);
void ff_ps_hybrid_analysis_neon(float (*out)[2], float (*in)[2],
                                const float (*filter)[8][2],
                                ptrdiff_t stride, int n);
void ff_ps_stereo_interpolate_neon(float (*l)[2], float (*r)[2],
                                   float h[2][4], float h_step[2][4],
                                   int len);
void ff_ps_stereo_interpolate_ipdopd_neon(float (*l)[2], float (*r)[2],
                                          float h[2][4], float h_step[2][4],
                                          int len);
					  */

void ff_alsdsp_reconstruct_neon(int32_t *samples, int32_t *coeffs, unsigned int opt_order);
void ff_alsdsp_reconstruct_all_neon(int32_t *samples, int32_t *samples_end, int32_t *coeffs, unsigned int opt_order);

av_cold void ff_alsdsp_init_neon(ALSDSPContext *s)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
        s->reconstruct     = ff_alsdsp_reconstruct_neon;
        s->reconstruct_all = ff_alsdsp_reconstruct_all_neon;
    }
}
