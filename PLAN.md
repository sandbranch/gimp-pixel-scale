# Plan

## Done

- `pixel-art:scale` (grows the output) and `pixel-art:rescale` (same size,
  on an image scaled up with interpolation None), with hqx, xBR,
  Scale2x/3x/4x and nearest neighbour at 2x, 3x and 4x, alpha weighted or
  straight, edges repeated or wrapped (README).
- The GIMP plug-in, Image > Scale Pixel Art..., and the filter, Filters >
  Enhance > Pixel Art Rescale..., non-destructive too.
- Tests: tests/check.sh (numerical checks, also under ASan and UBSan),
  tests/refcheck.sh (byte for byte against the hqx library, scale2x 4.0
  and FFmpeg 8.0.1), tests/gimp-check.sh (GIMP 3.2.6 without a window,
  against the gegl command line), tests/gui/gui-test.sh (the real dialogs
  on Broadway), tests/bench.sh.

### Why two operations

The first probe (GIMP 3.2.6) applied the growing operation as a filter
on a 28 x 24 layer with factor 3. Non-destructive, the layer and the
canvas stayed 28 x 24 while the filter drew 84 x 72 beyond them, and the
filter and its settings came back from the XCF. Merged (`merge_filters`),
the layer became 84 x 72 but the canvas stayed 28 x 24. So in GIMP a
growing filter needs Image > Fit Canvas to Layers after merging, and it
cannot be non-destructive in a useful way. GIMP's own scaling ops are not
filters for the same reason. Hence:

- `pixel-art:rescale` keeps the size and reads the middle pixel of each
  block of an image that GIMP scaled up with interpolation None. That
  scaling is exact at integer factors (tests/gimp-check.sh checks it).
- The plug-in does both steps, so that Image > Scale Image scales
  everything else in the image (masks, channels, paths, guides) the
  usual way.
- `pixel-art:scale` is in the "transform" category, which GIMP hides
  from its filters.

## Next

1. **xBRZ** (Zenju, C++; asked for in issue #14 of the GIMP 2 plug-in),
   a refinement of xBR. Its header says "distributed under GNU General
   Public License: https://www.gnu.org/licenses/gpl-3.0", without "or
   later", plus an exception for linking with some libraries; openSUSE
   packages 1.8 as GPL-3.0-only. It can be combined with this
   GPL-3.0-or-later code, and the combination is then GPL-3.0. Port it
   to C and test it byte for byte against its reference, as done here
   for the others.
2. **MMPX** (Morgan McGuire and Mara Gagiu), asked for in issue #17: a
   style-preserving 2x scaler. The header of its C version
   (github.com/ITotalJustice/mmpx, "from the C++ benchmark example" of
   the paper) says "Copyright 2020 Morgan McGuire & Mara Gagiu.
   Available under the MIT license." Its rules compare pixels for
   equality, as Scale2x does; whether it never makes new colors is to
   be checked on the code.
3. Factors above 4: 6x and 8x as combinations (Scale2x of Scale3x,
   hq2x of hq4x...) as G'MIC does. Scale2x3/2x4 (non-square) if asked.
4. Indexed images in the plug-in: Scale2x/3x/4x and nearest make no new
   colors, so they could keep the palette. Now the plug-in is for RGB and
   gray images only.
5. Upstream:
   - offer the operations to GEGL, in `operations/common-gpl3+`, where
     GPL-3+ operations such as `gegl:antialias` (Scale3x smoothing) live,
     with reference compositions for GEGL's tests;
   - tell bbbbbr (issue #8 of the GIMP 2 plug-in) that this exists, and
     ask whether he wants it as the GIMP 3 version of his plug-in.

## Found along the way

- GEGL 0.4.72: setting `application-license` after `gegl_init` loads the
  operation modules again. GLib then reports "Two different plugins
  tried to register" for every operation. This was observed in the tests
  and fixed by setting it before `gegl_init`; the GEGL source was not
  read for it.
- A GPL3+ operation (its `license` key) is hidden from programs that do
  not declare themselves GPL3 (`gegl_has_operation` is false). The
  `gegl` command line and GIMP do declare it; a test program must too.
- FFmpeg's hqx (`vf_hqx.c`) builds its YUV table with integer arithmetic,
  and the hqx library with doubles. The two tables differ for 10 426 of
  2^24 colors, so the two "hqx" give different results for some colors
  (9 to 35 pixels in the test images).
- FFmpeg's xBR (`vf_xbr.c`) halves both colors before adding them for its
  50% blend (`ALPHA_BLEND_128_W`), so that blend rounds down by up to one
  more than (a + b) / 2 would. Kept here, for byte-exact results; alpha
  is blended with the plain rounded-down mean.
- GIMP 3.2.6 on Broadway (for gimp-devtools): closed menus keep
  their canvases in the page, displayed, above the dialogs in z-order.
  Only a canvas that `document.elementFromPoint` returns at its own middle
  is really visible. And a click lands in a new window only after the
  pointer has moved into it.
