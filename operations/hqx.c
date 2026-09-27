/*
 * Pixel Art Scale: hq2x, hq3x and hq4x
 *
 * hqx.c
 *
 * The rules of hqx (hq2x_interp_1x1, hq3x_interp_2x1, hq4x_interp_2x2 and
 * the macros before them) are from FFmpeg's libavfilter/vf_hqx.c (FFmpeg
 * 8.0.1), a compact rewrite of Maxim Stepin's hq2x, hq3x and hq4x, under
 * this notice:
 *
 *   Copyright (c) 2014 Clément Bœsch
 *
 *   Permission to use, copy, modify, and/or distribute this software for
 *   any purpose with or without fee is hereby granted, provided that the
 *   above copyright notice and this permission notice appear in all
 *   copies.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 *   WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 *   WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 *   AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 *   DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 *   PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 *   TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 *   PERFORMANCE OF THIS SOFTWARE.
 *
 * Changed in them: WDIFF compares two pixels of the window by their index,
 * the blends interp_2px and interp_3px are MIX2 and MIX3 (below), and the
 * functions take the window (HqWin) instead of FFmpeg's YUV table, so the
 * pixels w5 and w7, which only WDIFF used, are no longer read out.
 *
 * The rest, and the changes, Copyright 2026 by David:
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * hqx looks at the 3 x 3 pixels around each pixel, marks which of the 8
 * neighbours differ from it (more than 48 in Y, 7 in U or 6 in V), and
 * fills the n x n output pixels from that pattern with fixed blends of
 * the pixel and its neighbours. The YUV conversion is that of the hqx
 * library of Cameron Zemek (github.com/grom358/hqx, LGPL-2.1+, the
 * reference these results are tested against), computed here per pixel
 * instead of from its table of 2^24 entries: see pas_hqx_yuv. (FFmpeg's
 * own table rounds differently for some colors.)
 *
 * Alpha (see pas.h). PAS_ALPHA_STRAIGHT: as the references, alpha is not
 * compared, and blended like a color. PAS_ALPHA_WEIGHTED: two pixels
 * also differ when their alphas differ by more than 48, the threshold of
 * Y: over a black or white background an alpha step of 48 changes the
 * luma seen by up to 48. Colors are compared premultiplied, and blends
 * are weighted by alpha.
 */

#include <stdlib.h>

#include "pas.h"

#define HQ_TR_Y  (48 << 16)
#define HQ_TR_U  (7 << 8)
#define HQ_TR_V  6
#define HQ_TR_A  48

/* the 3 x 3 pixels around a pixel, 0 to 8 in rows, 4 in the middle */
typedef struct
{
  uint32_t pix[9];
  uint32_t yuv[9];
  uint32_t a[9];     /* alpha of the key */
  PasAlpha alpha;
} HqWin;

/*
 * The YUV of the hqx library (init.c of github.com/grom358/hqx):
 *
 *   y = (uint32_t)(0.299*r + 0.587*g + 0.114*b);
 *   u = (uint32_t)(-0.169*r - 0.331*g + 0.5*b) + 128;
 *   v = (uint32_t)(0.5*r - 0.419*g - 0.081*b) + 128;
 *
 * in double precision. u and v can be negative before the 128 is added;
 * converting a negative double to an unsigned integer is undefined in C,
 * and on the usual machines (x86-64, AArch64) it gives the value truncated
 * towards zero, modulo 2^32. The same is done here in defined C, through
 * int. The operation is built with -ffp-contract=off, so that the
 * compiler does not fuse the multiplications and additions differently;
 * tests/refcheck.c compares all 2^24 colors with the library's table.
 */
uint32_t
pas_hqx_yuv (uint32_t c)
{
  const double r = PAS_R (c), g = PAS_G (c), b = PAS_B (c);
  const uint32_t y = (uint32_t) (0.299 * r + 0.587 * g + 0.114 * b);
  const uint32_t u = (uint32_t) ((int) (-0.169 * r - 0.331 * g + 0.5 * b) + 128);
  const uint32_t v = (uint32_t) ((int) (0.5 * r - 0.419 * g - 0.081 * b) + 128);

  return (y << 16) + (u << 8) + v;
}

