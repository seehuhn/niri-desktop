#include <gio/gio.h>
#include <glib/gstdio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include "thumbnails.h"

static char *tmp_dir;

static gboolean
on_timeout (gpointer data)
{
  *(gboolean *) data = TRUE;
  return G_SOURCE_REMOVE;
}

typedef struct {
  gboolean      done;
  GdkPaintable *paintable;
  GError       *error;
} Result;

static void
on_loaded (G_GNUC_UNUSED GObject *source, GAsyncResult *res, gpointer data)
{
  Result *r = data;
  r->paintable = thumbnails_load_finish (res, &r->error);
  r->done = TRUE;
}

static void
run_load (GFile *file, const char *type, Result *r)
{
  gboolean timed_out = FALSE;
  guint id = g_timeout_add_seconds (5, on_timeout, &timed_out);

  thumbnails_load_async (file, type, NULL, on_loaded, r);
  while (!r->done && !timed_out)
    g_main_context_iteration (NULL, TRUE);
  if (!timed_out)
    g_source_remove (id);
  g_assert_true (r->done);
}

static char *
make_png (const char *name)
{
  char *path = g_build_filename (tmp_dir, name, NULL);
  GdkPixbuf *pb = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, 300, 200);

  gdk_pixbuf_fill (pb, 0xff0000ff);
  g_assert_true (gdk_pixbuf_save (pb, path, "png", NULL, NULL));
  g_object_unref (pb);
  return path;
}

static void
test_is_image (void)
{
  g_assert_true (thumbnails_is_image ("image/png"));
  g_assert_true (thumbnails_is_image ("image/jpeg"));
  g_assert_false (thumbnails_is_image ("text/plain"));
  g_assert_false (thumbnails_is_image ("application/pdf"));
}

static void
test_cache_path (void)
{
  g_autoptr (GFile) f = g_file_new_for_uri ("file:///tmp/x.png");
  g_autofree char *path = thumbnails_cache_path (f);
  g_autofree char *md5 = g_compute_checksum_for_string (G_CHECKSUM_MD5, "file:///tmp/x.png", -1);
  g_autofree char *suffix = g_strdup_printf ("/thumbnails/large/%s.png", md5);

  g_assert_true (g_str_has_suffix (path, suffix));
}

static void
test_generates_and_caches (void)
{
  g_autofree char *src = make_png ("a.png");
  g_autoptr (GFile) f = g_file_new_for_path (src);
  g_autofree char *cache = thumbnails_cache_path (f);
  Result r = { 0 };

  run_load (f, "image/png", &r);
  g_assert_no_error (r.error);
  g_assert_nonnull (r.paintable);
  g_assert_cmpint (gdk_paintable_get_intrinsic_width (r.paintable), ==, 256);
  g_assert_cmpint (gdk_paintable_get_intrinsic_height (r.paintable), ==, 171);
  g_assert_true (g_file_test (cache, G_FILE_TEST_EXISTS));
  g_object_unref (r.paintable);
}

static void
test_uses_cache (void)
{
  g_autofree char *src = make_png ("b.png");
  g_autoptr (GFile) f = g_file_new_for_path (src);
  Result r = { 0 };
  Result r2 = { 0 };

  run_load (f, "image/png", &r);
  g_assert_no_error (r.error);
  g_object_unref (r.paintable);

  g_assert_cmpint (g_unlink (src), ==, 0);
  run_load (f, "image/png", &r2);
  g_assert_no_error (r2.error);
  g_assert_nonnull (r2.paintable);
  g_object_unref (r2.paintable);
}

static void
test_non_image_errors (void)
{
  g_autoptr (GFile) f = g_file_new_for_path ("/tmp/whatever.txt");
  Result r = { 0 };

  run_load (f, "text/plain", &r);
  g_assert_null (r.paintable);
  g_assert_nonnull (r.error);
  g_clear_error (&r.error);
}

static void
test_can_thumbnail (void)
{
  g_assert_true (thumbnails_can_thumbnail ("image/png"));
  g_assert_true (thumbnails_can_thumbnail ("application/x-lnxdesktop-test"));
  g_assert_false (thumbnails_can_thumbnail ("application/x-lnxdesktop-missing"));
  g_assert_false (thumbnails_can_thumbnail ("text/plain"));
  g_assert_false (thumbnails_can_thumbnail (NULL));
}

