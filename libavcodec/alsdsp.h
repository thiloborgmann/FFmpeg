/*
 * Copyright (c) 2021 Thilo Borgmann <thilo.borgmann _at_ mail.de>
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

#ifndef AVCODEC_ALSDSP_H
#define AVCODEC_ALSDSP_H

#include <stdint.h>
#include "libavutil/internal.h"
#include "libavutil/samplefmt.h"

typedef struct ALSDSPContext {
	void (*reconstruct_all)(int32_t *raw_samples, int32_t *raw_samples_end, int32_t *lpc_cof, unsigned int opt_order);
} ALSDSPContext;

void ff_alsdsp_init(ALSDSPContext *c);
void ff_alsdsp_init_neon(ALSDSPContext *c);

#endif /* AVCODEC_ALSDSP_H */
