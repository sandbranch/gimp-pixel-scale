/*
 * Pixel Art Scale: the GEGL side shared by pixel-art:scale and
 * pixel-art:rescale
 *
 * pas-gegl.c
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
 * Included by pixel-art-scale.c and pixel-art-rescale.c after gegl-op.h,
 * with PAS_IN_PLACE set to 0 or 1. Both scale a "source" image n times:
 *
 *   - pixel-art:scale (PAS_IN_PLACE 0): the source is the input; the
 *     output is n times larger, input pixel (x, y) becoming the output
 *     pixels n x to n x + n - 1, n y to n y + n - 1.
 *   - pixel-art:rescale (PAS_IN_PLACE 1): the input is the source already
 *     scaled up n times with nearest neighbour (GIMP's Image > Scale Image
 *     with interpolation None), in blocks of n x n pixels from the input's
 *     top left corner; the source pixel is the middle pixel of each block
 *     (n / 2, n / 2, rounded down). The output has the size and place of
 *     the input, so that GIMP can apply it as an ordinary filter.
 *
 * The scalers (scalenx.c, hqx.c, xbr.c) work on 8-bit values, as they
 * were designed: an 8-bit image is scaled in its own encoding (perceptual
 * or linear, so that no value changes on the way in or out), other images
 * as R'G'B'A u8 in their color space. They read the 3 x 3 or 5 x 5 pixels
 * around each pixel; at the edges of the image those are the edge pixels
 * repeated (as in the reference implementations) or, for tiles, the
 * pixels of the other side (edges = wrap).
 *
 * The operations render any part of the output on their own, so GEGL can
 * split the work between threads: for a piece of the output they read the
 * source pixels under it, with a border of PAS_MARGIN pixels.
 */

#include <string.h>

#include "pas.h"

/* a / n rounded down, also for negative a */
static inline gint
floor_div (gint a, gint n)
{
  return a >= 0 ? a / n : -((-a + n - 1) / n);
}

static gint
factor_of (GeglProperties *o)
{
  return CLAMP (o->factor, 2, 4);
}

/*
 * Where the source is. src: its bounding box, in its own coordinates;
 * the scaled source (src times n) lies in the output at (ox, oy) from
 * there; the source pixel (i, j) is the input pixel (bx + i n + c,
 * by + j n + c) in place, else the input pixel (i, j).
 */
typedef struct
{
  GeglRectangle src;
  gint          n, ox, oy, bx, by, c;
  gboolean      infinite;
} Geometry;

static gboolean
geometry (GeglOperation *operation, Geometry *g)
{
  GeglProperties *o = GEGL_PROPERTIES (operation);
  const GeglRectangle *in =
    gegl_operation_source_get_bounding_box (operation, "input");

  memset (g, 0, sizeof *g);
  g->n = factor_of (o);
  if (!in || in->width <= 0 || in->height <= 0)
    return FALSE;
  g->infinite = gegl_rectangle_is_infinite_plane (in);
  if (PAS_IN_PLACE && !g->infinite)
    {
      g->src.width = (in->width + g->n - 1) / g->n;
      g->src.height = (in->height + g->n - 1) / g->n;
      g->ox = g->bx = in->x;
      g->oy = g->by = in->y;
      g->c = g->n / 2;
    }
  else
    g->src = *in;
  return TRUE;
}

/* the 8-bit format the scalers work in: an 8-bit image's own encoding,
 * else R'G'B'A u8, in the image's space */
static const Babl *
work_format (GeglOperation *operation)
{
  const Babl *in = gegl_operation_get_source_format (operation, "input");
  const Babl *space = in ? babl_format_get_space (in) : NULL;
  const char *name = "R'G'B'A u8";

  if (in && !babl_format_is_palette (in) &&
      babl_format_get_type (in, 0) == babl_type ("u8"))
    {
      BablModelFlag flags = babl_get_model_flags (in);

      if (flags & BABL_MODEL_FLAG_LINEAR)
        name = "RGBA u8";
      else if (flags & BABL_MODEL_FLAG_PERCEPTUAL)
        name = "R~G~B~A u8";
    }
  return babl_format_with_space (name, space);
}

static void
prepare (GeglOperation *operation)
{
  const Babl *format = work_format (operation);

  gegl_operation_set_format (operation, "input", format);
  gegl_operation_set_format (operation, "output", format);
}

