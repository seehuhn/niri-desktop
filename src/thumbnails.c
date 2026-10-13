#include "thumbnails.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gstdio.h>
#include <unistd.h>

#define THUMB_SIZE 256
#define THUMBNAILER_TIMEOUT 10   /* seconds */

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

/* The table of external thumbnailers: MIME type -> Exec template.  It is
 * built once, on first use, from the *.thumbnailer files in the XDG data
 * directories. */
static GHashTable *thumbnailers;

static void
scan_thumbnailer_dir (GHashTable *table, const char *data_dir)
{
  g_autofree char *dir = g_build_filename (data_dir, "thumbnailers", NULL);
  g_autoptr (GDir) d = g_dir_open (dir, 0, NULL);
  g_autoptr (GPtrArray) names = g_ptr_array_new_with_free_func (g_free);
  const char *name;

  if (d == NULL)
    return;
  while ((name = g_dir_read_name (d)) != NULL)
    if (g_str_has_suffix (name, ".thumbnailer"))
      g_ptr_array_add (names, g_strdup (name));
  /* directory order is arbitrary; sort so that the result is stable */
  g_ptr_array_sort_values (names, (GCompareFunc) g_strcmp0);

  for (guint i = 0; i < names->len; i++)
    {
      g_autofree char *path = g_build_filename (dir, g_ptr_array_index (names, i), NULL);
      g_autoptr (GKeyFile) kf = g_key_file_new ();
      g_autofree char *exec = NULL;
      g_autofree char *try_exec = NULL;
      g_auto (GStrv) mimes = NULL;
      g_auto (GStrv) argv = NULL;
      g_autofree char *prog = NULL;

      if (!g_key_file_load_from_file (kf, path, G_KEY_FILE_NONE, NULL))
        continue;
      exec = g_key_file_get_string (kf, "Thumbnailer Entry", "Exec", NULL);
      try_exec = g_key_file_get_string (kf, "Thumbnailer Entry", "TryExec", NULL);
      mimes = g_key_file_get_string_list (kf, "Thumbnailer Entry", "MimeType", NULL, NULL);
      if (exec == NULL || mimes == NULL)
        continue;

      /* skip thumbnailers whose program is not installed */
      if (try_exec == NULL)
        {
          if (!g_shell_parse_argv (exec, NULL, &argv, NULL))
            continue;
          prog = g_find_program_in_path (argv[0]);
        }
      else
        prog = g_find_program_in_path (try_exec);
      if (prog == NULL)
        continue;

      /* directories are scanned in priority order, so the first entry wins */
      for (int j = 0; mimes[j] != NULL; j++)
        if (!g_hash_table_contains (table, mimes[j]))
          g_hash_table_insert (table, g_strdup (mimes[j]), g_strdup (exec));
    }
}

static gpointer
build_thumbnailers (G_GNUC_UNUSED gpointer data)
{
  const char * const *sys_dirs = g_get_system_data_dirs ();

  thumbnailers = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  scan_thumbnailer_dir (thumbnailers, g_get_user_data_dir ());
  for (int i = 0; sys_dirs[i] != NULL; i++)
    scan_thumbnailer_dir (thumbnailers, sys_dirs[i]);
  return thumbnailers;
}

/* Returns a copy of the Exec template for content_type, or NULL if no
 * usable external thumbnailer is registered for it. */
char *
thumbnails_find_exec (const char *content_type)
{
  static GOnce once = G_ONCE_INIT;

  if (content_type == NULL)
    return NULL;
  g_once (&once, build_thumbnailers, NULL);
  return g_strdup (g_hash_table_lookup (thumbnailers, content_type));
}

gboolean
thumbnails_can_thumbnail (const char *content_type)
{
  g_autofree char *exec = NULL;

  if (content_type == NULL)
    return FALSE;
  if (thumbnails_is_image (content_type))
    return TRUE;
  exec = thumbnails_find_exec (content_type);
  return exec != NULL;
}

/* Replaces %s, %u, %i, %o and %% in one argument. Other codes are kept. */
static char *
substitute_arg (const char *arg, const char *size, const char *uri,
                const char *path, const char *output)
{
  GString *out = g_string_new (NULL);

  for (const char *p = arg; *p != '\0'; p++)
    {
      const char *rep = NULL;

      if (*p == '%')
        switch (p[1])
          {
          case 's': rep = size; break;
          case 'u': rep = uri; break;
          case 'i': rep = path; break;
          case 'o': rep = output; break;
          case '%': rep = "%"; break;
          default: break;
          }
      if (rep != NULL)
        {
          g_string_append (out, rep);
          p++;
        }
      else
        g_string_append_c (out, *p);
    }
  return g_string_free (out, FALSE);
}

/* Splits the template into words first and substitutes afterwards, so that
 * file names containing spaces or quotes stay in one argument. */
