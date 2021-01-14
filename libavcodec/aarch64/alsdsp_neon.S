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

function ff_alsdsp_reconstruct_neon1, export = 1
        mov         x3, #1
        mov         v2.2D[0], x3
        shl         D2, D2, #19

        mov         x6, #4 // size of one elem
// ignore clobber
        //neg         x2, x2
        //mul         x2, x2, x6
// shift away clobber
        lsl         x2, x2, #32
        neg         x2, x2, lsr #32
        mul         x2, x2, x6 // count bytes for all elems intead of index
//

        add         x0, x0, x2
        add         x1, x1, x2

// single value madd
1:      ld1         {v0.S}[0], [x0] , x6
        ld1         {v1.S}[0], [x1] , x6

        smull       v3.2D, v0.2S, v1.2S
        add         v2.2D, v2.2D, v3.2D

        adds        x2, x2, x6
        b.lt        1b

        shrn        v2.2S, v2.2D, #20

        ld1         {v3.S}[0], [x0]
        sub         v3.2S, v3.2S, v2.2S

        st1         {v3.S}[0], [x0]
        ret
endfunc


function ff_alsdsp_reconstruct_neon, export = 1
        mov         x3, #1
        mov         v2.2D[0], x3
        shl         D2, D2, #19

        mov         x6, #4 // size of one elem
// ignore clobber
        //neg         x2, x2
        //mul         x2, x2, x6
// shift away clobber
        lsl         x2, x2, #32
        neg         x2, x2, lsr #32
        mul         x2, x2, x6 // count bytes for all elems intead of index
//
        mov         x6, #8 // iterate by size of all elems read (two-values: 2*size of elem)

        add         x0, x0, x2
        add         x1, x1, x2


// two-value madd
1:      ld1         {v0.2S}, [x0] , x6
        ld1         {v1.2S}, [x1] , x6

        smull       v3.2D, v0.2S, v1.2S
        add         v2.2D, v2.2D, v3.2D

        adds        x2, x2, x6
        b.lt        1b

        mov         x4, v2.2D[0]

        b.eq        2f
        sub         v2.2D, v2.2D, v3.2D
        sub         x0, x0, #4

2:
        mov         x5, v2.2D[1]

        add         x4, x4, x5 
        neg         x4, x4, asr #20

        ldr         w6, [x0]
        add         x6, x6, x4
        str         w6, [x0]
       
        ret
endfunc
