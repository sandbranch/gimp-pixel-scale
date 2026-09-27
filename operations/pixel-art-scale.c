/*
 * Pixel Art Scale: hqx, xBR, Scale2x/3x/4x and nearest neighbour, as a
 * GEGL operation
 *
 * pixel-art-scale.c
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
 * Scales an image up by a whole factor (2, 3 or 4) with one of the
 * classic pixel art scalers. The output is the input's bounding box times
 * the factor: input pixel (x, y) becomes the block of output pixels from
 * (n x, n y) to (n x + n - 1, n y + n - 1).
 *
 * The scalers (scalenx.c, hqx.c, xbr.c) work on 8-bit values, as they
 * were designed: an 8-bit image is scaled in its own encoding (perceptual
 * or linear, so that no value changes on the way in or out), other images
 * as R'G'B'A u8 in their color space. They read the 3 x 3 or 5 x 5 pixels
 * around each pixel; at the edges of the image those are the edge pixels
 * repeated (as in the reference implementations) or, for tiles, the
 * pixels of the other side (edges = wrap).
 *
 * The operation renders any part of the output on its own, so GEGL can
 * split the work between threads: for a piece of the output it reads the
 * input pixels under it, with a border of PAS_MARGIN pixels.
 */

#include <glib/gi18n-lib.h>

#ifdef GEGL_PROPERTIES

enum_start (pas_algorithm)
  enum_value (PAS_ALGORITHM_HQX,     "hqx",     N_("hqx (hq2x, hq3x, hq4x)"))
  enum_value (PAS_ALGORITHM_XBR,     "xbr",     N_("xBR"))
  enum_value (PAS_ALGORITHM_SCALENX, "scalenx", N_("Scale2x, Scale3x, Scale4x (EPX)"))
  enum_value (PAS_ALGORITHM_NEAREST, "nearest", N_("Nearest neighbour"))
enum_end (PasAlgorithm)

enum_start (pas_alpha_mode)
  enum_value (PAS_ALPHA_MODE_WEIGHTED, "weighted", N_("Weighted by alpha"))
  enum_value (PAS_ALPHA_MODE_STRAIGHT, "straight", N_("Like a color (as the reference code)"))
enum_end (PasAlphaMode)

enum_start (pas_edges)
  enum_value (PAS_EDGES_CLAMP, "clamp", N_("Repeat the edge pixels"))
  enum_value (PAS_EDGES_WRAP,  "wrap",  N_("Wrap around (for tiles)"))
enum_end (PasEdges)

property_enum (algorithm, _("Algorithm"),
               PasAlgorithm, pas_algorithm, PAS_ALGORITHM_HQX)
  description (_("hqx (Maxim Stepin) and xBR (Hyllian) smooth edges with "
                 "blends; Scale2x/3x/4x (EPX, AdvMAME) rounds corners and "
                 "diagonals using only the image's own colors; nearest "
                 "neighbour makes each pixel a square block"))

property_int (factor, _("Factor"), 2)
  description (_("How many times larger: 2, 3 or 4"))
  value_range (2, 4)

property_enum (alpha, _("Alpha"),
               PasAlphaMode, pas_alpha_mode, PAS_ALPHA_MODE_WEIGHTED)
  description (_("How transparency enters. Weighted by alpha: colors are "
                 "compared as they look (all fully transparent pixels are "
                 "the same) and alpha differences count, and blends are "
                 "weighted by alpha, so hidden colors do not bleed. Like a "
                 "color: the reference implementations' way, alpha is not "
                 "compared and is blended like a color. Opaque images give "
                 "the same result both ways."))

property_enum (edges, _("Edges"),
               PasEdges, pas_edges, PAS_EDGES_CLAMP)
  description (_("What lies beyond the edges of the image: its edge pixels "
                 "repeated, or the other side of the image, for tiles that "
                 "repeat seamlessly"))

#else

#define GEGL_OP_FILTER
#define GEGL_OP_NAME     pixel_art_scale
#define GEGL_OP_C_SOURCE pixel-art-scale.c

#include "gegl-op.h"

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
  GeglProperties *o = GEGL_PROPERTIES (operation);
  const GeglRectangle *in =
    gegl_operation_source_get_bounding_box (operation, "input");
  const gint n = factor_of (o);
  GeglRectangle result = { 0, 0, 0, 0 };

  if (!in || in->width <= 0 || in->height <= 0)
    return result;
  if (gegl_rectangle_is_infinite_plane (in))
    return *in;
  result.x = in->x * n;
  result.y = in->y * n;
  result.width = in->width * n;
  result.height = in->height * n;
  return result;
}