static GeglRectangle
get_bounding_box (GeglOperation *operation)
{
  const GeglRectangle *in =
    gegl_operation_source_get_bounding_box (operation, "input");
  GeglRectangle result = { 0, 0, 0, 0 };
  Geometry g;

  if (!geometry (operation, &g))
    return result;
  if (g.infinite || PAS_IN_PLACE)
    return *in;
  result.x = g.src.x * g.n;
  result.y = g.src.y * g.n;
  result.width = g.src.width * g.n;
  result.height = g.src.height * g.n;
  return result;
}

/* the source pixels under an output rectangle */
static GeglRectangle
source_tile (const Geometry *g, const GeglRectangle *roi)
{
  GeglRectangle t;

  t.x = floor_div (roi->x - g->ox, g->n);
  t.y = floor_div (roi->y - g->oy, g->n);
  t.width = floor_div (roi->x - g->ox + roi->width - 1, g->n) + 1 - t.x;
  t.height = floor_div (roi->y - g->oy + roi->height - 1, g->n) + 1 - t.y;
  return t;
}

/* the rectangle grown by the scalers' border; when wrapping around and
 * it crosses an edge of the source, the whole width (or height) of the
 * source, where the border comes from (an infinite input has no edges) */
static GeglRectangle
with_border (GeglOperation *operation, const Geometry *g, const GeglRectangle *r)
{
  GeglProperties *o = GEGL_PROPERTIES (operation);
  gint x0 = r->x - PAS_MARGIN, y0 = r->y - PAS_MARGIN;
  gint x1 = r->x + r->width + PAS_MARGIN, y1 = r->y + r->height + PAS_MARGIN;
  GeglRectangle b;

  if (o->edges == PAS_EDGES_WRAP && !g->infinite)
    {
      if (x0 < g->src.x || x1 > g->src.x + g->src.width)
        {
          x0 = g->src.x;
          x1 = g->src.x + g->src.width;
        }
      if (y0 < g->src.y || y1 > g->src.y + g->src.height)
        {
          y0 = g->src.y;
          y1 = g->src.y + g->src.height;
        }
    }
  b.x = x0;
  b.y = y0;
  b.width = x1 - x0;
  b.height = y1 - y0;
  return b;
}

/* the input pixels that hold a rectangle of source pixels */
static GeglRectangle
input_of (const Geometry *g, const GeglRectangle *s)
{
  GeglRectangle r = *s;

  if (PAS_IN_PLACE && !g->infinite)
    {
      r.x = g->bx + s->x * g->n;
      r.y = g->by + s->y * g->n;
      r.width = s->width * g->n;
      r.height = s->height * g->n;
    }
  return r;
}

static GeglRectangle
get_required_for_output (GeglOperation       *operation,
                         const gchar         *input_pad,
                         const GeglRectangle *roi)
{
  GeglRectangle empty = { 0, 0, 0, 0 }, t;
  Geometry g;

  (void) input_pad;
  if (!geometry (operation, &g) || roi->width <= 0 || roi->height <= 0)
    return empty;
  t = source_tile (&g, roi);
  t = with_border (operation, &g, &t);
  return input_of (&g, &t);
}

static GeglRectangle
get_invalidated_by_change (GeglOperation       *operation,
                           const gchar         *input_pad,
                           const GeglRectangle *input_region)
{
  GeglRectangle s = *input_region, r;
  Geometry g;

  (void) input_pad;
  if (!geometry (operation, &g) || g.infinite)
    return *input_region;
  if (PAS_IN_PLACE)
    {
      /* the blocks the change touches */
      s.x = floor_div (input_region->x - g.bx, g.n);
      s.y = floor_div (input_region->y - g.by, g.n);
      s.width = floor_div (input_region->x - g.bx + input_region->width - 1, g.n) + 1 - s.x;
      s.height = floor_div (input_region->y - g.by + input_region->height - 1, g.n) + 1 - s.y;
    }
  s = with_border (operation, &g, &s);
  /* a change near an edge shows at the other edge too when wrapping */
  r.x = g.ox + s.x * g.n;
  r.y = g.oy + s.y * g.n;
  r.width = s.width * g.n;
  r.height = s.height * g.n;
  return r;
}

/* the 8-bit RGBA bytes of the work format to 0xAARRGGBB and back */
static inline guint32
pack (const guint8 *p)
{
  return ((guint32) p[3] << 24) | ((guint32) p[0] << 16) |
         ((guint32) p[1] << 8) | p[2];
}

static inline void
unpack (guint32 c, guint8 *p)
{
  p[0] = PAS_R (c);
  p[1] = PAS_G (c);
  p[2] = PAS_B (c);
  p[3] = PAS_A (c);
}

/* where the source row or column v comes from: v itself inside the
 * source, else the edge repeated or the other side (wrap) */
