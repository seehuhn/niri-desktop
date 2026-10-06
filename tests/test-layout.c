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
  layout_arrange (it, 1, 500, 400);
  g_assert_cmpint (it[0].out_x, ==, 10);
  g_assert_cmpint (it[0].out_y, ==, 10);
  g_assert_false (it[0].parked);
}

static void
test_stored_overflow_parks (void)
{
  LayoutItem it[1] = { stored (450, 10) };
  layout_arrange (it, 1, 500, 400);
  g_assert_true (it[0].parked);
  g_assert_cmpint (it[0].out_x, ==, 388);
  g_assert_cmpint (it[0].out_y, ==, 0);
}

static void
test_new_fill (void)
{
  LayoutItem it[4] = { fresh (), fresh (), fresh (), fresh () };
  layout_arrange (it, 4, 500, 400);
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
  layout_arrange (it, 2, 500, 400);
  g_assert_false (it[0].parked);
  g_assert_cmpint (it[1].out_x, ==, 388);
  g_assert_cmpint (it[1].out_y, ==, 128);
}

static void
test_grows_downward (void)
{
  LayoutItem it[3] = { fresh (), fresh (), fresh () };
  int h = layout_arrange (it, 3, 120, 140);
  g_assert_cmpint (it[0].out_y, ==, 0);
  g_assert_cmpint (it[1].out_y, ==, 128);
  g_assert_cmpint (it[2].out_y, ==, 256);
  g_assert_cmpint (h, ==, 368);
}

static void
test_tiny_viewport (void)
{
  LayoutItem it[2] = { fresh (), fresh () };
  int h = layout_arrange (it, 2, 0, 0);
  g_assert_cmpint (it[0].out_x, ==, 0);
  g_assert_cmpint (it[0].out_y, ==, 0);
  g_assert_cmpint (it[1].out_x, ==, 0);
  g_assert_cmpint (it[1].out_y, ==, 128);
  g_assert_cmpint (h, ==, 240);

  LayoutItem jt[2] = { fresh (), fresh () };
  layout_arrange (jt, 2, 50, 50);
  g_assert_cmpint (jt[0].out_x, ==, 0);
  g_assert_cmpint (jt[1].out_x, ==, 0);
}

static void
test_overlapping_stored_kept (void)
{
  LayoutItem it[2] = { stored (10, 10), stored (10, 10) };
  layout_arrange (it, 2, 500, 400);
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
  layout_arrange (it, 1, 500, 400);
  g_assert_true (it[0].parked);
  layout_arrange (it, 1, 800, 400);
  g_assert_cmpint (it[0].out_x, ==, 450);
  g_assert_cmpint (it[0].out_y, ==, 10);
  g_assert_false (it[0].parked);
}

static void
test_deterministic (void)
{
  LayoutItem a[4] = { stored (450, 10), fresh (), stored (10, 10), fresh () };
  LayoutItem b[4] = { stored (450, 10), fresh (), stored (10, 10), fresh () };
  int ha = layout_arrange (a, 4, 500, 400);
  int hb = layout_arrange (b, 4, 500, 400);
  g_assert_cmpint (ha, ==, hb);
  for (int i = 0; i < 4; i++)
    {
      g_assert_cmpint (a[i].out_x, ==, b[i].out_x);
      g_assert_cmpint (a[i].out_y, ==, b[i].out_y);
      g_assert_cmpint (a[i].parked, ==, b[i].parked);
    }
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/layout/stored-fits-stays", test_stored_fits_stays);
  g_test_add_func ("/layout/stored-overflow-parks", test_stored_overflow_parks);
  g_test_add_func ("/layout/new-fill-top-right-then-down", test_new_fill);
  g_test_add_func ("/layout/park-skips-occupied", test_park_skips_occupied);
  g_test_add_func ("/layout/grows-downward", test_grows_downward);
  g_test_add_func ("/layout/tiny-viewport", test_tiny_viewport);
  g_test_add_func ("/layout/overlapping-stored-kept", test_overlapping_stored_kept);
  g_test_add_func ("/layout/round-trip-restores", test_round_trip_restores);
  g_test_add_func ("/layout/deterministic", test_deterministic);
  return g_test_run ();
}
