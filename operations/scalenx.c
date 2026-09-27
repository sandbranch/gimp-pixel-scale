/*
 * Pixel Art Scale: nearest neighbour, and Scale2x, Scale3x and Scale4x
 *
 * scalenx.c
 * Copyright 2026 by David
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
 * Scale2x and Scale3x are Andrea Mazzoleni's AdvMAME2x and AdvMAME3x
 * (Scale2x is the same as Eric Johnston's EPX), written here from the
 * description of the algorithm at https://www.scale2x.it/algorithm. With
 * the pixel E and its neighbours
 *
 *     A B C
 *     D E F
 *     G H I
 *
 * Scale2x makes E into four pixels:
 *
 *     E0 = D == B ? D : E     E1 = B == F ? F : E
 *     E2 = D == H ? D : E     E3 = H == F ? F : E
 *
 * if B != H and D != F; otherwise all four are E. Scale3x makes nine the
 * same way (see scale3x below). Scale4x is Scale2x applied twice; the
 * intermediate image repeats its own edge pixels, as in the reference
 * implementation (scalebit.c), when the edges are not wrapped around.
 *
 * Every output pixel is one of the input pixels: no new colors. Pixels
 * are the same when they are equal as 32-bit values (PAS_ALPHA_STRAIGHT,
 * as the reference compares them) or when they look the same, that is
 * when their premultiplied values are equal (PAS_ALPHA_WEIGHTED: all fully
 * transparent pixels are the same).
 */

#include <stdlib.h>
#include <string.h>

#include "pas.h"

void
pas_nearest (const PasSource *src, int n, const PasDest *dst)
{
  int x, y, i, j;

  for (y = 0; y < src->height; y++)
    {
      const uint32_t *s = src->pix + (long) y * src->stride;
      uint32_t *d = dst->pix + (long) y * n * dst->stride;

      for (x = 0; x < src->width; x++)
        for (i = 0; i < n; i++)
          d[x * n + i] = s[x];
      for (j = 1; j < n; j++)
        memcpy (d + (long) j * dst->stride, d,
                sizeof (uint32_t) * (size_t) src->width * (size_t) n);
    }
}

/* Scale2x of one pixel: k points at its key, p at the pixel itself (the
 * same offsets are used in both), out at the top left of the 2 x 2 block */
static inline void
scale2x_pixel (const uint32_t *k, const uint32_t *p, int stride,
               uint32_t *out, int out_stride)
{
  const uint32_t B = k[-stride], D = k[-1], F = k[1], H = k[stride];
  const uint32_t E = p[0];

  if (B != H && D != F)
    {
      out[0]              = D == B ? p[-1] : E;
      out[1]              = B == F ? p[1] : E;
      out[out_stride]     = D == H ? p[-1] : E;
      out[out_stride + 1] = H == F ? p[1] : E;
    }
  else
    {
      out[0] = out[1] = out[out_stride] = out[out_stride + 1] = E;
    }
}

static void
scale2x (const PasSource *src, const PasDest *dst)
{
  int x, y;

  for (y = 0; y < src->height; y++)
    {
      const long o = (long) y * src->stride;

      for (x = 0; x < src->width; x++)
        scale2x_pixel (src->key + o + x, src->pix + o + x, src->stride,
                       dst->pix + (long) 2 * y * dst->stride + 2 * x,
                       dst->stride);
    }
}

static void
scale3x (const PasSource *src, const PasDest *dst)
{
  const int s = src->stride, ds = dst->stride;
  int x, y;

  for (y = 0; y < src->height; y++)
    for (x = 0; x < src->width; x++)
      {
        const long o = (long) y * s + x;
        const uint32_t *k = src->key + o, *p = src->pix + o;
        uint32_t *out = dst->pix + (long) 3 * y * ds + 3 * x;
        const uint32_t A = k[-s - 1], B = k[-s], C = k[-s + 1];
        const uint32_t D = k[-1], E = k[0], F = k[1];
        const uint32_t G = k[s - 1], H = k[s], I = k[s + 1];
        const uint32_t e = p[0];

        if (B != H && D != F)
          {
            out[0]          = D == B ? p[-1] : e;
            out[1]          = (D == B && E != C) || (B == F && E != A) ? p[-s] : e;
            out[2]          = B == F ? p[1] : e;
            out[ds]         = (D == B && E != G) || (D == H && E != A) ? p[-1] : e;
            out[ds + 1]     = e;
            out[ds + 2]     = (B == F && E != I) || (H == F && E != C) ? p[1] : e;
            out[2 * ds]     = D == H ? p[-1] : e;
            out[2 * ds + 1] = (D == H && E != I) || (H == F && E != G) ? p[s] : e;
            out[2 * ds + 2] = H == F ? p[1] : e;
          }
        else
          {
            int i;

            for (i = 0; i < 3; i++)
              out[i * ds] = out[i * ds + 1] = out[i * ds + 2] = e;
          }
      }
}

/* where an intermediate row or column c of Scale4x comes from: c itself
 * when wrapping (the image repeats, so does the intermediate one), or c
 * clamped to the intermediate image, from lo to hi - 1 */
static inline int
clamp_mid (int c, int lo, int hi, int wrap)
{
  if (wrap)
    return c;
  return c < lo ? lo : c >= hi ? hi - 1 : c;
}

/*
 * Scale2x twice. The intermediate image covers the tile at twice the size
 * with a border of one pixel, which the second pass reads. Its pixels
 * outside the image come from its own edge (clamp_mid), so the second pass
 * sees the edge repeated, as the reference does it.
 */
static void
scale4x (const PasSource *src, const PasDest *dst)
{
  const int mw = 2 * src->width + 2, mh = 2 * src->height + 2;
  uint32_t *mid = malloc (sizeof (uint32_t) * 2 * (size_t) mw * (size_t) mh);
  uint32_t *midkey = mid + (size_t) mw * (size_t) mh;
  PasSource m;
  int x, y;

  if (!mid)
    return;

  /* mid[(Y + 1) * mw + X + 1] is the intermediate pixel (X, Y), for X
   * from -1 to 2 * width */
  for (y = -1; y <= 2 * src->height; y++)
    {
      const int cy = clamp_mid (y, 2 * src->y0, 2 * src->y1, src->wrap);

      for (x = -1; x <= 2 * src->width; x++)
        {
          const int cx = clamp_mid (x, 2 * src->x0, 2 * src->x1, src->wrap);
          /* the source pixel and which of its four the intermediate
           * pixel is (floor, also for -1) */
          const int sx = cx >> 1, sy = cy >> 1;
          const long o = (long) sy * src->stride + sx;
          uint32_t block[4];

          scale2x_pixel (src->key + o, src->pix + o, src->stride, block, 2);
          mid[(long) (y + 1) * mw + x + 1] = block[(cy & 1) * 2 + (cx & 1)];
        }
    }

  for (y = 0; y < mw * mh; y++)
    midkey[y] = src->alpha == PAS_ALPHA_WEIGHTED ? pas_premultiply (mid[y])
                                                 : mid[y];

  m = *src;
  m.pix = mid + mw + 1;
  m.key = midkey + mw + 1;
  m.stride = mw;
  m.width = 2 * src->width;
  m.height = 2 * src->height;
  scale2x (&m, dst);
  free (mid);
}

void
pas_scalenx (const PasSource *src, int n, const PasDest *dst)
{
  switch (n)
    {
    case 2:
      scale2x (src, dst);
      break;
    case 3:
      scale3x (src, dst);
      break;
    default:
      scale4x (src, dst);
      break;
    }
}