static void
test_thumbnailer_lookup (void)
{
  g_autofree char *user = thumbnails_find_exec ("application/x-lnxdesktop-prec");
  g_autofree char *sys1 = thumbnails_find_exec ("application/x-lnxdesktop-sys");
  g_autofree char *multi = thumbnails_find_exec ("application/x-lnxdesktop-multi");

  /* the user directory wins over system directories */
  g_assert_cmpstr (user, ==, "cp user %o");
  /* earlier system directories win over later ones */
  g_assert_cmpstr (sys1, ==, "cp sys1 %o");
  /* one entry can list several MIME types */
  g_assert_cmpstr (multi, ==, "cp multi %o");
  g_assert_null (thumbnails_find_exec ("application/x-lnxdesktop-missing"));
}

static void
test_expand_exec (void)
{
  g_autoptr (GError) error = NULL;
  g_auto (GStrv) argv = thumbnails_expand_exec (
    "tool -s %s '%u' --out=%o %i 100%% \"a b\"",
    "file:///tmp/a%20b.pdf", "/tmp/a b.pdf", "/tmp/out.png", 256, &error);

  g_assert_no_error (error);
  g_assert_cmpint (g_strv_length (argv), ==, 8);
  g_assert_cmpstr (argv[0], ==, "tool");
  g_assert_cmpstr (argv[1], ==, "-s");
  g_assert_cmpstr (argv[2], ==, "256");
  g_assert_cmpstr (argv[3], ==, "file:///tmp/a%20b.pdf");
  g_assert_cmpstr (argv[4], ==, "--out=/tmp/out.png");
  /* a path with a space stays one argument */
  g_assert_cmpstr (argv[5], ==, "/tmp/a b.pdf");
  g_assert_cmpstr (argv[6], ==, "100%");
  g_assert_cmpstr (argv[7], ==, "a b");
}

static void
test_expand_exec_bad (void)
{
  g_autoptr (GError) error = NULL;
  g_auto (GStrv) argv = thumbnails_expand_exec ("tool 'unterminated", "u", "p", "o", 256, &error);

  g_assert_null (argv);
  g_assert_nonnull (error);
}

static void
test_external_generates_and_caches (void)
{
  g_autofree char *src = g_build_filename (tmp_dir, "doc.dat", NULL);
  g_autoptr (GFile) f = g_file_new_for_path (src);
  g_autofree char *cache = thumbnails_cache_path (f);
  g_autoptr (GdkPixbuf) pb = NULL;
  Result r = { 0 };

  g_assert_true (g_file_set_contents (src, "dummy", -1, NULL));
  run_load (f, "application/x-lnxdesktop-test", &r);
  g_assert_no_error (r.error);
  g_assert_nonnull (r.paintable);
  /* the 300x200 output of the fake thumbnailer is scaled to fit 256 */
  g_assert_cmpint (gdk_paintable_get_intrinsic_width (r.paintable), ==, 256);
  g_assert_cmpint (gdk_paintable_get_intrinsic_height (r.paintable), ==, 171);
  g_object_unref (r.paintable);

  pb = gdk_pixbuf_new_from_file (cache, NULL);
  g_assert_nonnull (pb);
  g_assert_nonnull (gdk_pixbuf_get_option (pb, "tEXt::Thumb::MTime"));
  g_assert_nonnull (gdk_pixbuf_get_option (pb, "tEXt::Thumb::URI"));
}

static void
test_external_failure (void)
{
  g_autofree char *src = g_build_filename (tmp_dir, "bad.dat", NULL);
  g_autoptr (GFile) f = g_file_new_for_path (src);
  g_autofree char *cache = thumbnails_cache_path (f);
  Result r = { 0 };

  g_assert_true (g_file_set_contents (src, "dummy", -1, NULL));
  run_load (f, "application/x-lnxdesktop-fail", &r);
  g_assert_null (r.paintable);
  g_assert_nonnull (r.error);
  g_assert_false (g_file_test (cache, G_FILE_TEST_EXISTS));
  g_clear_error (&r.error);
}

