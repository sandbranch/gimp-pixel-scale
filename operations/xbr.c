/*
 * Pixel Art Scale: xBR 2x, 3x and 4x
 *
 * xbr.c
 *
 * The xBR rules (the FILT2, FILT3 and FILT4 macros and how they are
 * called) are from FFmpeg's libavfilter/vf_xbr.c (FFmpeg 8.0.1), under
 * this notice:
 *
 *   Copyright (c) 2011, 2012 Hyllian/Jararaca <sergiogdb@gmail.com>
 *   Copyright (c) 2014 Arwa Arif <arwaarif1994@gmail.com>
 *
 *   FFmpeg is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU Lesser General Public
 *   License as published by the Free Software Foundation; either
 *   version 2.1 of the License, or (at your option) any later version.
 *
 *   FFmpeg is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public
 *   License along with FFmpeg; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 *   02110-1301 USA
 *
 * This file is a modified version of it and is under the same license,
 * the GNU Lesser General Public License, version 2.1 or (at your option)
 * any later version; the changes are Copyright 2026 by David. Changed:
 * the macros take indices of the 21 pixels around a pixel instead of
 * their values, so that they can compare keys (KEY) and look up pixels
 * (PIX) and YUV values; df can count alpha; the ALPHA_BLEND macros are
 * functions that also blend alpha; the loop over the image reads the
 * pixels from a tile with a border (pas.h) instead of FFmpeg's frame.
 *
 * xBR is Hyllian's algorithm ("xBR algorithm tutorial",
 * https://forums.libretro.com/t/xbr-algorithm-tutorial/123): for each
 * corner of a pixel it weighs the color differences along the two
 * diagonals of a 5 x 5 neighbourhood (without its corners) to find an
 * edge, and if there is one it blends the pixel's corner of the output
 * towards the neighbour across the edge, more or less far depending on
 * how steep the edge is. Colors are compared by the sum of their Y, U
 * and V differences, from FFmpeg's integer YUV (pas_xbr_yuv).
 *
 * Alpha (see pas.h). PAS_ALPHA_STRAIGHT: as FFmpeg, alpha is not
 * compared; it is blended with the same weights as the colors (FFmpeg
 * has no alpha: its format is 0RGB). PAS_ALPHA_WEIGHTED: the difference
 * of two pixels is that of their premultiplied colors plus the difference
 * of their alphas, as in libxbr-standalone (Treeki's library made from
 * FFmpeg's code), and blends are weighted by alpha.
 */

#include <stdlib.h>

#include "pas.h"

/* FFmpeg's expressions are kept as they are, with && inside || */
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wparentheses"
#endif

#define LB_MASK       0x00FEFEFE
#define RED_BLUE_MASK 0x00FF00FF
#define GREEN_MASK    0x0000FF00

/*
 * FFmpeg's YUV table (init in vf_xbr.c), computed per color:
 *
 *   y = (299*r + 587*g + 114*b) / 1000
 *   u = (-169*(r-g) + 500*(b-g)) / 1000 + 128
 *   v = ( 500*(r-g) -  81*(b-g)) / 1000 + 128
 *
 * with C's integer division (towards zero). FFmpeg builds the table
 * incrementally along the gray axis; tests/refcheck.c checks that this
 * gives the same for all 2^24 colors.
 */
uint32_t
pas_xbr_yuv (uint32_t c)
{
  const int r = PAS_R (c), g = PAS_G (c), b = PAS_B (c);
  const int rg = r - g, bg = b - g;
  const uint32_t y = (uint32_t) ((299 * r + 587 * g + 114 * b) / 1000);
  const uint32_t u = (uint32_t) ((-169 * rg + 500 * bg) / 1000 + 128);
  const uint32_t v = (uint32_t) ((500 * rg - 81 * bg) / 1000 + 128);

  return (y << 16) + (u << 8) + v;
}

/* the pixels around the one being scaled, as FFmpeg names them:
 *
 *        A1 B1 C1
 *     A0 PA PB PC C4
 *     D0 PD PE PF F4
 *     G0 PG PH PI I4
 *        G5 H5 I5
 */
enum
{
  PE, PI, PH, PF, PG, PC, PD, PB, PA, G5, C4, G0, D0, C1, B1, F4, I4, H5,
  I5, A0, A1, XBR_N
};

static const int xbr_dx[XBR_N] =
  { 0, 1, 0, 1, -1, 1, -1, 0, -1, -1, 2, -2, -2, 1, 0, 2, 2, 0, 1, -2, -1 };
