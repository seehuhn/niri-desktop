#include "position-store.h"

#include <glib/gstdio.h>

#define FLUSH_DELAY_MS 500

struct _PositionStore
{
  char     *path;
  GKeyFile *keyfile;
  guint     flush_source;
};

static void
schedule_flush_cb (gpointer data)
{
  PositionStore *store = data;

  store->flush_source = 0;
  position_store_flush (store);
}

static void
schedule_flush (PositionStore *store)
{
  if (store->flush_source == 0)
    store->flush_source = g_timeout_add_once (FLUSH_DELAY_MS, schedule_flush_cb, store);
}

static char *
group_name (const char *name)
{
  return g_uri_escape_string (name, NULL, FALSE);
}

PositionStore *
position_store_new (const char *path)
{
  PositionStore *store = g_new0 (PositionStore, 1);
  g_autoptr (GError) error = NULL;

  store->path = g_strdup (path);
  store->keyfile = g_key_file_new ();

  if (!g_key_file_load_from_file (store->keyfile, path, G_KEY_FILE_NONE, &error))
    {
      if (!g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        {
          g_autofree char *bak = g_strconcat (path, ".bak", NULL);

          g_message ("Cannot load %s: %s; moving it to %s", path, error->message, bak);
          if (g_rename (path, bak) != 0)
            g_warning ("Cannot rename %s to %s", path, bak);
          g_key_file_free (store->keyfile);
          store->keyfile = g_key_file_new ();
        }
    }

  return store;
}

void
position_store_free (PositionStore *store)
{
  if (store == NULL)
    return;

  position_store_flush (store);
  g_key_file_free (store->keyfile);
  g_free (store->path);
  g_free (store);
}

gboolean
position_store_lookup (PositionStore *store, const char *name, int *x, int *y)
{
  g_autofree char *group = group_name (name);
  g_autoptr (GError) error_x = NULL;
  g_autoptr (GError) error_y = NULL;
  int px, py;

  if (!g_key_file_has_group (store->keyfile, group))
    return FALSE;

  px = g_key_file_get_integer (store->keyfile, group, "x", &error_x);
  py = g_key_file_get_integer (store->keyfile, group, "y", &error_y);
  if (error_x != NULL || error_y != NULL)
    return FALSE;

  if (x != NULL)
    *x = px;
  if (y != NULL)
    *y = py;
  return TRUE;
}

static void
set_internal (PositionStore *store, const char *name, int x, int y)
{
  g_autofree char *group = group_name (name);

  g_key_file_set_integer (store->keyfile, group, "x", x);
  g_key_file_set_integer (store->keyfile, group, "y", y);
  g_key_file_set_string (store->keyfile, group, "anchor", "left");
}

void
position_store_set (PositionStore *store, const char *name, int x, int y)
{
  set_internal (store, name, x, y);
  schedule_flush (store);
}

void
position_store_remove (PositionStore *store, const char *name)
{
  g_autofree char *group = group_name (name);

  if (g_key_file_remove_group (store->keyfile, group, NULL))
    schedule_flush (store);
}

void
position_store_rename (PositionStore *store, const char *old_name, const char *new_name)
{
  g_autofree char *old_group = NULL;
  int x, y;

  if (g_strcmp0 (old_name, new_name) == 0 ||
      !position_store_lookup (store, old_name, &x, &y))
    return;

  old_group = group_name (old_name);
  g_key_file_remove_group (store->keyfile, old_group, NULL);
  set_internal (store, new_name, x, y);
  schedule_flush (store);
}

void
position_store_flush (PositionStore *store)
{
  g_autofree char *dir = g_path_get_dirname (store->path);
  g_autofree char *data = NULL;
  g_autoptr (GError) error = NULL;
  gsize length = 0;

  if (store->flush_source != 0)
    {
      g_source_remove (store->flush_source);
      store->flush_source = 0;
    }

  if (g_mkdir_with_parents (dir, 0700) != 0)
    {
      g_warning ("Cannot create %s", dir);
      return;
    }

  data = g_key_file_to_data (store->keyfile, &length, NULL);
  if (!g_file_set_contents (store->path, data, length, &error))
    g_warning ("Cannot write %s: %s", store->path, error->message);
}
