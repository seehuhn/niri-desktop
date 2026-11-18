/* SPDX-FileCopyrightText: 2026 Jochen Voss <voss@seehuhn.de>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <glib.h>
#include "luminance.h"

/* A width x height RGBA image, black left of `split` and white from it. */
static guint8 *
split_image (int width, int height, int split)
{
  guint8 *pixels = g_new (guint8, (gsize) width * height * 4);
  int x, y;

  for (y = 0; y < height; y++)
    for (x = 0; x < width; x++)
      {
        guint8 *p = pixels + (y * width + x) * 4;

        p[0] = p[1] = p[2] = x < split ? 0 : 255;
        p[3] = 255;
      }
  return pixels;
}

static void
test_halves (void)
{
  g_autofree guint8 *pixels = split_image (1024, 512, 512);
  g_autoptr (LuminanceMap) map = luminance_map_new (pixels, 1024, 512, 1024 * 4);
  double mean;

  g_assert_true (luminance_map_mean (map, 10, 10, 100, 30, &mean));
  g_assert_cmpfloat_with_epsilon (mean, 0.0, 1e-9);
  g_assert_true (luminance_map_mean (map, 700, 400, 100, 30, &mean));
  g_assert_cmpfloat_with_epsilon (mean, 1.0, 1e-9);
  g_assert_true (luminance_map_mean (map, 0, 0, 1024, 512, &mean));
  g_assert_cmpfloat_with_epsilon (mean, 0.5, 1e-9);
}

static void
test_mid_grey (void)
{
  guint8 pixels[4 * 4 * 4];
  double mean;
  int i;

  for (i = 0; i < 16; i++)
    {
      pixels[i * 4] = pixels[i * 4 + 1] = pixels[i * 4 + 2] = 128;
      pixels[i * 4 + 3] = 255;
    }
  g_autoptr (LuminanceMap) map = luminance_map_new (pixels, 4, 4, 16);
  g_assert_true (luminance_map_mean (map, 1, 1, 2, 2, &mean));
  /* sRGB 128 is about 21.6% linear luminance. */
  g_assert_cmpfloat_with_epsilon (mean, 0.2159, 1e-3);
}

static void
test_clipping (void)
{
  g_autofree guint8 *pixels = split_image (300, 100, 150);
  g_autoptr (LuminanceMap) map = luminance_map_new (pixels, 300, 100, 300 * 4);
  double mean;

  g_assert_false (luminance_map_mean (map, -50, 10, 40, 10, &mean));
  g_assert_false (luminance_map_mean (map, 310, 10, 40, 10, &mean));
  g_assert_false (luminance_map_mean (map, 10, 100, 40, 10, &mean));
  /* Partly outside: only the white part inside counts. */
  g_assert_true (luminance_map_mean (map, 250, 50, 100, 100, &mean));
  g_assert_cmpfloat_with_epsilon (mean, 1.0, 1e-9);
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/luminance/halves", test_halves);
  g_test_add_func ("/luminance/mid-grey", test_mid_grey);
  g_test_add_func ("/luminance/clipping", test_clipping);
  return g_test_run ();
}
