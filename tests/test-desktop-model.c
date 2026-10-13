#include <gio/gio.h>
#include <glib/gstdio.h>
#include "desktop-model.h"

typedef struct {
  char          *dir;
  char          *store_path;
  GFile         *dir_file;
  DesktopModel  *model;
} Fixture;

static gboolean
on_timeout (gpointer data)
{
  *(gboolean *) data = TRUE;
  return G_SOURCE_REMOVE;
}

static void
touch (Fixture *f, const char *name, const char *content)
{
  g_autofree char *path = g_build_filename (f->dir, name, NULL);
  GError *error = NULL;
  g_file_set_contents (path, content, -1, &error);
  g_assert_no_error (error);
}

static void
wait_for_count (GListModel *m, guint n)
{
  gboolean timed_out = FALSE;
  guint id = g_timeout_add_seconds (5, on_timeout, &timed_out);

  while (g_list_model_get_n_items (m) != n && !timed_out)
    g_main_context_iteration (NULL, TRUE);
  if (!timed_out)
    g_source_remove (id);
  g_assert_cmpuint (g_list_model_get_n_items (m), ==, n);
}

static void
wait_for_item (DesktopModel *m, const char *name)
{
  gboolean timed_out = FALSE;
  guint id = g_timeout_add_seconds (5, on_timeout, &timed_out);

  while (!desktop_model_lookup (m, name) && !timed_out)
    g_main_context_iteration (NULL, TRUE);
  if (!timed_out)
    g_source_remove (id);
  g_assert_nonnull (desktop_model_lookup (m, name));
}

static void
on_loaded (GObject *source, GAsyncResult *res, gpointer data)
{
  GError *error = NULL;
  g_assert_true (desktop_model_load_finish (DESKTOP_MODEL (source), res, &error));
  g_assert_no_error (error);
  *(gboolean *) data = TRUE;
}

static void
load (DesktopModel *model)
{
  gboolean done = FALSE;
  gboolean timed_out = FALSE;
  guint id = g_timeout_add_seconds (5, on_timeout, &timed_out);

  desktop_model_load_async (model, NULL, on_loaded, &done);
  while (!done && !timed_out)
    g_main_context_iteration (NULL, TRUE);
  if (!timed_out)
    g_source_remove (id);
  g_assert_true (done);
}

static void
fixture_setup (Fixture *f, const char *const *files, const char *pos_name)
{
  GError *error = NULL;
  PositionStore *store;

  f->dir = g_dir_make_tmp ("niri-desktop-model-XXXXXX", &error);
  g_assert_no_error (error);
  f->store_path = g_build_filename (f->dir, "positions.ini", NULL);
  f->dir_file = g_file_new_build_filename (f->dir, "desktop", NULL);
  g_file_make_directory (f->dir_file, NULL, &error);
  g_assert_no_error (error);
  g_free (f->dir);
  f->dir = g_file_get_path (f->dir_file);
  for (; files && *files; files++)
    touch (f, *files, "x");
  store = position_store_new (f->store_path);
  if (pos_name)
    position_store_set (store, pos_name, 7, 8);
  f->model = desktop_model_new (f->dir_file, store);
}

static void
fixture_teardown (Fixture *f)
{
  g_autofree char *parent = g_path_get_dirname (f->dir);
  g_clear_object (&f->model);
  g_autoptr (GFile) pf = g_file_new_for_path (parent);
  g_autoptr (GFileEnumerator) en = g_file_enumerate_children (pf, "standard::name", 0, NULL, NULL);
  GFileInfo *info;
  /* Remove contents of the desktop dir and the parent (shallow, test files only). */
  {
    g_autoptr (GFileEnumerator) en2 = g_file_enumerate_children (f->dir_file, "standard::name", 0, NULL, NULL);
    while (en2 && (info = g_file_enumerator_next_file (en2, NULL, NULL)))
      {
        g_autoptr (GFile) c = g_file_get_child (f->dir_file, g_file_info_get_name (info));
        g_file_delete (c, NULL, NULL);
        g_object_unref (info);
      }
  }
  g_file_delete (f->dir_file, NULL, NULL);
  while (en && (info = g_file_enumerator_next_file (en, NULL, NULL)))
    {
      g_autoptr (GFile) c = g_file_get_child (pf, g_file_info_get_name (info));
      g_file_delete (c, NULL, NULL);
      g_object_unref (info);
    }
  g_file_delete (pf, NULL, NULL);
  g_free (f->dir);
  g_free (f->store_path);
  g_clear_object (&f->dir_file);
}