static inline gint
source_index (gint v, gint lo, gint size, gboolean wrap)
{
  if (v >= lo && v < lo + size)
    return v;
  if (wrap)
    return lo + ((v - lo) % size + size) % size;
  return v < lo ? lo : lo + size - 1;
}

/*
 * In place: the source pixels of the tile with its border, each the
 * middle pixel of its block of the input. The edges are repeated (or
 * wrapped around) block by block, not pixel by pixel: beyond the edge
 * comes the middle of the edge block. The blocks are read in runs of
 * neighbours, so a tile inside the image takes one read, and one at an
 * edge a few.
 */
static void
gather_blocks (const Geometry *g, GeglBuffer *source, const Babl *format,
               const GeglRectangle *tile, gboolean wrap, gint pw, gint ph,
               guint32 *pix)
{
  const gint m = PAS_MARGIN, n = g->n;
  gint *sx = g_new (gint, pw), *sy = g_new (gint, ph);
  gint x, y, x0, y0;

  for (x = 0; x < pw; x++)
    sx[x] = source_index (tile->x - m + x, g->src.x, g->src.width, wrap);
  for (y = 0; y < ph; y++)
    sy[y] = source_index (tile->y - m + y, g->src.y, g->src.height, wrap);

  for (y0 = 0; y0 < ph; )
    {
      gint y1 = y0 + 1;

      while (y1 < ph && sy[y1] == sy[y1 - 1] + 1)
        y1++;
      for (x0 = 0; x0 < pw; )
        {
          gint x1 = x0 + 1;
          GeglRectangle r;
          guint8 *b;

          while (x1 < pw && sx[x1] == sx[x1 - 1] + 1)
            x1++;
          /* the blocks sx[x0] to sx[x1 - 1], sy[y0] to sy[y1 - 1]; the
           * middle of a last, partial block may lie beyond the input, and
           * then its nearest pixel is read (the abyss is the input) */
          r.x = g->bx + sx[x0] * n;
          r.y = g->by + sy[y0] * n;
          r.width = (x1 - x0) * n;
          r.height = (y1 - y0) * n;
          b = g_malloc ((gsize) r.width * r.height * 4);
          gegl_buffer_get (source, &r, 1.0, format, b, r.width * 4,
                           GEGL_ABYSS_CLAMP);
          for (y = y0; y < y1; y++)
            for (x = x0; x < x1; x++)
              pix[(gsize) y * pw + x] =
                pack (b + 4 * ((gsize) ((y - y0) * n + g->c) * r.width +
                               (x - x0) * n + g->c));
          g_free (b);
          x0 = x1;
        }
      y0 = y1;
    }
  g_free (sx);
  g_free (sy);
}

