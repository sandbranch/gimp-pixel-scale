/*
 * Shared by the tests (check.c, refcheck.c, bench.c, sprites.c): loading
 * the operation, running it on images of packed pixels, reporting, and
 * the generated test images, among them a sprite drawn for these tests.
 *
 * Images are arrays of 0xAARRGGBB pixels, the layout of the scalers and
 * of the reference implementations.
 *
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PAS_TEST_COMMON_H
#define PAS_TEST_COMMON_H

/* each test uses some of the helpers */
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gegl.h>
#include <gegl-plugin.h>
#include <glib/gstdio.h>

/* ---- reporting ---- */

static int n_passed, n_failed, n_messages;

static void G_GNUC_PRINTF (2, 3)
report (gboolean ok, const char *fmt, ...)
{
  va_list ap;

  printf (ok ? "PASS  " : "FAIL  ");
  va_start (ap, fmt);
  vprintf (fmt, ap);
  va_end (ap);
  printf ("\n");
  fflush (stdout);
  if (ok)
    n_passed++;
  else
    n_failed++;
}

static void
log_handler (const gchar    *domain,
             GLogLevelFlags  level,
             const gchar    *message,
             gpointer        data)
{
  (void) data;
  if (level & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING))
    n_messages++;
  g_log_default_handler (domain, level, message, NULL);
}

/* ---- loading the operation ---- */

static gchar *module_dir, *module_link[2];

/*
 * GEGL loads every file in a module folder, and a build folder holds
 * files that it must not load: the module is linked into a folder of its
 * own. GEGL_PATH replaces GEGL's own list of folders, so that an installed
 * copy of the operation (in the user's folder) is not loaded instead.
 */
static void
remove_module_links (void)
{
  int i;

  for (i = 0; i < 2; i++)
    if (module_link[i])
      unlink (module_link[i]);
  if (module_dir)
    g_rmdir (module_dir);
}

static int
load_module (const char *path, int *argc, char ***argv)
{
  gchar *search, *dir;
  int i;

  /* the operation, and pixel-art-rescale.so from the same folder */
  module_dir = g_dir_make_tmp ("pixel-art-scale-XXXXXX", NULL);
  if (!module_dir)
    return 0;
  dir = g_path_get_dirname (path);
  for (i = 0; i < 2; i++)
    {
      gchar *target = i ? g_build_filename (dir, "pixel-art-rescale.so", NULL)
                        : g_canonicalize_filename (path, NULL);
      gchar *abs = g_canonicalize_filename (target, NULL);

      module_link[i] = g_build_filename (module_dir, i ? "pixel-art-rescale.so"
                                                       : "pixel-art-scale.so", NULL);
      if (symlink (abs, module_link[i]) != 0)
        {
          fprintf (stderr, "cannot link %s into a temporary folder\n", abs);
          remove_module_links ();
          return 0;
        }
      g_free (target);
      g_free (abs);
    }
  g_free (dir);
  search = g_strconcat (module_dir, G_SEARCHPATH_SEPARATOR_S,
                        GEGL_PLUGINSDIR, NULL);
  g_setenv ("GEGL_PATH", search, TRUE);
  g_free (search);

  /* the operation is GPL3+ (its "license" key): GEGL offers it only to
   * applications that say they are GPL3 too, as GIMP and gegl do. Said
   * before gegl_init: said after it, GEGL loads its modules once more */
  g_object_set (gegl_config (), "application-license", "GPL3", NULL);
  gegl_init (argc, argv);
  g_log_set_default_handler (log_handler, NULL);
  if (!gegl_has_operation ("pixel-art:scale") ||
      !gegl_has_operation ("pixel-art:rescale"))
    {
      fprintf (stderr, "pixel-art:scale or pixel-art:rescale did not load "
               "from the folder of %s\n", path);
      remove_module_links ();
      return 0;
    }
  return 1;
}