static const char *
name_at (GListModel *m, guint i)
{
  g_autoptr (DesktopItem) item = g_list_model_get_item (m, i);
  return desktop_item_get_name (item);
}

static void
test_load_sorted_skips_hidden (void)
{
  Fixture f = { 0 };
  const char *files[] = { "b.txt", "a.txt", ".hidden", NULL };
  GListModel *items;

  fixture_setup (&f, files, NULL);
  items = desktop_model_get_items (f.model);
  load (f.model);
  g_assert_cmpuint (g_list_model_get_n_items (items), ==, 2);
  g_assert_cmpstr (name_at (items, 0), ==, "a.txt");
  g_assert_cmpstr (name_at (items, 1), ==, "b.txt");
  for (guint i = 0; i < 2; i++)
    {
      g_autoptr (DesktopItem) item = g_list_model_get_item (items, i);
      g_assert_nonnull (desktop_item_get_content_type (item));
    }
  fixture_teardown (&f);
}

static void
test_positions_applied_on_load (void)
{
  Fixture f = { 0 };
  const char *files[] = { "a.txt", "b.txt", NULL };
  int x = 0, y = 0;

  fixture_setup (&f, files, "a.txt");
  load (f.model);
  g_assert_true (desktop_item_get_position (desktop_model_lookup (f.model, "a.txt"), &x, &y));
  g_assert_cmpint (x, ==, 7);
  g_assert_cmpint (y, ==, 8);
  g_assert_false (desktop_item_get_position (desktop_model_lookup (f.model, "b.txt"), &x, &y));
  fixture_teardown (&f);
}

static void
test_create_adds (void)
{
  Fixture f = { 0 };
  const char *files[] = { "a.txt", "b.txt", NULL };

  fixture_setup (&f, files, NULL);
  load (f.model);
  touch (&f, "c.txt", "x");
  wait_for_item (f.model, "c.txt");
  wait_for_count (desktop_model_get_items (f.model), 3);
  g_assert_nonnull (desktop_model_lookup (f.model, "c.txt"));
  fixture_teardown (&f);
}

static void
test_delete_removes_and_forgets_position (void)
{
  Fixture f = { 0 };
  const char *files[] = { "a.txt", "b.txt", NULL };
  DesktopItem *a;
  g_autoptr (GFile) file = NULL;

  fixture_setup (&f, files, NULL);
  load (f.model);
  a = desktop_model_lookup (f.model, "a.txt");
  desktop_model_set_position (f.model, a, 3, 4);
  file = g_file_get_child (f.dir_file, "a.txt");
  g_file_delete (file, NULL, NULL);
  wait_for_count (desktop_model_get_items (f.model), 1);
  g_assert_null (desktop_model_lookup (f.model, "a.txt"));
  /* Freeing the model flushes the store; reopen it to inspect. */
  g_clear_object (&f.model);
  {
    PositionStore *s = position_store_new (f.store_path);
    g_assert_false (position_store_lookup (s, "a.txt", NULL, NULL));
    position_store_free (s);
  }
  fixture_teardown (&f);
}

