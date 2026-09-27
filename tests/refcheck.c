/*
 * Compares pixel-art:scale, run through GEGL, byte for byte with the
 * reference implementations, built from their sources (tests/refcheck.sh,
 * after tests/get-refs.sh):
 *
 *   - hqx: Cameron Zemek's hqx library (grom358/hqx), and FFmpeg's vf_hqx.c
 *   - xBR: FFmpeg's vf_xbr.c
 *   - Scale2x, Scale3x, Scale4x: Andrea Mazzoleni's scale2x 4.0 library
 *     (scalebit.c), and FFmpeg's vf_epx.c (2x and 3x)
 *
 * on generated images: a sprite, checkerboards, diagonals, text-like
 * glyphs, gradients with alpha, noise in few colors and in all colors,
 * colors near the hqx thresholds, and small sizes down to 1 x 1. Also
 * compares the YUV conversions of the operation with the tables of the
 * references for all 2^24 colors.
 *
 *   refcheck <pixel-art-scale.so>
 *
 * Prints PASS or FAIL for each comparison and INFO for differences
 * between the references themselves; exits with 1 if anything failed.
 *
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <math.h>

#include "common.h"

#include "hqx.h"
#include "scalebit.h"
#include "pas.h"

/* from the hqx library (init.c) */
extern uint32_t RGBtoYUV[16777216];

void ff_ref_hqx (int n, const uint32_t *src, int w, int h, uint32_t *dst,
                 const uint32_t **table);
void ff_ref_xbr (int n, const uint32_t *src, int w, int h, uint32_t *dst,
                 const uint32_t **table);
void ff_ref_epx (int n, const uint32_t *src, int w, int h, uint32_t *dst);

typedef struct
{
  const char *name;
  Img         im;
  gboolean    opaque;
  gboolean    canonical; /* every fully transparent pixel is 0x00000000 */
} Case;

static Case cases[64];
static int n_cases;

static void
add (const char *name, Img im)
{
  Case *c = &cases[n_cases++];
  gsize i;

  c->name = name;
  c->im = im;
  c->opaque = TRUE;
  c->canonical = TRUE;
  for (i = 0; i < (gsize) im.w * im.h; i++)
    {
      if ((im.px[i] >> 24) != 255)
        c->opaque = FALSE;
      if (!(im.px[i] >> 24) && im.px[i])
        c->canonical = FALSE;
    }
}

/* one line of results: how many images and pixels, and the first
 * difference */
typedef struct
{
  int  images;
  long pixels, differ;
  char first[200];
} Tally;

static void
tally (Tally *t, const Case *c, int n, const Img *ours, const Img *ref,
       guint32 mask)
{
  long d = img_count_diff (ours, ref, mask);

  t->images++;
  t->pixels += (long) ours->w * ours->h;
  if (d != 0)
    {
      if (!t->differ && d > 0)
        {
          gsize i;

          for (i = 0; (ours->px[i] & mask) == (ref->px[i] & mask); i++)
            ;
          g_snprintf (t->first, sizeof t->first,
                      "first at %s x%d (%d, %d): ours %08x, reference %08x",
                      c->name, n, (int) (i % ours->w), (int) (i / ours->w),
                      ours->px[i], ref->px[i]);
        }
      else if (d < 0 && !t->differ)
        g_snprintf (t->first, sizeof t->first, "%s x%d: other size", c->name, n);
      t->differ += d > 0 ? d : 1;
    }
}

static void
tally_report (const Tally *t, const char *what)
{
  if (t->differ)
    report (FALSE, "%s: %ld of %ld pixels differ; %s", what, t->differ,
            t->pixels, t->first);
  else
    report (t->images > 0, "%s: byte-exact, %d images, %ld pixels",
            what, t->images, t->pixels);
}

static void
check_yuv (void)
{
  const uint32_t *ff_hqx, *ff_xbr;
  long bad_hqx = 0, bad_xbr = 0, ff_vs_lib = 0;
  guint32 c;

  hqxInit ();
  ff_ref_hqx (2, NULL, 0, 0, NULL, &ff_hqx);
  for (c = 0; c < (1u << 24); c++)
    {
      bad_hqx += pas_hqx_yuv (c) != RGBtoYUV[c];
      ff_vs_lib += ff_hqx[c] != RGBtoYUV[c];
    }
  report (bad_hqx == 0, "hqx YUV: the operation's = the hqx library's table "
          "for all 2^24 colors (%ld differ)", bad_hqx);
  printf ("INFO  hqx YUV: FFmpeg's vf_hqx.c table differs from the hqx "
          "library's for %ld of 2^24 colors\n", ff_vs_lib);

  ff_ref_xbr (2, NULL, 0, 0, NULL, &ff_xbr);
  for (c = 0; c < (1u << 24); c++)
    bad_xbr += pas_xbr_yuv (c) != ff_xbr[c];
  report (bad_xbr == 0, "xBR YUV: the operation's = FFmpeg's vf_xbr.c table "
          "for all 2^24 colors (%ld differ)", bad_xbr);
}

