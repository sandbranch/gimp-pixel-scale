/*
 * How long pixel-art:scale takes on a 1024 x 1024 sprite sheet (the test
 * sprite and blocks of noise in few colors, on transparent pixels), for
 * each algorithm and factor, with GEGL's threads and with one thread: the
 * best of three runs of rendering the whole output into memory.
 *
 *   bench <pixel-art-scale.so>
 *
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "common.h"

static Img
sheet (void)
{
  Img im = img_new (1024, 1024), mush = img_mushroom ();
  int x, y, tx, ty, k = 0;

  for (ty = 0; ty + 32 <= 1024; ty += 32)
    for (tx = 0; tx + 32 <= 1024; tx += 32, k++)
      {
        if (k % 3 == 2)
          {
            Img blocks = img_palette_noise (32, 32, (guint32) k, 6, TRUE);

            for (y = 0; y < 32; y++)
              for (x = 0; x < 32; x++)
                PX (im, tx + x, ty + y) = PX (blocks, x, y);
            img_free (&blocks);
          }
        else
          for (y = 0; y < mush.h; y++)
            for (x = 0; x < mush.w; x++)
              PX (im, tx + 2 + x, ty + 4 + y) = PX (mush, x, y);
      }
  img_free (&mush);
  return im;
}

static double
time_one (GeglBuffer *buf, const char *algorithm, int n)
{
  double best = 1e9;
  int i;

  for (i = 0; i < 3; i++)
    {
      GeglNode *scale, *g = scale_graph (buf, algorithm, n, NULL, NULL, &scale);
      GeglRectangle r = gegl_node_get_bounding_box (scale);
      guint8 *out = g_malloc ((gsize) r.width * r.height * 4);
      gint64 t0 = g_get_monotonic_time ();

      gegl_node_blit (scale, 1.0, &r, babl_format ("R'G'B'A u8"), out,
                      GEGL_AUTO_ROWSTRIDE, GEGL_BLIT_DEFAULT);
      best = MIN (best, (g_get_monotonic_time () - t0) / 1e6);
      g_free (out);
      g_object_unref (g);
    }
  return best;
}

int
main (int argc, char **argv)
{
  static const char *const algorithms[] = { "hqx", "xbr", "scalenx", "nearest" };
  Img im;
  GeglBuffer *buf;
  int threads, a, n;

  if (argc != 2)
    {
      fprintf (stderr, "usage: %s <pixel-art-scale.so>\n", argv[0]);
      return 2;
    }
  if (!load_module (argv[1], &argc, &argv))
    return 2;
  im = sheet ();
  buf = img_buffer (&im);
  g_object_get (gegl_config (), "threads", &threads, NULL);
  printf ("1024 x 1024 sprite sheet, seconds (best of 3)\n");
  printf ("%-9s %6s %12s %10s\n", "algorithm", "factor", "threads", "1 thread");
  for (a = 0; a < 4; a++)
    for (n = 2; n <= 4; n++)
      {
        double tt, t1;

        g_object_set (gegl_config (), "threads", threads, NULL);
        tt = time_one (buf, algorithms[a], n);
        g_object_set (gegl_config (), "threads", 1, NULL);
        t1 = time_one (buf, algorithms[a], n);
        printf ("%-9s %6d %8.3f (%d) %10.3f\n", algorithms[a], n, tt, threads, t1);
      }
  g_object_unref (buf);
  img_free (&im);
  unload_module ();
  return 0;
}