static const int xbr_dy[XBR_N] =
  { 0, 1, 1, 0, 1, -1, 0, -1, -1, 2, -1, 1, 0, -2, -2, 0, 1, 2, 2, -1, -2 };

typedef struct
{
  uint32_t pix[XBR_N];
  uint32_t key[XBR_N];
  uint32_t yuv[XBR_N];
  PasAlpha alpha;
} XbrWin;

#define PIX(i) (win->pix[i])
#define KEY(i) (win->key[i])

static inline unsigned
xbr_df (const XbrWin *win, int i, int j)
{
#define YMASK 0xff0000
#define UMASK 0x00ff00
#define VMASK 0x0000ff
#define ABSDIFF(a,b) (abs((int)(a)-(int)(b)))
  const uint32_t yuv1 = win->yuv[i], yuv2 = win->yuv[j];
  unsigned d = (ABSDIFF(yuv1 & YMASK, yuv2 & YMASK) >> 16) +
               (ABSDIFF(yuv1 & UMASK, yuv2 & UMASK) >>  8) +
                ABSDIFF(yuv1 & VMASK, yuv2 & VMASK);

  if (win->alpha == PAS_ALPHA_WEIGHTED)
    d += ABSDIFF (PAS_A (win->key[i]), PAS_A (win->key[j]));
  return d;
}

#define df(A, B) xbr_df(win, A, B)
#define eq(A, B) (df(A, B) < 155)

/* FFmpeg's blends of the colors: a moves m / 2^s of the way to b */
#define ALPHA_BLEND_BASE(a, b, m, s) (  (RED_BLUE_MASK & (((a) & RED_BLUE_MASK) + (((((b) & RED_BLUE_MASK) - ((a) & RED_BLUE_MASK)) * (m)) >> (s)))) \
                                      | (GREEN_MASK    & (((a) & GREEN_MASK)    + (((((b) & GREEN_MASK)    - ((a) & GREEN_MASK))    * (m)) >> (s)))))

/* the blend of two pixels, with alpha: the colors as FFmpeg does it
 * (halving each for m / 2^s = 1/2), alpha a moving m / 2^s of the way
 * (rounded down); weighted by alpha when asked for and the alphas
 * differ */
static inline uint32_t
xbr_blend (PasAlpha alpha, uint32_t a, uint32_t b, int m, int s)
{
  const uint32_t aa = PAS_A (a), ab = PAS_A (b);
  uint32_t rgb;

  if (alpha == PAS_ALPHA_WEIGHTED && aa != ab)
    {
      const uint32_t c[2] = { a, b };
      const int w[2] = { (1 << s) - m, m };

      return pas_blend_weighted (2, c, w, s);
    }
  if (m == 1 && s == 1)
    rgb = ((a & LB_MASK) >> 1) + ((b & LB_MASK) >> 1);
  else
    rgb = ALPHA_BLEND_BASE (a, b, (uint32_t) m, s);
  return rgb | (((aa * ((1u << s) - (uint32_t) m) + ab * (uint32_t) m) >> s) << 24);
}

#define ALPHA_BLEND_32_W(a, b)  xbr_blend(win->alpha, a, b, 1, 3)
#define ALPHA_BLEND_64_W(a, b)  xbr_blend(win->alpha, a, b, 1, 2)
#define ALPHA_BLEND_128_W(a, b) xbr_blend(win->alpha, a, b, 1, 1)
#define ALPHA_BLEND_192_W(a, b) xbr_blend(win->alpha, a, b, 3, 2)
#define ALPHA_BLEND_224_W(a, b) xbr_blend(win->alpha, a, b, 7, 3)

/* ---- the rules of xBR, from FFmpeg's vf_xbr.c (see the top) ---- */

