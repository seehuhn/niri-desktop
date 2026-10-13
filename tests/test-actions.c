#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include "actions.h"

static GFile *
make_dir (void)
{
  g_autoptr (GError) err = NULL;
  g_autofree char *dir = g_dir_make_tmp ("niri-desktop-actions-XXXXXX", &err);
  g_assert_no_error (err);
  return g_file_new_for_path (dir);
}

static void
touch (GFile *dir, const char *name)
{
  g_autoptr (GError) err = NULL;
  g_autoptr (GFile) f = g_file_get_child (dir, name);
  g_file_replace_contents (f, "", 0, NULL, FALSE, G_FILE_CREATE_NONE, NULL, NULL, &err);
  g_assert_no_error (err);
}

static void
cleanup (GFile *dir, const char **names)
{
  for (int i = 0; names[i]; i++)
    {
      g_autoptr (GFile) f = g_file_get_child (dir, names[i]);
      g_file_delete (f, NULL, NULL);
    }
  g_file_delete (dir, NULL, NULL);
}

static void
check (const char **existing, const char *name, const char *expected)
{
  g_autoptr (GFile) dir = make_dir ();
  for (int i = 0; existing[i]; i++)
    touch (dir, existing[i]);
  g_autofree char *got = actions_unique_name (dir, name);
  g_assert_cmpstr (got, ==, expected);
  cleanup (dir, existing);
}

static void
test_free (void)
{
  const char *e[] = { NULL };
  check (e, "a.txt", "a.txt");
}

static void
test_taken (void)
{
  const char *e1[] = { "a.txt", NULL };
  check (e1, "a.txt", "a (2).txt");
  const char *e2[] = { "a.txt", "a (2).txt", NULL };
  check (e2, "a.txt", "a (3).txt");
}

static void
test_no_extension (void)
{
  const char *e[] = { "Makefile", NULL };
  check (e, "Makefile", "Makefile (2)");
}

static void
test_single_extension (void)
{
  const char *e[] = { "archive.tar.gz", NULL };
  check (e, "archive.tar.gz", "archive.tar (2).gz");
}

static void
test_dotfile (void)
{
  const char *e[] = { ".bashrc", NULL };
  check (e, ".bashrc", ".bashrc (2)");
}

static void
test_reserved (void)
{
  g_autoptr (GFile) dir = make_dir ();
  g_autoptr (GHashTable) reserved = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  g_autofree char *first = actions_unique_name_with_reserved (dir, "a.txt", reserved);
  g_assert_cmpstr (first, ==, "a.txt");
  g_hash_table_add (reserved, g_strdup (first));
  g_autofree char *second = actions_unique_name_with_reserved (dir, "a.txt", reserved);
  g_assert_cmpstr (second, ==, "a (2).txt");
  const char *none[] = { NULL };
  cleanup (dir, none);
}

static void
write_file (GFile *dir, const char *name, const char *contents)
{
  g_autoptr (GError) err = NULL;
  g_autoptr (GFile) f = g_file_get_child (dir, name);
  g_file_replace_contents (f, contents, strlen (contents), NULL, FALSE, G_FILE_CREATE_NONE,
                           NULL, NULL, &err);
  g_assert_no_error (err);
}

/* src/ with a.txt and sub/b.txt */
static GFile *
make_tree (GFile *root)
{
  g_autoptr (GError) err = NULL;
  GFile *src = g_file_get_child (root, "src");
  g_autoptr (GFile) sub = g_file_get_child (src, "sub");
  g_file_make_directory (src, NULL, &err);
  g_assert_no_error (err);
  g_file_make_directory (sub, NULL, &err);
  g_assert_no_error (err);
  write_file (src, "a.txt", "alpha");
  write_file (sub, "b.txt", "beta");
  return src;
}

static void
assert_contents (GFile *dir, const char *path, const char *expected)
{
  g_autoptr (GError) err = NULL;
  g_autoptr (GFile) f = g_file_resolve_relative_path (dir, path);
  g_autofree char *data = NULL;
  g_file_load_contents (f, NULL, &data, NULL, NULL, &err);
  g_assert_no_error (err);
  g_assert_cmpstr (data, ==, expected);
}

static void
test_copy_recursive (void)
{
  g_autoptr (GError) err = NULL;
  g_autoptr (GFile) root = make_dir ();
  g_autoptr (GFile) src = make_tree (root);
  g_autoptr (GFile) dest = g_file_get_child (root, "dest");

  g_assert_true (actions_copy_recursive (src, dest, NULL, &err));
  g_assert_no_error (err);
  assert_contents (dest, "a.txt", "alpha");
  assert_contents (dest, "sub/b.txt", "beta");
  assert_contents (src, "sub/b.txt", "beta");

  g_assert_true (actions_delete_recursive (src, NULL, &err));
  g_assert_true (actions_delete_recursive (dest, NULL, &err));
  g_assert_no_error (err);
  g_assert_true (g_file_delete (root, NULL, NULL));
}

static void
test_copy_recursive_existing_fails (void)
{
  g_autoptr (GError) err = NULL;
  g_autoptr (GFile) root = make_dir ();
  g_autoptr (GFile) src = make_tree (root);

  g_assert_false (actions_copy_recursive (src, src, NULL, &err));
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_EXISTS);
  g_clear_error (&err);

  g_assert_true (actions_delete_recursive (src, NULL, &err));
  g_assert_true (g_file_delete (root, NULL, NULL));
}

static void
test_delete_recursive (void)
{
  g_autoptr (GError) err = NULL;
  g_autoptr (GFile) root = make_dir ();
  g_autoptr (GFile) src = make_tree (root);

  g_assert_true (actions_delete_recursive (src, NULL, &err));
  g_assert_no_error (err);
  g_assert_false (g_file_query_exists (src, NULL));
  g_assert_true (g_file_delete (root, NULL, NULL));
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/actions/unique-name-free", test_free);
  g_test_add_func ("/actions/unique-name-taken", test_taken);
  g_test_add_func ("/actions/unique-name-no-extension", test_no_extension);
  g_test_add_func ("/actions/unique-name-single-extension", test_single_extension);
  g_test_add_func ("/actions/unique-name-dotfile", test_dotfile);
  g_test_add_func ("/actions/unique-name-reserved", test_reserved);
  g_test_add_func ("/actions/copy-recursive", test_copy_recursive);
  g_test_add_func ("/actions/copy-recursive-existing-fails", test_copy_recursive_existing_fails);
  g_test_add_func ("/actions/delete-recursive", test_delete_recursive);
  return g_test_run ();
}
