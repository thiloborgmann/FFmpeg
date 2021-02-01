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


//void ff_alsdsp_reconstruct_neon(int32_t *samples, int32_t *coeffs, unsigned int opt_order);
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

//void ff_alsdsp_reconstruct_all_neon(int32_t *samples, int32_t *samples_end, int32_t *coeffs, unsigned int opt_order);
// x0: int32_t *samples
// x1: int32_t *samples_end
// x2: int32_t *coeffs
// w3: unsigned int opt_order
function ff_alsdsp_reconstruct_all_neon, export = 1
// avoid 32-bit clubber from register
    lsl         x3, x3, #32
    neg         x3, x3, lsr #32
// x10 counts the bytes left to read, set to 4 * -opt_order
    lsl         x10, x3, #2

// loop over samples value by value
// check for end of samples
1:  cmp         x0, x1
    b.eq        8f

// local copies of x0, x2: samples-opt_order, coeffs-opt_order
    add         x4, x0, x10
    add         x5, x2, x10
// reset local counter: count -opt_order .. 0
    mov         x6, x3

// reset local acc
//    movi        v2.2D, #0
//    movi        v3.2D, #0

//
    movi        v4.2D, #0
    movi        v5.2D, #0
    movi        v6.2D, #0
    movi        v7.2D, #0

// loop over 8 samples if >= 8 more to read
    adds        x6, x6, #8
    b.gt        3f

2:  ld1		{v0.4S - v1.4S}, [x4], #32
    ld1         {v2.4S - v3.4S}, [x5], #32

    smlal       v4.2D, v0.2S, v2.2S
    smlal2      v5.2D, v0.4S, v2.4S
    smlal       v6.2D, v1.2S, v3.2S
    smlal2      v7.2D, v1.4S, v3.4S

    adds        x6, x6, #8
    b.le        2b

// reduce to two NEON registers
// acc values into register
3:  subs        x6, x6, #8 // x6 holds now how many to go.. -x
    
    add         v2.2D, v4.2D, v5.2D
    add         v3.2D, v6.2D, v7.2D

// loop over 4 samples if >= 4 more to read
    adds        x6, x6, #4
    b.gt        5f

4:  ld1		{v0.4S}, [x4], #16
    ld1         {v1.4S}, [x5], #16

    smlal       v2.2D, v0.2S, v1.2S
    smlal2      v3.2D, v0.4S, v1.4S

    adds        x6, x6, #4
    b.le        4b

// reduce to A64 registers
// acc values into register
5:  subs        x6, x6, #4 // x6 holds now how many to go.. -x

    add         v2.2D, v2.2D, v3.2D
    mov         x7, v2.2D[0]
    mov         x8, v2.2D[1]
    add         x7, x7, x8
    b.eq        7f

// loop over the remaining < 4 samples to read
6:  ldrsw       x8, [x4], #4
    ldrsw       x9, [x5], #4

    madd        x7, x8, x9, x7
    adds        x6, x6, #1
    b.lt        6b

// add 1<<19 and store s-=X>>20
7:  mov         x9, #1
    lsl         x9, x9, #19
    add         x7, x7, x9
    neg         x7, x7, asr #20

    //ldrsh       w9, [x4]
    ldrsw       x9, [x4]
    add         x9, x9, x7
    str         w9, [x4]    

// increment samples and loop
    add         x0, x0, #4
    b           1b

8:  ret
endfunc
