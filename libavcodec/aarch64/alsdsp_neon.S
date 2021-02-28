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

#include "libavutil/aarch64/asm.S"
#include "neon.S"

//void ff_alsdsp_reconstruct_all_neon(int32_t *samples, int32_t *samples_end, int32_t *coeffs, unsigned int opt_order);
// x0: int32_t *samples
// x1: int32_t *samples_end
// x2: int32_t *coeffs
// w3: unsigned int opt_order
function ff_alsdsp_reconstruct_all_neon, export = 1
    sub         sp, sp, #128
    st1         {v8.4s  - v11.4s}, [sp], #64
    st1         {v12.4s - v15.4s}, [sp], #64
// avoid 32-bit clubber from register
    lsl         x3, x3, #32
    neg         x3, x3, lsr #32
// x10 counts the bytes left to read, set to 4 * -opt_order
    lsl         x10, x3, #2

// loop x0 .. x1
1:  cmp         x0, x1
    b.eq        4f

// samples - opt_order, coeffs - opt_order
    add         x4, x0, x10
    add         x5, x2, x10
// reset local counter: count -opt_order .. 0
    mov         x6, x3

// reset local acc
    movi        v8.2d,  #0
    movi        v9.2d,  #0
    movi        v10.2d, #0
    movi        v11.2d, #0
    movi        v12.2d, #0
    movi        v13.2d, #0
    movi        v14.2d, #0
    movi        v15.2d, #0

// loop over 16 samples while >= 16 more to read
    adds        x6, x6, #16
    b.gt        3f

2:  ld1         {v0.4s - v3.4s}, [x4], #64
    ld1         {v4.4s - v7.4s}, [x5], #64

    smlal       v8.2d,  v0.2s, v4.2s
    smlal2      v9.2d,  v0.4s, v4.4s
    smlal       v10.2d, v1.2s, v5.2s
    smlal2      v11.2d, v1.4s, v5.4s
    smlal       v12.2d, v2.2s, v6.2s
    smlal2      v13.2d, v2.4s, v6.4s
    smlal       v14.2d, v3.2s, v7.2s
    smlal2      v15.2d, v3.4s, v7.4s

    adds        x6, x6, #16
    b.le        2b

// reduce to four NEON registers
// acc values into register
3:  subs        x6, x6, #16

    add         v4.2d, v8.2d,  v9.2d
    add         v5.2d, v10.2d, v11.2d
    add         v6.2d, v12.2d, v13.2d
    add         v7.2d, v14.2d, v15.2d

// next 8 samples
    cmn         x6, #8
    b.gt        3f

    ld1         {v0.4s - v1.4s}, [x4], #32
    ld1         {v2.4s - v3.4s}, [x5], #32

    smlal       v4.2d, v0.2s, v2.2s
    smlal2      v5.2d, v0.4s, v2.4s
    smlal       v6.2d, v1.2s, v3.2s
    smlal2      v7.2d, v1.4s, v3.4s

    adds        x6, x6, #8

// reduce to two NEON registers
// acc values into register
3:  add         v2.2d, v4.2d, v5.2d
    add         v3.2d, v6.2d, v7.2d

// next 4 samples
    cmn         x6, #4
    b.gt        3f

    ld1         {v0.4s}, [x4], #16
    ld1         {v1.4s}, [x5], #16

    smlal       v2.2d, v0.2s, v1.2s
    smlal2      v3.2d, v0.4s, v1.4s

    adds        x6, x6, #4

// reduce to A64 registers
// acc values into register
3:  add         v2.2d, v2.2d, v3.2d
    mov         x7, v2.2d[0]
    mov         x8, v2.2d[1]
    add         x7, x7, x8

    cmn         x6, #0
    b.eq        3f

// loop over the remaining < 4 samples to read
2:  ldrsw       x8, [x4], #4
    ldrsw       x9, [x5], #4

    madd        x7, x8, x9, x7
    adds        x6, x6, #1
    b.lt        2b

// add 1<<19 and store s-=X>>20
3:  mov         x9, #1
    lsl         x9, x9, #19
    add         x7, x7, x9
    neg         x7, x7, asr #20

    ldrsw       x9, [x4]
    add         x9, x9, x7
    str         w9, [x4]

// increment samples and loop
    add         x0, x0, #4
    b           1b

4:  sub         sp, sp, #128
    ld1         {v8.4s  - v11.4s}, [sp], #64
    ld1         {v12.4s - v15.4s}, [sp], #64

    ret
endfunc
