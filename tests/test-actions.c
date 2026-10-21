/* SPDX-FileCopyrightText: 2026 Jochen Voss <voss@seehuhn.de>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

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
test_numbered (void)
{
  const char *e1[] = { "a.txt", "a (2).txt", NULL };
  check (e1, "a (2).txt", "a (3).txt");
  const char *e2[] = { "a (9).txt", NULL };
  check (e2, "a (9).txt", "a (10).txt");
  const char *e3[] = { "a (2).txt", "a (3).txt", NULL };
  check (e3, "a (2).txt", "a (4).txt");
  const char *e4[] = { "Makefile (2)", NULL };
  check (e4, "Makefile (2)", "Makefile (3)");
  const char *e5[] = { "a (1).txt", NULL };
  check (e5, "a (1).txt", "a (2).txt");
}

static void
test_not_numbered (void)
{
  const char *e1[] = { "a (x).txt", NULL };
  check (e1, "a (x).txt", "a (x) (2).txt");
  const char *e2[] = { "a(2).txt", NULL };
  check (e2, "a(2).txt", "a(2) (2).txt");
  const char *e3[] = { "a (0).txt", NULL };
  check (e3, "a (0).txt", "a (0) (2).txt");
  const char *e4[] = { "a (02).txt", NULL };
  check (e4, "a (02).txt", "a (02) (2).txt");
  const char *e5[] = { "(2)", NULL };
  check (e5, "(2)", "(2) (2)");
  const char *e6[] = { "a (99999999999).txt", NULL };
  check (e6, "a (99999999999).txt", "a (99999999999) (2).txt");
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

/* Runs the main loop until file exists, failing after a few seconds. */
static void
wait_for (GFile *file)
{
  gint64 deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;

  while (!g_file_query_exists (file, NULL))
    {
      g_assert_cmpint (g_get_monotonic_time (), <, deadline);
      g_main_context_iteration (NULL, FALSE);
    }
}

static void
test_transfer_into_folder (void)
{
  g_autoptr (GError) err = NULL;
  g_autoptr (GFile) root = make_dir ();
  g_autoptr (GFile) folder = g_file_get_child (root, "folder");
  g_autoptr (GFile) a = g_file_get_child (root, "a.txt");
  g_autoptr (GFile) b = g_file_get_child (root, "b.txt");
  g_autoptr (GFile) moved_a = g_file_get_child (folder, "a (2).txt");
  g_autoptr (GFile) moved_b = g_file_get_child (folder, "b.txt");
  GList *files = NULL;

  g_assert_true (g_file_make_directory (folder, NULL, &err));
  touch (folder, "a.txt");
  touch (root, "a.txt");
  touch (root, "b.txt");

  files = g_list_append (files, a);
  files = g_list_append (files, b);
  files = g_list_append (files, folder);
  actions_transfer (NULL, files, folder, TRUE);
  g_list_free (files);

  wait_for (moved_a);
  wait_for (moved_b);
  g_assert_false (g_file_query_exists (a, NULL));
  g_assert_false (g_file_query_exists (b, NULL));
  g_assert_true (g_file_query_exists (folder, NULL));

  g_assert_true (actions_delete_recursive (root, NULL, &err));
  g_assert_no_error (err);
}

static void
test_transfer_copy (void)
{
  g_autoptr (GError) err = NULL;
  g_autoptr (GFile) root = make_dir ();
  g_autoptr (GFile) folder = g_file_get_child (root, "folder");
  g_autoptr (GFile) sub = g_file_get_child (folder, "sub");
  g_autoptr (GFile) other = g_file_get_child (root, "other");
  g_autoptr (GFile) a = g_file_get_child (root, "a.txt");
  g_autoptr (GFile) other_a = g_file_get_child (other, "a.txt");
  g_autoptr (GFile) copy_a = g_file_get_child (sub, "a.txt");
  g_autoptr (GFile) copy_a2 = g_file_get_child (sub, "a (2).txt");
  g_autoptr (GFile) nested = g_file_get_child (sub, "folder");
  GList *files = NULL;

  g_assert_true (g_file_make_directory (folder, NULL, &err));
  g_assert_true (g_file_make_directory (sub, NULL, &err));
  g_assert_true (g_file_make_directory (other, NULL, &err));
  touch (root, "a.txt");
  touch (other, "a.txt");

  /* Two sources with one name, and an ancestor of the target. */
  files = g_list_append (files, a);
  files = g_list_append (files, other_a);
  files = g_list_append (files, folder);
  actions_transfer (NULL, files, sub, FALSE);
  g_list_free (files);

  wait_for (copy_a);
  wait_for (copy_a2);
  g_assert_true (g_file_query_exists (a, NULL));
  g_assert_true (g_file_query_exists (other_a, NULL));
  g_assert_false (g_file_query_exists (nested, NULL));

  g_assert_true (actions_delete_recursive (root, NULL, &err));
  g_assert_no_error (err);
}

static void
test_same_filesystem (void)
{
  g_autoptr (GFile) root = make_dir ();
  g_autoptr (GFile) a = g_file_get_child (root, "a.txt");
  g_autoptr (GFile) missing = g_file_get_child (root, "missing");
  g_autoptr (GFile) remote = g_file_new_for_uri ("http://example.org/x");

  touch (root, "a.txt");
  g_assert_true (actions_same_filesystem (a, root));
  g_assert_false (actions_same_filesystem (a, missing));
  g_assert_false (actions_same_filesystem (a, remote));
  cleanup (root, (const char *[]) { "a.txt", NULL });
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
  g_test_add_func ("/actions/unique-name-numbered", test_numbered);
  g_test_add_func ("/actions/unique-name-not-numbered", test_not_numbered);
  g_test_add_func ("/actions/copy-recursive", test_copy_recursive);
  g_test_add_func ("/actions/copy-recursive-existing-fails", test_copy_recursive_existing_fails);
  g_test_add_func ("/actions/delete-recursive", test_delete_recursive);
  g_test_add_func ("/actions/transfer-into-folder", test_transfer_into_folder);
  g_test_add_func ("/actions/transfer-copy", test_transfer_copy);
  g_test_add_func ("/actions/same-filesystem", test_same_filesystem);
  return g_test_run ();
}
