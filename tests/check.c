/*
 * Numerical checks of pixel-art:scale, through GEGL:
 *
 *   - the size and place of the output, for every algorithm and factor,
 *     down to 1 x 1, 1 x N and N x 1 images, and with an offset origin
 *   - Scale2x and Scale3x on a small case worked out by hand; Scale4x is
 *     Scale2x twice
 *   - nearest neighbour makes exact blocks; Scale2x/3x/4x and nearest make
 *     no new colors
 *   - alpha: transparent stays transparent, opaque stays opaque, colors
 *     hidden under transparent pixels change nothing that shows (weighted),
 *     a transparent background acts like a color of its own (weighted), so
 *     an outline next to transparent black is seen
 *   - edges: repeating them is the same as padding the image with its edge
 *     pixels; wrapping around is the same as scaling the image tiled 3 x 3
 *   - pieces, threads and repeated runs give the same bytes
 *   - the regions the operation asks for and invalidates, and that a
 *     change of the input shows exactly there
 *   - pixel-art:rescale on the image scaled up with nearest neighbour is
 *     pixel-art:scale on the image, it reads the middle of each block,
 *     keeps the size and place, and asks for and invalidates its regions
 *   - 8-bit linear, gray, float and R~G~B~ input; infinite and empty input
 *
 *   check <pixel-art-scale.so>   (pixel-art-rescale.so from the same folder)
 *
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "common.h"

static const char *const algorithms[] = { "hqx", "xbr", "scalenx", "nearest" };
static const char *const modes[] = { "weighted", "straight" };

static Img
img_crop (const Img *im, int x0, int y0, int w, int h)
{
  Img c = img_new (w, h);
  int y;

  for (y = 0; y < h; y++)
    memcpy (&PX (c, 0, y), &PX (*im, x0, y0 + y), sizeof (guint32) * w);
  return c;
}

/* the image with a border of m pixels, its edge pixels repeated */
static Img
img_pad (const Img *im, int m)
{
  Img p = img_new (im->w + 2 * m, im->h + 2 * m);
  int x, y;

  for (y = 0; y < p.h; y++)
    for (x = 0; x < p.w; x++)
      PX (p, x, y) = PX (*im, CLAMP (x - m, 0, im->w - 1), CLAMP (y - m, 0, im->h - 1));
  return p;
}

/* the image tiled 3 x 3 */
static Img
img_tile3 (const Img *im)
{
  Img t = img_new (3 * im->w, 3 * im->h);
  int x, y;

  for (y = 0; y < t.h; y++)
    for (x = 0; x < t.w; x++)
      PX (t, x, y) = PX (*im, x % im->w, y % im->h);
  return t;
}

static guint32
premultiplied (guint32 c)
{
  const guint32 a = c >> 24;

  return (a << 24) | (((((c >> 16) & 0xff) * a + 127) / 255) << 16) |
         (((((c >> 8) & 0xff) * a + 127) / 255) << 8) | (((c & 0xff) * a + 127) / 255);
}

/* ---- size and place ---- */

static void
test_sizes (void)
{
  static const int sizes[][2] = { { 1, 1 }, { 1, 7 }, { 7, 1 }, { 2, 2 },
                                  { 13, 9 }, { 64, 40 } };
  int a, n, s;

  for (a = 0; a < 4; a++)
    {
      int bad = 0, cases = 0;

      for (n = 2; n <= 4; n++)
        for (s = 0; s < (int) G_N_ELEMENTS (sizes); s++)
          {
            Img in = img_palette_noise (sizes[s][0], sizes[s][1], 100 + s, 5, TRUE);
            GeglBuffer *buf = img_buffer (&in);
            GeglNode *scale, *g = scale_graph (buf, algorithms[a], n, NULL, NULL, &scale);
            GeglRectangle r = gegl_node_get_bounding_box (scale);

            cases++;
            if (r.x != 0 || r.y != 0 || r.width != in.w * n || r.height != in.h * n)
              {
                if (!bad)
                  printf ("      %s x%d of %d x %d: %d, %d, %d x %d\n", algorithms[a],
                          n, in.w, in.h, r.x, r.y, r.width, r.height);
                bad++;
              }
            g_object_unref (g);
            g_object_unref (buf);
            img_free (&in);
          }
      report (bad == 0, "%s: the output is exactly n times the input, at the "
              "origin (%d cases: 1x1, 1x7, 7x1, 2x2, 13x9, 64x40, x2 to x4)",
              algorithms[a], cases);
    }
}

static void
test_offset (void)
{
  int a, bad = 0;

  for (a = 0; a < 4; a++)
    {
      Img in = img_palette_noise (11, 8, 7, 6, TRUE);
      GeglBuffer *buf = img_buffer_at (&in, -5, 3);
      GeglNode *scale, *g = scale_graph (buf, algorithms[a], 3, NULL, NULL, &scale);
      GeglRectangle r = gegl_node_get_bounding_box (scale);
      Img at = render_rect (scale, &r), zero = run_op (&in, algorithms[a], 3, NULL, NULL);

      if (r.x != -15 || r.y != 9 || r.width != 33 || r.height != 24 ||
          !img_equal (&at, &zero))
        bad++;
      img_free (&at);
      img_free (&zero);
      g_object_unref (g);
      g_object_unref (buf);
      img_free (&in);
    }
  report (bad == 0, "an input at (-5, 3) gives the output at (-15, 9) for x3, "
          "the same pixels as at the origin (all algorithms)");
}

/* ---- known cases ---- */

#define K 0xff202020u
#define W 0xffe0e0e0u