static void
test_rename_keeps_position (void)
{
  Fixture f = { 0 };
  const char *files[] = { "a.txt", "b.txt", NULL };
  GListModel *items;
  g_autoptr (GFile) a = NULL;
  g_autoptr (GFile) b = NULL;
  g_autoptr (GFile) r = NULL;
  DesktopItem *z;
  int x = 0, y = 0;
  gboolean timed_out = FALSE;
  guint id;

  fixture_setup (&f, files, "a.txt");
  items = desktop_model_get_items (f.model);
  load (f.model);
  a = g_file_get_child (f.dir_file, "a.txt");
  r = g_file_set_display_name (a, "z.txt", NULL, NULL);
  g_assert_nonnull (r);
  id = g_timeout_add_seconds (5, on_timeout, &timed_out);
  while (!desktop_model_lookup (f.model, "z.txt") && !timed_out)
    g_main_context_iteration (NULL, TRUE);
  if (!timed_out)
    g_source_remove (id);
  z = desktop_model_lookup (f.model, "z.txt");
  g_assert_nonnull (z);
  g_assert_null (desktop_model_lookup (f.model, "a.txt"));
  g_assert_cmpuint (g_list_model_get_n_items (items), ==, 2);
  g_assert_cmpstr (name_at (items, 1), ==, "z.txt");
  g_assert_true (desktop_item_get_position (z, &x, &y));
  g_assert_cmpint (x, ==, 7);
  g_assert_cmpint (y, ==, 8);

  /* Renaming an item without position creates no store entry. */
  b = g_file_get_child (f.dir_file, "b.txt");
  {
    GFile *yf = g_file_set_display_name (b, "y.txt", NULL, NULL);
    g_assert_nonnull (yf);
    g_object_unref (yf);
  }
  timed_out = FALSE;
  id = g_timeout_add_seconds (5, on_timeout, &timed_out);
  while (!desktop_model_lookup (f.model, "y.txt") && !timed_out)
    g_main_context_iteration (NULL, TRUE);
  if (!timed_out)
    g_source_remove (id);
  g_assert_nonnull (desktop_model_lookup (f.model, "y.txt"));
  g_assert_false (desktop_item_get_position (desktop_model_lookup (f.model, "y.txt"), &x, &y));
  g_clear_object (&f.model);
  {
    PositionStore *s = position_store_new (f.store_path);
    g_assert_false (position_store_lookup (s, "y.txt", NULL, NULL));
    g_assert_false (position_store_lookup (s, "a.txt", NULL, NULL));
    g_assert_true (position_store_lookup (s, "z.txt", &x, &y));
    position_store_free (s);
  }
  fixture_teardown (&f);
}

static void
test_change_reloads (void)
{
  Fixture f = { 0 };
  const char *files[] = { "a.txt", NULL };
  DesktopItem *a;
  g_autofree char *path = NULL;
  gboolean timed_out = FALSE;
  guint id;

  fixture_setup (&f, files, NULL);
  load (f.model);
  a = desktop_model_lookup (f.model, "a.txt");
  g_assert_cmpint (desktop_item_get_size (a), ==, 1);
  path = g_build_filename (f.dir, "a.txt", NULL);
  {
    FILE *fp = g_fopen (path, "a");
    g_assert_nonnull (fp);
    fputs ("more text", fp);
    fclose (fp);
  }
  id = g_timeout_add_seconds (5, on_timeout, &timed_out);
  while (desktop_item_get_size (a) != 10 && !timed_out)
    g_main_context_iteration (NULL, TRUE);
  if (!timed_out)
    g_source_remove (id);
  g_assert_cmpint (desktop_item_get_size (a), ==, 10);
  fixture_teardown (&f);
}

static void
wait_for_size (DesktopItem *item, goffset size)
{
  gboolean timed_out = FALSE;
  guint id = g_timeout_add_seconds (5, on_timeout, &timed_out);

  while (desktop_item_get_size (item) != size && !timed_out)
    g_main_context_iteration (NULL, TRUE);
  if (!timed_out)
    g_source_remove (id);
  g_assert_cmpint (desktop_item_get_size (item), ==, size);
}

static void
test_overwrite_set_contents (void)
{
  Fixture f = { 0 };
  const char *files[] = { "a.txt", NULL };
  DesktopItem *a;

  fixture_setup (&f, files, NULL);
  load (f.model);
  a = desktop_model_lookup (f.model, "a.txt");
  touch (&f, "a.txt", "hello world");
  wait_for_size (a, 11);
  g_assert_cmpuint (g_list_model_get_n_items (desktop_model_get_items (f.model)), ==, 1);
  g_assert_true (desktop_model_lookup (f.model, "a.txt") == a);
  fixture_teardown (&f);
}

