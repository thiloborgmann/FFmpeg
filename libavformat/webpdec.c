/*
 * WebP demuxer
 * Copyright (c) 2020 Pexeso Inc.
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

/**
 * @file
 * WebP demuxer.
 */

#include "avformat.h"
#include "internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"

typedef struct WebPDemuxContext {
    const AVClass *class;
    /**
     * Time span in milliseconds before the next frame
     * should be drawn on screen.
     */
    int delay;
    /**
     * Minimum allowed delay between frames in milliseconds.
     * Values below this threshold are considered to be invalid
     * and set to value of default_delay.
     */
    int min_delay;
    int max_delay;
    int default_delay;

    /*
     * loop options
     */
    int ignore_loop;                ///< ignore loop setting
    int num_loop;                   ///< number of times to loop the animation
    int cur_loop;                   ///< current loop counter
    int64_t file_start;             ///< start position of the current animation file
    uint32_t remaining_size;        ///< remaining size of the current animation file

    /*
     * variables for the key frame detection
     */
    int nb_frames;                  ///< number of frames of the current animation file
    int vp8x_flags;
    int canvas_width;               ///< width of the canvas
    int canvas_height;              ///< height of the canvas
} WebPDemuxContext;

/**
 * Major web browsers display WebPs at ~10-15fps when rate is not
 * explicitly set or have too low values. We assume default rate to be 10.
 * Default delay = 1000 microseconds / 10fps = 100 milliseconds per frame.
 */
#define WEBP_DEFAULT_DELAY   100
/**
 * By default delay values less than this threshold considered to be invalid.
 */
#define WEBP_MIN_DELAY       10

static int webp_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;

    if (AV_RB32(b)     == MKBETAG('R', 'I', 'F', 'F') &&
        AV_RB32(b + 8) == MKBETAG('W', 'E', 'B', 'P'))
        return AVPROBE_SCORE_MAX;

    return 0;
}

static int webp_read_header(AVFormatContext *s)
{
    WebPDemuxContext *wdc = s->priv_data;
    AVIOContext      *pb  = s->pb;
    AVStream         *st;
    int ret, n;
    uint32_t chunk_type, chunk_size;
    int canvas_width  = 0;
    int canvas_height = 0;
    int width         = 0;
    int height        = 0;

    wdc->delay = wdc->default_delay;
    wdc->num_loop = 1;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    wdc->file_start     = avio_tell(pb);
    wdc->remaining_size = avio_size(pb) - wdc->file_start;

    while (wdc->remaining_size > 8 && !avio_feof(pb)) {
        chunk_type = avio_rl32(pb);
        chunk_size = avio_rl32(pb);
        if (chunk_size == UINT32_MAX)
            return AVERROR_INVALIDDATA;
        chunk_size += chunk_size & 1;
        if (avio_feof(pb))
            break;

        if (wdc->remaining_size < 8 + chunk_size)
            return AVERROR_INVALIDDATA;

        if (chunk_type == MKTAG('R', 'I', 'F', 'F')) {
            wdc->remaining_size = 8 + chunk_size;
            chunk_size = 4;
        }

        wdc->remaining_size -= 8 + chunk_size;

        switch (chunk_type) {
        case MKTAG('V', 'P', '8', 'X'):
            if (chunk_size >= 10) {
                avio_skip(pb, 4);
                canvas_width  = avio_rl24(pb) + 1;
                canvas_height = avio_rl24(pb) + 1;
                ret = avio_skip(pb, chunk_size - 10);
            } else
                ret = avio_skip(pb, chunk_size);
            break;
        case MKTAG('V', 'P', '8', ' '):
            if (chunk_size >= 10) {
                avio_skip(pb, 6);
                width  = avio_rl16(pb) & 0x3fff;
                height = avio_rl16(pb) & 0x3fff;
                ret = avio_skip(pb, chunk_size - 10);
            } else
                ret = avio_skip(pb, chunk_size);
            break;
        case MKTAG('V', 'P', '8', 'L'):
            if (chunk_size >= 5) {
                avio_skip(pb, 1);
                n = avio_rl32(pb);
                width  = (n & 0x3fff) + 1;          // first 14 bits
                height = ((n >> 14) & 0x3fff) + 1;  // next 14 bits
                ret = avio_skip(pb, chunk_size - 5);
            } else
                ret = avio_skip(pb, chunk_size);
            break;
        case MKTAG('A', 'N', 'M', 'F'):
            if (chunk_size >= 12) {
                avio_skip(pb, 6);
                width  = avio_rl24(pb) + 1;
                height = avio_rl24(pb) + 1;
                ret = avio_skip(pb, chunk_size - 12);
            } else
                ret = avio_skip(pb, chunk_size);
            break;
        default:
            ret = avio_skip(pb, chunk_size);
            break;
        }

        if (ret < 0)
            return ret;

        // set canvas size if no VP8X chunk was present
        if (!canvas_width && width > 0)
            canvas_width = width;
        if (!canvas_height && height > 0)
            canvas_height = height;
    }

    // WebP format operates with time in "milliseconds", therefore timebase is 1/1000
    avpriv_set_pts_info(st, 64, 1, 1000);
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id   = AV_CODEC_ID_WEBP;
    st->codecpar->codec_tag  = MKTAG('W', 'E', 'B', 'P');
    st->codecpar->width      = canvas_width;
    st->codecpar->height     = canvas_height;
    st->start_time           = 0;

    // jump to start
    if ((ret = avio_seek(pb, wdc->file_start, SEEK_SET)) < 0)
        return ret;
    wdc->remaining_size = 0;

    return 0;
}