static void
test_hand (void)
{
  /* K W       worked out from the rules at scale2x.it, with the edge
   * W K       pixels repeated beyond the image */
  static const guint32 in_px[4] = { K, W, W, K };
  static const guint32 want2[16] = {
    K, K, W, W,
    K, W, K, W,
    W, K, W, K,
    W, W, K, K,
  };
  static const guint32 want3[36] = {
    K, K, K, W, W, W,
    K, K, W, K, W, W,
    K, W, W, K, K, W,
    W, K, K, W, W, K,
    W, W, K, W, K, K,
    W, W, W, K, K, K,
  };
  Img in = img_new (2, 2), o;
  int m;

  memcpy (in.px, in_px, sizeof in_px);
  for (m = 0; m < 2; m++)
    {
      o = run_op (&in, "scalenx", 2, modes[m], NULL);
      report (o.w == 4 && o.h == 4 && !memcmp (o.px, want2, sizeof want2),
              "Scale2x of a 2 x 2 checker, worked out by hand (alpha %s)", modes[m]);
      img_free (&o);
      o = run_op (&in, "scalenx", 3, modes[m], NULL);
      report (o.w == 6 && o.h == 6 && !memcmp (o.px, want3, sizeof want3),
              "Scale3x of a 2 x 2 checker, worked out by hand (alpha %s)", modes[m]);
      img_free (&o);
    }
  img_free (&in);

  /* a single dark pixel in a light 3 x 3 field has no neighbour of its
   * own color: Scale2x and Scale3x leave it a square */
  {
    Img dot = img_new (3, 3);
    int i, ok = 1;

    for (i = 0; i < 9; i++)
      dot.px[i] = W;
    dot.px[4] = K;
    for (m = 2; m <= 3; m++)
      {
        Img s = run_op (&dot, "scalenx", m, NULL, NULL);
        Img nn = run_op (&dot, "nearest", m, NULL, NULL);

        ok = ok && img_equal (&s, &nn);
        img_free (&s);
        img_free (&nn);
      }
    report (ok, "Scale2x and Scale3x leave a lone pixel a square");
    img_free (&dot);
  }
}

/* Scale4x is Scale2x twice, with the edges repeated at the intermediate
 * size (a graph of two operations does exactly that) */
static void
test_scale4x_twice (void)
{
  Img ims[3];
  int i, m, bad = 0;

  ims[0] = img_mushroom ();
  ims[1] = img_palette_noise (23, 17, 3, 5, TRUE);
  ims[2] = img_diagonals (20, 15, ARGB (255, 0, 0, 0), ARGB (255, 255, 255, 255));
  for (i = 0; i < 3; i++)
    for (m = 0; m < 2; m++)
      {
        Img four = run_op (&ims[i], "scalenx", 4, modes[m], NULL);
        Img two = run_op (&ims[i], "scalenx", 2, modes[m], NULL);
        Img twice = run_op (&two, "scalenx", 2, modes[m], NULL);

        bad += !img_equal (&four, &twice);
        img_free (&four);
        img_free (&two);
        img_free (&twice);
      }
  for (i = 0; i < 3; i++)
    img_free (&ims[i]);
  report (bad == 0, "Scale4x = Scale2x of Scale2x (3 images, both alpha modes)");
}

/* ---- colors ---- */

static void
test_nearest_blocks (void)
{
  Img in = img_palette_noise (19, 14, 5, 9, TRUE);
  int n, x, y, bad = 0;

  for (n = 2; n <= 4; n++)
    {
      Img o = run_op (&in, "nearest", n, NULL, NULL);

      for (y = 0; y < o.h; y++)
        for (x = 0; x < o.w; x++)
          bad += PX (o, x, y) != PX (in, x / n, y / n);
      img_free (&o);
    }
  img_free (&in);
  report (bad == 0, "nearest: every pixel becomes an exact n x n block (%d wrong)", bad);
}

static void
test_no_new_colors (void)
{
  Img ims[4];
  int a, i, n, m;

  ims[0] = img_mushroom ();
  ims[1] = img_palette_noise (37, 29, 21, 8, TRUE);
  ims[2] = img_text (TRUE);
  ims[3] = img_gradient_alpha (30, 26);
  for (a = 2; a < 4; a++)
    {
      long bad = 0, pixels = 0;

      for (i = 0; i < 4; i++)
        {
          GHashTable *colors = g_hash_table_new (NULL, NULL);
          gsize k;

          for (k = 0; k < (gsize) ims[i].w * ims[i].h; k++)
            g_hash_table_add (colors, GUINT_TO_POINTER (ims[i].px[k]));
          for (n = 2; n <= 4; n++)
            for (m = 0; m < 2; m++)
              {
                Img o = run_op (&ims[i], algorithms[a], n, modes[m], NULL);

                for (k = 0; k < (gsize) o.w * o.h; k++)
                  bad += !g_hash_table_contains (colors, GUINT_TO_POINTER (o.px[k]));
                pixels += (long) o.w * o.h;
                img_free (&o);
              }
          g_hash_table_destroy (colors);
        }
      report (bad == 0, "%s makes no new colors: all %ld output pixels are "
              "input pixels (4 images with alpha, x2 to x4, both alpha modes)",
              algorithms[a], pixels);
    }
  for (i = 0; i < 4; i++)
    img_free (&ims[i]);
}

/* ---- alpha ---- */