static void
check_hqx (void)
{
  static const char *const modes[] = { "straight", "weighted" };
  int n, m, i;

  for (n = 2; n <= 4; n++)
    {
      Tally lib[2] = { { 0 } }, ffv = { 0 }, ff_ours = { 0 };

      for (i = 0; i < n_cases; i++)
        {
          const Case *c = &cases[i];
          Img ref = img_new (c->im.w * n, c->im.h * n);
          Img ffr = img_new (c->im.w * n, c->im.h * n);

          if (n == 2)
            hq2x_32 (c->im.px, ref.px, c->im.w, c->im.h);
          else if (n == 3)
            hq3x_32 (c->im.px, ref.px, c->im.w, c->im.h);
          else
            hq4x_32 (c->im.px, ref.px, c->im.w, c->im.h);
          ff_ref_hqx (n, c->im.px, c->im.w, c->im.h, ffr.px, NULL);

          for (m = 0; m < 2; m++)
            {
              Img ours;

              if (m == 1 && !c->opaque)
                continue;
              ours = run_op (&c->im, "hqx", n, modes[m], "clamp");
              tally (&lib[m], c, n, &ours, &ref, 0xffffffff);
              if (m == 0)
                tally (&ff_ours, c, n, &ours, &ffr, 0xffffffff);
              img_free (&ours);
            }
          tally (&ffv, c, n, &ffr, &ref, 0xffffffff);
          img_free (&ref);
          img_free (&ffr);
        }
      {
        gchar *s = g_strdup_printf ("hq%dx, alpha straight, vs the hqx library "
                                    "(all images, all 4 channels)", n);
        tally_report (&lib[0], s);
        g_free (s);
        s = g_strdup_printf ("hq%dx, alpha weighted, vs the hqx library "
                             "(opaque images)", n);
        tally_report (&lib[1], s);
        g_free (s);
      }
      printf ("INFO  hq%dx: FFmpeg's vf_hqx.c differs from the hqx library in "
              "%ld of %ld pixels (%s)\n", n, ffv.differ, ffv.pixels,
              ffv.differ ? ffv.first : "none");
      printf ("INFO  hq%dx: the operation differs from FFmpeg's vf_hqx.c in "
              "%ld of %ld pixels\n", n, ff_ours.differ, ff_ours.pixels);
    }
}

static void
check_xbr (void)
{
  int n, i;

  for (n = 2; n <= 4; n++)
    {
      Tally straight = { 0 }, weighted = { 0 };
      long not_opaque = 0;

      for (i = 0; i < n_cases; i++)
        {
          const Case *c = &cases[i];
          Img ref = img_new (c->im.w * n, c->im.h * n);
          Img ours;

          ff_ref_xbr (n, c->im.px, c->im.w, c->im.h, ref.px, NULL);
          /* FFmpeg's format is 0RGB: compare the colors */
          ours = run_op (&c->im, "xbr", n, "straight", "clamp");
          tally (&straight, c, n, &ours, &ref, 0x00ffffff);
          img_free (&ours);
          if (c->opaque)
            {
              gsize k;

              ours = run_op (&c->im, "xbr", n, "weighted", "clamp");
              tally (&weighted, c, n, &ours, &ref, 0x00ffffff);
              for (k = 0; k < (gsize) ours.w * ours.h; k++)
                not_opaque += (ours.px[k] >> 24) != 255;
              img_free (&ours);
            }
          img_free (&ref);
        }
      {
        gchar *s = g_strdup_printf ("xbr %dx, alpha straight, vs FFmpeg's "
                                    "vf_xbr.c (all images, R G B)", n);
        tally_report (&straight, s);
        g_free (s);
        s = g_strdup_printf ("xbr %dx, alpha weighted, vs FFmpeg's vf_xbr.c "
                             "(opaque images, R G B)", n);
        tally_report (&weighted, s);
        g_free (s);
      }
      report (not_opaque == 0, "xbr %dx: opaque images stay opaque "
              "(%ld pixels not)", n, not_opaque);
    }
}

