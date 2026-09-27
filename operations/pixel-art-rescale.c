/*
 * Pixel Art Rescale: redraws pixel art that was scaled up with nearest
 * neighbour, with hqx, xBR or Scale2x/3x/4x, a GEGL operation
 *
 * pixel-art-rescale.c
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
 * pixel-art:rescale takes an image that was scaled up 2, 3 or 4 times with
 * nearest neighbour (GIMP: Image > Scale Image, interpolation None, or
 * Image > Scale Pixel Art..., the plug-in of this repository, which does
 * both steps), and redraws it as pixel-art:scale would have scaled the
 * original. The size stays the same, so GIMP can apply it as a filter,
 * non-destructively too: Filters > Enhance > Pixel Art Rescale... See
 * pas-gegl.c.
 */

#include <glib/gi18n-lib.h>

#ifdef GEGL_PROPERTIES

enum_start (pas_rescale_algorithm)
  enum_value (PAS_ALGORITHM_HQX,     "hqx",     N_("hqx (hq2x, hq3x, hq4x)"))
  enum_value (PAS_ALGORITHM_XBR,     "xbr",     N_("xBR"))
  enum_value (PAS_ALGORITHM_SCALENX, "scalenx", N_("Scale2x, Scale3x, Scale4x (EPX)"))
  enum_value (PAS_ALGORITHM_NEAREST, "nearest", N_("Nearest neighbour"))
enum_end (PasRescaleAlgorithm)

enum_start (pas_rescale_alpha_mode)
  enum_value (PAS_ALPHA_MODE_WEIGHTED, "weighted", N_("Weighted by alpha"))
  enum_value (PAS_ALPHA_MODE_STRAIGHT, "straight", N_("Like a color (as the reference code)"))
enum_end (PasRescaleAlphaMode)

enum_start (pas_rescale_edges)
  enum_value (PAS_EDGES_CLAMP, "clamp", N_("Repeat the edge pixels"))
  enum_value (PAS_EDGES_WRAP,  "wrap",  N_("Wrap around (for tiles)"))
enum_end (PasRescaleEdges)

property_enum (algorithm, _("Algorithm"),
               PasRescaleAlgorithm, pas_rescale_algorithm, PAS_ALGORITHM_HQX)
  description (_("hqx (Maxim Stepin) and xBR (Hyllian) smooth edges with "
                 "blends; Scale2x/3x/4x (EPX, AdvMAME) rounds corners and "
                 "diagonals using only the image's own colors; nearest "
                 "neighbour makes each pixel a square block"))

property_int (factor, _("Factor"), 2)
  description (_("How many times the image was scaled up with nearest "
                 "neighbour (interpolation None): 2, 3 or 4. The operation "
                 "reads the middle pixel of each factor x factor block."))
  value_range (2, 4)

property_enum (alpha, _("Alpha"),
               PasRescaleAlphaMode, pas_rescale_alpha_mode, PAS_ALPHA_MODE_WEIGHTED)
  description (_("How transparency enters. Weighted by alpha: colors are "
                 "compared as they look (all fully transparent pixels are "
                 "the same) and alpha differences count, and blends are "
                 "weighted by alpha, so hidden colors do not bleed. Like a "
                 "color: the reference implementations' way, alpha is not "
                 "compared and is blended like a color. Opaque images give "
                 "the same result both ways."))

property_enum (edges, _("Edges"),
               PasRescaleEdges, pas_rescale_edges, PAS_EDGES_CLAMP)
  description (_("What lies beyond the edges of the image: its edge pixels "
                 "repeated, or the other side of the image, for tiles that "
                 "repeat seamlessly"))


#else

#define GEGL_OP_FILTER
#define GEGL_OP_NAME     pixel_art_rescale
#define GEGL_OP_C_SOURCE pixel-art-rescale.c

#include "gegl-op.h"

#define PAS_IN_PLACE 1
#include "pas-gegl.c"

static void
gegl_op_class_init (GeglOpClass *klass)
{
  GeglOperationClass *operation_class = GEGL_OPERATION_CLASS (klass);

  pas_class_init (operation_class);
  gegl_operation_class_set_keys (operation_class,
    "name",            "pixel-art:rescale",
    "title",           _("Pixel Art Rescale"),
    "categories",      "enhance",
    "license",         "GPL3+",
    "description",     _("Redraws pixel art that was scaled up 2, 3 or 4 "
                         "times with nearest neighbour (interpolation None) "
                         "with hqx, xBR or Scale2x/3x/4x (EPX), keeping the "
                         "size"),
    "gimp:menu-path",  "<Image>/Filters/Enhance",
    "gimp:menu-label", _("Pixel Art Rescale..."),
    NULL);
}

#endif