static void
test_alpha (void)
{
  Img clear = img_new (9, 7), opaque = img_noise (21, 17, 31, FALSE);
  Img mush = img_mushroom ();
  int a, n, m;
  long not_clear = 0, not_opaque = 0;

  for (a = 0; a < 4; a++)
    for (n = 2; n <= 4; n++)
      for (m = 0; m < 2; m++)
        {
          Img o = run_op (&clear, algorithms[a], n, modes[m], NULL);
          gsize k;

          for (k = 0; k < (gsize) o.w * o.h; k++)
            not_clear += (o.px[k] >> 24) != 0;
          img_free (&o);
          o = run_op (&opaque, algorithms[a], n, modes[m], NULL);
          for (k = 0; k < (gsize) o.w * o.h; k++)
            not_opaque += (o.px[k] >> 24) != 255;
          img_free (&o);
        }
  report (not_clear == 0, "a transparent image stays transparent (all "
          "algorithms, factors and alpha modes)");
  report (not_opaque == 0, "an opaque image stays opaque (random colors, all "
          "algorithms, factors and alpha modes)");

  /* hidden colors: the same sprites with random colors under their
   * transparent pixels look the same (premultiplied, the same bytes) */
  {
    Img ims[3], hidden[3];
    int i;
    long differ_weighted = 0, differ_straight_hqx = 0;

    ims[0] = img_mushroom ();
    ims[1] = img_text (TRUE);
    ims[2] = img_palette_noise (30, 22, 41, 6, TRUE);
    for (i = 0; i < 3; i++)
      hidden[i] = img_hide_colors (&ims[i], 50 + i);
    for (a = 0; a < 4; a++)
      for (i = 0; i < 3; i++)
        for (n = 2; n <= 4; n++)
          for (m = 0; m < 2; m++)
            {
              Img o1 = run_op (&ims[i], algorithms[a], n, modes[m], NULL);
              Img o2 = run_op (&hidden[i], algorithms[a], n, modes[m], NULL);
              long d = 0;
              gsize k;

              for (k = 0; k < (gsize) o1.w * o1.h; k++)
                d += premultiplied (o1.px[k]) != premultiplied (o2.px[k]);
              if (m == 0)
                differ_weighted += d;
              else if (a == 0)
                differ_straight_hqx += d;
              img_free (&o1);
              img_free (&o2);
            }
    report (differ_weighted == 0, "alpha weighted: colors hidden under "
            "transparent pixels change nothing that shows (3 sprites, all "
            "algorithms and factors: %ld pixels differ)", differ_weighted);
    report (differ_straight_hqx > 0, "alpha straight (the references' way): "
            "hidden colors do show, in %ld pixels of hqx", differ_straight_hqx);
    for (i = 0; i < 3; i++)
      {
        img_free (&ims[i]);
        img_free (&hidden[i]);
      }
  }

  /* A transparent background should act like a color of its own: then
   * the result, composited over a color that the sprite does not have,
   * is what scaling the sprite composited over that color gives (hqx
   * compares with thresholds, so its choices are the same; Scale2x
   * compares for equality). The mushroom's dark outline and the black
   * outline of the text have (nearly) the RGB of transparent black, so
   * the references' way (straight) misses those edges. */
  {
    const guint32 bg = ARGB (255, 0, 255, 0);
    Img ims[2];
    const char *const exact[] = { "hqx", "scalenx" };
    int s, i;

    ims[0] = img_mushroom ();
    ims[1] = img_text (TRUE);
    for (s = 0; s < 2; s++)
      {
        int worst[2] = { 0, 0 };

        for (m = 0; m < 2; m++)
          for (i = 0; i < 2; i++)
            for (n = 2; n <= 4; n++)
              {
                Img on = img_new (ims[i].w, ims[i].h);
                Img o, oo;
                gsize k;

                for (k = 0; k < (gsize) on.w * on.h; k++)
                  on.px[k] = (ims[i].px[k] >> 24) ? ims[i].px[k] : bg;
                o = run_op (&ims[i], exact[s], n, modes[m], NULL);
                oo = run_op (&on, exact[s], n, modes[m], NULL);
                for (k = 0; k < (gsize) o.w * o.h; k++)
                  {
                    const int al = o.px[k] >> 24;
                    int sh;

                    for (sh = 0; sh <= 16; sh += 8)
                      {
                        const int c = (o.px[k] >> sh) & 0xff, b = (bg >> sh) & 0xff;
                        const int want = (oo.px[k] >> sh) & 0xff;
                        const int got = (c * al + b * (255 - al) + 127) / 255;

                        worst[m] = MAX (worst[m], abs (got - want));
                      }
                  }
                img_free (&on);
                img_free (&o);
                img_free (&oo);
              }
        /* (Scale2x compares whole pixels, alpha too, so both ways see
         * the outline; they differ only for hidden colors, above) */
        report (s == 0 ? worst[0] <= 2 && worst[1] > 50
                       : worst[0] == 0 && worst[1] == 0,
                "%s: scaled with alpha weighted and put on green = the sprite "
                "on green scaled (largest difference %d of 255; straight: %d), "
                "mushroom and outlined text, x2 to x4", exact[s], worst[0],
                worst[1]);
      }
    img_free (&ims[0]);
    img_free (&ims[1]);
  }

  /* weighted blends: a red pixel next to transparent pixels hiding green
   * never turns green */
  {
    Img im = img_new (6, 6);
    int i;
    long greenish = 0;

    for (i = 0; i < 36; i++)
      im.px[i] = 0x0000ff00;
    for (i = 0; i < 6; i++)
      PX (im, i, i) = ARGB (255, 255, 0, 0);
    for (a = 0; a < 2; a++)
      for (n = 2; n <= 4; n++)
        {
          Img o = run_op (&im, algorithms[a], n, "weighted", NULL);
          gsize k;

          for (k = 0; k < (gsize) o.w * o.h; k++)
            greenish += (o.px[k] >> 24) && ((o.px[k] >> 8) & 0xff);
          img_free (&o);
        }
    report (greenish == 0, "alpha weighted: no green from under transparent "
            "pixels in the blends of hqx and xBR (%ld pixels)", greenish);
    img_free (&im);
  }
  img_free (&clear);
  img_free (&opaque);
  img_free (&mush);
}

