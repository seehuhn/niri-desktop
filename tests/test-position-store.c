#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include "position-store.h"

typedef struct {
  char *dir;
  char *path;
} Fixture;

static void
fixture_set_up (Fixture *f, gconstpointer data)
{
  (void) data;
  f->dir = g_dir_make_tmp ("lnxdesktop-test-XXXXXX", NULL);
  g_assert_nonnull (f->dir);
  f->path = g_build_filename (f->dir, "positions.ini", NULL);
}

static void
fixture_tear_down (Fixture *f, gconstpointer data)
{
  (void) data;
  g_autofree char *bak = g_strconcat (f->path, ".bak", NULL);
  g_remove (f->path);
  g_remove (bak);
  g_rmdir (f->dir);
  g_free (f->path);
  g_free (f->dir);
}

static void
test_missing (Fixture *f, gconstpointer data)
{
  (void) data;
  int x, y;
  PositionStore *s = position_store_new (f->path);
  g_assert_false (position_store_lookup (s, "a.txt", &x, &y));
  position_store_free (s);
}

static void
test_round_trip (Fixture *f, gconstpointer data)
{
  (void) data;
  int x = 0, y = 0;
  PositionStore *s = position_store_new (f->path);
  position_store_set (s, "a.txt", 10, 20);
  position_store_flush (s);
  position_store_free (s);
  s = position_store_new (f->path);
  g_assert_true (position_store_lookup (s, "a.txt", &x, &y));
  g_assert_cmpint (x, ==, 10);
  g_assert_cmpint (y, ==, 20);
  position_store_free (s);
}

static void
test_weird_names (Fixture *f, gconstpointer data)
{
  (void) data;
  int x = 0, y = 0;
  PositionStore *s = position_store_new (f->path);
  position_store_set (s, "weird [1] \xc3\xbc.txt", 3, 4);
  position_store_set (s, "a=b", 5, 6);
  position_store_flush (s);
  position_store_free (s);

  g_autofree char *contents = NULL;
  g_assert_true (g_file_get_contents (f->path, &contents, NULL, NULL));
  g_assert_null (strstr (contents, "[weird [1]"));
  g_assert_null (strstr (contents, "[1]"));

  s = position_store_new (f->path);
  g_assert_true (position_store_lookup (s, "weird [1] \xc3\xbc.txt", &x, &y));
  g_assert_cmpint (x, ==, 3);
  g_assert_cmpint (y, ==, 4);
  g_assert_true (position_store_lookup (s, "a=b", &x, &y));
  g_assert_cmpint (x, ==, 5);
  g_assert_cmpint (y, ==, 6);
  position_store_free (s);
}

static void
test_rename (Fixture *f, gconstpointer data)
{
  (void) data;
  int x = 0, y = 0;
  PositionStore *s = position_store_new (f->path);
  position_store_set (s, "a", 1, 2);
  position_store_rename (s, "a", "b");
  g_assert_true (position_store_lookup (s, "b", &x, &y));
  g_assert_cmpint (x, ==, 1);
  g_assert_cmpint (y, ==, 2);
  g_assert_false (position_store_lookup (s, "a", &x, &y));
  position_store_rename (s, "unknown", "c");
  g_assert_false (position_store_lookup (s, "unknown", &x, &y));
  g_assert_false (position_store_lookup (s, "c", &x, &y));
  position_store_free (s);
}

static void
test_remove (Fixture *f, gconstpointer data)
{
  (void) data;
  int x, y;
  PositionStore *s = position_store_new (f->path);
  position_store_set (s, "a", 1, 2);
  position_store_remove (s, "a");
  position_store_flush (s);
  position_store_free (s);
  s = position_store_new (f->path);
  g_assert_false (position_store_lookup (s, "a", &x, &y));
  position_store_free (s);
}

static void
test_corrupt (Fixture *f, gconstpointer data)
{
  (void) data;
  int x, y;
  const char *junk = "not a keyfile\n[\n";
  g_assert_true (g_file_set_contents (f->path, junk, -1, NULL));
  PositionStore *s = position_store_new (f->path);
  g_assert_false (position_store_lookup (s, "anything", &x, &y));
  position_store_free (s);

  g_autofree char *bak = g_strconcat (f->path, ".bak", NULL);
  g_autofree char *contents = NULL;
  g_assert_true (g_file_get_contents (bak, &contents, NULL, NULL));
  g_assert_cmpstr (contents, ==, junk);
}

static void
test_free_flushes (Fixture *f, gconstpointer data)
{
  (void) data;
  int x = 0, y = 0;
  PositionStore *s = position_store_new (f->path);
  position_store_set (s, "a", 7, 8);
  position_store_free (s);
  s = position_store_new (f->path);
  g_assert_true (position_store_lookup (s, "a", &x, &y));
  g_assert_cmpint (x, ==, 7);
  g_assert_cmpint (y, ==, 8);
  position_store_free (s);
}

static void
test_anchor (Fixture *f, gconstpointer data)
{
  (void) data;
  PositionStore *s = position_store_new (f->path);
  position_store_set (s, "a", 1, 2);
  position_store_flush (s);
  g_autofree char *contents = NULL;
  g_assert_true (g_file_get_contents (f->path, &contents, NULL, NULL));
  g_assert_nonnull (strstr (contents, "anchor=left"));
  position_store_free (s);
}

#define T(name, fn) g_test_add ("/store/" name, Fixture, NULL, fixture_set_up, fn, fixture_tear_down)

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  T ("missing-file-is-empty", test_missing);
  T ("round-trip", test_round_trip);
  T ("weird-names", test_weird_names);
  T ("rename", test_rename);
  T ("remove", test_remove);
  T ("corrupt-renamed-to-bak", test_corrupt);
  T ("free-flushes", test_free_flushes);
  T ("anchor-key-written", test_anchor);
  return g_test_run ();
}
