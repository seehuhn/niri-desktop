#include <glib.h>
#include <glib/gstdio.h>
#include "settings.h"

static int
load_with (const char *contents)
{
  g_autoptr (GError) err = NULL;
  g_autofree char *dir = g_dir_make_tmp ("lnxdesktop-settings-XXXXXX", &err);
  g_assert_no_error (err);
  g_autofree char *path = g_build_filename (dir, "config.ini", NULL);
  g_file_set_contents (path, contents, -1, &err);
  g_assert_no_error (err);
  int v = settings_load_icon_size_from (path);
  g_remove (path);
  g_rmdir (dir);
  return v;
}

static void
test_missing_file_default (void)
{
  g_assert_cmpint (settings_load_icon_size_from ("/nonexistent/lnxdesktop/config.ini"), ==, 64);
}

static void
test_reads_value (void)
{
  g_assert_cmpint (load_with ("[desktop]\nicon-size=48\n"), ==, 48);
}

static void
test_clamps (void)
{
  g_assert_cmpint (load_with ("[desktop]\nicon-size=500\n"), ==, 128);
  g_assert_cmpint (load_with ("[desktop]\nicon-size=4\n"), ==, 32);
  g_assert_cmpint (load_with ("[desktop]\nicon-size=99999999999\n"), ==, 128);
  g_assert_cmpint (load_with ("[desktop]\nicon-size=abc\n"), ==, 64);
  g_assert_cmpint (load_with ("[other]\nicon-size=48\n"), ==, 64);
  g_assert_cmpint (load_with ("[desktop]\nfoo=1\n"), ==, 64);
}

static void
test_pitch (void)
{
  g_assert_cmpint (settings_pitch_width (64), ==, 80);
  g_assert_cmpint (settings_pitch_height (64), ==, 112);
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/settings/missing-file-default", test_missing_file_default);
  g_test_add_func ("/settings/reads-value", test_reads_value);
  g_test_add_func ("/settings/clamps", test_clamps);
  g_test_add_func ("/settings/pitch", test_pitch);
  return g_test_run ();
}