/* ---- edges ---- */

static void
test_edges (void)
{
  Img ims[3];
  int a, i, n;

  ims[0] = img_mushroom ();
  ims[1] = img_palette_noise (17, 13, 61, 5, TRUE);
  ims[2] = img_diagonals (15, 12, ARGB (255, 10, 10, 30), ARGB (255, 250, 200, 60));
  for (a = 0; a < 4; a++)
    {
      int bad_clamp = 0, bad_wrap = 0;

      for (i = 0; i < 3; i++)
        for (n = 2; n <= 4; n++)
          {
            /* repeated edges: as if the image had 2 more pixels on each
             * side, copies of its edge (not for Scale4x, whose second pass
             * repeats the edges of the intermediate image instead) */
            if (!(a == 2 && n == 4))
              {
                Img pad = img_pad (&ims[i], 2);
                Img o = run_op (&ims[i], algorithms[a], n, NULL, "clamp");
                Img op = run_op (&pad, algorithms[a], n, NULL, "clamp");
                Img crop = img_crop (&op, 2 * n, 2 * n, ims[i].w * n, ims[i].h * n);

                bad_clamp += !img_equal (&o, &crop);
                img_free (&pad);
                img_free (&o);
                img_free (&op);
                img_free (&crop);
              }
            /* wrapped edges: the middle of the image tiled 3 x 3 */
            {
              Img tiles = img_tile3 (&ims[i]);
              Img o = run_op (&ims[i], algorithms[a], n, NULL, "wrap");
              Img ot = run_op (&tiles, algorithms[a], n, NULL, "clamp");
              Img crop = img_crop (&ot, ims[i].w * n, ims[i].h * n,
                                   ims[i].w * n, ims[i].h * n);

              bad_wrap += !img_equal (&o, &crop);
              img_free (&tiles);
              img_free (&o);
              img_free (&ot);
              img_free (&crop);
            }
          }
      report (bad_clamp == 0, "%s: repeated edges = the image padded with its "
              "edge pixels (3 images, x2 to x4%s)", algorithms[a],
              a == 2 ? "; Scale4x: see its own check" : "");
      report (bad_wrap == 0, "%s: wrapped edges = the middle of the image "
              "tiled 3 x 3 (3 images, x2 to x4)", algorithms[a]);
    }
  for (i = 0; i < 3; i++)
    img_free (&ims[i]);

  /* one pixel: n x n of it, whatever the algorithm */
  {
    Img one = img_new (1, 1);
    int m, bad = 0;

    one.px[0] = ARGB (200, 10, 200, 90);
    for (a = 0; a < 4; a++)
      for (n = 2; n <= 4; n++)
        for (m = 0; m < 2; m++)
          {
            Img o = run_op (&one, algorithms[a], n, modes[m], "clamp");
            Img ow = run_op (&one, algorithms[a], n, modes[m], "wrap");
            int k;

            for (k = 0; k < n * n; k++)
              bad += o.px[k] != one.px[0] || ow.px[k] != one.px[0];
            img_free (&o);
            img_free (&ow);
          }
    report (bad == 0, "a 1 x 1 image becomes n x n of its pixel (all "
            "algorithms, factors, alpha modes and edges)");
    img_free (&one);
  }

  /* 1 x N and N x 1: like the middle column (row) of the image repeated
   * three times side by side */
  {
    Img col = img_palette_noise (1, 12, 71, 4, TRUE), row = img_palette_noise (12, 1, 72, 4, TRUE);
    Img col3 = img_new (3, 12), row3 = img_new (12, 3);
    int x, y, bad = 0;

    for (y = 0; y < 12; y++)
      for (x = 0; x < 3; x++)
        {
          PX (col3, x, y) = PX (col, 0, y);
          PX (row3, y, x) = PX (row, y, 0);
        }
    for (a = 0; a < 4; a++)
      for (n = 2; n <= 4; n++)
        {
          Img o = run_op (&col, algorithms[a], n, NULL, NULL);
          Img o3 = run_op (&col3, algorithms[a], n, NULL, NULL);
          Img c3 = img_crop (&o3, n, 0, n, 12 * n);
          Img r = run_op (&row, algorithms[a], n, NULL, NULL);
          Img r3 = run_op (&row3, algorithms[a], n, NULL, NULL);
          Img cr3 = img_crop (&r3, 0, n, 12 * n, n);

          bad += !img_equal (&o, &c3) + !img_equal (&r, &cr3);
          img_free (&o);
          img_free (&o3);
          img_free (&c3);
          img_free (&r);
          img_free (&r3);
          img_free (&cr3);
        }
    report (bad == 0, "1 x 12 and 12 x 1 images: the same as the middle of "
            "the line repeated three times (all algorithms, x2 to x4)");
    img_free (&col);
    img_free (&row);
    img_free (&col3);
    img_free (&row3);
  }
}

/* ---- pieces, threads, repeats ---- */

