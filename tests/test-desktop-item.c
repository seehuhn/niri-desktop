/* SPDX-FileCopyrightText: 2026 Jochen Voss <voss@seehuhn.de>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gio/gio.h>
#include <glib/gstdio.h>
#include "desktop-item.h"

static gboolean
on_timeout (gpointer data)
{
  *(gboolean *) data = TRUE;
  return G_SOURCE_REMOVE;
}

static void
run_until (gboolean *done)
{
  gboolean timed_out = FALSE;
  guint id = g_timeout_add_seconds (5, on_timeout, &timed_out);

  while (!*done && !timed_out)
    g_main_context_iteration (NULL, TRUE);
  if (!timed_out)
    g_source_remove (id);
  g_assert_true (*done);
}

typedef struct {
  gboolean  done;
  GError   *error;
  gboolean  ok;
} ReloadResult;

static void
on_reloaded (GObject *source, GAsyncResult *res, gpointer data)
{
  ReloadResult *r = data;
  r->ok = desktop_item_reload_finish (DESKTOP_ITEM (source), res, &r->error);
  r->done = TRUE;
}

static gboolean
reload (DesktopItem *item, GError **error)
{
  ReloadResult r = { 0 };
  desktop_item_reload_async (item, NULL, on_reloaded, &r);
  run_until (&r.done);
  if (error)
    *error = r.error;
  else
    g_clear_error (&r.error);
  return r.ok;
}

static char *
make_dir (void)
{
  GError *error = NULL;
  char *dir = g_dir_make_tmp ("niri-desktop-item-XXXXXX", &error);
  g_assert_no_error (error);
  return dir;
}

static void
count_notify (GObject *obj G_GNUC_UNUSED, GParamSpec *pspec G_GNUC_UNUSED, gpointer data)
{
  (*(int *) data)++;
}

static void
test_name_before_reload (void)
{
  g_autoptr (GFile) file = g_file_new_for_path ("/tmp/some-name.txt");
  g_autoptr (DesktopItem) item = desktop_item_new (file);

  g_assert_cmpstr (desktop_item_get_name (item), ==, "some-name.txt");
  g_assert_null (desktop_item_get_display_name (item));
}

static void
test_reload_fills_info (void)
{
  g_autofree char *dir = make_dir ();
  g_autofree char *path = g_build_filename (dir, "note.txt", NULL);
  g_file_set_contents (path, "hello", -1, NULL);
  g_autoptr (GFile) file = g_file_new_for_path (path);
  g_autoptr (DesktopItem) item = desktop_item_new (file);
  int n = 0;
  g_signal_connect (item, "notify::info", G_CALLBACK (count_notify), &n);

  g_autoptr (GError) error = NULL;
  g_assert_true (reload (item, &error));
  g_assert_no_error (error);

  g_assert_cmpstr (desktop_item_get_display_name (item), ==, "note.txt");
  g_assert_cmpstr (desktop_item_get_content_type (item), ==, "text/plain");
  g_assert_cmpint (desktop_item_get_size (item), ==, 5);
  g_assert_nonnull (desktop_item_get_modified (item));
  g_assert_nonnull (desktop_item_get_icon (item));
  g_assert_false (desktop_item_is_directory (item));
  g_assert_cmpint (n, ==, 1);

  g_remove (path);
  g_rmdir (dir);
}

static void
test_directory (void)
{
  g_autofree char *dir = make_dir ();
  g_autofree char *sub = g_build_filename (dir, "sub", NULL);
  g_assert_cmpint (g_mkdir (sub, 0755), ==, 0);
  g_autoptr (GFile) file = g_file_new_for_path (sub);
  g_autoptr (DesktopItem) item = desktop_item_new (file);

  g_assert_true (reload (item, NULL));
  g_assert_true (desktop_item_is_directory (item));

  g_rmdir (sub);
  g_rmdir (dir);
}

static void
test_position (void)
{
  g_autoptr (GFile) file = g_file_new_for_path ("/tmp/p");
  g_autoptr (DesktopItem) item = desktop_item_new (file);
  int n = 0, x = 0, y = 0;
  g_signal_connect (item, "notify::position", G_CALLBACK (count_notify), &n);

  g_assert_false (desktop_item_get_position (item, &x, &y));
  desktop_item_set_position (item, 1, 2);
  g_assert_true (desktop_item_get_position (item, &x, &y));
  g_assert_cmpint (x, ==, 1);
  g_assert_cmpint (y, ==, 2);
  desktop_item_clear_position (item);
  g_assert_false (desktop_item_get_position (item, &x, &y));
  g_assert_cmpint (n, ==, 2);
}

static void
test_reload_missing (void)
{
  g_autofree char *dir = make_dir ();
  g_autofree char *path = g_build_filename (dir, "missing", NULL);
  g_autoptr (GFile) file = g_file_new_for_path (path);
  g_autoptr (DesktopItem) item = desktop_item_new (file);
  g_autoptr (GError) error = NULL;

  g_assert_false (reload (item, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);

  g_rmdir (dir);
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/item/name-before-reload", test_name_before_reload);
  g_test_add_func ("/item/reload-fills-info", test_reload_fills_info);
  g_test_add_func ("/item/directory", test_directory);
  g_test_add_func ("/item/position", test_position);
  g_test_add_func ("/item/reload-missing-file-errors", test_reload_missing);
  return g_test_run ();
}
