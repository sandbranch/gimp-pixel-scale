/*
 * Writes the test images of tests/common.h as PNG files, for the tests in
 * GIMP and for the pictures in docs/: the mushroom sprite, the outlined
 * text, and a 1024 x 1024 sprite sheet.
 *
 *   sprites <pixel-art-scale.so> <folder>
 *
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "common.h"

static void
save (const Img *im, const char *folder, const char *name)
{
  GeglBuffer *buf = img_buffer (im);
  gchar *path = g_build_filename (folder, name, NULL);
  GeglNode *g = gegl_node_new ();
  GeglNode *src = gegl_node_new_child (g, "operation", "gegl:buffer-source",
                                       "buffer", buf, NULL);
  GeglNode *png = gegl_node_new_child (g, "operation", "gegl:png-save",
                                       "path", path, "bitdepth", 8, NULL);

  gegl_node_link (src, png);
  gegl_node_process (png);
  printf ("%s\n", path);
  g_object_unref (g);
  g_object_unref (buf);
  g_free (path);
}

int
main (int argc, char **argv)
{
  Img im;

  if (argc != 3)
    {
      fprintf (stderr, "usage: %s <pixel-art-scale.so> <folder>\n", argv[0]);
      return 2;
    }
  if (!load_module (argv[1], &argc, &argv))
    return 2;
  im = img_mushroom ();
  save (&im, argv[2], "mushroom.png");
  img_free (&im);
  im = img_text (TRUE);
  save (&im, argv[2], "text-outline.png");
  img_free (&im);
  im = img_palette_noise (40, 30, 7, 6, TRUE);
  save (&im, argv[2], "blocks.png");
  img_free (&im);
  unload_module ();
  return n_messages ? 1 : 0;
}
