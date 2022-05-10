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

/**
 * @file
 * No-reference jerkdetect filter
 *
 * Implementing:
 * ?
 *
 * @author Thilo Borgmann <thilo.borgmann _at_ mail.de>
 */

#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixelutils.h"
#include "internal.h"

typedef struct JRKContext {
    const AVClass *class;

    int hsub, vsub;
    int nb_planes;

    int period_min;    // minimum period to search for
    int period_max;    // maximum period to search for
    int planes;        // number of planes to filter

    double block_total;
    uint64_t nb_frames;

    float *gradients;

    // jerkiness params
    double jerk_total;
    double prev_mafd;
    AVFrame* prev_picref;
    av_pixelutils_sad_fn sad;       ///< Sum of the absolute difference function (scene detect only)
    int64_t prev_scene_time;


} JRKContext;

#define OFFSET(x) offsetof(JRKContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption jerkdetect_options[] = {
    { "period_min", "Minimum period to search for", OFFSET(period_min), AV_OPT_TYPE_INT, {.i64=3}, 2, 32, FLAGS},
    { "period_max", "Maximum period to search for", OFFSET(period_max), AV_OPT_TYPE_INT, {.i64=24}, 2, 64, FLAGS},
    { "planes",        "set planes to filter", OFFSET(planes), AV_OPT_TYPE_INT, {.i64=1}, 0, 15, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(jerkdetect);

static av_cold int jerkdetect_init(AVFilterContext *ctx)
{
    return 0;
}

static int jerkdetect_config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    JRKContext      *s   = ctx->priv;
    const int bufsize    = inlink->w * inlink->h;
    const AVPixFmtDescriptor *pix_desc;

    pix_desc = av_pix_fmt_desc_get(inlink->format);
    s->hsub = pix_desc->log2_chroma_w;
    s->vsub = pix_desc->log2_chroma_h;
    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    s->gradients = av_calloc(bufsize, sizeof(*s->gradients));

    if (!s->gradients)
        return AVERROR(ENOMEM);

    return 0;
}

// same function as ffmpeg scene change filter;
// TODO: deduplicate
// this returns a probability of scene change, which is used to indicate
// motion intensity between frames
static double get_scene_score(JRKContext *s, AVFrame *frame)
{
    double ret = 0;
    //JRKContext *s = ctx->priv;
    AVFrame *prev_picref = s->prev_picref;

    if (prev_picref &&
        frame->height == prev_picref->height &&
        frame->width  == prev_picref->width) {
        int x, y, nb_sad = 0;
        int64_t sad = 0;
        double mafd, diff;
        uint8_t *p1 =       frame->data[0];
        uint8_t *p2 = prev_picref->data[0];
        const int p1_linesize =       frame->linesize[0];
        const int p2_linesize = prev_picref->linesize[0];

        for (y = 0; y < frame->height - 7; y += 8) {
            for (x = 0; x < frame->width - 7; x += 8) {
                sad += s->sad(p1 + x, p1_linesize, p2 + x, p2_linesize);
                nb_sad += 8 * 8;
            }
            p1 += 8 * p1_linesize;
            p2 += 8 * p2_linesize;
        }
        emms_c();
        mafd = nb_sad ? (double)sad / nb_sad : 0;
        diff = fabs(mafd - s->prev_mafd);
        ret  = av_clipf(FFMIN(mafd, diff) / 100., 0, 1);
        s->prev_mafd = mafd;
        av_frame_free(&prev_picref);
    }
    s->prev_picref = av_frame_clone(frame);
    return ret;
}

static float calculate_jerkiness(JRKContext *s, int w, int h, int hsub, int vsub/*,
                                 uint8_t* dir, int dir_linesize,
                                 uint8_t* dst, int dst_linesize,
                                 uint8_t* src, int src_linesize*/)
{
    //AVFilterContext *ctx  = inlink->dst;
    //JRKContext *s         = ctx->priv;
    //AVFilterLink *outlink = ctx->outputs[0];

    const int inw = w;
    const int inh = h;
    // estimate of jerkiness based on scene change and scene duration
    double score = 100 * get_scene_score(s, in);

    // motion too small does not cause perceived jerkiness
    if (score > 0.3) {
        double dt = ((double)(inlink->current_pts_us - s->prev_scene_time)) / AV_TIME_BASE;

        // e.g. slide shows are all perceived the same
        score = FFMIN(5.0, score);

        // scene duration less than 0.1 seconds does not cause perceived jerkiness
        if (dt > 0.1) {
            s->jerk_total =
                10 * ((s->jerk_total / 10) * (s->prev_scene_time / AV_TIME_BASE) + dt * score) /
                ((double)inlink->current_pts_us/AV_TIME_BASE);
        }
        s->prev_scene_time = inlink->current_pts_us;
    }

    return s->jerk_total;
}

static void set_meta(AVDictionary **metadata, const char *key, float d)
{
    char value[128];
    snprintf(value, sizeof(value), "%f", d);
    av_dict_set(metadata, key, value, 0);
}

static int jerkdetect_filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    float *gradients   = s->gradients;

    float jerk = 0.0f;
    int nplanes = 0;
    AVDictionary **metadata;
    metadata = &in->metadata;

    for (int plane = 0; plane < s->nb_planes; plane++) {
        int hsub = plane == 1 || plane == 2 ? s->hsub : 0;
        int vsub = plane == 1 || plane == 2 ? s->vsub : 0;
        int w = AV_CEIL_RSHIFT(inw, hsub);
        int h = AV_CEIL_RSHIFT(inh, vsub);

        if (!((1 << plane) & s->planes))
            continue;

        nplanes++;

        //block += calculate_blockiness(s, w, h, gradients, w, in->data[plane], in->linesize[plane]);
        calculate_jerkiness(s, w, h, hsub, vsub);
    }

    if (nplanes)
        jerk /= nplanes;

    s->jerk_total += jerk;

    // write stats
    av_log(ctx, AV_LOG_VERBOSE, "jerk: %.7f\n", jerk);

    set_meta(metadata, "lavfi.jerk", jerk);

    s->nb_frames = inlink->frame_count_in;

    return ff_filter_frame(outlink, in);
}

static av_cold void jerkdetect_uninit(AVFilterContext *ctx)
{
    JRKContext *s = ctx->priv;

    if (s->nb_frames > 0) {
        av_log(ctx, AV_LOG_INFO, "block mean: %.7f\n",
               s->block_total / s->nb_frames);
    }

    av_freep(&s->gradients);
}

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

static const AVFilterPad jerkdetect_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = jerkdetect_config_input,
        .filter_frame = jerkdetect_filter_frame,
    },
};

static const AVFilterPad jerkdetect_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
};

const AVFilter ff_vf_jerkdetect = {
    .name          = "jerkdetect",
    .description   = NULL_IF_CONFIG_SMALL("Jerkdetect filter."),
    .priv_size     = sizeof(JRKContext),
    .init          = jerkdetect_init,
    .uninit        = jerkdetect_uninit,
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    FILTER_INPUTS(jerkdetect_inputs),
    FILTER_OUTPUTS(jerkdetect_outputs),
    .priv_class    = &jerkdetect_class,
    .flags         = AVFILTER_FLAG_METADATA_ONLY,
};