/* the input pixels under the output rectangle roi, with the border the
 * scalers read; when wrapping around and the border crosses an edge of
 * the image, the whole width (or height) of it, where the border comes
 * from (an infinite input has no edges) */
static GeglRectangle
input_for (GeglOperation *operation, const GeglRectangle *roi)
{
  GeglProperties *o = GEGL_PROPERTIES (operation);
  const GeglRectangle *in =
    gegl_operation_source_get_bounding_box (operation, "input");
  const gint n = factor_of (o);
  const gint m = PAS_MARGIN;
  GeglRectangle r;
  gint x0, y0, x1, y1;

  if (roi->width <= 0 || roi->height <= 0)
    {
      GeglRectangle empty = { 0, 0, 0, 0 };
      return empty;
    }
  x0 = floor_div (roi->x, n) - m;
  y0 = floor_div (roi->y, n) - m;
  x1 = floor_div (roi->x + roi->width - 1, n) + 1 + m;
  y1 = floor_div (roi->y + roi->height - 1, n) + 1 + m;

  if (in && o->edges == PAS_EDGES_WRAP && in->width > 0 && in->height > 0 &&
      !gegl_rectangle_is_infinite_plane (in))
    {
      if (x0 < in->x || x1 > in->x + in->width)
        {
          x0 = in->x;
          x1 = in->x + in->width;
        }
      if (y0 < in->y || y1 > in->y + in->height)
        {
          y0 = in->y;
          y1 = in->y + in->height;
        }
    }
  r.x = x0;
  r.y = y0;
  r.width = x1 - x0;
  r.height = y1 - y0;
  return r;
}

static GeglRectangle
get_required_for_output (GeglOperation       *operation,
                         const gchar         *input_pad,
                         const GeglRectangle *roi)
{
  (void) input_pad;
  return input_for (operation, roi);
}