#define FILT2(PE, PI, PH, PF, PG, PC, PD, PB, PA, G5, C4, G0, D0, C1, B1, F4, I4, H5, I5, A0, A1,    \
              N0, N1, N2, N3) do {                                                                   \
    if (KEY(PE) != KEY(PH) && KEY(PE) != KEY(PF)) {                                                  \
        const unsigned e = df(PE,PC) + df(PE,PG) + df(PI,H5) + df(PI,F4) + (df(PH,PF)<<2);           \
        const unsigned i = df(PH,PD) + df(PH,I5) + df(PF,I4) + df(PF,PB) + (df(PE,PI)<<2);           \
        if (e <= i) {                                                                                \
            const uint32_t px = PIX(df(PE,PF) <= df(PE,PH) ? PF : PH);                               \
            if (e < i && (!eq(PF,PB) && !eq(PH,PD) || eq(PE,PI)                                      \
                          && (!eq(PF,I4) && !eq(PH,I5))                                              \
                          || eq(PE,PG) || eq(PE,PC))) {                                              \
                const unsigned ke = df(PF,PG);                                                       \
                const unsigned ki = df(PH,PC);                                                       \
                const int left    = ke<<1 <= ki && KEY(PE) != KEY(PG) && KEY(PD) != KEY(PG);         \
                const int up      = ke >= ki<<1 && KEY(PE) != KEY(PC) && KEY(PB) != KEY(PC);         \
                if (left && up) {                                                                    \
                    E[N3] = ALPHA_BLEND_224_W(E[N3], px);                                            \
                    E[N2] = ALPHA_BLEND_64_W( E[N2], px);                                            \
                    E[N1] = E[N2];                                                                   \
                } else if (left) {                                                                   \
                    E[N3] = ALPHA_BLEND_192_W(E[N3], px);                                            \
                    E[N2] = ALPHA_BLEND_64_W( E[N2], px);                                            \
                } else if (up) {                                                                     \
                    E[N3] = ALPHA_BLEND_192_W(E[N3], px);                                            \
                    E[N1] = ALPHA_BLEND_64_W( E[N1], px);                                            \
                } else { /* diagonal */                                                              \
                    E[N3] = ALPHA_BLEND_128_W(E[N3], px);                                            \
                }                                                                                    \
            } else {                                                                                 \
                E[N3] = ALPHA_BLEND_128_W(E[N3], px);                                                \
            }                                                                                        \
        }                                                                                            \
    }                                                                                                \
} while (0)

#define FILT3(PE, PI, PH, PF, PG, PC, PD, PB, PA, G5, C4, G0, D0, C1, B1, F4, I4, H5, I5, A0, A1,    \
              N0, N1, N2, N3, N4, N5, N6, N7, N8) do {                                               \
    if (KEY(PE) != KEY(PH) && KEY(PE) != KEY(PF)) {                                                  \
        const unsigned e = df(PE,PC) + df(PE,PG) + df(PI,H5) + df(PI,F4) + (df(PH,PF)<<2);           \
        const unsigned i = df(PH,PD) + df(PH,I5) + df(PF,I4) + df(PF,PB) + (df(PE,PI)<<2);           \
        if (e <= i) {                                                                                \
            const uint32_t px = PIX(df(PE,PF) <= df(PE,PH) ? PF : PH);                               \
            if (e < i && (!eq(PF,PB) && !eq(PF,PC) || !eq(PH,PD) && !eq(PH,PG) || eq(PE,PI)          \
                          && (!eq(PF,F4) && !eq(PF,I4) || !eq(PH,H5) && !eq(PH,I5))                  \
                          || eq(PE,PG) || eq(PE,PC))) {                                              \
                const unsigned ke = df(PF,PG);                                                       \
                const unsigned ki = df(PH,PC);                                                       \
                const int left    = ke<<1 <= ki && KEY(PE) != KEY(PG) && KEY(PD) != KEY(PG);         \
                const int up      = ke >= ki<<1 && KEY(PE) != KEY(PC) && KEY(PB) != KEY(PC);         \
                if (left && up) {                                                                    \
                    E[N7] = ALPHA_BLEND_192_W(E[N7], px);                                            \
                    E[N6] = ALPHA_BLEND_64_W( E[N6], px);                                            \
                    E[N5] = E[N7];                                                                   \
                    E[N2] = E[N6];                                                                   \
                    E[N8] = px;                                                                      \
                } else if (left) {                                                                   \
                    E[N7] = ALPHA_BLEND_192_W(E[N7], px);                                            \
                    E[N5] = ALPHA_BLEND_64_W( E[N5], px);                                            \
                    E[N6] = ALPHA_BLEND_64_W( E[N6], px);                                            \
                    E[N8] = px;                                                                      \
                } else if (up) {                                                                     \
                    E[N5] = ALPHA_BLEND_192_W(E[N5], px);                                            \
                    E[N7] = ALPHA_BLEND_64_W( E[N7], px);                                            \
                    E[N2] = ALPHA_BLEND_64_W( E[N2], px);                                            \
                    E[N8] = px;                                                                      \
                } else { /* diagonal */                                                              \
                    E[N8] = ALPHA_BLEND_224_W(E[N8], px);                                            \
                    E[N5] = ALPHA_BLEND_32_W( E[N5], px);                                            \
                    E[N7] = ALPHA_BLEND_32_W( E[N7], px);                                            \
                }                                                                                    \
            } else {                                                                                 \
                E[N8] = ALPHA_BLEND_128_W(E[N8], px);                                                \
            }                                                                                        \
        }                                                                                            \
    }                                                                                                \
} while (0)

