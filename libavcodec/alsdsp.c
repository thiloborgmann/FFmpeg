/*
 * Copyright (c) 2012 Mans Rullgard <mans@mansr.com>
 *
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

#include "libavutil/attributes.h"
#include "libavutil/samplefmt.h"
#include "mathops.h"
#include "alsdsp.h"
#include "config.h"

static void als_reconstruct_all_c(int32_t *raw_samples, int32_t *raw_samples_end, int32_t *lpc_cof, unsigned int opt_order)
{
	int64_t y;
	int sb;

        for (; raw_samples < raw_samples_end; raw_samples++) {
            y = 1 << 19;
 
            for (sb = -opt_order; sb < 0; sb++)
                y += (uint64_t)MUL64(lpc_cof[sb], raw_samples[sb]);
 
            *raw_samples -= y >> 20;
        }
}


av_cold void ff_alsdsp_init(ALSDSPContext *ctx)
{
    ctx->reconstruct_all = als_reconstruct_all_c;

    if (ARCH_AARCH64)
        ff_alsdsp_init_neon(ctx);
}
