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

int
main (int argc, char **argv)
{
  g_autoptr (GError) error = NULL;
  int rc;

  tmp_dir = g_dir_make_tmp ("lnxdesktop-thumb-XXXXXX", &error);
  g_assert_no_error (error);
  g_setenv ("XDG_CACHE_HOME", tmp_dir, TRUE);

  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/thumb/is-image", test_is_image);
  g_test_add_func ("/thumb/cache-path", test_cache_path);
  g_test_add_func ("/thumb/generates-and-caches", test_generates_and_caches);
  g_test_add_func ("/thumb/uses-cache", test_uses_cache);
  g_test_add_func ("/thumb/non-image-errors", test_non_image_errors);
  rc = g_test_run ();
  g_free (tmp_dir);
  return rc;
}