static void
test_pieces (void)
{
  Img in = img_palette_noise (70, 53, 81, 7, TRUE);
  int a, n, i, e;

  for (a = 0; a < 4; a++)
    {
      int bad = 0;

      for (n = 2; n <= 4; n++)
        for (e = 0; e < 2; e++)
          {
            const char *edges = e ? "wrap" : "clamp";
            Img full = run_op (&in, algorithms[a], n, NULL, edges);
            GeglBuffer *buf = img_buffer (&in);
            GeglNode *scale, *g = scale_graph (buf, algorithms[a], n, NULL, edges, &scale);

            rng_seed (1000 + a * 10 + n);
            for (i = 0; i < 12; i++)
              {
                GeglRectangle r;
                Img piece, want;

                r.width = 1 + (int) (rng () % 40);
                r.height = 1 + (int) (rng () % 40);
                r.x = (int) (rng () % (guint32) (full.w - r.width + 1));
                r.y = (int) (rng () % (guint32) (full.h - r.height + 1));
                piece = render_rect (scale, &r);
                want = img_crop (&full, r.x, r.y, r.width, r.height);
                bad += !img_equal (&piece, &want);
                img_free (&piece);
                img_free (&want);
              }
            g_object_unref (g);
            g_object_unref (buf);
            img_free (&full);
          }
      report (bad == 0, "%s: 72 pieces of the output at odd places and sizes "
              "= the same part of the whole (x2 to x4, both edges)", algorithms[a]);
    }

  /* threads: 1 and 12 give the same bytes, and so does a second run */
  {
    int bad = 0, threads;

    g_object_get (gegl_config (), "threads", &threads, NULL);
    for (a = 0; a < 4; a++)
      {
        Img big = img_palette_noise (300, 220, 91, 9, TRUE), o1, o12, again;

        g_object_set (gegl_config (), "threads", 1, NULL);
        o1 = run_op (&big, algorithms[a], 3, NULL, NULL);
        g_object_set (gegl_config (), "threads", 12, NULL);
        o12 = run_op (&big, algorithms[a], 3, NULL, NULL);
        again = run_op (&big, algorithms[a], 3, NULL, NULL);
        bad += !img_equal (&o1, &o12) + !img_equal (&o12, &again);
        img_free (&o1);
        img_free (&o12);
        img_free (&again);
        img_free (&big);
      }
    g_object_set (gegl_config (), "threads", threads, NULL);
    report (bad == 0, "1 and 12 threads, and a second run, give the same "
            "bytes (300 x 220 at x3, all algorithms)");
  }
  img_free (&in);
}

/* ---- regions ---- */

static gboolean
rect_is (const GeglRectangle *r, int x, int y, int w, int h)
{
  if (r->x == x && r->y == y && r->width == w && r->height == h)
    return TRUE;
  printf ("      got %d, %d, %d x %d, expected %d, %d, %d x %d\n",
          r->x, r->y, r->width, r->height, x, y, w, h);
  return FALSE;
}

static void
test_regions (void)
{
  Img in = img_palette_noise (10, 10, 5, 4, FALSE);
  GeglBuffer *buf = img_buffer (&in);
  GeglNode *scale, *g;
  GeglOperation *op;
  GeglRectangle roi, r;
  int e;

  for (e = 0; e < 2; e++)
    {
      const char *edges = e ? "wrap" : "clamp";

      g = scale_graph (buf, "hqx", 2, NULL, edges, &scale);
      gegl_node_get_bounding_box (scale);
      op = gegl_node_get_gegl_operation (scale);

      roi = (GeglRectangle) { 0, 0, 1, 1 };
      r = gegl_operation_get_required_for_output (op, "input", &roi);
      report (rect_is (&r, e ? 0 : -2, e ? 0 : -2, e ? 10 : 5, e ? 10 : 5),
              "edges %s: output (0, 0) at x2 needs input %s", edges,
              e ? "all of it (it wraps)" : "(-2, -2) to (2, 2)");
      roi = (GeglRectangle) { 8, 9, 3, 2 };
      r = gegl_operation_get_required_for_output (op, "input", &roi);
      report (rect_is (&r, 2, 2, 6, 6), "edges %s: output (8, 9) to (10, 10) "
              "at x2 needs input (2, 2) to (7, 7)", edges);
      roi = (GeglRectangle) { 3, 3, 1, 1 };
      r = gegl_operation_get_invalidated_by_change (op, "input", &roi);
      report (rect_is (&r, 2, 2, 10, 10), "edges %s: a change at input (3, 3) "
              "invalidates output (2, 2) to (11, 11)", edges);
      roi = (GeglRectangle) { 9, 4, 1, 1 };
      r = gegl_operation_get_invalidated_by_change (op, "input", &roi);
      report (rect_is (&r, e ? 0 : 14, 4, e ? 20 : 10, 10), "edges %s: a change "
              "at input (9, 4) invalidates %s", edges,
              e ? "whole rows of the output (the change wraps around)"
                : "output (14, 4) to (23, 13)");
      g_object_unref (g);
    }
  g_object_unref (buf);

  /* a change of the input shows in a rendered graph where the operation
   * said, and the result is that of a new graph */
  for (e = 0; e < 2; e++)
    {
      const char *edges = e ? "wrap" : "clamp";
      Img big = img_palette_noise (40, 30, 111, 6, TRUE);
      GeglBuffer *b = img_buffer (&big);
      GeglRectangle full, pix = { 39, 12, 1, 1 };
      Img before, after, fresh;
      guint8 red[4] = { 255, 0, 0, 255 };
      long outside = 0;
      int x, y;

      g = scale_graph (b, "xbr", 3, NULL, edges, &scale);
      full = gegl_node_get_bounding_box (scale);
      before = render_rect (scale, &full);
      gegl_buffer_set (b, &pix, 0, babl_format ("R'G'B'A u8"), red, 4);
      PX (big, 39, 12) = ARGB (255, 255, 0, 0);
      after = render_rect (scale, &full);
      fresh = run_op (&big, "xbr", 3, NULL, edges);
      op = gegl_node_get_gegl_operation (scale);
      r = gegl_operation_get_invalidated_by_change (op, "input", &pix);
      for (y = 0; y < full.height; y++)
        for (x = 0; x < full.width; x++)
          if (PX (before, x, y) != PX (after, x, y))
            {
              GeglRectangle p = { x, y, 1, 1 };

              outside += !gegl_rectangle_contains (&r, &p);
            }
      report (img_equal (&after, &fresh) && outside == 0 &&
              img_count_diff (&before, &after, 0xffffffff) > 0,
              "edges %s: after a change at the edge of the input, the graph "
              "renders what a new one does, and changes only where the "
              "operation said", edges);
      img_free (&before);
      img_free (&after);
      img_free (&fresh);
      img_free (&big);
      g_object_unref (g);
      g_object_unref (b);
    }
  img_free (&in);
}