static void
unload_module (void)
{
  gegl_exit ();
  /* GEGL loads the module again when it is used after a pause: remove it
   * only at the end, or not at all for LeakSanitizer, which needs it to
   * name the functions when it reports after main () */
  if (!g_getenv ("PAS_CHECK_KEEP_MODULE"))
    remove_module_links ();
  g_free (module_link[0]);
  g_free (module_link[1]);
  g_free (module_dir);
}

/* ---- images ---- */

typedef struct
{
  int      w, h;
  guint32 *px;
} Img;

static Img
img_new (int w, int h)
{
  Img im = { w, h, g_new0 (guint32, (gsize) w * (gsize) h) };
  return im;
}

static void
img_free (Img *im)
{
  g_free (im->px);
  im->px = NULL;
}

#define PX(im, x, y) ((im).px[(gsize) (y) * (im).w + (x)])

static gboolean
img_equal (const Img *a, const Img *b)
{
  return a->w == b->w && a->h == b->h &&
         memcmp (a->px, b->px, sizeof (guint32) * (gsize) a->w * a->h) == 0;
}

/* how many pixels differ (under the mask), or -1 for other sizes */
static long
img_count_diff (const Img *a, const Img *b, guint32 mask)
{
  long n = 0;
  gsize i;

  if (a->w != b->w || a->h != b->h)
    return -1;
  for (i = 0; i < (gsize) a->w * a->h; i++)
    n += (a->px[i] & mask) != (b->px[i] & mask);
  return n;
}

static void
to_bytes (const Img *im, guint8 *b)
{
  gsize i;

  for (i = 0; i < (gsize) im->w * im->h; i++)
    {
      const guint32 c = im->px[i];

      b[4 * i]     = (c >> 16) & 0xff;
      b[4 * i + 1] = (c >> 8) & 0xff;
      b[4 * i + 2] = c & 0xff;
      b[4 * i + 3] = c >> 24;
    }
}

static void
from_bytes (Img *im, const guint8 *b)
{
  gsize i;

  for (i = 0; i < (gsize) im->w * im->h; i++)
    im->px[i] = ((guint32) b[4 * i + 3] << 24) | ((guint32) b[4 * i] << 16) |
                ((guint32) b[4 * i + 1] << 8) | b[4 * i + 2];
}

/* a GEGL buffer of the image, R'G'B'A u8, at (x, y) */
static GeglBuffer *
img_buffer_at (const Img *im, int x, int y)
{
  GeglRectangle r = { x, y, im->w, im->h };
  const Babl *f = babl_format ("R'G'B'A u8");
  GeglBuffer *buf = gegl_buffer_new (&r, f);
  guint8 *b = g_malloc ((gsize) im->w * im->h * 4);

  to_bytes (im, b);
  gegl_buffer_set (buf, &r, 0, f, b, GEGL_AUTO_ROWSTRIDE);
  g_free (b);
  return buf;
}

static GeglBuffer *
img_buffer (const Img *im)
{
  return img_buffer_at (im, 0, 0);
}

/* sets an enum property by the nick of its value ("hqx", "weighted"...);
 * gegl_node_set takes the number, and a string there would be a pointer */
static void
set_enum (GeglNode *node, const char *property, const char *nick)
{
  GParamSpec *pspec = gegl_node_find_property (node, property);
  GEnumValue *v = NULL;

  if (pspec && G_IS_PARAM_SPEC_ENUM (pspec))
    v = g_enum_get_value_by_nick (G_PARAM_SPEC_ENUM (pspec)->enum_class, nick);
  if (!v)
    {
      fprintf (stderr, "no value %s of %s\n", nick, property);
      abort ();
    }
  gegl_node_set (node, property, v->value, NULL);
}

/* a graph: buffer-source -> pixel-art:scale; *scale is the scaling node */
static GeglNode *
scale_graph (GeglBuffer *buf, const char *algorithm, int factor,
             const char *alpha, const char *edges, GeglNode **scale)
{
  GeglNode *g = gegl_node_new ();
  GeglNode *src = gegl_node_new_child (g, "operation", "gegl:buffer-source",
                                       "buffer", buf, NULL);

  *scale = gegl_node_new_child (g, "operation", "pixel-art:scale", NULL);
  gegl_node_set (*scale, "factor", factor, NULL);
  set_enum (*scale, "algorithm", algorithm);
  set_enum (*scale, "alpha", alpha ? alpha : "weighted");
  set_enum (*scale, "edges", edges ? edges : "clamp");
  gegl_node_link (src, *scale);
  return g;
}