static GeglRectangle
get_invalidated_by_change (GeglOperation       *operation,
                           const gchar         *input_pad,
                           const GeglRectangle *input_region)
{
  GeglProperties *o = GEGL_PROPERTIES (operation);
  const GeglRectangle *in =
    gegl_operation_source_get_bounding_box (operation, "input");
  const gint n = factor_of (o);
  gint x0 = input_region->x - PAS_MARGIN, y0 = input_region->y - PAS_MARGIN;
  gint x1 = input_region->x + input_region->width + PAS_MARGIN;
  gint y1 = input_region->y + input_region->height + PAS_MARGIN;
  GeglRectangle r;

  (void) input_pad;
  if (in && gegl_rectangle_is_infinite_plane (in))
    return *in;
  if (in && o->edges == PAS_EDGES_WRAP)
    {
      /* a change near an edge shows at the other edge too */
      if (x0 < in->x || x1 > in->x + in->width)
        {
          x0 = in->x;
          x1 = in->x + in->width;
        }
      if (y0 < in->y || y1 > in->y + in->height)
        {
          y0 = in->y;
          y1 = in->y + in->height;
        }
    }
  r.x = x0 * n;
  r.y = y0 * n;
  r.width = (x1 - x0) * n;
  r.height = (y1 - y0) * n;
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

static gboolean
process (GeglOperation       *operation,
         GeglBuffer          *input,
         GeglBuffer          *output,
         const GeglRectangle *roi,
         gint                 level)
{
  GeglProperties *o = GEGL_PROPERTIES (operation);
  const Babl *format = gegl_operation_get_format (operation, "output");
  const GeglRectangle *in =
    gegl_operation_source_get_bounding_box (operation, "input");
  const gint n = factor_of (o);
  const gint m = PAS_MARGIN;
  GeglRectangle tile, fetch;
  GeglBuffer *source;
  GeglAbyssPolicy abyss;
  PasSource src;
  PasDest dst;
  guint8 *bytes;
  guint32 *pix, *key, *yuv = NULL, *out;
  gsize count, out_count, i;
  gint pw, ph, ow;
  gboolean ok = TRUE;

  /* (mipmap levels are not used: the operation does not ask for them) */
  (void) level;
  if (roi->width <= 0 || roi->height <= 0 || !in ||
      in->width <= 0 || in->height <= 0)
    return TRUE;

  /* the input pixels under roi */
  tile.x = floor_div (roi->x, n);
  tile.y = floor_div (roi->y, n);
  tile.width = floor_div (roi->x + roi->width - 1, n) + 1 - tile.x;
  tile.height = floor_div (roi->y + roi->height - 1, n) + 1 - tile.y;

  pw = tile.width + 2 * m;
  ph = tile.height + 2 * m;
  count = (gsize) pw * (gsize) ph;
  out_count = (gsize) tile.width * n * (gsize) tile.height * n;
  ow = tile.width * n;

  /* the tile with its border, the edges repeated or wrapped around: from
   * a sub-buffer of the input's bounding box, so that the abyss is that
   * box (for an infinite input, from the input itself) */
  fetch.x = tile.x - m;
  fetch.y = tile.y - m;
  fetch.width = pw;
  fetch.height = ph;
  if (gegl_rectangle_is_infinite_plane (in))
    {
      source = g_object_ref (input);
      abyss = GEGL_ABYSS_NONE;
    }
  else
    {
      source = gegl_buffer_create_sub_buffer (input, in);
      abyss = o->edges == PAS_EDGES_WRAP ? GEGL_ABYSS_LOOP : GEGL_ABYSS_CLAMP;
    }

  bytes = g_try_malloc (MAX (count, out_count) * 4);
  pix = g_try_malloc (count * sizeof (guint32));
  key = g_try_malloc (count * sizeof (guint32));
  out = g_try_malloc (out_count * sizeof (guint32));
  if (o->algorithm == PAS_ALGORITHM_HQX || o->algorithm == PAS_ALGORITHM_XBR)
    yuv = g_try_malloc (count * sizeof (guint32));
  if (!bytes || !pix || !key || !out ||
      ((o->algorithm == PAS_ALGORITHM_HQX || o->algorithm == PAS_ALGORITHM_XBR) && !yuv))
    {
      g_warning ("pixel-art:scale: not enough memory for a piece of %d x %d pixels",
                 roi->width, roi->height);
      ok = FALSE;
      goto done;
    }

  gegl_buffer_get (source, &fetch, 1.0, format, bytes,
                   pw * 4, abyss);

  src.alpha = o->alpha == PAS_ALPHA_MODE_STRAIGHT ? PAS_ALPHA_STRAIGHT
                                                  : PAS_ALPHA_WEIGHTED;
  for (i = 0; i < count; i++)
    {
      pix[i] = pack (bytes + 4 * i);
      key[i] = src.alpha == PAS_ALPHA_WEIGHTED ? pas_premultiply (pix[i])
                                               : pix[i];
    }
  if (yuv)
    {
      uint32_t (*to_yuv) (uint32_t) =
        o->algorithm == PAS_ALGORITHM_HQX ? pas_hqx_yuv : pas_xbr_yuv;

      for (i = 0; i < count; i++)
        yuv[i] = (i > 0 && key[i] == key[i - 1]) ? yuv[i - 1]
                                                 : to_yuv (key[i]);
    }

  src.pix = pix + (gsize) m * pw + m;
  src.key = key + (gsize) m * pw + m;
  src.yuv = yuv ? yuv + (gsize) m * pw + m : NULL;
  src.stride = pw;
  src.width = tile.width;
  src.height = tile.height;
  src.x0 = in->x - tile.x;
  src.y0 = in->y - tile.y;
  src.x1 = in->x + in->width - tile.x;
  src.y1 = in->y + in->height - tile.y;
  src.wrap = o->edges == PAS_EDGES_WRAP;
  if (gegl_rectangle_is_infinite_plane (in))
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
    const gint dx = roi->x - tile.x * n, dy = roi->y - tile.y * n;

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
gegl_op_class_init (GeglOpClass *klass)
{
  GeglOperationClass       *operation_class = GEGL_OPERATION_CLASS (klass);
  GeglOperationFilterClass *filter_class    = GEGL_OPERATION_FILTER_CLASS (klass);

  operation_class->prepare                   = prepare;
  operation_class->get_bounding_box          = get_bounding_box;
  operation_class->get_required_for_output   = get_required_for_output;
  operation_class->get_invalidated_by_change = get_invalidated_by_change;
  operation_class->threaded                  = TRUE;
  operation_class->opencl_support            = FALSE;
  filter_class->process                      = process;

  gegl_operation_class_set_keys (operation_class,
    "name",            "pixel-art:scale",
    "title",           _("Pixel Art Scale"),
    "categories",      "transform",
    "license",         "GPL3+",
    "description",     _("Scales pixel art up 2, 3 or 4 times with hqx, "
                         "xBR, Scale2x/3x/4x (EPX) or nearest neighbour"),
    NULL);
}

#endif