char **
thumbnails_expand_exec (const char *template, const char *uri, const char *path,
                        const char *output, int size, GError **error)
{
  g_autofree char *size_str = g_strdup_printf ("%d", size);
  g_auto (GStrv) argv = NULL;
  int argc;

  if (!g_shell_parse_argv (template, &argc, &argv, error))
    return NULL;
  for (int i = 0; i < argc; i++)
    {
      char *expanded = substitute_arg (argv[i], size_str, uri, path, output);

      g_free (argv[i]);
      argv[i] = expanded;
    }
  return g_steal_pointer (&argv);
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

/* Writes the thumbnail into the cache, tagged with the source URI and
 * mtime.  The file is written under a temporary name and renamed, so that
 * readers never see a partial file.  Failures are ignored. */
static void
save_cache (GdkPixbuf *pb, const char *cache_path, const char *uri, const char *mtime)
{
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

static void
cancel_child (G_GNUC_UNUSED GCancellable *parent, gpointer child)
{
  g_cancellable_cancel (child);
}

static gboolean
timeout_child (gpointer child)
{
  g_cancellable_cancel (child);
  return G_SOURCE_REMOVE;
}

/* Runs argv, waiting at most THUMBNAILER_TIMEOUT seconds.  The process is
 * killed on timeout or when cancellable is cancelled. */
static gboolean
run_thumbnailer (char **argv, GCancellable *cancellable, GError **error)
{
  g_autoptr (GSubprocessLauncher) launcher =
    g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
                               G_SUBPROCESS_FLAGS_STDERR_SILENCE);
  g_autoptr (GSubprocess) proc = g_subprocess_launcher_spawnv (launcher, (const char * const *) argv, error);
  g_autoptr (GCancellable) child = g_cancellable_new ();
  g_autoptr (GError) wait_error = NULL;
  GSource *timer;
  gulong handler = 0;
  gboolean ok;

  if (proc == NULL)
    return FALSE;

  /* the child cancellable fires on either cancellation or timeout */
  if (cancellable != NULL)
    handler = g_cancellable_connect (cancellable, G_CALLBACK (cancel_child), child, NULL);
  timer = g_timeout_source_new_seconds (THUMBNAILER_TIMEOUT);
  g_source_set_callback (timer, timeout_child, child, NULL);
  g_source_attach (timer, NULL);

  ok = g_subprocess_wait (proc, child, &wait_error);

  g_source_destroy (timer);
  g_source_unref (timer);
  if (handler != 0)
    g_cancellable_disconnect (cancellable, handler);

  if (!ok)
    {
      /* timed out or cancelled: kill the process and reap it */
      g_subprocess_force_exit (proc);
      g_subprocess_wait (proc, NULL, NULL);
      g_set_error (error, G_IO_ERROR,
                   g_cancellable_is_cancelled (cancellable) ? G_IO_ERROR_CANCELLED
                                                            : G_IO_ERROR_TIMED_OUT,
                   "Thumbnailer did not finish: %s", wait_error->message);
      return FALSE;
    }
  if (!g_subprocess_get_if_exited (proc) || g_subprocess_get_exit_status (proc) != 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Thumbnailer failed");
      return FALSE;
    }
  return TRUE;
}

/* Renders a thumbnail with an external program.  Returns NULL and sets
 * error on failure; the temporary output file is always removed. */
static GdkPixbuf *
render_external (const char *exec, const char *uri, const char *path,
                 GCancellable *cancellable, GError **error)
{
  g_autofree char *out = NULL;
  g_auto (GStrv) argv = NULL;
  GdkPixbuf *pb = NULL;
  /* use gnome-desktop's name: AppArmor profiles (e.g. evince's) only allow
   * thumbnailers to write to /tmp/gnome-desktop-* */
  int fd = g_file_open_tmp ("gnome-desktop-thumbnailer-XXXXXX.png", &out, error);

  if (fd < 0)
    return NULL;
  close (fd);

  argv = thumbnails_expand_exec (exec, uri, path, out, THUMB_SIZE, error);
  if (argv != NULL && run_thumbnailer (argv, cancellable, error))
    {
      pb = gdk_pixbuf_new_from_file (out, error);
      if (pb != NULL &&
          (gdk_pixbuf_get_width (pb) > THUMB_SIZE || gdk_pixbuf_get_height (pb) > THUMB_SIZE))
        {
          int w = gdk_pixbuf_get_width (pb);
          int h = gdk_pixbuf_get_height (pb);
          double scale = (double) THUMB_SIZE / MAX (w, h);
          GdkPixbuf *small = gdk_pixbuf_scale_simple (pb, MAX (1, (int) (w * scale + 0.5)),
                                                      MAX (1, (int) (h * scale + 0.5)),
                                                      GDK_INTERP_BILINEAR);

          g_object_unref (pb);
          pb = small;
        }
    }
  g_remove (out);
  return pb;
}

static void
load_thread (GTask *task, G_GNUC_UNUSED gpointer source, gpointer task_data,
             GCancellable *cancellable)
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
      if (thumbnails_is_image (d->content_type))
        {
          raw = gdk_pixbuf_new_from_file_at_scale (path, THUMB_SIZE, THUMB_SIZE, TRUE, &error);
          if (raw == NULL)
            {
              g_task_return_error (task, error);
              return;
            }
          pb = gdk_pixbuf_apply_embedded_orientation (raw);
          if (pb == NULL)
            pb = g_object_ref (raw);
        }
      else
        {
          g_autofree char *exec = thumbnails_find_exec (d->content_type);

          if (exec == NULL)
            {
              g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                                       "No thumbnailer for %s", d->content_type);
              return;
            }
          pb = render_external (exec, uri, path, cancellable, &error);
          if (pb == NULL)
            {
              g_task_return_error (task, error);
              return;
            }
        }

      save_cache (pb, cache_path, uri, mtime);
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
  if (!thumbnails_can_thumbnail (content_type))
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                               "Cannot thumbnail type: %s", content_type ? content_type : "(none)");
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
