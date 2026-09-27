/*
 * Pixel Art Scale: scales pixel art up with hqx, xBR, Scale2x/3x/4x or
 * nearest neighbour, a GEGL operation
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
 * pixel-art:scale makes the image 2, 3 or 4 times larger: its output is
 * the input's bounding box times the factor. For GEGL graphs and the gegl
 * command line. GIMP applies filters within the layer, so there it is
 * pixel-art:rescale (pixel-art-rescale.c) that does the work, on an image
 * already scaled up with interpolation None. See pas-gegl.c.
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

#define PAS_IN_PLACE 0
#include "pas-gegl.c"

static void
gegl_op_class_init (GeglOpClass *klass)
{
  GeglOperationClass *operation_class = GEGL_OPERATION_CLASS (klass);

  pas_class_init (operation_class);
  /* "transform": GIMP hides it from its filters, which cannot grow the
   * canvas; there pixel-art:rescale is the one to use */
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