#define FILT4(PE, PI, PH, PF, PG, PC, PD, PB, PA, G5, C4, G0, D0, C1, B1, F4, I4, H5, I5, A0, A1,    \
              N15, N14, N11, N3, N7, N10, N13, N12, N9, N6, N2, N1, N5, N8, N4, N0) do {             \
    if (KEY(PE) != KEY(PH) && KEY(PE) != KEY(PF)) {                                                  \
        const unsigned e = df(PE,PC) + df(PE,PG) + df(PI,H5) + df(PI,F4) + (df(PH,PF)<<2);           \
        const unsigned i = df(PH,PD) + df(PH,I5) + df(PF,I4) + df(PF,PB) + (df(PE,PI)<<2);           \
        if (e <= i) {                                                                                \
            const uint32_t px = PIX(df(PE,PF) <= df(PE,PH) ? PF : PH);                               \
            if (e < i && (!eq(PF,PB) && !eq(PH,PD) || eq(PE,PI)                                      \
                          && (!eq(PF,I4) && !eq(PH,I5))                                              \
                          || eq(PE,PG) || eq(PE,PC))) {                                              \
                const unsigned ke = df(PF,PG);                                                       \
                const unsigned ki = df(PH,PC);                                                       \
                const int left    = ke<<1 <= ki && KEY(PE) != KEY(PG) && KEY(PD) != KEY(PG);         \
                const int up      = ke >= ki<<1 && KEY(PE) != KEY(PC) && KEY(PB) != KEY(PC);         \
                if (left && up) {                                                                    \
                    E[N13] = ALPHA_BLEND_192_W(E[N13], px);                                          \
                    E[N12] = ALPHA_BLEND_64_W( E[N12], px);                                          \
                    E[N15] = E[N14] = E[N11] = px;                                                   \
                    E[N10] = E[N3]  = E[N12];                                                        \
                    E[N7]  = E[N13];                                                                 \
                } else if (left) {                                                                   \
                    E[N11] = ALPHA_BLEND_192_W(E[N11], px);                                          \
                    E[N13] = ALPHA_BLEND_192_W(E[N13], px);                                          \
                    E[N10] = ALPHA_BLEND_64_W( E[N10], px);                                          \
                    E[N12] = ALPHA_BLEND_64_W( E[N12], px);                                          \
                    E[N14] = px;                                                                     \
                    E[N15] = px;                                                                     \
                } else if (up) {                                                                     \
                    E[N14] = ALPHA_BLEND_192_W(E[N14], px);                                          \
                    E[N7 ] = ALPHA_BLEND_192_W(E[N7 ], px);                                          \
                    E[N10] = ALPHA_BLEND_64_W( E[N10], px);                                          \
                    E[N3 ] = ALPHA_BLEND_64_W( E[N3 ], px);                                          \
                    E[N11] = px;                                                                     \
                    E[N15] = px;                                                                     \
                } else { /* diagonal */                                                              \
                    E[N11] = ALPHA_BLEND_128_W(E[N11], px);                                          \
                    E[N14] = ALPHA_BLEND_128_W(E[N14], px);                                          \
                    E[N15] = px;                                                                     \
                }                                                                                    \
            } else {                                                                                 \
                E[N15] = ALPHA_BLEND_128_W(E[N15], px);                                              \
            }                                                                                        \
        }                                                                                            \
    }                                                                                                \
} while (0)

/* ---- end of the rules from FFmpeg ---- */

