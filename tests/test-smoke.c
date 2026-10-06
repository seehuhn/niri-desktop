#include <glib.h>

static void
test_glib (void)
{
  g_assert_null (glib_check_version (2, 80, 0));
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/smoke/glib", test_glib);
  return g_test_run ();
}
