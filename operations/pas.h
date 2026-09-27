/*
 * Pixel Art Scale: the parts shared by the scalers
 *
 * pas.h
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
 * The scalers work on 8-bit pixels packed as 0xAARRGGBB (alpha in the top
 * byte, blue in the lowest), the layout of the reference implementations,
 * so that their packed arithmetic can be kept as it is.
 *
 * A scaler gets a PasSource: the pixels of a tile of the image with a
 * border of PAS_MARGIN pixels on every side, already filled in by the
 * operation from the image's edges (repeated or wrapped around), so the
 * scalers never test for edges themselves. pix points at the first pixel
 * of the tile; pix[y * stride + x] is valid for x and y from -PAS_MARGIN
 * to the tile's width or height + PAS_MARGIN - 1.
 *
 * Alpha. The reference implementations compare only R, G and B (hqx, xBR)
 * or whole pixels (Scale2x), and blend alpha like a fourth color. With
 * PAS_ALPHA_STRAIGHT the scalers do the same, and match them byte for byte.
 * With PAS_ALPHA_WEIGHTED (the default of the operation):
 *
 *   - colors are compared as they look over black, that is premultiplied
 *     by alpha (key[], and yuv[] made from it), so all fully transparent
 *     pixels are the same, whatever color they hide; alpha is compared as
 *     well (see hqx.c and xbr.c for how);
 *   - blends are weighted by alpha, so the color of a (nearly) transparent
 *     pixel does not bleed into its visible neighbours. When all the
 *     pixels in a blend have the same alpha, the reference arithmetic is
 *     used, so opaque images give exactly the results of the reference
 *     implementations in both modes.
 */

#ifndef PAS_H
#define PAS_H

#include <stdint.h>

/* the border a scaler may read around its tile: xBR reads 2 pixels far,
 * Scale4x (Scale2x twice) 2 pixels at the original size */
#define PAS_MARGIN 2

typedef enum
{
  PAS_ALPHA_WEIGHTED,
  PAS_ALPHA_STRAIGHT
} PasAlpha;

typedef struct
{
  const uint32_t *pix;   /* the pixels, 0xAARRGGBB, not premultiplied */
  const uint32_t *key;   /* what is compared: pix premultiplied by alpha
                          * (PAS_ALPHA_WEIGHTED) or pix itself */
  const uint32_t *yuv;   /* YUV of key, 0x00YYUUVV, for hqx and xBR */
  int             stride;/* in pixels, of pix, key and yuv alike */
  int             width; /* the tile, in pixels of the source */
  int             height;
  PasAlpha        alpha;
  /* the image, relative to the tile's first pixel: x from x0 to x1 - 1,
   * y from y0 to y1 - 1 (Scale4x repeats its intermediate image at these
   * edges, like the reference); wrap: the edges wrap around instead */
  int             x0, y0, x1, y1;
  int             wrap;
} PasSource;

/* the output: rows of width * n pixels, stride in pixels */
typedef struct
{
  uint32_t *pix;
  int       stride;
} PasDest;

#define PAS_A(c) ((c) >> 24)
#define PAS_R(c) (((c) >> 16) & 0xff)
#define PAS_G(c) (((c) >> 8) & 0xff)
#define PAS_B(c) ((c) & 0xff)

/* (a * b + 127) / 255, the premultiplied 8-bit value */
static inline uint32_t
pas_mul8 (uint32_t a, uint32_t b)
{
  return (a * b + 127) / 255;
}

static inline uint32_t
pas_premultiply (uint32_t c)
{
  uint32_t a = PAS_A (c);

  if (a == 255)
    return c;
  return (a << 24) | (pas_mul8 (PAS_R (c), a) << 16) |
         (pas_mul8 (PAS_G (c), a) << 8) | pas_mul8 (PAS_B (c), a);
}

/*
 * A blend weighted by alpha of up to three pixels c[i] with weights w[i]
 * that add up to 1 << s: alpha is (sum w a) >> s, as in the reference;
 * each color is (sum w a c) / (sum w a), rounded down, so that when all
 * alphas are the same it is (sum w c) >> s, the reference's result. With
 * all alphas 0 the colors are blended without weights.
 */
static inline uint32_t
pas_blend_weighted (int n, const uint32_t *c, const int *w, int s)
{
  uint32_t wa = 0, r = 0, g = 0, b = 0, a;
  int i;

  for (i = 0; i < n; i++)
    wa += (uint32_t) w[i] * PAS_A (c[i]);
  a = wa >> s;
  if (wa == 0)
    {
      for (i = 0; i < n; i++)
        {
          r += (uint32_t) w[i] * PAS_R (c[i]);
          g += (uint32_t) w[i] * PAS_G (c[i]);
          b += (uint32_t) w[i] * PAS_B (c[i]);
        }
      return ((r >> s) << 16) | ((g >> s) << 8) | (b >> s);
    }
  for (i = 0; i < n; i++)
    {
      uint32_t k = (uint32_t) w[i] * PAS_A (c[i]);

      r += k * PAS_R (c[i]);
      g += k * PAS_G (c[i]);
      b += k * PAS_B (c[i]);
    }
  return (a << 24) | ((r / wa) << 16) | ((g / wa) << 8) | (b / wa);
}

/* the scalers; n is the factor, 2 to 4 */
void pas_nearest (const PasSource *src, int n, const PasDest *dst);
void pas_scalenx (const PasSource *src, int n, const PasDest *dst);
void pas_hqx     (const PasSource *src, int n, const PasDest *dst);
void pas_xbr     (const PasSource *src, int n, const PasDest *dst);

/* the YUV of a color, as hqx and xBR compute it (0x00YYUUVV); c is
 * 0x??RRGGBB, alpha is ignored */
uint32_t pas_hqx_yuv (uint32_t c);
uint32_t pas_xbr_yuv (uint32_t c);

#endif
