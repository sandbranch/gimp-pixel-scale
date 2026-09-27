/*
 * Just enough of FFmpeg's libavutil and libavfilter declarations to
 * compile FFmpeg's vf_hqx.c, vf_xbr.c and vf_epx.c unchanged, so that
 * tests/refcheck.c can run their scalers on its own pixels (see
 * tests/ffmpeg-ref.c). Nothing here is from FFmpeg; the names and fields
 * are the ones those three files use.
 *
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FFSHIM_H
#define FFSHIM_H

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define av_always_inline inline __attribute__((always_inline))
#define av_cold
#define av_assert0(x) assert(x)
#define FFMAX(a, b) ((a) > (b) ? (a) : (b))
#define FFMIN(a, b) ((a) > (b) ? (b) : (a))
#define FFMAX3(a, b, c) FFMAX(FFMAX(a, b), c)
#define FFMIN3(a, b, c) FFMIN(FFMIN(a, b), c)
#define AVERROR(e) (-(e))
#define AVERROR_BUG (-1)
#define NULL_IF_CONFIG_SMALL(x) x
#define AV_LOG_VERBOSE 40

enum AVPixelFormat {
    AV_PIX_FMT_NONE = -1, AV_PIX_FMT_RGB32, AV_PIX_FMT_0RGB32,
    AV_PIX_FMT_RGBA, AV_PIX_FMT_BGRA, AV_PIX_FMT_ARGB, AV_PIX_FMT_ABGR
};
enum AVMediaType { AVMEDIA_TYPE_VIDEO };
enum AVOptionType { AV_OPT_TYPE_INT };
#define AV_OPT_FLAG_FILTERING_PARAM 1
#define AV_OPT_FLAG_VIDEO_PARAM 2
#define AVFILTER_FLAG_SLICE_THREADS 1

typedef struct AVOption {
    const char *name, *help;
    int offset;
    enum AVOptionType type;
    union { int64_t i64; } default_val;
    double min, max;
    int flags;
} AVOption;

typedef struct AVClass {
    const char *class_name;
    const AVOption *option;
} AVClass;

#define AVFILTER_DEFINE_CLASS(fname) \
    static const AVClass fname##_class = { #fname, fname##_options }

typedef struct AVFrame {
    uint8_t *data[4];
    int linesize[4];
    int width, height;
} AVFrame;

typedef struct AVFilterContext AVFilterContext;
typedef struct AVFilterLink {
    int w, h, format;
    AVFilterContext *src, *dst;
} AVFilterLink;

struct AVFilterContext {
    void *priv;
    AVFilterLink **inputs, **outputs;
};

typedef struct AVPixFmtDescriptor { const char *name; } AVPixFmtDescriptor;

typedef struct AVFilterPad {
    const char *name;
    enum AVMediaType type;
    int (*filter_frame)(AVFilterLink *link, AVFrame *frame);
    int (*config_props)(AVFilterLink *link);
} AVFilterPad;

typedef struct FFFilter {
    struct {
        const char *name, *description;
        const AVClass *priv_class;
        int flags;
    } p;
    int priv_size;
    int (*init)(AVFilterContext *ctx);
    const AVFilterPad *inputs, *outputs;
    enum AVPixelFormat pix_fmt;
    const enum AVPixelFormat *pix_fmts;
} FFFilter;

#define FILTER_INPUTS(array) .inputs = (array)
#define FILTER_OUTPUTS(array) .outputs = (array)
#define FILTER_SINGLE_PIXFMT(fmt) .pix_fmt = (fmt)
#define FILTER_PIXFMTS_ARRAY(array) .pix_fmts = (array)

/* the frame path of the filters is not used (the tests call the slice
 * functions themselves), but it has to compile */
static inline AVFrame *ff_get_video_buffer(AVFilterLink *l, int w, int h)
{ (void) l; (void) w; (void) h; return NULL; }
static inline void av_frame_free(AVFrame **f) { (void) f; }
static inline int av_frame_copy_props(AVFrame *a, const AVFrame *b)
{ (void) a; (void) b; return 0; }
static inline int ff_filter_get_nb_threads(AVFilterContext *c) { (void) c; return 1; }
static inline int ff_filter_frame(AVFilterLink *l, AVFrame *f) { (void) l; (void) f; return 0; }
static inline int ff_filter_execute(AVFilterContext *ctx,
    int (*func)(AVFilterContext *, void *, int, int), void *arg, int *ret, int nb)
{ (void) ret; for (int i = 0; i < nb; i++) func(ctx, arg, i, nb); return 0; }
static inline const char *av_get_pix_fmt_name(int f) { (void) f; return ""; }
static inline const AVPixFmtDescriptor *av_pix_fmt_desc_get(int f)
{ static const AVPixFmtDescriptor d = { "" }; (void) f; return &d; }
#define av_log(ctx, level, ...) ((void) (ctx), (void) (level))

#endif