/* the part r of a node's output, as an image */
static Img
render_rect (GeglNode *node, const GeglRectangle *r)
{
  Img out = img_new (r->width, r->height);
  guint8 *b = g_malloc ((gsize) r->width * r->height * 4 + 4);

  gegl_node_blit (node, 1.0, r, babl_format ("R'G'B'A u8"), b,
                  GEGL_AUTO_ROWSTRIDE, GEGL_BLIT_DEFAULT);
  from_bytes (&out, b);
  g_free (b);
  return out;
}

/* the operation on an image: all of its output */
static Img
run_op (const Img *in, const char *algorithm, int factor, const char *alpha,
        const char *edges)
{
  GeglBuffer *buf = img_buffer (in);
  GeglNode *scale;
  GeglNode *g = scale_graph (buf, algorithm, factor, alpha, edges, &scale);
  GeglRectangle r = gegl_node_get_bounding_box (scale);
  Img out = render_rect (scale, &r);

  g_object_unref (g);
  g_object_unref (buf);
  return out;
}

/* ---- generated images ---- */

static guint32 rng_state = 2463534242u;

static void
rng_seed (guint32 s)
{
  rng_state = s ? s : 2463534242u;
}

static guint32
rng (void)
{
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 17;
  rng_state ^= rng_state << 5;
  return rng_state;
}

#define ARGB(a, r, g, b) (((guint32) (a) << 24) | ((guint32) (r) << 16) | \
                          ((guint32) (g) << 8) | (guint32) (b))

/* a mushroom, 28 x 24, drawn for these tests (and the README) */
static const char *const mushroom_art[] = {
  "..........kkkkkkkk..........",
  ".......kkkrrrrrrrrkkk.......",
  ".....kkrrrrrrrrrhhhrrkk.....",
  "....krrrwwwrrrrrrrhhhrrk....",
  "...krrrwwwwwrrrrrrrhhrrrk...",
  "..krrrrwwwwwrrrrrrrrrrrrrk..",
  "..krrrrrwwwrrrrrrwwwrrrrrk..",
  ".krrrrrrrrrrrrrrwwwwwrrrrRk.",
  ".krwwrrrrrrrrrrrwwwwwrrrRRk.",
  "krwwwwrrrrrwwrrrrwwwrrrrRRRk",
  "krwwwwrrrrwwwwrrrrrrrrrRRRRk",
  "kRrwwrrrrrwwwwrrrrrrrrRRRRRk",
  "kRRrrrrrrrrwwrrrrrrrRRRRRRRk",
  ".kRRRRrrrrrrrrrrrRRRRRRRRRk.",
  "..kkRRRRRRRRRRRRRRRRRRRRkk..",
  "....kkkkssssssssssssskkk....",
  ".......kssssssssssssSk......",
  ".......ksseesssseessSk......",
  ".......ksseesssseessSk......",
  ".......kspssssssssspSk......",
  ".......ksssssmmssssSSk......",
  ".......kssssssssssSSSk......",
  "........kSSSSSSSSSSSk.......",
  ".........kkkkkkkkkkk........",
};

static guint32
mushroom_color (char c)
{
  switch (c)
    {
    case 'k': return ARGB (255, 43, 29, 47);
    case 'r': return ARGB (255, 216, 58, 58);
    case 'R': return ARGB (255, 158, 40, 53);
    case 'w': return ARGB (255, 244, 239, 230);
    case 'h': return ARGB (255, 255, 138, 128);
    case 's': return ARGB (255, 240, 217, 181);
    case 'S': return ARGB (255, 201, 166, 122);
    case 'e': return ARGB (255, 26, 26, 26);
    case 'p': return ARGB (255, 242, 157, 176);
    case 'm': return ARGB (255, 122, 48, 69);
    default:  return 0;
    }
}

