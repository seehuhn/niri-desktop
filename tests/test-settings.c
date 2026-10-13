#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include "settings.h"

static int
load_with (const char *contents)
{
  g_autoptr (GError) err = NULL;
  g_autofree char *dir = g_dir_make_tmp ("niri-desktop-settings-XXXXXX", &err);
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
  g_assert_cmpint (settings_load_icon_size_from ("/nonexistent/niri-desktop/config.ini"), ==, 64);
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

static char *
make_tmp_dir (void)
{
  g_autoptr (GError) err = NULL;
  char *dir = g_dir_make_tmp ("niri-desktop-settings-XXXXXX", &err);
  g_assert_no_error (err);
  return dir;
}

static void
test_background_missing (void)
{
  g_autofree char *bg = settings_load_background_from ("/nonexistent/niri-desktop/config.ini");
  g_assert_null (bg);
}

static void
test_background_round_trip (void)
{
  g_autoptr (GError) err = NULL;
  g_autofree char *dir = make_tmp_dir ();
  g_autofree char *path = g_build_filename (dir, "config.ini", NULL);

  g_assert_true (settings_save_background_to (path, "/pics/a b.jpg", &err));
  g_assert_no_error (err);
  g_autofree char *bg = settings_load_background_from (path);
  g_assert_cmpstr (bg, ==, "/pics/a b.jpg");
  g_remove (path);
  g_rmdir (dir);
}

static void
test_background_keeps_other_keys (void)
{
  g_autoptr (GError) err = NULL;
  g_autofree char *dir = make_tmp_dir ();
  g_autofree char *path = g_build_filename (dir, "config.ini", NULL);

  g_file_set_contents (path, "# my comment\n[desktop]\nicon-size=48\n", -1, &err);
  g_assert_no_error (err);
  g_assert_true (settings_save_background_to (path, "/x.png", &err));
  g_assert_no_error (err);
  g_assert_cmpint (settings_load_icon_size_from (path), ==, 48);
  g_autofree char *bg = settings_load_background_from (path);
  g_assert_cmpstr (bg, ==, "/x.png");
  g_autofree char *contents = NULL;
  g_assert_true (g_file_get_contents (path, &contents, NULL, &err));
  g_assert_nonnull (strstr (contents, "# my comment"));
  g_remove (path);
  g_rmdir (dir);
}

static void
test_background_remove (void)
{
  g_autoptr (GError) err = NULL;
  g_autofree char *dir = make_tmp_dir ();
  g_autofree char *path = g_build_filename (dir, "config.ini", NULL);

  g_file_set_contents (path, "[desktop]\nicon-size=48\nbackground=/x.png\n", -1, &err);
  g_assert_no_error (err);
  g_assert_true (settings_save_background_to (path, NULL, &err));
  g_assert_no_error (err);
  g_autofree char *bg = settings_load_background_from (path);
  g_assert_null (bg);
  g_assert_cmpint (settings_load_icon_size_from (path), ==, 48);
  g_remove (path);
  g_rmdir (dir);
}

static void
test_background_creates_dir (void)
{
  g_autoptr (GError) err = NULL;
  g_autofree char *dir = make_tmp_dir ();
  g_autofree char *sub = g_build_filename (dir, "sub", "niri-desktop", NULL);
  g_autofree char *path = g_build_filename (sub, "config.ini", NULL);
  g_autofree char *sub1 = g_build_filename (dir, "sub", NULL);

  g_assert_true (settings_save_background_to (path, "/y.png", &err));
  g_assert_no_error (err);
  g_autofree char *bg = settings_load_background_from (path);
  g_assert_cmpstr (bg, ==, "/y.png");
  g_remove (path);
  g_rmdir (sub);
  g_rmdir (sub1);
  g_rmdir (dir);
}

static void
test_background_malformed_not_overwritten (void)
{
  g_autoptr (GError) err = NULL;
  g_autofree char *dir = make_tmp_dir ();
  g_autofree char *path = g_build_filename (dir, "config.ini", NULL);
  g_autofree char *contents = NULL;
  const char *bad = "[desktop\nicon-size=48";

  g_file_set_contents (path, bad, -1, &err);
  g_assert_no_error (err);
  g_assert_false (settings_save_background_to (path, "/x.png", &err));
  g_assert_nonnull (err);
  g_assert_true (g_file_get_contents (path, &contents, NULL, NULL));
  g_assert_cmpstr (contents, ==, bad);
  g_remove (path);
  g_rmdir (dir);
}

static void
test_background_empty_is_null (void)
{
  g_autoptr (GError) err = NULL;
  g_autofree char *dir = make_tmp_dir ();
  g_autofree char *path = g_build_filename (dir, "config.ini", NULL);

  g_file_set_contents (path, "[desktop]\nbackground=\n", -1, &err);
  g_assert_no_error (err);
  g_autofree char *bg = settings_load_background_from (path);
  g_assert_null (bg);
  g_remove (path);
  g_rmdir (dir);
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/settings/missing-file-default", test_missing_file_default);
  g_test_add_func ("/settings/reads-value", test_reads_value);
  g_test_add_func ("/settings/clamps", test_clamps);
  g_test_add_func ("/settings/pitch", test_pitch);
  g_test_add_func ("/settings/background-missing", test_background_missing);
  g_test_add_func ("/settings/background-round-trip", test_background_round_trip);
  g_test_add_func ("/settings/background-keeps-other-keys", test_background_keeps_other_keys);
  g_test_add_func ("/settings/background-remove", test_background_remove);
  g_test_add_func ("/settings/background-creates-dir", test_background_creates_dir);
  g_test_add_func ("/settings/background-malformed-not-overwritten", test_background_malformed_not_overwritten);
  g_test_add_func ("/settings/background-empty-is-null", test_background_empty_is_null);
  return g_test_run ();
}