/* ---- pixel-art:rescale, on an image scaled up with nearest neighbour ---- */

/* the operation named op on an image */
static Img
run_named (const char *op, const Img *in, const char *algorithm, int factor,
           const char *alpha, const char *edges)
{
  GeglBuffer *buf = img_buffer (in);
  GeglNode *scale;
  GeglNode *g = scale_graph (buf, algorithm, factor, alpha, edges, &scale);
  GeglRectangle r;
  Img out;

  gegl_node_set (scale, "operation", op, NULL);
  gegl_node_set (scale, "factor", factor, NULL);
  set_enum (scale, "algorithm", algorithm);
  set_enum (scale, "alpha", alpha ? alpha : "weighted");
  set_enum (scale, "edges", edges ? edges : "clamp");
  r = gegl_node_get_bounding_box (scale);
  out = render_rect (scale, &r);
  g_object_unref (g);
  g_object_unref (buf);
  return out;
}

static void
test_rescale (void)
{
  Img ims[4];
  int a, i, n, m, e;

  ims[0] = img_mushroom ();
  ims[1] = img_text (TRUE);
  ims[2] = img_palette_noise (19, 11, 131, 7, TRUE);
  ims[3] = img_noise (9, 7, 132, TRUE);
  for (a = 0; a < 4; a++)
    {
      int bad = 0, cases = 0;

      for (i = 0; i < 4; i++)
        for (n = 2; n <= 4; n++)
          for (m = 0; m < 2; m++)
            for (e = 0; e < 2; e++)
              {
                const char *edges = e ? "wrap" : "clamp";
                Img big = run_op (&ims[i], "nearest", n, NULL, NULL);
                Img want = run_op (&ims[i], algorithms[a], n, modes[m], edges);
                Img got = run_named ("pixel-art:rescale", &big, algorithms[a], n,
                                     modes[m], edges);

                bad += !img_equal (&got, &want);
                cases++;
                img_free (&big);
                img_free (&want);
                img_free (&got);
              }
      report (bad == 0, "rescale %s of the image scaled up with nearest "
              "neighbour = scale %s of the image (%d cases: 4 images, x2 to "
              "x4, both alpha modes and edges)", algorithms[a], algorithms[a], cases);
    }

  /* the middle pixel of each block counts: the others may be anything */
  {
    int bad = 0;

    for (n = 2; n <= 4; n++)
      for (e = 0; e < 2; e++)
        {
          const char *edges = e ? "wrap" : "clamp";
          Img big = run_op (&ims[0], "nearest", n, NULL, NULL);
          Img want = run_op (&ims[0], "hqx", n, NULL, edges);
          Img got;
          int x, y;

          for (y = 0; y < big.h; y++)
            for (x = 0; x < big.w; x++)
              if (x % n != n / 2 || y % n != n / 2)
                PX (big, x, y) = rng () | 0xff000000u;
          got = run_named ("pixel-art:rescale", &big, "hqx", n, NULL, edges);
          bad += !img_equal (&got, &want);
          img_free (&big);
          img_free (&want);
          img_free (&got);
        }
    report (bad == 0, "rescale reads the middle pixel of each block, also "
            "beyond the edges (the others set to noise change nothing, x2 "
            "to x4, both edges)");
  }

  /* a size that is not a multiple of the factor: the output keeps it and
   * is the part of the full blocks' result */
  {
    Img big = run_op (&ims[2], "nearest", 3, NULL, NULL);
    Img cut = img_crop (&big, 0, 0, big.w - 2, big.h - 1);
    Img got = run_named ("pixel-art:rescale", &cut, "xbr", 3, NULL, NULL);
    Img all = run_op (&ims[2], "xbr", 3, NULL, NULL);
    Img want = img_crop (&all, 0, 0, cut.w, cut.h);

    report (got.w == cut.w && got.h == cut.h && img_equal (&got, &want),
            "rescale of a %d x %d image at x3 (not a multiple of 3): the same "
            "size, the part of the result of the whole blocks", cut.w, cut.h);
    img_free (&big);
    img_free (&cut);
    img_free (&got);
    img_free (&all);
    img_free (&want);
  }

  /* pieces, and an input at an offset */
  {
    Img big = run_op (&ims[2], "nearest", 4, NULL, NULL);
    Img full = run_named ("pixel-art:rescale", &big, "hqx", 4, NULL, NULL);
    GeglBuffer *buf = img_buffer_at (&big, 13, -9);
    GeglNode *scale, *g = scale_graph (buf, "hqx", 4, NULL, NULL, &scale);
    GeglRectangle bb;
    int k, bad = 0;

    gegl_node_set (scale, "operation", "pixel-art:rescale", NULL);
    gegl_node_set (scale, "factor", 4, NULL);
    set_enum (scale, "algorithm", "hqx");
    bb = gegl_node_get_bounding_box (scale);
    rng_seed (77);
    for (k = 0; k < 20; k++)
      {
        GeglRectangle r;
        Img piece, want;

        r.width = 1 + (int) (rng () % 30);
        r.height = 1 + (int) (rng () % 30);
        r.x = 13 + (int) (rng () % (guint32) (full.w - r.width + 1));
        r.y = -9 + (int) (rng () % (guint32) (full.h - r.height + 1));
        piece = render_rect (scale, &r);
        want = img_crop (&full, r.x - 13, r.y + 9, r.width, r.height);
        bad += !img_equal (&piece, &want);
        img_free (&piece);
        img_free (&want);
      }
    report (bb.x == 13 && bb.y == -9 && bb.width == big.w && bb.height == big.h &&
            bad == 0, "rescale of an input at (13, -9): the output has its "
            "size and place, and 20 pieces are the same part of the whole");
    g_object_unref (g);
    g_object_unref (buf);
    img_free (&big);
    img_free (&full);
  }

  /* regions: output (0, 0) at x2 needs the blocks -2 to 0 (input -4 to
   * 1); a change at input (5, 5) is block (2, 2), which shows in the
   * blocks 0 to 4 (output 0 to 9) */
  {
    Img big = run_op (&ims[2], "nearest", 2, NULL, NULL);
    GeglBuffer *buf = img_buffer (&big);
    GeglNode *scale, *g = scale_graph (buf, "hqx", 2, NULL, NULL, &scale);
    GeglOperation *op;
    GeglRectangle roi = { 0, 0, 1, 1 }, r;

    gegl_node_set (scale, "operation", "pixel-art:rescale", NULL);
    gegl_node_set (scale, "factor", 2, NULL);
    gegl_node_get_bounding_box (scale);
    op = gegl_node_get_gegl_operation (scale);
    r = gegl_operation_get_required_for_output (op, "input", &roi);
    report (rect_is (&r, -4, -4, 10, 10), "rescale: output (0, 0) at x2 needs "
            "input (-4, -4) to (5, 5), the blocks around it");
    roi = (GeglRectangle) { 5, 5, 1, 1 };
    r = gegl_operation_get_invalidated_by_change (op, "input", &roi);
    report (rect_is (&r, 0, 0, 10, 10), "rescale: a change at input (5, 5) "
            "invalidates output (0, 0) to (9, 9)");
    g_object_unref (g);
    g_object_unref (buf);
    img_free (&big);
  }
  for (i = 0; i < 4; i++)
    img_free (&ims[i]);
}

