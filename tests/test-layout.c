/* SPDX-FileCopyrightText: 2026 Jochen Voss <voss@seehuhn.de>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <glib.h>
#include "layout.h"

static LayoutItem
stored (int x, int y)
{
  LayoutItem it = { .has_position = TRUE, .x = x, .y = y, .width = 96, .height = 112 };
  return it;
}

static LayoutItem
fresh (void)
{
  LayoutItem it = { .width = 96, .height = 112 };
  return it;
}

static void
test_stored_fits_stays (void)
{
  LayoutItem it[1] = { stored (10, 10) };
  layout_arrange (it, 1, 500, 400, 112, 128);
  g_assert_cmpint (it[0].out_x, ==, 10);
  g_assert_cmpint (it[0].out_y, ==, 10);
  g_assert_false (it[0].parked);
}

static void
test_stored_overflow_parks (void)
{
  LayoutItem it[1] = { stored (450, 10) };
  layout_arrange (it, 1, 500, 400, 112, 128);
  g_assert_true (it[0].parked);
  g_assert_cmpint (it[0].out_x, ==, 388);
  g_assert_cmpint (it[0].out_y, ==, 0);
}

static void
test_new_fill (void)
{
  LayoutItem it[4] = { fresh (), fresh (), fresh (), fresh () };
  layout_arrange (it, 4, 500, 400, 112, 128);
  g_assert_cmpint (it[0].out_x, ==, 388);
  g_assert_cmpint (it[0].out_y, ==, 0);
  g_assert_cmpint (it[1].out_x, ==, 388);
  g_assert_cmpint (it[1].out_y, ==, 128);
  g_assert_cmpint (it[2].out_x, ==, 388);
  g_assert_cmpint (it[2].out_y, ==, 256);
  g_assert_cmpint (it[3].out_x, ==, 276);
  g_assert_cmpint (it[3].out_y, ==, 0);
  g_assert_true (it[3].parked);
}

static void
test_park_skips_occupied (void)
{
  LayoutItem it[2] = { stored (390, 0), fresh () };
  layout_arrange (it, 2, 500, 400, 112, 128);
  g_assert_false (it[0].parked);
  g_assert_cmpint (it[1].out_x, ==, 388);
  g_assert_cmpint (it[1].out_y, ==, 128);
}

static void
test_new_after_displaced (void)
{
  LayoutItem it[2] = { fresh (), stored (450, 10) };
  layout_arrange (it, 2, 500, 400, 112, 128);
  g_assert_cmpint (it[1].out_x, ==, 388);
  g_assert_cmpint (it[1].out_y, ==, 0);
  g_assert_cmpint (it[0].out_x, ==, 388);
  g_assert_cmpint (it[0].out_y, ==, 128);
}

static void
test_grows_downward (void)
{
  LayoutItem it[3] = { fresh (), fresh (), fresh () };
  int h = layout_arrange (it, 3, 120, 140, 112, 128);
  g_assert_cmpint (it[0].out_y, ==, 0);
  g_assert_cmpint (it[1].out_y, ==, 128);
  g_assert_cmpint (it[2].out_y, ==, 256);
  g_assert_cmpint (h, ==, 368);
}

static void
test_tiny_viewport (void)
{
  LayoutItem it[2] = { fresh (), fresh () };
  int h = layout_arrange (it, 2, 0, 0, 112, 128);
  g_assert_cmpint (it[0].out_x, ==, 0);
  g_assert_cmpint (it[0].out_y, ==, 0);
  g_assert_cmpint (it[1].out_x, ==, 0);
  g_assert_cmpint (it[1].out_y, ==, 128);
  g_assert_cmpint (h, ==, 240);

  LayoutItem jt[2] = { fresh (), fresh () };
  layout_arrange (jt, 2, 50, 50, 112, 128);
  g_assert_cmpint (jt[0].out_x, ==, 0);
  g_assert_cmpint (jt[1].out_x, ==, 0);
}

static void
test_overlapping_stored_kept (void)
{
  LayoutItem it[2] = { stored (10, 10), stored (10, 10) };
  layout_arrange (it, 2, 500, 400, 112, 128);
  for (int i = 0; i < 2; i++)
    {
      g_assert_cmpint (it[i].out_x, ==, 10);
      g_assert_cmpint (it[i].out_y, ==, 10);
      g_assert_false (it[i].parked);
    }
}

static void
test_round_trip_restores (void)
{
  LayoutItem it[1] = { stored (450, 10) };
  layout_arrange (it, 1, 500, 400, 112, 128);
  g_assert_true (it[0].parked);
  layout_arrange (it, 1, 800, 400, 112, 128);
  g_assert_cmpint (it[0].out_x, ==, 450);
  g_assert_cmpint (it[0].out_y, ==, 10);
  g_assert_false (it[0].parked);
}

static void
test_deterministic (void)
{
  LayoutItem a[4] = { stored (450, 10), fresh (), stored (10, 10), fresh () };
  LayoutItem b[4] = { stored (450, 10), fresh (), stored (10, 10), fresh () };
  int ha = layout_arrange (a, 4, 500, 400, 112, 128);
  int hb = layout_arrange (b, 4, 500, 400, 112, 128);
  g_assert_cmpint (ha, ==, hb);
  for (int i = 0; i < 4; i++)
    {
      g_assert_cmpint (a[i].out_x, ==, b[i].out_x);
      g_assert_cmpint (a[i].out_y, ==, b[i].out_y);
      g_assert_cmpint (a[i].parked, ==, b[i].parked);
    }
}

static void
test_stored_touching_edge_fits (void)
{
  LayoutItem it[1] = { stored (500 - 96, 400 - 112) };
  layout_arrange (it, 1, 500, 400, 112, 128);
  g_assert_false (it[0].parked);
  g_assert_cmpint (it[0].out_x, ==, 404);
  g_assert_cmpint (it[0].out_y, ==, 288);
}

static void
test_clamp_group_inside (void)
{
  int x = 50, y = 60;
  layout_clamp_group (&x, &y, 112, 256, 500, 400);
  g_assert_cmpint (x, ==, 50);
  g_assert_cmpint (y, ==, 60);
  x = -20;
  y = -5;
  layout_clamp_group (&x, &y, 112, 256, 500, 400);
  g_assert_cmpint (x, ==, 0);
  g_assert_cmpint (y, ==, 0);
}

static void
test_clamp_group_right_bottom (void)
{
  int x = 450, y = 390;
  layout_clamp_group (&x, &y, 112, 256, 500, 400);
  g_assert_cmpint (x, ==, 388);
  g_assert_cmpint (y, ==, 144);
}

static void
test_clamp_group_too_small_viewport (void)
{
  int x = 30, y = 40;
  layout_clamp_group (&x, &y, 112, 256, 100, 200);
  g_assert_cmpint (x, ==, 0);
  g_assert_cmpint (y, ==, 0);
}

static void
test_cover_wide_image (void)
{
  double x, y, w, h;
  layout_cover_rect (600, 800, 3000, 1000, &x, &y, &w, &h);
  g_assert_cmpfloat_with_epsilon (h, 800, 1e-9);
  g_assert_cmpfloat_with_epsilon (w, 2400, 1e-9);
  g_assert_cmpfloat_with_epsilon (x, -900, 1e-9);
  g_assert_cmpfloat_with_epsilon (y, 0, 1e-9);
}

static void
test_cover_tall_image (void)
{
  double x, y, w, h;
  layout_cover_rect (800, 600, 1000, 3000, &x, &y, &w, &h);
  g_assert_cmpfloat_with_epsilon (w, 800, 1e-9);
  g_assert_cmpfloat_with_epsilon (h, 2400, 1e-9);
  g_assert_cmpfloat_with_epsilon (x, 0, 1e-9);
  g_assert_cmpfloat_with_epsilon (y, -900, 1e-9);
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/layout/stored-fits-stays", test_stored_fits_stays);
  g_test_add_func ("/layout/stored-overflow-parks", test_stored_overflow_parks);
  g_test_add_func ("/layout/new-fill-top-right-then-down", test_new_fill);
  g_test_add_func ("/layout/park-skips-occupied", test_park_skips_occupied);
  g_test_add_func ("/layout/new-after-displaced", test_new_after_displaced);
  g_test_add_func ("/layout/grows-downward", test_grows_downward);
  g_test_add_func ("/layout/tiny-viewport", test_tiny_viewport);
  g_test_add_func ("/layout/overlapping-stored-kept", test_overlapping_stored_kept);
  g_test_add_func ("/layout/round-trip-restores", test_round_trip_restores);
  g_test_add_func ("/layout/deterministic", test_deterministic);
  g_test_add_func ("/layout/stored-touching-edge-fits", test_stored_touching_edge_fits);
  g_test_add_func ("/layout/clamp-group-inside", test_clamp_group_inside);
  g_test_add_func ("/layout/clamp-group-right-bottom", test_clamp_group_right_bottom);
  g_test_add_func ("/layout/clamp-group-too-small-viewport", test_clamp_group_too_small_viewport);
  g_test_add_func ("/layout/cover-wide-image", test_cover_wide_image);
  g_test_add_func ("/layout/cover-tall-image", test_cover_tall_image);
  return g_test_run ();
}
