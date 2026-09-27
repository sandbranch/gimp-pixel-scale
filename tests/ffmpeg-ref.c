/*
 * FFmpeg's hqx, xBR and EPX filters as plain functions, for
 * tests/refcheck.c: includes FFmpeg 8.0.1's libavfilter/vf_hqx.c,
 * vf_xbr.c or vf_epx.c unchanged (downloaded by tests/get-refs.sh) with
 * the declarations of tests/ffshim, and calls their slice functions on
 * frames that point at the caller's pixels. Built three times, with
 * -DFF_HQX, -DFF_XBR or -DFF_EPX.
 *
 * Pixels are 0xAARRGGBB words, FFmpeg's AV_PIX_FMT_RGB32 and 0RGB32 on
 * any machine (for EPX, which only compares and copies whole pixels, the
 * layout does not matter).
 *
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <stdint.h>
#include <stdlib.h>

#if defined(FF_HQX)
#include "vf_hqx.c"
#define CONTEXT HQXContext
#elif defined(FF_XBR)
#include "vf_xbr.c"
#define CONTEXT XBRContext
#elif defined(FF_EPX)
#include "vf_epx.c"
#define CONTEXT EPXContext
#endif

static CONTEXT *context __attribute__ ((unused));

static void
frames (AVFrame *in, AVFrame *out, const uint32_t *src, int w, int h,
        uint32_t *dst, int n)
{
  in->data[0] = (uint8_t *) src;
  in->linesize[0] = w * 4;
  in->width = w;
  in->height = h;
  out->data[0] = (uint8_t *) dst;
  out->linesize[0] = w * n * 4;
  out->width = w * n;
  out->height = h * n;
}

#if defined(FF_HQX) || defined(FF_XBR)

/* runs the filter; *table (if not NULL) is FFmpeg's YUV table */
#if defined(FF_HQX)
void ff_ref_hqx (int n, const uint32_t *src, int w, int h, uint32_t *dst,
                 const uint32_t **table)
#else
void ff_ref_xbr (int n, const uint32_t *src, int w, int h, uint32_t *dst,
                 const uint32_t **table)
#endif
{
  AVFilterContext ctx = { 0 };
  AVFrame in = { 0 }, out = { 0 };
  ThreadData td;

  if (!context)
    context = calloc (1, sizeof (*context));
  ctx.priv = context;
  context->n = n;
  init (&ctx);
  if (table)
    *table = context->rgbtoyuv;
  if (!src)
    return;
  frames (&in, &out, src, w, h, dst, n);
  td.in = &in;
  td.out = &out;
  td.rgbtoyuv = context->rgbtoyuv;
  context->func (&ctx, &td, 0, 1);
}

#else

void
ff_ref_epx (int n, const uint32_t *src, int w, int h, uint32_t *dst)
{
  AVFilterContext ctx = { 0 };
  AVFrame in = { 0 }, out = { 0 };
  ThreadData td;

  frames (&in, &out, src, w, h, dst, n);
  td.in = &in;
  td.out = &out;
  if (n == 2)
    epx2_slice (&ctx, &td, 0, 1);
  else
    epx3_slice (&ctx, &td, 0, 1);
}

#endif