/* ---- formats and special inputs ---- */

static void
test_formats (void)
{
  const int w = 13, h = 9, n = 3;
  guint8 *in8 = g_malloc (w * h * 4), *out8 = g_malloc (w * h * n * n * 4);
  float *inf = g_malloc (w * h * 4 * sizeof (float));
  GeglRectangle r = { 0, 0, w, h }, ro = { 0, 0, w * n, h * n };
  const char *const u8[] = { "RGBA u8", "R'G'B'A u8", "R~G~B~A u8" };
  int i, f, a;

  rng_seed (5);
  for (i = 0; i < w * h * 4; i++)
    in8[i] = rng () & 0xff;
  for (i = 0; i < w * h; i++)
    in8[4 * i + 3] = (i % 3) ? 255 : 128;

  /* 8-bit input in its own encoding: nearest and Scale2x copy values
   * exactly (linear 8-bit values would change on a trip through R'G'B') */
  for (f = 0; f < 3; f++)
    for (a = 2; a < 4; a++)
      {
        GeglBuffer *b = gegl_buffer_new (&r, babl_format (u8[f]));
        GeglNode *scale, *g;
        const Babl *wf;
        int x, y, bad = 0;

        gegl_buffer_set (b, &r, 0, babl_format (u8[f]), in8, GEGL_AUTO_ROWSTRIDE);
        g = scale_graph (b, algorithms[a], n, NULL, NULL, &scale);
        gegl_node_blit (scale, 1.0, &ro, babl_format (u8[f]), out8,
                        GEGL_AUTO_ROWSTRIDE, GEGL_BLIT_DEFAULT);
        wf = gegl_operation_get_format (gegl_node_get_gegl_operation (scale), "output");
        if (a == 3)
          for (y = 0; y < h * n; y++)
            for (x = 0; x < w * n; x++)
              bad += memcmp (out8 + 4 * (y * w * n + x),
                             in8 + 4 * ((y / n) * w + x / n), 4) != 0;
        else
          {
            GHashTable *colors = g_hash_table_new (NULL, NULL);
            guint32 c;

            for (i = 0; i < w * h; i++)
              {
                memcpy (&c, in8 + 4 * i, 4);
                g_hash_table_add (colors, GUINT_TO_POINTER (c));
              }
            for (i = 0; i < w * h * n * n; i++)
              {
                memcpy (&c, out8 + 4 * i, 4);
                bad += !g_hash_table_contains (colors, GUINT_TO_POINTER (c));
              }
            g_hash_table_destroy (colors);
          }
        report (bad == 0 && !strcmp (babl_get_name (wf), u8[f]),
                "%s input: %s keeps the values exactly, working in %s",
                u8[f], algorithms[a], babl_get_name (wf));
        g_object_unref (g);
        g_object_unref (b);
      }

  /* gray 8-bit: the gray values come back */
  {
    GeglBuffer *b = gegl_buffer_new (&r, babl_format ("Y'A u8"));
    guint8 gray[13 * 9 * 2], back[39 * 27 * 2];
    GeglNode *scale, *g;
    int x, y, bad = 0;

    for (i = 0; i < w * h * 2; i++)
      gray[i] = in8[i];
    gegl_buffer_set (b, &r, 0, babl_format ("Y'A u8"), gray, GEGL_AUTO_ROWSTRIDE);
    g = scale_graph (b, "nearest", n, NULL, NULL, &scale);
    gegl_node_blit (scale, 1.0, &ro, babl_format ("Y'A u8"), back,
                    GEGL_AUTO_ROWSTRIDE, GEGL_BLIT_DEFAULT);
    for (y = 0; y < h * n; y++)
      for (x = 0; x < w * n; x++)
        bad += memcmp (back + 2 * (y * w * n + x), gray + 2 * ((y / n) * w + x / n), 2) != 0;
    report (bad == 0, "gray 8-bit input (Y'A u8): the values come back exactly");
    g_object_unref (g);
    g_object_unref (b);
  }

  /* float input: scaled as R'G'B'A u8, the nearest 8-bit values */
  {
    GeglBuffer *b = gegl_buffer_new (&r, babl_format ("R'G'B'A float"));
    GeglNode *scale, *g;
    const Babl *wf;
    int x, y, bad = 0;

    for (i = 0; i < w * h * 4; i++)
      inf[i] = in8[i] / 255.0f;
    gegl_buffer_set (b, &r, 0, babl_format ("R'G'B'A float"), inf, GEGL_AUTO_ROWSTRIDE);
    g = scale_graph (b, "nearest", n, NULL, NULL, &scale);
    gegl_node_blit (scale, 1.0, &ro, babl_format ("R'G'B'A u8"), out8,
                    GEGL_AUTO_ROWSTRIDE, GEGL_BLIT_DEFAULT);
    wf = gegl_operation_get_format (gegl_node_get_gegl_operation (scale), "output");
    for (y = 0; y < h * n; y++)
      for (x = 0; x < w * n; x++)
        bad += memcmp (out8 + 4 * (y * w * n + x), in8 + 4 * ((y / n) * w + x / n), 4) != 0;
    report (bad == 0 && !strcmp (babl_get_name (wf), "R'G'B'A u8"),
            "float input: scaled in %s, 8-bit values exactly", babl_get_name (wf));
    g_object_unref (g);
    g_object_unref (b);
  }
  g_free (in8);
  g_free (out8);
  g_free (inf);
}