static Img
img_mushroom (void)
{
  const int h = G_N_ELEMENTS (mushroom_art), w = (int) strlen (mushroom_art[0]);
  Img im = img_new (w, h);
  int x, y;

  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      PX (im, x, y) = mushroom_color (mushroom_art[y][x]);
  return im;
}

/* the mushroom on a background color (opaque) */
static Img
img_mushroom_on (guint32 bg)
{
  Img im = img_mushroom ();
  gsize i;

  for (i = 0; i < (gsize) im.w * im.h; i++)
    if (!(im.px[i] >> 24))
      im.px[i] = bg;
  return im;
}

static Img
img_checker (int w, int h, int cell, guint32 c1, guint32 c2)
{
  Img im = img_new (w, h);
  int x, y;

  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      PX (im, x, y) = ((x / cell + y / cell) & 1) ? c2 : c1;
  return im;
}

/* lines of slopes 1, -1, 1/2, 2, 1/3, a circle and a box, on a background */
static Img
img_diagonals (int w, int h, guint32 bg, guint32 fg)
{
  Img im = img_new (w, h);
  int x, y, i;

  for (i = 0; i < w * h; i++)
    im.px[i] = bg;
  for (i = 0; i < w && i < h; i++)
    {
      PX (im, i, i) = fg;
      PX (im, w - 1 - i, i) = fg;
    }
  for (x = 0; x < w; x++)
    {
      if (x / 2 < h)
        PX (im, x, x / 2) = fg;
      if (2 * x < h)
        PX (im, x, 2 * x) = fg;
      if (h - 1 - x / 3 >= 0)
        PX (im, x, h - 1 - x / 3) = fg;
    }
  for (i = 0; i < 360; i++)
    {
      const double a = i * G_PI / 180.0, rr = MIN (w, h) / 3.0;

      x = (int) (w / 2 + rr * cos (a));
      y = (int) (h / 2 + rr * sin (a));
      if (x >= 0 && x < w && y >= 0 && y < h)
        PX (im, x, y) = fg;
    }
  for (x = w / 5; x < w / 3; x++)
    for (y = h / 5; y < h / 3; y++)
      PX (im, x, y) = fg;
  return im;
}

