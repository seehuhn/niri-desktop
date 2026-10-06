#include <glib.h>
#include <glib/gstdio.h>
#include "actions.h"

static GFile *
make_dir (void)
{
  g_autoptr (GError) err = NULL;
  g_autofree char *dir = g_dir_make_tmp ("lnxdesktop-actions-XXXXXX", &err);
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

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/actions/unique-name-free", test_free);
  g_test_add_func ("/actions/unique-name-taken", test_taken);
  g_test_add_func ("/actions/unique-name-no-extension", test_no_extension);
  g_test_add_func ("/actions/unique-name-single-extension", test_single_extension);
  g_test_add_func ("/actions/unique-name-dotfile", test_dotfile);
  return g_test_run ();
}
