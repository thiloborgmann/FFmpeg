/*
 * Cypress RLE image format
 * Copyright (c) 2019 Thilo Borgmann <thilo.borgmann _at_ mail.de>
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

//#define DEBUG

//#include "libavutil/avassert.h"
//#include "libavutil/bprint.h"
//#include "libavutil/imgutils.h"
//#include "libavutil/stereo3d.h"
//#include "libavutil/mastering_display_metadata.h"

#include "cyrle_common.h"


typedef struct CYRLEDecContext {
    /*AVCodecContext *avctx;

    GetByteContext gb;
    ThreadFrame previous_picture;
    ThreadFrame last_picture;
    ThreadFrame picture;

    enum PNGHeaderState hdr_state;
    enum PNGImageState pic_state;
    int width, height;
    int cur_w, cur_h;
    int last_w, last_h;
    int x_offset, y_offset;
    int last_x_offset, last_y_offset;
    uint8_t dispose_op, blend_op;
    uint8_t last_dispose_op;
    int bit_depth;
    int color_type;
    int compression_type;
    int interlace_type;
    int filter_type;
    int channels;
    int bits_per_pixel;
    int bpp;
    int has_trns;
    uint8_t transparent_color_be[6];

    uint8_t *image_buf;
    int image_linesize;
    uint32_t palette[256];
    uint8_t *crow_buf;
    uint8_t *last_row;
    unsigned int last_row_size;
    uint8_t *tmp_row;
    unsigned int tmp_row_size;
    uint8_t *buffer;
    int buffer_size;
    int pass;
    int y;
    z_stream zstream;*/
} CYRLEDecContext;



static int decode_cyrle(AVCodecContext *avctx,
                        void *data, int *got_frame,
                        AVPacket *avpkt)
{
/*
    PNGDecContext *const s = avctx->priv_data;
    const uint8_t *buf     = avpkt->data;
    int buf_size           = avpkt->size;
    AVFrame *p;
    int64_t sig;
    int ret;

    ff_thread_release_buffer(avctx, &s->last_picture);
    FFSWAP(ThreadFrame, s->picture, s->last_picture);
    p = s->picture.f;

    bytestream2_init(&s->gb, buf, buf_size);

    // check signature
    sig = bytestream2_get_be64(&s->gb);
    if (sig != PNGSIG &&
        sig != MNGSIG) {
        av_log(avctx, AV_LOG_ERROR, "Invalid PNG signature 0x%08"PRIX64".\n", sig);
        return AVERROR_INVALIDDATA;
    }

    s->y = s->has_trns = 0;
    s->hdr_state = 0;
    s->pic_state = 0;

    s->zstream.zalloc = ff_png_zalloc;
    s->zstream.zfree  = ff_png_zfree;
    s->zstream.opaque = NULL;
    ret = inflateInit(&s->zstream);
    if (ret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "inflateInit returned error %d\n", ret);
        return AVERROR_EXTERNAL;
    }

    if ((ret = decode_frame_common(avctx, s, p, avpkt)) < 0)
        goto the_end;

    if (avctx->skip_frame == AVDISCARD_ALL) {
        *got_frame = 0;
        ret = bytestream2_tell(&s->gb);
        goto the_end;
    }

    if ((ret = av_frame_ref(data, s->picture.f)) < 0)
        goto the_end;

    *got_frame = 1;

    ret = bytestream2_tell(&s->gb);
the_end:
    inflateEnd(&s->zstream);
    s->crow_buf = NULL;
    return ret;
	*/
			return 0;
}

static void decode_flush(AVCodecContext *avctx)
{
    CYRLEDecContext *s = avctx->priv_data;

//    av_frame_unref(s->last_picture.f);
}


static av_cold int cyrle_dec_init(AVCodecContext *avctx)
{
/*    PNGDecContext *s = avctx->priv_data;

    avctx->color_range = AVCOL_RANGE_JPEG;

    if (avctx->codec_id == AV_CODEC_ID_LSCR)
        avctx->pix_fmt = AV_PIX_FMT_BGR24;

    s->avctx = avctx;
    s->previous_picture.f = av_frame_alloc();
    s->last_picture.f = av_frame_alloc();
    s->picture.f = av_frame_alloc();
    if (!s->previous_picture.f || !s->last_picture.f || !s->picture.f) {
        av_frame_free(&s->previous_picture.f);
        av_frame_free(&s->last_picture.f);
        av_frame_free(&s->picture.f);
        return AVERROR(ENOMEM);
    }

    if (!avctx->internal->is_copy) {
        avctx->internal->allocate_progress = 1;
        ff_pngdsp_init(&s->dsp);
    }
*/
    return 0;
}

static av_cold int cyrle_dec_end(AVCodecContext *avctx)
{
 /*
    PNGDecContext *s = avctx->priv_data;

    ff_thread_release_buffer(avctx, &s->previous_picture);
    av_frame_free(&s->previous_picture.f);
    ff_thread_release_buffer(avctx, &s->last_picture);
    av_frame_free(&s->last_picture.f);
    ff_thread_release_buffer(avctx, &s->picture);
    av_frame_free(&s->picture.f);
    av_freep(&s->buffer);
    s->buffer_size = 0;
    av_freep(&s->last_row);
    s->last_row_size = 0;
    av_freep(&s->tmp_row);
    s->tmp_row_size = 0;
*/
    return 0;
}

AVCodec ff_cyrle_decoder = {
    .name           = "cyrle",
    .long_name      = NULL_IF_CONFIG_SMALL("Cypress RLE image"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_CYRLE,
    .priv_data_size = sizeof(CYRLEDecContext),
    .init           = cyrle_dec_init,
    .close          = cyrle_dec_end,
    .decode         = decode_cyrle,
    .capabilities   = AV_CODEC_CAP_DR1,
};

