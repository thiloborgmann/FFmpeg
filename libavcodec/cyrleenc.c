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

#include "cyrle_common.h"


typedef struct CYRLEEncContext {
	int32_t width;
	int32_t height;
} CYRLEEncContext;

static int encode_cyrle(AVCodecContext *avctx, AVPacket *pkt,
                      const AVFrame *pict, int *got_packet)
{
    CYRLEEncContext *s = avctx->priv_data;
/*    int ret;
    int enc_row_size;
    size_t max_packet_size;

    enc_row_size    = deflateBound(&s->zstream, (avctx->width * s->bits_per_pixel + 7) >> 3);
    max_packet_size =
        AV_INPUT_BUFFER_MIN_SIZE + // headers
        avctx->height * (
            enc_row_size +
            12 * (((int64_t)enc_row_size + IOBUF_SIZE - 1) / IOBUF_SIZE) // IDAT * ceil(enc_row_size / IOBUF_SIZE)
        );
    if (max_packet_size > INT_MAX)
        return AVERROR(ENOMEM);
    ret = ff_alloc_packet2(avctx, pkt, max_packet_size, 0);
    if (ret < 0)
        return ret;

    s->bytestream_start =
    s->bytestream       = pkt->data;
    s->bytestream_end   = pkt->data + pkt->size;

    AV_WB64(s->bytestream, PNGSIG);
    s->bytestream += 8;

    ret = encode_headers(avctx, pict);
    if (ret < 0)
        return ret;

    ret = encode_frame(avctx, pict);
    if (ret < 0)
        return ret;

    png_write_chunk(&s->bytestream, MKTAG('I', 'E', 'N', 'D'), NULL, 0);

    pkt->size = s->bytestream - s->bytestream_start;
    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;
*/
    return 0;
}

static av_cold int cyrle_enc_init(AVCodecContext *avctx)
{
    CYRLEEncContext *s = avctx->priv_data;

    av_log(NULL, AV_LOG_INFO, "cyrle_enc_init()\n");

	s->width = avctx->width;
	s->height = avctx->height;

    av_log(NULL, AV_LOG_INFO, "w: %i\nh: %i\n", s->width, s->height);
    return 0;
}

static av_cold int cyrle_enc_close(AVCodecContext *avctx)
{
    av_log(NULL, AV_LOG_INFO, "cyrle_enc_close()\n");
    return 0;
}




#define OFFSET(x) offsetof(PNGEncContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
/*    {"dpi", "Set image resolution (in dots per inch)",  OFFSET(dpi), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 0x10000, VE},
    {"dpm", "Set image resolution (in dots per meter)", OFFSET(dpm), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 0x10000, VE},
    { "pred", "Prediction method", OFFSET(filter_type), AV_OPT_TYPE_INT, { .i64 = PNG_FILTER_VALUE_NONE }, PNG_FILTER_VALUE_NONE, PNG_FILTER_VALUE_MIXED, VE, "pred" },
        { "none",  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = PNG_FILTER_VALUE_NONE },  INT_MIN, INT_MAX, VE, "pred" },
        { "sub",   NULL, 0, AV_OPT_TYPE_CONST, { .i64 = PNG_FILTER_VALUE_SUB },   INT_MIN, INT_MAX, VE, "pred" },
        { "up",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = PNG_FILTER_VALUE_UP },    INT_MIN, INT_MAX, VE, "pred" },
        { "avg",   NULL, 0, AV_OPT_TYPE_CONST, { .i64 = PNG_FILTER_VALUE_AVG },   INT_MIN, INT_MAX, VE, "pred" },
        { "paeth", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = PNG_FILTER_VALUE_PAETH }, INT_MIN, INT_MAX, VE, "pred" },
*/        { "mixed", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 0 }, INT_MIN, INT_MAX, VE, "pred" },
    { NULL},
};

static const AVClass cyrleenc_class = {
    .class_name = "CYRLE encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_cyrle_encoder = {
    .name           = "cyrle",
    .long_name      = NULL_IF_CONFIG_SMALL("Cypress RLE image"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_CYRLE,
    .priv_data_size = sizeof(CYRLEEncContext),
    .init           = cyrle_enc_init,
    .close          = cyrle_enc_close,
    .encode2        = encode_cyrle,
    .capabilities   = AV_CODEC_CAP_INTRA_ONLY,
    .pix_fmts       = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_RGB24/*, AV_PIX_FMT_RGBA,
        AV_PIX_FMT_RGB48BE, AV_PIX_FMT_RGBA64BE,
        AV_PIX_FMT_PAL8,
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY8A,
        AV_PIX_FMT_GRAY16BE, AV_PIX_FMT_YA16BE,
        AV_PIX_FMT_MONOBLACK*/, AV_PIX_FMT_NONE
    },
    .priv_class     = &cyrleenc_class,
};