static inline int
hq_yuv_diff (uint32_t yuv1, uint32_t yuv2)
{
  return abs ((int) (yuv1 & 0xff0000) - (int) (yuv2 & 0xff0000)) > HQ_TR_Y ||
         abs ((int) (yuv1 & 0x00ff00) - (int) (yuv2 & 0x00ff00)) > HQ_TR_U ||
         abs ((int) (yuv1 & 0x0000ff) - (int) (yuv2 & 0x0000ff)) > HQ_TR_V;
}

/* whether the pixels i and j of the window differ */
static inline int
hq_diff (const HqWin *win, int i, int j)
{
  if (win->alpha == PAS_ALPHA_WEIGHTED &&
      abs ((int) win->a[i] - (int) win->a[j]) > HQ_TR_A)
    return 1;
  return hq_yuv_diff (win->yuv[i], win->yuv[j]);
}

/* (c1*w1 + c2*w2) >> s, on all four channels: the reference's arithmetic */
static inline uint32_t
interp_2px (uint32_t c1, int w1, uint32_t c2, int w2, int s)
{
  return (((((c1 & 0xff00ff00) >> 8) * w1 + ((c2 & 0xff00ff00) >> 8) * w2) << (8 - s)) & 0xff00ff00) |
         (((((c1 & 0x00ff00ff)     ) * w1 + ((c2 & 0x00ff00ff)     ) * w2) >>      s ) & 0x00ff00ff);
}

/* (c1*w1 + c2*w2 + c3*w3) >> s */
static inline uint32_t
interp_3px (uint32_t c1, int w1, uint32_t c2, int w2, uint32_t c3, int w3, int s)
{
  return (((((c1 & 0xff00ff00) >> 8) * w1 + ((c2 & 0xff00ff00) >> 8) * w2 + ((c3 & 0xff00ff00) >> 8) * w3) << (8 - s)) & 0xff00ff00) |
         (((((c1 & 0x00ff00ff)     ) * w1 + ((c2 & 0x00ff00ff)     ) * w2 + ((c3 & 0x00ff00ff)     ) * w3) >>      s ) & 0x00ff00ff);
}

static inline uint32_t
hq_mix2 (PasAlpha alpha, uint32_t c1, int w1, uint32_t c2, int w2, int s)
{
  if (alpha == PAS_ALPHA_WEIGHTED && PAS_A (c1) != PAS_A (c2))
    {
      const uint32_t c[2] = { c1, c2 };
      const int w[2] = { w1, w2 };

      return pas_blend_weighted (2, c, w, s);
    }
  return interp_2px (c1, w1, c2, w2, s);
}

static inline uint32_t
hq_mix3 (PasAlpha alpha, uint32_t c1, int w1, uint32_t c2, int w2,
         uint32_t c3, int w3, int s)
{
  if (alpha == PAS_ALPHA_WEIGHTED &&
      (PAS_A (c1) != PAS_A (c2) || PAS_A (c1) != PAS_A (c3)))
    {
      const uint32_t c[3] = { c1, c2, c3 };
      const int w[3] = { w1, w2, w3 };

      return pas_blend_weighted (3, c, w, s);
    }
  return interp_3px (c1, w1, c2, w2, c3, w3, s);
}

/* ---- the rules of hqx, from FFmpeg's vf_hqx.c (see the top) ---- */

/* m is the mask of diff with the center pixel that matters in the pattern, and
 * r is the expected result (bit set to 1 if there is difference with the
 * center, 0 otherwise) */
#define P(m, r) ((k_shuffled & (m)) == (r))

/* adjust 012345678 to 01235678: the mask doesn't contain the (null) diff
 * between the center/current pixel and itself */
#define DROP4(z) ((z) > 4 ? (z)-1 : (z))

/* shuffle the input mask: move bit n (4-adjusted) to position stored in p<n> */
#define SHF(x, rot, n) (((x) >> ((rot) ? 7-DROP4(n) : DROP4(n)) & 1) << DROP4(p##n))

/* used to check if there is YUV difference between 2 pixels (here: between
 * the pixels p<a> and p<b> of the window, see hq_diff) */
#define WDIFF(a, b) hq_diff (win, p##a, p##b)

/* the blends (here: weighted by alpha if asked for, see pas.h) */
#define MIX2(c1, w1, c2, w2, s) hq_mix2 (win->alpha, c1, w1, c2, w2, s)
#define MIX3(c1, w1, c2, w2, c3, w3, s) \
    hq_mix3 (win->alpha, c1, w1, c2, w2, c3, w3, s)

