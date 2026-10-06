#include "thumbnails.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gstdio.h>
#include <unistd.h>

#define THUMB_SIZE 256

typedef struct {
  GFile *file;
  char  *content_type;
} LoadData;

static void
load_data_free (LoadData *d)
{
  g_object_unref (d->file);
  g_free (d->content_type);
  g_free (d);
}

gboolean
thumbnails_is_image (const char *content_type)
{
  g_autoptr (GSList) formats = gdk_pixbuf_get_formats ();
  gboolean found = FALSE;

  if (content_type == NULL)
    return FALSE;

  for (GSList *l = formats; l != NULL && !found; l = l->next)
    {
      char **mimes = gdk_pixbuf_format_get_mime_types (l->data);

      for (int i = 0; mimes != NULL && mimes[i] != NULL; i++)
        if (g_strcmp0 (mimes[i], content_type) == 0)
          {
            found = TRUE;
            break;
          }
      g_strfreev (mimes);
    }
  return found;
}

char *
thumbnails_cache_path (GFile *file)
{
  g_autofree char *uri = g_file_get_uri (file);
  g_autofree char *md5 = g_compute_checksum_for_string (G_CHECKSUM_MD5, uri, -1);
  g_autofree char *name = g_strconcat (md5, ".png", NULL);

  return g_build_filename (g_get_user_cache_dir (), "thumbnails", "large", name, NULL);
}

/* Returns the source mtime as a string, or NULL if the source is missing. */
static char *
source_mtime (GFile *file)
{
  g_autoptr (GFileInfo) info =
    g_file_query_info (file, G_FILE_ATTRIBUTE_TIME_MODIFIED,
                       G_FILE_QUERY_INFO_NONE, NULL, NULL);

  if (info == NULL)
    return NULL;
  return g_strdup_printf ("%" G_GUINT64_FORMAT,
                          g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED));
}

static GdkPixbuf *
load_cached (const char *cache_path, const char *mtime)
{
  GdkPixbuf *pb = gdk_pixbuf_new_from_file (cache_path, NULL);

  if (pb == NULL)
    return NULL;
  if (mtime != NULL &&
      g_strcmp0 (gdk_pixbuf_get_option (pb, "tEXt::Thumb::MTime"), mtime) != 0)
    g_clear_object (&pb);
  return pb;
}

static void
load_thread (GTask *task, G_GNUC_UNUSED gpointer source, gpointer task_data,
             G_GNUC_UNUSED GCancellable *cancellable)
{
  LoadData *d = task_data;
  g_autofree char *cache_path = thumbnails_cache_path (d->file);
  g_autofree char *mtime = source_mtime (d->file);
  g_autoptr (GdkPixbuf) pb = load_cached (cache_path, mtime);
  GError *error = NULL;

  if (pb == NULL)
    {
      g_autofree char *path = g_file_get_path (d->file);
      g_autofree char *uri = g_file_get_uri (d->file);
      g_autoptr (GdkPixbuf) raw = NULL;

      if (path == NULL)
        {
          g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                                   "Not a local file");
          return;
        }
      if (g_task_return_error_if_cancelled (task))
        return;
      raw = gdk_pixbuf_new_from_file_at_scale (path, THUMB_SIZE, THUMB_SIZE, TRUE, &error);
      if (raw == NULL)
        {
          g_task_return_error (task, error);
          return;
        }
      pb = gdk_pixbuf_apply_embedded_orientation (raw);
      if (pb == NULL)
        pb = g_object_ref (raw);

      g_autofree char *dir = g_path_get_dirname (cache_path);
      if (g_mkdir_with_parents (dir, 0700) == 0)
        {
          const char *keys[] = { "tEXt::Thumb::URI", "tEXt::Thumb::MTime", NULL };
          const char *vals[] = { uri, mtime, NULL };

          if (mtime == NULL)
            keys[1] = NULL;
          g_autofree char *tmp = g_strdup_printf ("%s.tmp-%d", cache_path, (int) getpid ());

          if (gdk_pixbuf_savev (pb, tmp, "png", (char **) keys, (char **) vals, NULL))
            {
              if (g_rename (tmp, cache_path) == 0)
                g_chmod (cache_path, 0600);
              else
                g_remove (tmp);
            }
          else
            g_remove (tmp);
        }
    }

  g_task_return_pointer (task, gdk_texture_new_for_pixbuf (pb), g_object_unref);
}

void
thumbnails_load_async (GFile               *file,
                       const char          *content_type,
                       GCancellable        *cancellable,
                       GAsyncReadyCallback  callback,
                       gpointer             user_data)
{
  g_autoptr (GTask) task = g_task_new (NULL, cancellable, callback, user_data);
  LoadData *d;

  g_task_set_source_tag (task, thumbnails_load_async);
  if (!thumbnails_is_image (content_type))
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                               "Not an image type: %s", content_type ? content_type : "(none)");
      return;
    }

  d = g_new0 (LoadData, 1);
  d->file = g_object_ref (file);
  d->content_type = g_strdup (content_type);
  g_task_set_task_data (task, d, (GDestroyNotify) load_data_free);
  g_task_run_in_thread (task, load_thread);
}

GdkPaintable *
thumbnails_load_finish (GAsyncResult *res, GError **error)
{
  return g_task_propagate_pointer (G_TASK (res), error);
}