static void
test_special_inputs (void)
{
  GeglNode *g = gegl_node_new ();
  GeglNode *color = gegl_node_new_child (g, "operation", "gegl:color", NULL);
  GeglNode *scale = gegl_node_new_child (g, "operation", "pixel-art:scale", NULL);
  GeglColor *c = gegl_color_new ("rgba(0.2, 0.4, 0.6, 1.0)");
  GeglRectangle r, piece = { -7, 3, 10, 10 };
  Img out;
  int i, bad = 0;

  gegl_node_set (color, "value", c, NULL);
  gegl_node_link (color, scale);
  r = gegl_node_get_bounding_box (scale);
  out = render_rect (scale, &piece);
  for (i = 1; i < 100; i++)
    bad += out.px[i] != out.px[0];
  report (gegl_rectangle_is_infinite_plane (&r) && bad == 0 && (out.px[0] >> 24) == 255,
          "an infinite input (gegl:color) gives an infinite output of its color");
  img_free (&out);
  g_object_unref (c);
  g_object_unref (g);

  {
    GeglRectangle none = { 0, 0, 0, 0 };
    GeglBuffer *b = gegl_buffer_new (&none, babl_format ("R'G'B'A u8"));
    GeglNode *s2, *g2 = scale_graph (b, "hqx", 3, NULL, NULL, &s2);

    r = gegl_node_get_bounding_box (s2);
    report (r.width == 0 && r.height == 0, "an empty input gives an empty output");
    g_object_unref (g2);
    g_object_unref (b);
  }

  /* without an input at all */
  {
    GeglNode *g3 = gegl_node_new ();
    GeglNode *s3 = gegl_node_new_child (g3, "operation", "pixel-art:scale", NULL);

    r = gegl_node_get_bounding_box (s3);
    report (r.width == 0 && r.height == 0, "no input gives an empty output");
    g_object_unref (g3);
  }
}

int
main (int argc, char **argv)
{
  if (argc != 2)
    {
      fprintf (stderr, "usage: %s <pixel-art-scale.so>\n", argv[0]);
      return 2;
    }
  if (!load_module (argv[1], &argc, &argv))
    return 2;

  test_sizes ();
  test_offset ();
  test_hand ();
  test_scale4x_twice ();
  test_nearest_blocks ();
  test_no_new_colors ();
  test_alpha ();
  test_edges ();
  test_pieces ();
  test_regions ();
  test_rescale ();
  test_formats ();
  test_special_inputs ();

  report (n_messages == 0, "no warnings or criticals from GLib or GEGL");
  unload_module ();
  printf ("\n%d passed, %d failed\n", n_passed, n_failed);
  return n_failed ? 1 : 0;
}
