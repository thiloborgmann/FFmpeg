/*
 * Copyright (c) 2023 Thilo Borgmann <thilo.borgmann _at_ mail.de>
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
 * Filter for syncing video frames from external source
 *
 * @author Thilo Borgmann <thilo.borgmann _at_ mail.de>
 */

#include "libavutil/avstring.h"
#include "libavutil/error.h"
#include "libavutil/opt.h"
#include "libavformat/avio.h"
#include "video.h"
#include "filters.h"
#include <sys/errno.h>

#define BUF_SIZE 256

typedef struct FsyncContext {
    const AVClass *class;
    AVIOContext *avio_ctx;
    char *filename;
    char *format;
    char *buf;
    char *cur;
    char *end;
    AVFrame *last_frame;
} FsyncContext;

#define OFFSET(x) offsetof(FsyncContext, x)
#define DEFINE_OPTIONS(filt_name, FLAGS)                                                                                      \
static const AVOption filt_name##_options[] = {                                                                               \
    { "file",   "set the file name to use for frame sync", OFFSET(filename), AV_OPT_TYPE_STRING, { .str = "" }, .flags=FLAGS }, \
    { "f",      "set the file name to use for frame sync", OFFSET(filename), AV_OPT_TYPE_STRING, { .str = "" }, .flags=FLAGS }, \
    { "format", "set the line format", OFFSET(format), AV_OPT_TYPE_STRING, { .str = "%li %li %i/%i" }, .flags=FLAGS }, \
    { "fmt",    "set the line format", OFFSET(format), AV_OPT_TYPE_STRING, { .str = "%li %li %i/%i" }, .flags=FLAGS }, \
    { NULL }                                                                                                                  \
}
//"%li %*i/%*i %li %i/%i"

// fills the buffer from cur to end, add \0 at EOF
static int buf_fill(FsyncContext *ctx)
{
    int ret;
    int num = ctx->end - ctx->cur;
    
    ret = avio_read(ctx->avio_ctx, ctx->cur, num);
    if (ret < 0)
        return ret;
    if (ret < num) {
        *(ctx->cur + ret) = '\0';
    }

    return ret;
}

// copies cur to end to the beginning and fills the rest
static int buf_reload(FsyncContext *ctx)
{
    int i, ret;
    int num = ctx->end - ctx->cur;

    for (i = 0; i < num; i++) {
        ctx->buf[i] = *ctx->cur++;
    }
    
    ctx->cur = ctx->buf + i;
    ret = buf_fill(ctx);
    if (ret < 0)
        return ret;
    ctx->cur = ctx->buf;

    return ret;
}

// skip from cur over eol
static void buf_skip_eol(FsyncContext *ctx)
{
    char *i;
    for (i = ctx->cur; i < ctx->end; i++) {
        if (*i != '\n')// && *i != '\r')
            break;
    }
    ctx->cur = i;
}

// get number of bytes from cur until eol
static int buf_get_line_count(FsyncContext *ctx)
{
    int ret = 0;
    char *i;
    for (i = ctx->cur; i < ctx->end; i++, ret++) {
        if (*i == '\0' || *i == '\n')
            return ret;
    }

    return -1;
}

// get number of bytes from cur to '\0'
static int buf_get_zero(FsyncContext *ctx)
{
    int ret = 0;
    char *i;
    for (i = ctx->cur; i < ctx->end; i++, ret++) {
        if (*i == '\0')
            return ret;
    }

    return ret;
}

static int activate(AVFilterContext *ctx)
{
    FsyncContext *s       = ctx->priv;
    AVFilterLink *inlink  = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];

    int ret, line_count;
    int64_t ptsi, pts;
    int tb_num, tb_den;
    AVFrame *frame;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    buf_skip_eol(s);
    line_count = buf_get_line_count(s);
    if (line_count < 0) {
        line_count = buf_reload(s);
        if (line_count < 0)
            return line_count;
        line_count = buf_get_line_count(s);
        if (line_count < 0)
            return line_count;
    }
    
    if (avio_feof(s->avio_ctx) && buf_get_zero(s) < 3) {
        av_log(ctx, AV_LOG_DEBUG, "End of file. To zero = %i\n", buf_get_zero(s));
        goto end;
    }

    if (s->last_frame) {
        ret = av_sscanf(s->cur, s->format, &ptsi, &pts, &tb_num, &tb_den);
        if (ret != 4) {
            av_log(ctx, AV_LOG_ERROR, "Unexpected format found (%i).\n", ret);
            return AVERROR_INVALIDDATA;
        }

        av_log(ctx, AV_LOG_DEBUG, "frame %lli ", s->last_frame->pts);

        if (s->last_frame->pts >= ptsi) {
            av_log(ctx, AV_LOG_DEBUG, "> %lli: DUP LAST with pts = %lli\n", ptsi, pts);

            // clone frame
            frame = av_frame_clone(s->last_frame);
            if (!frame) {
                return AVERROR(ENOMEM);
            }
            av_frame_copy_props(frame, s->last_frame);

            // set output pts and timebase
            frame->pts = pts;
            frame->time_base.num = tb_num;
            frame->time_base.den = tb_den;

            // advance cur to eol, skip over eol in the next call
            s->cur += line_count;

            // call again
            if (ff_inoutlink_check_flow(inlink, outlink))
                ff_filter_set_ready(ctx, 100);

            // filter frame
            return ff_filter_frame(outlink, frame);
        } else if (s->last_frame->pts < ptsi) {
            av_log(ctx, AV_LOG_DEBUG, "< %lli: DROP\n", ptsi);
            av_frame_free(&s->last_frame);

            // call again
            if (ff_inoutlink_check_flow(inlink, outlink))
                ff_filter_set_ready(ctx, 100);

            return 0;
        }
    }

end:
    ret = ff_inlink_consume_frame(inlink, &s->last_frame);
    if (ret < 0)
        return ret;

    FF_FILTER_FORWARD_STATUS(inlink, outlink);
    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

static av_cold int fsync_init(AVFilterContext *ctx)
{
    FsyncContext *s = ctx->priv;
    int ret;

    av_log(ctx, AV_LOG_DEBUG, "filename: %s\n", s->filename);

    s->buf = av_malloc(BUF_SIZE);
    if (!s->buf)
        return AVERROR(ENOMEM);

    ret = avio_open(&s->avio_ctx, s->filename, AVIO_FLAG_READ);
    if (ret < 0)
        return ret;

    s->cur = s->buf;
    s->end = s->buf + BUF_SIZE; 

    ret = buf_fill(s);
    if (ret < 0)
        return ret;

    return 0;
}

static av_cold void fsync_uninit(AVFilterContext *ctx)
{
    FsyncContext *s = ctx->priv;

    avio_close(s->avio_ctx);
    av_freep(&s->buf);
    av_frame_unref(s->last_frame);
}

DEFINE_OPTIONS(fsync, AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM);
AVFILTER_DEFINE_CLASS(fsync);

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_GBRP,     AV_PIX_FMT_GBRAP,
    AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV440P,
    AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA420P,
    AV_PIX_FMT_NONE
};

const AVFilter ff_vf_fsync = {
    .name          = "fsync",
    .description   = NULL_IF_CONFIG_SMALL("Synchronize video frames from external source."),
    .init          = fsync_init,
    .uninit        = fsync_uninit,
    .priv_size     = sizeof(FsyncContext),
    .priv_class    = &fsync_class,
    .activate      = activate,
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    FILTER_INPUTS(ff_video_default_filterpad),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    .flags         = AVFILTER_FLAG_METADATA_ONLY,
};