static void
test_overwrite_replace (void)
{
  Fixture f = { 0 };
  const char *files[] = { "a.txt", NULL };
  DesktopItem *a;
  g_autoptr (GFile) file = NULL;
  g_autoptr (GFileOutputStream) out = NULL;
  GError *error = NULL;

  fixture_setup (&f, files, NULL);
  load (f.model);
  a = desktop_model_lookup (f.model, "a.txt");
  file = g_file_get_child (f.dir_file, "a.txt");
  out = g_file_replace (file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, &error);
  g_assert_no_error (error);
  g_output_stream_write_all (G_OUTPUT_STREAM (out), "hello world", 11, NULL, NULL, &error);
  g_assert_no_error (error);
  g_output_stream_close (G_OUTPUT_STREAM (out), NULL, &error);
  g_assert_no_error (error);
  wait_for_size (a, 11);
  g_assert_cmpuint (g_list_model_get_n_items (desktop_model_get_items (f.model)), ==, 1);
  fixture_teardown (&f);
}

static void
test_creates_missing_directory (void)
{
  g_autofree char *tmp = g_dir_make_tmp ("niri-desktop-model-XXXXXX", NULL);
  g_autofree char *sub = g_build_filename (tmp, "no", "such", "dir", NULL);
  g_autofree char *sp = g_build_filename (tmp, "p.ini", NULL);
  g_autoptr (GFile) d = g_file_new_for_path (sub);
  DesktopModel *m = desktop_model_new (d, position_store_new (sp));

  g_assert_true (g_file_test (sub, G_FILE_TEST_IS_DIR));
  g_assert_true (desktop_model_is_monitoring (m));
  g_object_unref (m);
  g_file_delete (d, NULL, NULL);
  {
    g_autofree char *p2 = g_path_get_dirname (sub);
    g_autofree char *p1 = g_path_get_dirname (p2);
    g_autoptr (GFile) f2 = g_file_new_for_path (p2);
    g_autoptr (GFile) f1 = g_file_new_for_path (p1);
    g_autoptr (GFile) fs = g_file_new_for_path (sp);
    g_file_delete (f2, NULL, NULL);
    g_file_delete (f1, NULL, NULL);
    g_file_delete (fs, NULL, NULL);
  }
  {
    g_autoptr (GFile) ft = g_file_new_for_path (tmp);
    g_file_delete (ft, NULL, NULL);
  }
}

static void
test_preseed_position (void)
{
  Fixture f = { 0 };
  DesktopItem *item;
  int x = 0, y = 0;

  fixture_setup (&f, NULL, NULL);
  load (f.model);
  desktop_model_preseed_position (f.model, "new.txt", 30, 40);
  touch (&f, "new.txt", "x");
  wait_for_item (f.model, "new.txt");
  wait_for_count (desktop_model_get_items (f.model), 1);
  item = desktop_model_lookup (f.model, "new.txt");
  g_assert_nonnull (item);
  g_assert_true (desktop_item_get_position (item, &x, &y));
  g_assert_cmpint (x, ==, 30);
  g_assert_cmpint (y, ==, 40);
  fixture_teardown (&f);
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/model/load-sorted-skips-hidden", test_load_sorted_skips_hidden);
  g_test_add_func ("/model/positions-applied-on-load", test_positions_applied_on_load);
  g_test_add_func ("/model/create-adds", test_create_adds);
  g_test_add_func ("/model/delete-removes-and-forgets-position", test_delete_removes_and_forgets_position);
  g_test_add_func ("/model/rename-keeps-position", test_rename_keeps_position);
  g_test_add_func ("/model/change-reloads", test_change_reloads);
  g_test_add_func ("/model/overwrite-set-contents", test_overwrite_set_contents);
  g_test_add_func ("/model/overwrite-replace", test_overwrite_replace);
  g_test_add_func ("/model/creates-missing-directory", test_creates_missing_directory);
  g_test_add_func ("/model/preseed-position", test_preseed_position);
  return g_test_run ();
}