/* bootstrap template for every interpolation code. It defines the shuffled
 * masks and surrounding pixels. The rot flag is used to indicate if it's a
 * rotation; its basic effect is to shuffle k using p8..p0 instead of p0..p8 */
#define INTERP_BOOTSTRAP(rot)                                           \
    const int k_shuffled = SHF(k,rot,0) | SHF(k,rot,1) | SHF(k,rot,2)   \
                         | SHF(k,rot,3) |       0      | SHF(k,rot,5)   \
                         | SHF(k,rot,6) | SHF(k,rot,7) | SHF(k,rot,8);  \
                                                                        \
    const uint32_t w0 = w[p0], w1 = w[p1],                              \
                   w3 = w[p3], w4 = w[p4]

/* Assuming p0..p8 is mapped to pixels 0..8, this function interpolates the
 * top-left pixel in the total of the 2x2 pixels to interpolates. The function
 * is also used for the 3 other pixels */
static inline uint32_t hq2x_interp_1x1(const HqWin *win, int k,
                                                 const uint32_t *w,
                                                 int p0, int p1, int p2,
                                                 int p3, int p4, int p5,
                                                 int p6, int p7, int p8)
{
    INTERP_BOOTSTRAP(0);

    if ((P(0xbf,0x37) || P(0xdb,0x13)) && WDIFF(1, 5))
        return MIX2(w4, 3, w3, 1, 2);
    if ((P(0xdb,0x49) || P(0xef,0x6d)) && WDIFF(7, 3))
        return MIX2(w4, 3, w1, 1, 2);
    if ((P(0x0b,0x0b) || P(0xfe,0x4a) || P(0xfe,0x1a)) && WDIFF(3, 1))
        return w4;
    if ((P(0x6f,0x2a) || P(0x5b,0x0a) || P(0xbf,0x3a) || P(0xdf,0x5a) ||
         P(0x9f,0x8a) || P(0xcf,0x8a) || P(0xef,0x4e) || P(0x3f,0x0e) ||
         P(0xfb,0x5a) || P(0xbb,0x8a) || P(0x7f,0x5a) || P(0xaf,0x8a) ||
         P(0xeb,0x8a)) && WDIFF(3, 1))
        return MIX2(w4, 3, w0, 1, 2);
    if (P(0x0b,0x08))
        return MIX3(w4, 2, w0, 1, w1, 1, 2);
    if (P(0x0b,0x02))
        return MIX3(w4, 2, w0, 1, w3, 1, 2);
    if (P(0x2f,0x2f))
        return MIX3(w4, 14, w3, 1, w1, 1, 4);
    if (P(0xbf,0x37) || P(0xdb,0x13))
        return MIX3(w4, 5, w1, 2, w3, 1, 3);
    if (P(0xdb,0x49) || P(0xef,0x6d))
        return MIX3(w4, 5, w3, 2, w1, 1, 3);
    if (P(0x1b,0x03) || P(0x4f,0x43) || P(0x8b,0x83) || P(0x6b,0x43))
        return MIX2(w4, 3, w3, 1, 2);
    if (P(0x4b,0x09) || P(0x8b,0x89) || P(0x1f,0x19) || P(0x3b,0x19))
        return MIX2(w4, 3, w1, 1, 2);
    if (P(0x7e,0x2a) || P(0xef,0xab) || P(0xbf,0x8f) || P(0x7e,0x0e))
        return MIX3(w4, 2, w3, 3, w1, 3, 3);
    if (P(0xfb,0x6a) || P(0x6f,0x6e) || P(0x3f,0x3e) || P(0xfb,0xfa) ||
        P(0xdf,0xde) || P(0xdf,0x1e))
        return MIX2(w4, 3, w0, 1, 2);
    if (P(0x0a,0x00) || P(0x4f,0x4b) || P(0x9f,0x1b) || P(0x2f,0x0b) ||
        P(0xbe,0x0a) || P(0xee,0x0a) || P(0x7e,0x0a) || P(0xeb,0x4b) ||
        P(0x3b,0x1b))
        return MIX3(w4, 2, w3, 1, w1, 1, 2);
    return MIX3(w4, 6, w3, 1, w1, 1, 3);
}