static int webp_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    WebPDemuxContext *wdc = s->priv_data;
    AVIOContext      *pb  = s->pb;
    int ret, n;
    int64_t packet_start = avio_tell(pb);
    int64_t packet_end;
    uint32_t chunk_type;
    uint32_t chunk_size;
    int width = 0;
    int height = 0;
    int is_frame = 0;
    int key_frame = 0;

    if (wdc->remaining_size == 0) {
        wdc->remaining_size = avio_size(pb) - avio_tell(pb);
        if (wdc->remaining_size == 0) { // EOF
            int ret;
            wdc->delay = wdc->default_delay;
            if (wdc->ignore_loop ||
               (wdc->num_loop && wdc->cur_loop == wdc->num_loop - 1))
                return AVERROR_EOF;

            av_log(s, AV_LOG_DEBUG, "loop: %d\n", wdc->cur_loop);

            wdc->cur_loop++;
            ret = avio_seek(pb, wdc->file_start, SEEK_SET);
            if (ret < 0)
                return AVERROR_INVALIDDATA;
            wdc->remaining_size = avio_size(pb) - avio_tell(pb);
        }
    }

    while (wdc->remaining_size > 0 && !avio_feof(pb)) {
        chunk_type = avio_rl32(pb);
        chunk_size = avio_rl32(pb);
        if (chunk_size == UINT32_MAX)
            return AVERROR_INVALIDDATA;
        chunk_size += chunk_size & 1;

        if (avio_feof(pb))
            break;

        // dive into RIFF chunk
        if (chunk_type == MKTAG('R', 'I', 'F', 'F') && chunk_size > 4) {
            wdc->file_start = avio_tell(pb) - 8;
            wdc->remaining_size = 8 + chunk_size;
            chunk_size = 4;
        }

        switch (chunk_type) {
        case MKTAG('V', 'P', '8', 'X'):
            avio_seek(pb, chunk_size, SEEK_CUR);
            break;
        case MKTAG('A', 'N', 'I', 'M'):
            if (chunk_size >= 6) {
                avio_seek(pb, 4, SEEK_CUR);
                wdc->num_loop = avio_rb16(pb);
                avio_seek(pb, chunk_size - 6, SEEK_CUR);
            }
            break;
        case MKTAG('V', 'P', '8', ' '):
            if (is_frame)
                goto flush;
            is_frame = 1;

            if (chunk_size >= 10) {
                avio_skip(pb, 6);
                width  = avio_rl16(pb) & 0x3fff;
                height = avio_rl16(pb) & 0x3fff;
                wdc->nb_frames++;
                ret = avio_skip(pb, chunk_size - 10);
            } else
                ret = avio_skip(pb, chunk_size);
            break;
        case MKTAG('V', 'P', '8', 'L'):
            if (is_frame)
                goto flush;
            is_frame = 1;

            if (chunk_size >= 5) {
                avio_skip(pb, 1);
                n = avio_rl32(pb);
                width     = (n & 0x3fff) + 1;           // first 14 bits
                height    = ((n >> 14) & 0x3fff) + 1;   // next 14 bits
                wdc->nb_frames++;
                ret = avio_skip(pb, chunk_size - 5);
            } else
                ret = avio_skip(pb, chunk_size);
            break;
        case MKTAG('A', 'N', 'M', 'F'):
            if (is_frame)
                goto flush;

            if (chunk_size >= 16) {
                avio_skip(pb, 6);
                width      = avio_rl24(pb) + 1;
                height     = avio_rl24(pb) + 1;
                wdc->delay = avio_rl24(pb);
                avio_skip(pb, 1); // anmf_flags
                if (wdc->delay < wdc->min_delay)
                    wdc->delay = wdc->default_delay;
                wdc->delay = FFMIN(wdc->delay, wdc->max_delay);
                chunk_size = 16;
                ret = 0;
            } else
                ret = avio_skip(pb, chunk_size);
            break;
        default:
            ret = avio_skip(pb, chunk_size);
            break;
        }
        if (ret == AVERROR_EOF) {
            // EOF was reached but the position may still be in the middle
            // of the buffer. Seek to the end of the buffer so that EOF is
            // handled properly in the next invocation of webp_read_packet.
            if ((ret = avio_seek(pb, pb->buf_end - pb->buf_ptr, SEEK_CUR) < 0))
                return ret;
            wdc->remaining_size = 0;
            return AVERROR_EOF;
        }
        if (ret < 0)
            return ret;

        if (!wdc->canvas_width && width > 0)
            wdc->canvas_width = width;
        if (!wdc->canvas_height && height > 0)
            wdc->canvas_height = height;

        if (wdc->remaining_size < 8 + chunk_size)
            return AVERROR_INVALIDDATA;
        wdc->remaining_size -= 8 + chunk_size;

        packet_end = avio_tell(pb);
    }