static gboolean
process (GeglOperation       *operation,
         GeglBuffer          *input,
         GeglBuffer          *output,
         const GeglRectangle *roi,
         gint                 level)
{
  GeglProperties *o = GEGL_PROPERTIES (operation);
  const Babl *format = gegl_operation_get_format (operation, "output");
  const gint m = PAS_MARGIN;
  const gboolean yuv_needed =
    o->algorithm == PAS_ALGORITHM_HQX || o->algorithm == PAS_ALGORITHM_XBR;
  GeglRectangle tile, fetch, full;
  GeglBuffer *source;
  GeglAbyssPolicy abyss;
  Geometry g;
  PasSource src;
  PasDest dst;
  guint8 *bytes = NULL;
  guint32 *pix = NULL, *key = NULL, *yuv = NULL, *out = NULL;
  gsize count, out_count, fetch_count, i;
  gint n, pw, ph, ow;
  gboolean ok = TRUE;

  /* (mipmap levels are not used: the operation does not ask for them) */
  (void) level;
  if (roi->width <= 0 || roi->height <= 0 || !geometry (operation, &g))
    return TRUE;
  n = g.n;

  tile = source_tile (&g, roi);
  pw = tile.width + 2 * m;
  ph = tile.height + 2 * m;
  count = (gsize) pw * (gsize) ph;
  out_count = (gsize) tile.width * n * (gsize) tile.height * n;
  ow = tile.width * n;

  /* the tile with its border, the edges repeated or wrapped around: from
   * a sub-buffer of the input's bounding box, so that the abyss is that
   * box (for an infinite input, from the input itself); in place, the
   * middle pixels of those blocks (gather_blocks) */
  fetch.x = tile.x - m;
  fetch.y = tile.y - m;
  fetch.width = pw;
  fetch.height = ph;
  fetch_count = PAS_IN_PLACE ? 0 : (gsize) fetch.width * (gsize) fetch.height;
  if (g.infinite)
    {
      source = g_object_ref (input);
      abyss = GEGL_ABYSS_NONE;
    }
  else
    {
      full = *gegl_operation_source_get_bounding_box (operation, "input");
      source = gegl_buffer_create_sub_buffer (input, &full);
      abyss = o->edges == PAS_EDGES_WRAP ? GEGL_ABYSS_LOOP : GEGL_ABYSS_CLAMP;
    }

  bytes = g_try_malloc (MAX (fetch_count, out_count) * 4);
  pix = g_try_malloc (count * sizeof (guint32));
  key = g_try_malloc (count * sizeof (guint32));
  out = g_try_malloc (out_count * sizeof (guint32));
  if (yuv_needed)
    yuv = g_try_malloc (count * sizeof (guint32));
  if (!bytes || !pix || !key || !out || (yuv_needed && !yuv))
    {
      g_warning ("%s: not enough memory for a piece of %d x %d pixels",
                 gegl_operation_get_name (operation), roi->width, roi->height);
      ok = FALSE;
      goto done;
    }

  src.alpha = o->alpha == PAS_ALPHA_MODE_STRAIGHT ? PAS_ALPHA_STRAIGHT
                                                  : PAS_ALPHA_WEIGHTED;
  if (PAS_IN_PLACE && !g.infinite)
    gather_blocks (&g, source, format, &tile, o->edges == PAS_EDGES_WRAP,
                   pw, ph, pix);
  else
    {
      gegl_buffer_get (source, &fetch, 1.0, format, bytes, fetch.width * 4, abyss);
      for (i = 0; i < count; i++)
        pix[i] = pack (bytes + 4 * i);
    }

  for (i = 0; i < count; i++)
    key[i] = src.alpha == PAS_ALPHA_WEIGHTED ? pas_premultiply (pix[i]) : pix[i];
  if (yuv)
    {
      uint32_t (*to_yuv) (uint32_t) =
        o->algorithm == PAS_ALGORITHM_HQX ? pas_hqx_yuv : pas_xbr_yuv;

      for (i = 0; i < count; i++)
        yuv[i] = (i > 0 && key[i] == key[i - 1]) ? yuv[i - 1] : to_yuv (key[i]);
    }

  src.pix = pix + (gsize) m * pw + m;
  src.key = key + (gsize) m * pw + m;
  src.yuv = yuv ? yuv + (gsize) m * pw + m : NULL;
  src.stride = pw;
  src.width = tile.width;
  src.height = tile.height;
  src.x0 = g.src.x - tile.x;
  src.y0 = g.src.y - tile.y;
  src.x1 = g.src.x + g.src.width - tile.x;
  src.y1 = g.src.y + g.src.height - tile.y;
  src.wrap = o->edges == PAS_EDGES_WRAP;
  if (g.infinite)
    {
      src.x0 = src.y0 = G_MININT / 4;
      src.x1 = src.y1 = G_MAXINT / 4;
    }
  dst.pix = out;
  dst.stride = ow;

  switch (o->algorithm)
    {
    case PAS_ALGORITHM_XBR:
      pas_xbr (&src, n, &dst);
      break;
    case PAS_ALGORITHM_SCALENX:
      pas_scalenx (&src, n, &dst);
      break;
    case PAS_ALGORITHM_NEAREST:
      pas_nearest (&src, n, &dst);
      break;
    default:
      pas_hqx (&src, n, &dst);
      break;
    }

  for (i = 0; i < out_count; i++)
    unpack (out[i], bytes + 4 * i);

  /* the output covers whole blocks; write the part in roi */
  {
    const gint dx = roi->x - (g.ox + tile.x * n), dy = roi->y - (g.oy + tile.y * n);

    gegl_buffer_set (output, roi, 0, format,
                     bytes + ((gsize) dy * ow + dx) * 4, ow * 4);
  }

done:
  g_free (bytes);
  g_free (pix);
  g_free (key);
  g_free (yuv);
  g_free (out);
  g_object_unref (source);
  return ok;
}

static void
pas_class_init (GeglOperationClass *operation_class)
{
  GeglOperationFilterClass *filter_class = GEGL_OPERATION_FILTER_CLASS (operation_class);

  operation_class->prepare                   = prepare;
  operation_class->get_bounding_box          = get_bounding_box;
  operation_class->get_required_for_output   = get_required_for_output;
  operation_class->get_invalidated_by_change = get_invalidated_by_change;
  operation_class->threaded                  = TRUE;
  operation_class->opencl_support            = FALSE;
  filter_class->process                      = process;
}