void
pas_xbr (const PasSource *src, int n, const PasDest *dst)
{
  const int s = src->stride;
  const int nl = dst->stride, nl1 = nl + nl, nl2 = nl1 + nl;
  long off[XBR_N];
  XbrWin w;
  const XbrWin *win = &w;
  int x, y, i;

  for (i = 0; i < XBR_N; i++)
    off[i] = (long) xbr_dy[i] * s + xbr_dx[i];
  w.alpha = src->alpha;

  for (y = 0; y < src->height; y++)
    for (x = 0; x < src->width; x++)
      {
        const long o = (long) y * s + x;
        uint32_t *E = dst->pix + (long) n * y * nl + (long) n * x;

        for (i = 0; i < XBR_N; i++)
          {
            w.pix[i] = src->pix[o + off[i]];
            w.key[i] = src->key[o + off[i]];
            w.yuv[i] = src->yuv[o + off[i]];
          }

        if (n == 2) {
            E[0]  = E[1]      =     // 0, 1
            E[nl] = E[nl + 1] = PIX(PE); // 2, 3

            FILT2(PE, PI, PH, PF, PG, PC, PD, PB, PA, G5, C4, G0, D0, C1, B1, F4, I4, H5, I5, A0, A1, 0, 1, nl, nl+1);
            FILT2(PE, PC, PF, PB, PI, PA, PH, PD, PG, I4, A1, I5, H5, A0, D0, B1, C1, F4, C4, G5, G0, nl, 0, nl+1, 1);
            FILT2(PE, PA, PB, PD, PC, PG, PF, PH, PI, C1, G0, C4, F4, G5, H5, D0, A0, B1, A1, I4, I5, nl+1, nl, 1, 0);
            FILT2(PE, PG, PD, PH, PA, PI, PB, PF, PC, A0, I5, A1, B1, I4, F4, H5, G5, D0, G0, C1, C4, 1, nl+1, 0, nl);
        } else if (n == 3) {
            E[0]   = E[1]     = E[2]     =     // 0, 1, 2
            E[nl]  = E[nl+1]  = E[nl+2]  =     // 3, 4, 5
            E[nl1] = E[nl1+1] = E[nl1+2] = PIX(PE); // 6, 7, 8

            FILT3(PE, PI, PH, PF, PG, PC, PD, PB, PA, G5, C4, G0, D0, C1, B1, F4, I4, H5, I5, A0, A1, 0, 1, 2, nl, nl+1, nl+2, nl1, nl1+1, nl1+2);
            FILT3(PE, PC, PF, PB, PI, PA, PH, PD, PG, I4, A1, I5, H5, A0, D0, B1, C1, F4, C4, G5, G0, nl1, nl, 0, nl1+1, nl+1, 1, nl1+2, nl+2, 2);
            FILT3(PE, PA, PB, PD, PC, PG, PF, PH, PI, C1, G0, C4, F4, G5, H5, D0, A0, B1, A1, I4, I5, nl1+2, nl1+1, nl1, nl+2, nl+1, nl, 2, 1, 0);
            FILT3(PE, PG, PD, PH, PA, PI, PB, PF, PC, A0, I5, A1, B1, I4, F4, H5, G5, D0, G0, C1, C4, 2, nl+2, nl1+2, 1, nl+1, nl1+1, 0, nl, nl1);
        } else if (n == 4) {
            E[0]   = E[1]     = E[2]     = E[3]     =     //  0,  1,  2,  3
            E[nl]  = E[nl+1]  = E[nl+2]  = E[nl+3]  =     //  4,  5,  6,  7
            E[nl1] = E[nl1+1] = E[nl1+2] = E[nl1+3] =     //  8,  9, 10, 11
            E[nl2] = E[nl2+1] = E[nl2+2] = E[nl2+3] = PIX(PE); // 12, 13, 14, 15

            FILT4(PE, PI, PH, PF, PG, PC, PD, PB, PA, G5, C4, G0, D0, C1, B1, F4, I4, H5, I5, A0, A1, nl2+3, nl2+2, nl1+3, 3, nl+3, nl1+2, nl2+1, nl2, nl1+1, nl+2, 2, 1, nl+1, nl1, nl, 0);
            FILT4(PE, PC, PF, PB, PI, PA, PH, PD, PG, I4, A1, I5, H5, A0, D0, B1, C1, F4, C4, G5, G0, 3, nl+3, 2, 0, 1, nl+2, nl1+3, nl2+3, nl1+2, nl+1, nl, nl1, nl1+1, nl2+2, nl2+1, nl2);
            FILT4(PE, PA, PB, PD, PC, PG, PF, PH, PI, C1, G0, C4, F4, G5, H5, D0, A0, B1, A1, I4, I5, 0, 1, nl, nl2, nl1, nl+1, 2, 3, nl+2, nl1+1, nl2+1, nl2+2, nl1+2, nl+3, nl1+3, nl2+3);
            FILT4(PE, PG, PD, PH, PA, PI, PB, PF, PC, A0, I5, A1, B1, I4, F4, H5, G5, D0, G0, C1, C4, nl2, nl1, nl2+1, nl2+3, nl2+2, nl1+1, nl, 0, nl+1, nl1+2, nl1+3, nl+3, nl+2, 1, 2, 3);
        }
      }
}
