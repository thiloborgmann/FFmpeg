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

#include "libavutil/aarch64/asm.S"
/*    for (; raw_samples < raw_samples_end; raw_samples++) {

//        y = 1 << 19;
//
//        for (sb = -opt_order; sb < 0; sb++)
//            y += (uint64_t)MUL64(lpc_cof[sb], raw_samples[sb]);
//
//        *raw_samples -= y >> 20;


        dsp->reconstruct(raw_samples, lpc_cof, opt_order);
    }
*/

function ff_alsdsp_reconstruct_neon, export = 1
        mov         x3, #1
        mov         v2.2D[0], x3
        mov         v2.2D[1], x3
        shl         D2, D2, #19

        mov         x6, #4
        neg         x4, x2
        mul         x4, x4, x6

        add         x0, x0, x4
        add         x1, x1, x4

1:      ld1         {v0.S}[0], [x0] , x6
        ld1         {v1.S}[0], [x1] , x6

        smull       v3.2D, v0.2S, v1.2S
        add         v2.2D, v2.2D, v3.2D

        adds        x4, x4, x6
        b.lt        1b

	shrn        v2.2S, v2.2D, #20

        ld1         {v3.S}[0], [x0]
        sub         v3.2S, v3.2S, v2.2S

        st1         {v3.S}[0], [x0]
        
        ret
endfunc