flush:
    if ((ret = avio_seek(pb, packet_start, SEEK_SET)) < 0)
        return ret;

    if ((ret = av_get_packet(pb, pkt, packet_end - packet_start)) < 0)
        return ret;

    key_frame = is_frame && wdc->nb_frames == 1;
    if (key_frame)
        pkt->flags |= AV_PKT_FLAG_KEY;
    else
        pkt->flags &= ~AV_PKT_FLAG_KEY;

    pkt->stream_index = 0;
    pkt->duration = is_frame ? wdc->delay : 0;
    pkt->pts = pkt->dts = AV_NOPTS_VALUE;

    if (is_frame && wdc->nb_frames == 1)
        s->streams[0]->r_frame_rate = (AVRational) {1000, pkt->duration};

    return ret;
}

static const AVOption options[] = {
    { "min_delay"     , "minimum valid delay between frames (in milliseconds)", offsetof(WebPDemuxContext, min_delay)    , AV_OPT_TYPE_INT, {.i64 = WEBP_MIN_DELAY}    , 0, 1000 * 60, AV_OPT_FLAG_DECODING_PARAM },
    { "max_webp_delay", "maximum valid delay between frames (in milliseconds)", offsetof(WebPDemuxContext, max_delay)    , AV_OPT_TYPE_INT, {.i64 = 0xffffff}          , 0, 0xffffff , AV_OPT_FLAG_DECODING_PARAM },
    { "default_delay" , "default delay between frames (in milliseconds)"      , offsetof(WebPDemuxContext, default_delay), AV_OPT_TYPE_INT, {.i64 = WEBP_DEFAULT_DELAY}, 0, 1000 * 60, AV_OPT_FLAG_DECODING_PARAM },
    { "ignore_loop"   , "ignore loop setting"                                 , offsetof(WebPDemuxContext, ignore_loop)  , AV_OPT_TYPE_BOOL,{.i64 = 1}                 , 0, 1        , AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

static const AVClass demuxer_class = {
    .class_name = "WebP demuxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEMUXER,
};

AVInputFormat ff_webp_demuxer = {
    .name           = "webp",
    .long_name      = NULL_IF_CONFIG_SMALL("WebP image"),
    .priv_data_size = sizeof(WebPDemuxContext),
    .read_probe     = webp_probe,
    .read_header    = webp_read_header,
    .read_packet    = webp_read_packet,
    .flags          = AVFMT_GENERIC_INDEX,
    .priv_class     = &demuxer_class,
};