/* a 5 x 7 font for the letters of the text below */
static const struct { char c; const char *rows[7]; } font5x7[] = {
  { 'P', { "####.", "#...#", "#...#", "####.", "#....", "#....", "#...." } },
  { 'I', { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####" } },
  { 'X', { "#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#" } },
  { 'E', { "#####", "#....", "#....", "####.", "#....", "#....", "#####" } },
  { 'L', { "#....", "#....", "#....", "#....", "#....", "#....", "#####" } },
  { 'A', { ".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" } },
  { 'R', { "####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#" } },
  { 'T', { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.." } },
  { 'S', { ".####", "#....", "#....", ".###.", "....#", "....#", "####." } },
  { 'C', { ".###.", "#...#", "#....", "#....", "#....", "#...#", ".###." } },
  { 'O', { ".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." } },
  { 'G', { ".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".###." } },
  { '2', { ".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####" } },
  { '4', { "...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#." } },
};

static const char *
glyph_row (char c, int row)
{
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (font5x7); i++)
    if (font5x7[i].c == c)
      return font5x7[i].rows[row];
  return ".....";
}

/*
 * Text-like glyphs: two lines of text in dark gray on a light background
 * (opaque), and, with outline, white letters with a black outline on
 * transparent black (the case where the references do not see the
 * outline's edge, since black and transparent black have the same RGB).
 */
static Img
img_text (gboolean outline)
{
  const char *lines[2] = { "PIXEL ART", "SCALE 2X 4X" };
  const int w = 2 + 6 * 11 + 2, h = 2 + 8 * 2 + 2;
  Img im = img_new (w, h);
  int l, i, x, y;

  for (i = 0; i < w * h; i++)
    im.px[i] = outline ? 0 : ARGB (255, 236, 232, 220);
  for (l = 0; l < 2; l++)
    for (i = 0; lines[l][i]; i++)
      for (y = 0; y < 7; y++)
        for (x = 0; x < 5; x++)
          if (glyph_row (lines[l][i], y)[x] == '#')
            PX (im, 2 + 6 * i + x, 2 + 8 * l + y) =
              outline ? ARGB (255, 255, 255, 255) : ARGB (255, 40, 40, 52);
  if (outline)
    {
      Img copy = img_new (w, h);

      memcpy (copy.px, im.px, sizeof (guint32) * w * h);
      for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
          if (!PX (copy, x, y))
            {
              int dx, dy;

              for (dy = -1; dy <= 1; dy++)
                for (dx = -1; dx <= 1; dx++)
                  if (x + dx >= 0 && x + dx < w && y + dy >= 0 && y + dy < h &&
                      PX (copy, x + dx, y + dy) == ARGB (255, 255, 255, 255))
                    PX (im, x, y) = ARGB (255, 0, 0, 0);
            }
      img_free (&copy);
    }
  return im;
}

/* a gradient in steps of 4 pixels, with alpha falling from 255 to 0 in
 * steps along the diagonal */
static Img
img_gradient_alpha (int w, int h)
{
  Img im = img_new (w, h);
  int x, y;

  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
        const int sx = x / 4 * 4, sy = y / 4 * 4;
        const int a = 255 - (255 * ((sx + sy) / 3) / MAX (1, (w + h) / 3));

        PX (im, x, y) = ARGB (CLAMP (a, 0, 255), 255 * sx / MAX (1, w - 1),
                              255 * sy / MAX (1, h - 1), 128);
      }
  return im;
}

/* blocks of 1 to 3 pixels in random colors of a palette of n colors, of
 * which transparent ones if with_alpha (transparent black, and one half
 * transparent color) */
static Img
img_palette_noise (int w, int h, guint32 seed, int n, gboolean with_alpha)
{
  Img im = img_new (w, h);
  guint32 pal[16];
  int i, x, y;

  rng_seed (seed);
  for (i = 0; i < n && i < 16; i++)
    pal[i] = ARGB (255, rng () & 0xff, rng () & 0xff, rng () & 0xff);
  if (with_alpha && n >= 3)
    {
      pal[0] = 0;
      pal[1] = (pal[1] & 0x00ffffff) | 0x80000000;
    }
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
        if (x > 0 && rng () % 3 == 0)
          PX (im, x, y) = PX (im, x - 1, y);
        else if (y > 0 && rng () % 3 == 0)
          PX (im, x, y) = PX (im, x, y - 1);
        else
          PX (im, x, y) = pal[rng () % (guint32) MIN (n, 16)];
      }
  return im;
}

/* random pixels, every value possible (alpha 255 unless with_alpha) */
static Img
img_noise (int w, int h, guint32 seed, gboolean with_alpha)
{
  Img im = img_new (w, h);
  int i;

  rng_seed (seed);
  for (i = 0; i < w * h; i++)
    im.px[i] = with_alpha ? rng () : (rng () | 0xff000000u);
  return im;
}

/* colors near each other (differences around the hqx thresholds) */
static Img
img_near_colors (int w, int h, guint32 seed)
{
  Img im = img_new (w, h);
  int i;

  rng_seed (seed);
  for (i = 0; i < w * h; i++)
    {
      const int base = 100 + (int) (rng () % 3) * 20;

      im.px[i] = ARGB (255, base + (int) (rng () % 12), base + (int) (rng () % 12),
                       base + (int) (rng () % 12));
    }
  return im;
}

/* the image with random colors under its fully transparent pixels */
static Img
img_hide_colors (const Img *in, guint32 seed)
{
  Img im = img_new (in->w, in->h);
  gsize i;

  rng_seed (seed);
  for (i = 0; i < (gsize) in->w * in->h; i++)
    im.px[i] = (in->px[i] >> 24) ? in->px[i] : (rng () & 0x00ffffff);
  return im;
}

#endif