static void
write_thumbnailer (const char *dir, const char *name, const char *try_exec,
                   const char *exec, const char *mimes)
{
  g_autofree char *path = g_build_filename (dir, name, NULL);
  g_autofree char *text = g_strdup_printf (
    "[Thumbnailer Entry]\n%s%s%sExec=%s\nMimeType=%s\n",
    try_exec ? "TryExec=" : "", try_exec ? try_exec : "", try_exec ? "\n" : "",
    exec, mimes);

  g_assert_true (g_file_set_contents (path, text, -1, NULL));
}

/* Lay out fake XDG data directories; this must run before the first
 * thumbnailer lookup, since the table is built once from the environment. */
static void
setup_thumbnailers (const char *png)
{
  g_autofree char *user = g_build_filename (tmp_dir, "user", "thumbnailers", NULL);
  g_autofree char *sys1 = g_build_filename (tmp_dir, "sys1", "thumbnailers", NULL);
  g_autofree char *sys2 = g_build_filename (tmp_dir, "sys2", "thumbnailers", NULL);
  /* keep the real directories at the end: gdk-pixbuf and the MIME database need them */
  g_autofree char *dirs = g_strdup_printf ("%s/sys1:%s/sys2:%s", tmp_dir, tmp_dir,
                                           g_getenv ("XDG_DATA_DIRS") ? g_getenv ("XDG_DATA_DIRS")
                                                                      : "/usr/local/share:/usr/share");
  g_autofree char *e = g_strdup_printf ("cp %s %%o", png);
  g_autofree char *home = g_build_filename (tmp_dir, "user", NULL);

  g_assert_cmpint (g_mkdir_with_parents (user, 0700), ==, 0);
  g_assert_cmpint (g_mkdir_with_parents (sys1, 0700), ==, 0);
  g_assert_cmpint (g_mkdir_with_parents (sys2, 0700), ==, 0);
  g_setenv ("XDG_DATA_HOME", home, TRUE);
  g_setenv ("XDG_DATA_DIRS", dirs, TRUE);

  write_thumbnailer (user, "t.thumbnailer", "cp", e, "application/x-lnxdesktop-test");
  write_thumbnailer (user, "p.thumbnailer", "cp", "cp user %o", "application/x-lnxdesktop-prec");
  write_thumbnailer (user, "f.thumbnailer", NULL, "false", "application/x-lnxdesktop-fail");
  write_thumbnailer (sys1, "p.thumbnailer", "cp", "cp sys1 %o",
                     "application/x-lnxdesktop-prec;application/x-lnxdesktop-sys");
  write_thumbnailer (sys2, "p.thumbnailer", "cp", "cp sys2 %o",
                     "application/x-lnxdesktop-prec;application/x-lnxdesktop-sys");
  write_thumbnailer (sys2, "m.thumbnailer", "cp", "cp multi %o",
                     "application/x-lnxdesktop-a;application/x-lnxdesktop-multi");
  /* the program does not exist, so this entry must be skipped */
  write_thumbnailer (sys1, "x.thumbnailer", "lnxdesktop-no-such-program",
                     "lnxdesktop-no-such-program %o", "application/x-lnxdesktop-missing");
}

int
main (int argc, char **argv)
{
  g_autoptr (GError) error = NULL;
  int rc;

  tmp_dir = g_dir_make_tmp ("lnxdesktop-thumb-XXXXXX", &error);
  g_assert_no_error (error);
  g_setenv ("XDG_CACHE_HOME", tmp_dir, TRUE);
  {
    g_autofree char *png = make_png ("fake-output.png");

    setup_thumbnailers (png);
  }

  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/thumb/is-image", test_is_image);
  g_test_add_func ("/thumb/cache-path", test_cache_path);
  g_test_add_func ("/thumb/generates-and-caches", test_generates_and_caches);
  g_test_add_func ("/thumb/uses-cache", test_uses_cache);
  g_test_add_func ("/thumb/non-image-errors", test_non_image_errors);
  g_test_add_func ("/thumb/can-thumbnail", test_can_thumbnail);
  g_test_add_func ("/thumb/thumbnailer-lookup", test_thumbnailer_lookup);
  g_test_add_func ("/thumb/expand-exec", test_expand_exec);
  g_test_add_func ("/thumb/expand-exec-bad", test_expand_exec_bad);
  g_test_add_func ("/thumb/external-generates-and-caches", test_external_generates_and_caches);
  g_test_add_func ("/thumb/external-failure", test_external_failure);
  rc = g_test_run ();
  g_free (tmp_dir);
  return rc;
}
