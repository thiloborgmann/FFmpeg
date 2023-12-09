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
#include "libavutil/parseutils.h"
#include "video.h"
#include "filters.h"

#define BUF_SIZE 256

typedef struct FsyncContext {
    const AVClass *class;
    AVIOContext *avio_ctx; // reading the map file
    AVFrame *last_frame;   // buffering the last frame for duplicating eventually
    char *filename;        // user-specified map file
    char *format;          // user-specified line format according to -stats_enc* options
    char *format_str;      // sscanf compatible user-specified line format of the map file
    char *buf;             // line buffer for the map file
    char *cur;             // current position in the line buffer
    char *end;             // end pointer of the line buffer
    int64_t ptsi;          // input pts to map to [0-N] output pts
    int64_t pts;           // output pts
    int64_t tb_num;        // output timebase num
    int64_t tb_den;        // output timebase den
    int64_t *param[4];     // mapping of ptsi, pts, tb_num, tb_den into user-specified format
} FsyncContext;

#define OFFSET(x) offsetof(FsyncContext, x)
#define DEFINE_OPTIONS(filt_name, FLAGS)                                                                                        \
static const AVOption filt_name##_options[] = {                                                                                 \
    { "file",   "set the file name to use for frame sync", OFFSET(filename), AV_OPT_TYPE_STRING, { .str = "" }, .flags=FLAGS }, \
    { "f",      "set the file name to use for frame sync", OFFSET(filename), AV_OPT_TYPE_STRING, { .str = "" }, .flags=FLAGS }, \
    { "format", "set the line format",                     OFFSET(format),   AV_OPT_TYPE_STRING, { .str = "{ptsi} {pts} {tb}" }, .flags=FLAGS }, \
    { "fmt",    "set the line format",                     OFFSET(format),   AV_OPT_TYPE_STRING, { .str = "{ptsi} {pts} {tb}" }, .flags=FLAGS }, \
    { NULL }                                                                                                                    \
}

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
        av_log(ctx, AV_LOG_DEBUG, "format = %s\n", s->format);

        // default: av_sscanf(s->cur, "{ptsi} {pts} {tb}", &s->ptsi, &s->pts, &s->tb_num, &s->tb_den);
        ret = av_sscanf(s->cur, s->format_str, s->param[0], s->param[1], s->param[2], s->param[3]);
        if (ret != 4) {
            av_log(ctx, AV_LOG_ERROR, "Unexpected format found (%i / 4).\n", ret);
            ff_outlink_set_status(outlink, AVERROR_INVALIDDATA, AV_NOPTS_VALUE);
            return AVERROR_INVALIDDATA;
        }

        av_log(ctx, AV_LOG_DEBUG, "frame %lli ", s->last_frame->pts);

        if (s->last_frame->pts >= s->ptsi) {
            av_log(ctx, AV_LOG_DEBUG, "> %lli: DUP LAST with pts = %lli\n", s->ptsi, s->pts);

            // clone frame
            frame = av_frame_clone(s->last_frame);
            if (!frame) {
                ff_outlink_set_status(outlink, AVERROR(ENOMEM), AV_NOPTS_VALUE);
                return AVERROR(ENOMEM);
            }
            av_frame_copy_props(frame, s->last_frame);

            // set output pts and timebase
            frame->pts = s->pts;
            frame->time_base = av_make_q((int)s->tb_num, (int)s->tb_den);

            // advance cur to eol, skip over eol in the next call
            s->cur += line_count;

            // call again
            if (ff_inoutlink_check_flow(inlink, outlink))
                ff_filter_set_ready(ctx, 100);

            // filter frame
            return ff_filter_frame(outlink, frame);
        } else if (s->last_frame->pts < s->ptsi) {
            av_log(ctx, AV_LOG_DEBUG, "< %lli: DROP\n", s->ptsi);
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

static int fsync_config_props(AVFilterLink* outlink)
{
    AVFilterContext *ctx = outlink->src;
    FsyncContext    *s   = ctx->priv;
    int ret;

    // read first line to get output timebase
    // default: av_sscanf(s->cur, "{ptsi} {pts} {tb}", &s->ptsi, &s->pts, &s->tb_num, &s->tb_den);
    ret = av_sscanf(s->cur, s->format_str, s->param[0], s->param[1], s->param[2], s->param[3]);
    if (ret != 4) {
        av_log(ctx, AV_LOG_ERROR, "Unexpected format found (%i of 4).\n", ret);
        ff_outlink_set_status(outlink, AVERROR_INVALIDDATA, AV_NOPTS_VALUE);
        return AVERROR_INVALIDDATA;
    }

    outlink->frame_rate = av_make_q(1, 0); // unknown or dynamic
    outlink->time_base  = av_make_q((int)s->tb_num, (int)s->tb_den);

    return 0;
}

static av_cold int fsync_init(AVFilterContext *ctx)
{
    FsyncContext *s = ctx->priv;
    AVEncStatsComponent *components = NULL;
    int nb_components = 0;
    int ret, i;
    int j = 0;
    int has_i = 0;
    int has_o = 0;
    int has_t = 0;

    av_log(ctx, AV_LOG_DEBUG, "filename: %s\n", s->filename);

    s->buf = av_malloc(BUF_SIZE);
    if (!s->buf)
        return AVERROR(ENOMEM);

    s->format_str = av_mallocz(BUF_SIZE);
    if (!s->format_str)
        return AVERROR(ENOMEM);

    ret = avio_open(&s->avio_ctx, s->filename, AVIO_FLAG_READ);
    if (ret < 0)
        return ret;

    s->cur = s->buf;
    s->end = s->buf + BUF_SIZE; 

    ret = buf_fill(s);
    if (ret < 0)
        return ret;

    // parse format into format_str for av_sscanf
    ret = av_parse_enc_stats_components(&components, &nb_components, s->format);
    if (ret < 0)
        return ret;

    for (i = 0; i < nb_components; i++) {
        AVEncStatsComponent *c = &components[i];
        switch (c->type) {
        default:
            av_log(ctx, AV_LOG_ERROR, "Unknown format specifier: %i {%s}\n", c->type, c->str);
            return AVERROR(EINVAL);
        case ENC_STATS_LITERAL:
                        if (c->str)  {av_strlcat(s->format_str, c->str,     BUF_SIZE); continue;}
                        else                                                           continue;
        case ENC_STATS_FILE_IDX:      av_strlcat(s->format_str, "%*d",      BUF_SIZE); continue;
        case ENC_STATS_STREAM_IDX:    av_strlcat(s->format_str, "%*d",      BUF_SIZE); continue;
        case ENC_STATS_FRAME_NUM:     av_strlcat(s->format_str, "%*"PRIu64, BUF_SIZE); continue;
        case ENC_STATS_FRAME_NUM_IN:  av_strlcat(s->format_str, "%*"PRIu64, BUF_SIZE); continue;
        case ENC_STATS_TIMEBASE:     {av_strlcat(s->format_str, "%d/%d",    BUF_SIZE);
                                      if (!has_t) {s->param[j++] = &s->tb_num;
                                                   s->param[j++] = &s->tb_den;
                                                   has_t = 1;}                         continue;}
        case ENC_STATS_TIMEBASE_IN:   av_strlcat(s->format_str, "%*d/%*d",  BUF_SIZE); continue;
        case ENC_STATS_PTS:          {av_strlcat(s->format_str, "%"PRId64,  BUF_SIZE);
                                      if (!has_o) {s->param[j++] = &s->pts;
                                                   has_o = 1;}                         continue;}
        case ENC_STATS_PTS_TIME:      av_strlcat(s->format_str, "%*g",      BUF_SIZE); continue;
        case ENC_STATS_PTS_IN:       {av_strlcat(s->format_str, "%"PRId64,  BUF_SIZE);
                                      if (!has_i) {s->param[j++] = &s->ptsi;
                                                   has_i = 1;}                         continue;}
        case ENC_STATS_PTS_TIME_IN:   av_strlcat(s->format_str, "%*g",      BUF_SIZE); continue;
        case ENC_STATS_DTS:           av_strlcat(s->format_str, "%*"PRId64, BUF_SIZE); continue;
        case ENC_STATS_DTS_TIME:      av_strlcat(s->format_str, "%*g",      BUF_SIZE); continue;
        case ENC_STATS_SAMPLE_NUM:    av_strlcat(s->format_str, "%*"PRIu64, BUF_SIZE); continue;
        case ENC_STATS_NB_SAMPLES:    av_strlcat(s->format_str, "%*d",      BUF_SIZE); continue;
        case ENC_STATS_PKT_SIZE:      av_strlcat(s->format_str, "%*d",      BUF_SIZE); continue;
        case ENC_STATS_BITRATE:       av_strlcat(s->format_str, "%*g",      BUF_SIZE); continue;
        case ENC_STATS_AVG_BITRATE:   av_strlcat(s->format_str, "%*g",      BUF_SIZE); continue;
        }
        av_freep(c->str);
    }
    av_freep(&components);

    // check for all necessary specifiers found
    if (j != 4) {
        if (!has_i) av_log(ctx, AV_LOG_ERROR, "Format specifier {ptis} missing in format string\n");
        if (!has_o) av_log(ctx, AV_LOG_ERROR, "Format specifier {pti} missing in format string\n");
        if (!has_t) av_log(ctx, AV_LOG_ERROR, "Format specifier {tb} missing in format string\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static av_cold void fsync_uninit(AVFilterContext *ctx)
{
    FsyncContext *s = ctx->priv;

    avio_close(s->avio_ctx);
    av_freep(&s->buf);
    av_freep(&s->format_str);
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

static const AVFilterPad avfilter_vf_fsync_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = fsync_config_props,
    },
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
    FILTER_OUTPUTS(avfilter_vf_fsync_outputs),
    .flags         = AVFILTER_FLAG_METADATA_ONLY,
};