/* Assuming p0..p8 is mapped to pixels 0..8, this function interpolates the
 * top-left and top-center pixel in the total of the 3x3 pixels to
 * interpolates. The function is also used for the 3 other couples of pixels
 * defining the outline. The center pixel is not defined through this function,
 * since it's just the same as the original value. */
static inline void hq3x_interp_2x1(uint32_t *dst, int dst_linesize,
                                             const HqWin *win, int k,
                                             const uint32_t *w,
                                             int pos00, int pos01,
                                             int p0, int p1, int p2,
                                             int p3, int p4, int p5,
                                             int p6, int p7, int p8,
                                             int rotate)
{
    INTERP_BOOTSTRAP(rotate);

    uint32_t *dst00 = &dst[dst_linesize*(pos00>>1) + (pos00&1)];
    uint32_t *dst01 = &dst[dst_linesize*(pos01>>1) + (pos01&1)];

    if ((P(0xdb,0x49) || P(0xef,0x6d)) && WDIFF(7, 3))
        *dst00 = MIX2(w4, 3, w1, 1, 2);
    else if ((P(0xbf,0x37) || P(0xdb,0x13)) && WDIFF(1, 5))
        *dst00 = MIX2(w4, 3, w3, 1, 2);
    else if ((P(0x0b,0x0b) || P(0xfe,0x4a) || P(0xfe,0x1a)) && WDIFF(3, 1))
        *dst00 = w4;
    else if ((P(0x6f,0x2a) || P(0x5b,0x0a) || P(0xbf,0x3a) || P(0xdf,0x5a) ||
              P(0x9f,0x8a) || P(0xcf,0x8a) || P(0xef,0x4e) || P(0x3f,0x0e) ||
              P(0xfb,0x5a) || P(0xbb,0x8a) || P(0x7f,0x5a) || P(0xaf,0x8a) ||
              P(0xeb,0x8a)) && WDIFF(3, 1))
        *dst00 = MIX2(w4, 3, w0, 1, 2);
    else if (P(0x4b,0x09) || P(0x8b,0x89) || P(0x1f,0x19) || P(0x3b,0x19))
        *dst00 = MIX2(w4, 3, w1, 1, 2);
    else if (P(0x1b,0x03) || P(0x4f,0x43) || P(0x8b,0x83) || P(0x6b,0x43))
        *dst00 = MIX2(w4, 3, w3, 1, 2);
    else if (P(0x7e,0x2a) || P(0xef,0xab) || P(0xbf,0x8f) || P(0x7e,0x0e))
        *dst00 = MIX2(w3, 1, w1, 1, 1);
    else if (P(0x4f,0x4b) || P(0x9f,0x1b) || P(0x2f,0x0b) || P(0xbe,0x0a) ||
             P(0xee,0x0a) || P(0x7e,0x0a) || P(0xeb,0x4b) || P(0x3b,0x1b))
        *dst00 = MIX3(w4, 2, w3, 7, w1, 7, 4);
    else if (P(0x0b,0x08) || P(0xf9,0x68) || P(0xf3,0x62) || P(0x6d,0x6c) ||
             P(0x67,0x66) || P(0x3d,0x3c) || P(0x37,0x36) || P(0xf9,0xf8) ||
             P(0xdd,0xdc) || P(0xf3,0xf2) || P(0xd7,0xd6) || P(0xdd,0x1c) ||
             P(0xd7,0x16) || P(0x0b,0x02))
        *dst00 = MIX2(w4, 3, w0, 1, 2);
    else
        *dst00 = MIX3(w4, 2, w3, 1, w1, 1, 2);

    if ((P(0xfe,0xde) || P(0x9e,0x16) || P(0xda,0x12) || P(0x17,0x16) ||
         P(0x5b,0x12) || P(0xbb,0x12)) && WDIFF(1, 5))
        *dst01 = w4;
    else if ((P(0x0f,0x0b) || P(0x5e,0x0a) || P(0xfb,0x7b) || P(0x3b,0x0b) ||
              P(0xbe,0x0a) || P(0x7a,0x0a)) && WDIFF(3, 1))
        *dst01 = w4;
    else if (P(0xbf,0x8f) || P(0x7e,0x0e) || P(0xbf,0x37) || P(0xdb,0x13))
        *dst01 = MIX2(w1, 3, w4, 1, 2);
    else if (P(0x02,0x00) || P(0x7c,0x28) || P(0xed,0xa9) || P(0xf5,0xb4) ||
             P(0xd9,0x90))
        *dst01 = MIX2(w4, 3, w1, 1, 2);
    else if (P(0x4f,0x4b) || P(0xfb,0x7b) || P(0xfe,0x7e) || P(0x9f,0x1b) ||
             P(0x2f,0x0b) || P(0xbe,0x0a) || P(0x7e,0x0a) || P(0xfb,0x4b) ||
             P(0xfb,0xdb) || P(0xfe,0xde) || P(0xfe,0x56) || P(0x57,0x56) ||
             P(0x97,0x16) || P(0x3f,0x1e) || P(0xdb,0x12) || P(0xbb,0x12))
        *dst01 = MIX2(w4, 7, w1, 1, 3);
    else
        *dst01 = w4;
}