static void
check_scalenx (void)
{
  static const char *const modes[] = { "straight", "weighted" };
  int n, m, i;

  for (n = 2; n <= 4; n++)
    {
      Tally lib[2] = { { 0 } }, ffe[2] = { { 0 } };
      int skipped = 0;

      for (i = 0; i < n_cases; i++)
        {
          const Case *c = &cases[i];
          Img ref = img_new (c->im.w * n, c->im.h * n);
          Img ffr = img_new (c->im.w * n, c->im.h * n);
          gboolean have_lib =
            scale_precondition (n, 4, c->im.w, c->im.h) == 0;

          if (have_lib)
            scale (n, ref.px, ref.w * 4, c->im.px, c->im.w * 4, 4,
                   c->im.w, c->im.h);
          else
            skipped++;
          if (n <= 3)
            ff_ref_epx (n, c->im.px, c->im.w, c->im.h, ffr.px);
          for (m = 0; m < 2; m++)
            {
              Img ours;

              /* weighted: the same when transparent pixels hide nothing */
              if (m == 1 && !c->canonical)
                continue;
              ours = run_op (&c->im, "scalenx", n, modes[m], "clamp");
              if (have_lib)
                tally (&lib[m], c, n, &ours, &ref, 0xffffffff);
              if (n <= 3)
                tally (&ffe[m], c, n, &ours, &ffr, 0xffffffff);
              img_free (&ours);
            }
          img_free (&ref);
          img_free (&ffr);
        }
      for (m = 0; m < 2; m++)
        {
          gchar *s = g_strdup_printf ("scale%dx, alpha %s, vs the scale2x "
                                      "library (%s; %d images below its "
                                      "minimum size skipped)", n, modes[m],
                                      m ? "images whose transparent pixels "
                                          "are all 0x00000000" : "all images",
                                      skipped);
          tally_report (&lib[m], s);
          g_free (s);
          if (n <= 3)
            {
              s = g_strdup_printf ("scale%dx, alpha %s, vs FFmpeg's vf_epx.c "
                                   "(%s)", n, modes[m],
                                   m ? "the same images" : "all images");
              tally_report (&ffe[m], s);
              g_free (s);
            }
        }
    }
}

int
main (int argc, char **argv)
{
  Img mush;
  int i;

  if (argc != 2)
    {
      fprintf (stderr, "usage: %s <pixel-art-scale.so>\n", argv[0]);
      return 2;
    }
  if (!load_module (argv[1], &argc, &argv))
    return 2;

  mush = img_mushroom ();
  add ("mushroom", mush);
  add ("mushroom on blue", img_mushroom_on (ARGB (255, 120, 170, 230)));
  add ("mushroom, hidden colors", img_hide_colors (&mush, 11));
  add ("checker 1", img_checker (16, 16, 1, ARGB (255, 0, 0, 0), ARGB (255, 255, 255, 255)));
  add ("checker 2", img_checker (17, 15, 2, ARGB (255, 200, 30, 40), ARGB (255, 20, 60, 200)));
  add ("checker 3", img_checker (19, 13, 3, ARGB (255, 250, 250, 250), ARGB (255, 10, 120, 10)));
  add ("checker alpha", img_checker (18, 14, 2, 0, ARGB (255, 90, 60, 30)));
  add ("diagonals", img_diagonals (40, 32, ARGB (255, 20, 24, 40), ARGB (255, 240, 220, 90)));
  add ("diagonals alpha", img_diagonals (33, 29, 0, ARGB (255, 240, 220, 90)));
  add ("text", img_text (FALSE));
  add ("text outline", img_text (TRUE));
  add ("gradient alpha", img_gradient_alpha (48, 40));
  add ("palette 1", img_palette_noise (37, 29, 1, 4, FALSE));
  add ("palette 2", img_palette_noise (37, 29, 2, 8, FALSE));
  add ("palette 3", img_palette_noise (41, 23, 3, 16, FALSE));
  add ("palette alpha 4", img_palette_noise (37, 29, 4, 4, TRUE));
  add ("palette alpha 5", img_palette_noise (29, 37, 5, 8, TRUE));
  add ("palette alpha 6", img_palette_noise (40, 40, 6, 12, TRUE));
  add ("noise 7", img_noise (33, 27, 7, FALSE));
  add ("noise 8", img_noise (24, 24, 8, FALSE));
  add ("noise alpha 9", img_noise (31, 23, 9, TRUE));
  add ("near colors 10", img_near_colors (40, 40, 10));
  add ("near colors 11", img_near_colors (23, 31, 11));
  add ("2x2", img_palette_noise (2, 2, 12, 3, FALSE));
  add ("3x2", img_palette_noise (3, 2, 13, 3, TRUE));
  add ("2x5", img_palette_noise (2, 5, 14, 3, FALSE));
  add ("4x4", img_palette_noise (4, 4, 15, 3, FALSE));
  add ("5x7", img_palette_noise (5, 7, 16, 4, TRUE));
  add ("1x1", img_palette_noise (1, 1, 17, 3, FALSE));
  add ("1x9", img_palette_noise (1, 9, 18, 3, FALSE));
  add ("9x1", img_palette_noise (9, 1, 19, 3, FALSE));
  add ("1x6 alpha", img_palette_noise (1, 6, 20, 3, TRUE));

  printf ("%d images\n", n_cases);
  check_yuv ();
  check_hqx ();
  check_xbr ();
  check_scalenx ();

  report (n_messages == 0, "no warnings or criticals from GLib or GEGL");
  for (i = 0; i < n_cases; i++)
    img_free (&cases[i].im);
  unload_module ();
  printf ("\n%d passed, %d failed\n", n_passed, n_failed);
  return n_failed ? 1 : 0;
}