/* Assuming p0..p8 is mapped to pixels 0..8, this function interpolates the
 * top-left block of 2x2 pixels in the total of the 4x4 pixels (or 4 blocks) to
 * interpolates. The function is also used for the 3 other blocks of 2x2
 * pixels. */
static inline void hq4x_interp_2x2(uint32_t *dst, int dst_linesize,
                                             const HqWin *win, int k,
                                             const uint32_t *w,
                                             int pos00, int pos01,
                                             int pos10, int pos11,
                                             int p0, int p1, int p2,
                                             int p3, int p4, int p5,
                                             int p6, int p7, int p8)
{
    INTERP_BOOTSTRAP(0);

    uint32_t *dst00 = &dst[dst_linesize*(pos00>>1) + (pos00&1)];
    uint32_t *dst01 = &dst[dst_linesize*(pos01>>1) + (pos01&1)];
    uint32_t *dst10 = &dst[dst_linesize*(pos10>>1) + (pos10&1)];
    uint32_t *dst11 = &dst[dst_linesize*(pos11>>1) + (pos11&1)];

    const int cond00 = (P(0xbf,0x37) || P(0xdb,0x13)) && WDIFF(1, 5);
    const int cond01 = (P(0xdb,0x49) || P(0xef,0x6d)) && WDIFF(7, 3);
    const int cond02 = (P(0x6f,0x2a) || P(0x5b,0x0a) || P(0xbf,0x3a) ||
                        P(0xdf,0x5a) || P(0x9f,0x8a) || P(0xcf,0x8a) ||
                        P(0xef,0x4e) || P(0x3f,0x0e) || P(0xfb,0x5a) ||
                        P(0xbb,0x8a) || P(0x7f,0x5a) || P(0xaf,0x8a) ||
                        P(0xeb,0x8a)) && WDIFF(3, 1);
    const int cond03 = P(0xdb,0x49) || P(0xef,0x6d);
    const int cond04 = P(0xbf,0x37) || P(0xdb,0x13);
    const int cond05 = P(0x1b,0x03) || P(0x4f,0x43) || P(0x8b,0x83) ||
                       P(0x6b,0x43);
    const int cond06 = P(0x4b,0x09) || P(0x8b,0x89) || P(0x1f,0x19) ||
                       P(0x3b,0x19);
    const int cond07 = P(0x0b,0x08) || P(0xf9,0x68) || P(0xf3,0x62) ||
                       P(0x6d,0x6c) || P(0x67,0x66) || P(0x3d,0x3c) ||
                       P(0x37,0x36) || P(0xf9,0xf8) || P(0xdd,0xdc) ||
                       P(0xf3,0xf2) || P(0xd7,0xd6) || P(0xdd,0x1c) ||
                       P(0xd7,0x16) || P(0x0b,0x02);
    const int cond08 = (P(0x0f,0x0b) || P(0x2b,0x0b) || P(0xfe,0x4a) ||
                        P(0xfe,0x1a)) && WDIFF(3, 1);
    const int cond09 = P(0x2f,0x2f);
    const int cond10 = P(0x0a,0x00);
    const int cond11 = P(0x0b,0x09);
    const int cond12 = P(0x7e,0x2a) || P(0xef,0xab);
    const int cond13 = P(0xbf,0x8f) || P(0x7e,0x0e);
    const int cond14 = P(0x4f,0x4b) || P(0x9f,0x1b) || P(0x2f,0x0b) ||
                       P(0xbe,0x0a) || P(0xee,0x0a) || P(0x7e,0x0a) ||
                       P(0xeb,0x4b) || P(0x3b,0x1b);
    const int cond15 = P(0x0b,0x03);

    if (cond00)
        *dst00 = MIX2(w4, 5, w3, 3, 3);
    else if (cond01)
        *dst00 = MIX2(w4, 5, w1, 3, 3);
    else if ((P(0x0b,0x0b) || P(0xfe,0x4a) || P(0xfe,0x1a)) && WDIFF(3, 1))
        *dst00 = w4;
    else if (cond02)
        *dst00 = MIX2(w4, 5, w0, 3, 3);
    else if (cond03)
        *dst00 = MIX2(w4, 3, w3, 1, 2);
    else if (cond04)
        *dst00 = MIX2(w4, 3, w1, 1, 2);
    else if (cond05)
        *dst00 = MIX2(w4, 5, w3, 3, 3);
    else if (cond06)
        *dst00 = MIX2(w4, 5, w1, 3, 3);
    else if (P(0x0f,0x0b) || P(0x5e,0x0a) || P(0x2b,0x0b) || P(0xbe,0x0a) ||
             P(0x7a,0x0a) || P(0xee,0x0a))
        *dst00 = MIX2(w1, 1, w3, 1, 1);
    else if (cond07)
        *dst00 = MIX2(w4, 5, w0, 3, 3);
    else
        *dst00 = MIX3(w4, 2, w1, 1, w3, 1, 2);

    if (cond00)
        *dst01 = MIX2(w4, 7, w3, 1, 3);
    else if (cond08)
        *dst01 = w4;
    else if (cond02)
        *dst01 = MIX2(w4, 3, w0, 1, 2);
    else if (cond09)
        *dst01 = w4;
    else if (cond10)
        *dst01 = MIX3(w4, 5, w1, 2, w3, 1, 3);
    else if (P(0x0b,0x08))
        *dst01 = MIX3(w4, 5, w1, 2, w0, 1, 3);
    else if (cond11)
        *dst01 = MIX2(w4, 5, w1, 3, 3);
    else if (cond04)
        *dst01 = MIX2(w1, 3, w4, 1, 2);
    else if (cond12)
        *dst01 = MIX3(w1, 2, w4, 1, w3, 1, 2);
    else if (cond13)
        *dst01 = MIX2(w1, 5, w3, 3, 3);
    else if (cond05)
        *dst01 = MIX2(w4, 7, w3, 1, 3);
    else if (P(0xf3,0x62) || P(0x67,0x66) || P(0x37,0x36) || P(0xf3,0xf2) ||
             P(0xd7,0xd6) || P(0xd7,0x16) || P(0x0b,0x02))
        *dst01 = MIX2(w4, 3, w0, 1, 2);
    else if (cond14)
        *dst01 = MIX2(w1, 1, w4, 1, 1);
    else
        *dst01 = MIX2(w4, 3, w1, 1, 2);

    if (cond01)
        *dst10 = MIX2(w4, 7, w1, 1, 3);
    else if (cond08)
        *dst10 = w4;
    else if (cond02)
        *dst10 = MIX2(w4, 3, w0, 1, 2);
    else if (cond09)
        *dst10 = w4;
    else if (cond10)
        *dst10 = MIX3(w4, 5, w3, 2, w1, 1, 3);
    else if (P(0x0b,0x02))
        *dst10 = MIX3(w4, 5, w3, 2, w0, 1, 3);
    else if (cond15)
        *dst10 = MIX2(w4, 5, w3, 3, 3);
    else if (cond03)
        *dst10 = MIX2(w3, 3, w4, 1, 2);
    else if (cond13)
        *dst10 = MIX3(w3, 2, w4, 1, w1, 1, 2);
    else if (cond12)
        *dst10 = MIX2(w3, 5, w1, 3, 3);
    else if (cond06)
        *dst10 = MIX2(w4, 7, w1, 1, 3);
    else if (P(0x0b,0x08) || P(0xf9,0x68) || P(0x6d,0x6c) || P(0x3d,0x3c) ||
             P(0xf9,0xf8) || P(0xdd,0xdc) || P(0xdd,0x1c))
        *dst10 = MIX2(w4, 3, w0, 1, 2);
    else if (cond14)
        *dst10 = MIX2(w3, 1, w4, 1, 1);
    else
        *dst10 = MIX2(w4, 3, w3, 1, 2);

    if ((P(0x7f,0x2b) || P(0xef,0xab) || P(0xbf,0x8f) || P(0x7f,0x0f)) &&
         WDIFF(3, 1))
        *dst11 = w4;
    else if (cond02)
        *dst11 = MIX2(w4, 7, w0, 1, 3);
    else if (cond15)
        *dst11 = MIX2(w4, 7, w3, 1, 3);
    else if (cond11)
        *dst11 = MIX2(w4, 7, w1, 1, 3);
    else if (P(0x0a,0x00) || P(0x7e,0x2a) || P(0xef,0xab) || P(0xbf,0x8f) ||
             P(0x7e,0x0e))
        *dst11 = MIX3(w4, 6, w3, 1, w1, 1, 3);
    else if (cond07)
        *dst11 = MIX2(w4, 7, w0, 1, 3);
    else
        *dst11 = w4;
}

/* ---- end of the rules from FFmpeg ---- */

void
pas_hqx (const PasSource *src, int n, const PasDest *dst)
{
  const int s = src->stride, ds = dst->stride;
  HqWin win;
  int x, y, i;

  win.alpha = src->alpha;
  for (y = 0; y < src->height; y++)
    for (x = 0; x < src->width; x++)
      {
        const long o = (long) y * s + x;
        const long off[9] = { -s - 1, -s, -s + 1, -1, 0, 1, s - 1, s, s + 1 };
        const uint32_t *w = win.pix;
        uint32_t *out = dst->pix + (long) n * y * ds + (long) n * x;
        int pattern = 0, bit = 0;

        for (i = 0; i < 9; i++)
          {
            win.pix[i] = src->pix[o + off[i]];
            win.yuv[i] = src->yuv[o + off[i]];
            win.a[i]   = PAS_A (src->key[o + off[i]]);
          }
        /* which neighbours differ from the middle pixel (as the
         * reference: equal keys never differ) */
        for (i = 0; i < 9; i++)
          {
            if (i == 4)
              continue;
            if (src->key[o + off[i]] != src->key[o] && hq_diff (&win, 4, i))
              pattern |= 1 << bit;
            bit++;
          }

        if (n == 2)
          {
            out[0]      = hq2x_interp_1x1 (&win, pattern, w, 0,1,2,3,4,5,6,7,8);
            out[1]      = hq2x_interp_1x1 (&win, pattern, w, 2,1,0,5,4,3,8,7,6);
            out[ds]     = hq2x_interp_1x1 (&win, pattern, w, 6,7,8,3,4,5,0,1,2);
            out[ds + 1] = hq2x_interp_1x1 (&win, pattern, w, 8,7,6,5,4,3,2,1,0);
          }
        else if (n == 3)
          {
            hq3x_interp_2x1 (out,          ds, &win, pattern, w, 0,1, 0,1,2,3,4,5,6,7,8, 0);
            hq3x_interp_2x1 (out + 1,      ds, &win, pattern, w, 1,3, 2,5,8,1,4,7,0,3,6, 1);
            hq3x_interp_2x1 (out + ds,     ds, &win, pattern, w, 2,0, 6,3,0,7,4,1,8,5,2, 1);
            hq3x_interp_2x1 (out + ds + 1, ds, &win, pattern, w, 3,2, 8,7,6,5,4,3,2,1,0, 0);
            out[ds + 1] = w[4];
          }
        else
          {
            hq4x_interp_2x2 (out,              ds, &win, pattern, w, 0,1,2,3, 0,1,2,3,4,5,6,7,8);
            hq4x_interp_2x2 (out + 2,          ds, &win, pattern, w, 1,0,3,2, 2,1,0,5,4,3,8,7,6);
            hq4x_interp_2x2 (out + 2 * ds,     ds, &win, pattern, w, 2,3,0,1, 6,7,8,3,4,5,0,1,2);
            hq4x_interp_2x2 (out + 2 * ds + 2, ds, &win, pattern, w, 3,2,1,0, 8,7,6,5,4,3,2,1,0);
          }
      }
}
