/* SPDX-FileCopyrightText: 2026 Jochen Voss <voss@seehuhn.de>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "desktop-model.h"
#include <string.h>

#define BATCH_SIZE 64

struct _DesktopModel
{
  GObject        parent_instance;
  GFile         *directory;
  PositionStore *store;
  GListStore    *items;
  GFileMonitor  *monitor;
};

G_DEFINE_FINAL_TYPE (DesktopModel, desktop_model, G_TYPE_OBJECT)

static gboolean
is_hidden (const char *name)
{
  return name == NULL || name[0] == '.';
}

static gint
compare_items (gconstpointer a, gconstpointer b, gpointer user_data G_GNUC_UNUSED)
{
  g_autofree char *ka = g_utf8_collate_key_for_filename (desktop_item_get_name ((DesktopItem *) a), -1);
  g_autofree char *kb = g_utf8_collate_key_for_filename (desktop_item_get_name ((DesktopItem *) b), -1);

  return strcmp (ka, kb);
}

static void
insert_sorted (DesktopModel *self, DesktopItem *item)
{
  g_list_store_insert_sorted (self->items, item, compare_items, NULL);
}

static gboolean
find_index (DesktopModel *self, const char *name, guint *index)
{
  guint n = g_list_model_get_n_items (G_LIST_MODEL (self->items));

  for (guint i = 0; i < n; i++)
    {
      g_autoptr (DesktopItem) item = g_list_model_get_item (G_LIST_MODEL (self->items), i);

      if (g_strcmp0 (desktop_item_get_name (item), name) == 0)
        {
          if (index)
            *index = i;
          return TRUE;
        }
    }
  return FALSE;
}

DesktopItem *
desktop_model_lookup (DesktopModel *self, const char *name)
{
  guint i;
  DesktopItem *item;

  g_return_val_if_fail (DESKTOP_IS_MODEL (self), NULL);
  if (!find_index (self, name, &i))
    return NULL;
  item = g_list_model_get_item (G_LIST_MODEL (self->items), i);
  g_object_unref (item);        /* the store keeps it alive */
  return item;
}

/* Creates, positions and inserts an item; NULL if hidden or already present. */
static DesktopItem *
add_item (DesktopModel *self, GFile *file)
{
  g_autofree char *name = g_file_get_basename (file);
  g_autoptr (DesktopItem) item = NULL;
  int x, y;

  if (is_hidden (name) || find_index (self, name, NULL))
    return NULL;

  item = desktop_item_new (file);
  if (position_store_lookup (self->store, name, &x, &y))
    desktop_item_set_position (item, x, y);
  insert_sorted (self, item);
  return item;                  /* borrowed from the store */
}

static void
remove_item (DesktopModel *self, const char *name)
{
  guint i;

  if (find_index (self, name, &i))
    g_list_store_remove (self->items, i);
  position_store_remove (self->store, name);
}

static void
on_item_reloaded (GObject *source, GAsyncResult *res, gpointer data G_GNUC_UNUSED)
{
  g_autoptr (GError) error = NULL;

  if (!desktop_item_reload_finish (DESKTOP_ITEM (source), res, &error))
    g_debug ("reload failed: %s", error->message);
}

static void
reload_item (DesktopItem *item)
{
  desktop_item_reload_async (item, NULL, on_item_reloaded, NULL);
}

static void
on_monitor_changed (GFileMonitor *monitor G_GNUC_UNUSED, GFile *file, GFile *other,
                    GFileMonitorEvent event, gpointer data)
{
  DesktopModel *self = data;
  g_autofree char *name = g_file_get_basename (file);
  DesktopItem *item;

  switch (event)
    {
    case G_FILE_MONITOR_EVENT_CREATED:
    case G_FILE_MONITOR_EVENT_MOVED_IN:
      if ((item = add_item (self, file)) != NULL)
        reload_item (item);
      break;

    case G_FILE_MONITOR_EVENT_DELETED:
    case G_FILE_MONITOR_EVENT_MOVED_OUT:
      if (!is_hidden (name))
        remove_item (self, name);
      break;

    case G_FILE_MONITOR_EVENT_RENAMED:
      {
        g_autofree char *new_name = other ? g_file_get_basename (other) : NULL;
        guint i;

        if (new_name == NULL)
          break;
        if (is_hidden (new_name))
          {
            if (!is_hidden (name))
              remove_item (self, name);
          }
        else if (is_hidden (name) || !find_index (self, name, &i))
          {
            /* An existing item (e.g. a replaced file) just needs a reload. */
            if ((item = add_item (self, other)) != NULL ||
                (item = desktop_model_lookup (self, new_name)) != NULL)
              reload_item (item);
          }
        else if (find_index (self, new_name, NULL))
          {
            /* Renamed onto an existing item (replace via temp file): drop the
               temporary entry and refresh the item that was replaced. */
            g_list_store_remove (self->items, i);
            position_store_remove (self->store, name);
            reload_item (desktop_model_lookup (self, new_name));
          }
        else
          {
            g_autoptr (DesktopItem) moved = g_list_model_get_item (G_LIST_MODEL (self->items), i);

            int x, y;

            position_store_rename (self->store, name, new_name);
            g_list_store_remove (self->items, i);
            desktop_item_set_file (moved, other);
            /* A stored position for the new name (e.g. a preseeded drop that
               arrived via a temporary file) wins over the old one. */
            if (position_store_lookup (self->store, new_name, &x, &y))
              desktop_item_set_position (moved, x, y);
            else
              desktop_item_clear_position (moved);
            insert_sorted (self, moved);
            reload_item (moved);
          }
      }
      break;

    case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
      if (!is_hidden (name) && (item = desktop_model_lookup (self, name)) != NULL)
        reload_item (item);
      break;

    default:
      break;
    }
}

void
desktop_model_set_position (DesktopModel *self, DesktopItem *item, int x, int y)
{
  g_return_if_fail (DESKTOP_IS_MODEL (self));
  g_return_if_fail (DESKTOP_IS_ITEM (item));

  desktop_item_set_position (item, x, y);
  position_store_set (self->store, desktop_item_get_name (item), x, y);
}

void
desktop_model_preseed_position (DesktopModel *self, const char *name, int x, int y)
{
  g_return_if_fail (DESKTOP_IS_MODEL (self));

  position_store_set (self->store, name, x, y);
}

GListModel *
desktop_model_get_items (DesktopModel *self)
{
  g_return_val_if_fail (DESKTOP_IS_MODEL (self), NULL);
  return G_LIST_MODEL (self->items);
}

GFile *
desktop_model_get_directory (DesktopModel *self)
{
  g_return_val_if_fail (DESKTOP_IS_MODEL (self), NULL);
  return self->directory;
}

gboolean
desktop_model_is_monitoring (DesktopModel *self)
{
  g_return_val_if_fail (DESKTOP_IS_MODEL (self), FALSE);
  return self->monitor != NULL;
}

/* ---- loading ---- */

typedef struct {
  GFileEnumerator *enumerator;
  guint            pending;
  gboolean         enumerating;
  GError          *error;
} LoadData;

static void
load_data_free (LoadData *d)
{
  g_clear_object (&d->enumerator);
  g_clear_error (&d->error);
  g_free (d);
}

static void
load_maybe_finish (GTask *task)
{
  LoadData *d = g_task_get_task_data (task);

  if (d->enumerating || d->pending > 0)
    return;
  if (d->error)
    g_task_return_error (task, g_steal_pointer (&d->error));
  else
    g_task_return_boolean (task, TRUE);
}

static void
on_load_item_reloaded (GObject *source, GAsyncResult *res, gpointer data)
{
  GTask *task = data;
  LoadData *d = g_task_get_task_data (task);
  g_autoptr (GError) error = NULL;

  if (!desktop_item_reload_finish (DESKTOP_ITEM (source), res, &error))
    {
      g_debug ("reload failed: %s", error->message);
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        {
          guint i;
          DesktopModel *model = g_task_get_source_object (task);

          if (find_index (model, desktop_item_get_name (DESKTOP_ITEM (source)), &i))
            g_list_store_remove (model->items, i);
        }
    }
  d->pending--;
  load_maybe_finish (task);
  g_object_unref (task);
}

static void
on_next_files (GObject *source, GAsyncResult *res, gpointer data)
{
  GTask *task = data;
  DesktopModel *self = g_task_get_source_object (task);
  LoadData *d = g_task_get_task_data (task);
  GError *error = NULL;
  GList *infos = g_file_enumerator_next_files_finish (G_FILE_ENUMERATOR (source), res, &error);

  if (error)
    {
      d->error = error;
      d->enumerating = FALSE;
      load_maybe_finish (task);
      g_object_unref (task);
      return;
    }

  if (infos == NULL)
    {
      d->enumerating = FALSE;
      g_file_enumerator_close_async (d->enumerator, G_PRIORITY_DEFAULT, NULL, NULL, NULL);
      load_maybe_finish (task);
      g_object_unref (task);
      return;
    }

  for (GList *l = infos; l; l = l->next)
    {
      GFileInfo *info = l->data;
      g_autoptr (GFile) child = g_file_get_child (self->directory, g_file_info_get_name (info));
      DesktopItem *item = add_item (self, child);

      if (item)
        {
          d->pending++;
          desktop_item_reload_async (item, g_task_get_cancellable (task),
                                     on_load_item_reloaded, g_object_ref (task));
        }
    }
  g_list_free_full (infos, g_object_unref);

  g_file_enumerator_next_files_async (d->enumerator, BATCH_SIZE, G_PRIORITY_DEFAULT,
                                      g_task_get_cancellable (task), on_next_files, task);
}

static void
on_enumerated (GObject *source, GAsyncResult *res, gpointer data)
{
  GTask *task = data;
  LoadData *d = g_task_get_task_data (task);
  GError *error = NULL;

  d->enumerator = g_file_enumerate_children_finish (G_FILE (source), res, &error);
  if (d->enumerator == NULL)
    {
      g_task_return_error (task, error);
      g_object_unref (task);
      return;
    }
  g_file_enumerator_next_files_async (d->enumerator, BATCH_SIZE, G_PRIORITY_DEFAULT,
                                      g_task_get_cancellable (task), on_next_files, task);
}

void
desktop_model_load_async (DesktopModel *self, GCancellable *cancellable,
                          GAsyncReadyCallback callback, gpointer user_data)
{
  GTask *task;
  LoadData *d;

  g_return_if_fail (DESKTOP_IS_MODEL (self));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, desktop_model_load_async);
  d = g_new0 (LoadData, 1);
  d->enumerating = TRUE;
  g_task_set_task_data (task, d, (GDestroyNotify) load_data_free);
  g_file_enumerate_children_async (self->directory, G_FILE_ATTRIBUTE_STANDARD_NAME,
                                   G_FILE_QUERY_INFO_NONE, G_PRIORITY_DEFAULT,
                                   cancellable, on_enumerated, task);
}

gboolean
desktop_model_load_finish (DesktopModel *self, GAsyncResult *res, GError **error)
{
  g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
  return g_task_propagate_boolean (G_TASK (res), error);
}

/* ---- object ---- */

static void
desktop_model_dispose (GObject *object)
{
  DesktopModel *self = DESKTOP_MODEL (object);

  if (self->monitor)
    {
      g_signal_handlers_disconnect_by_data (self->monitor, self);
      g_file_monitor_cancel (self->monitor);
      g_clear_object (&self->monitor);
    }
  g_clear_pointer (&self->store, position_store_free);
  g_clear_object (&self->items);
  g_clear_object (&self->directory);

  G_OBJECT_CLASS (desktop_model_parent_class)->dispose (object);
}

static void
desktop_model_class_init (DesktopModelClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = desktop_model_dispose;
}

static void
desktop_model_init (DesktopModel *self)
{
  self->items = g_list_store_new (DESKTOP_TYPE_ITEM);
}

DesktopModel *
desktop_model_new (GFile *directory, PositionStore *store)
{
  DesktopModel *self;
  g_autoptr (GError) error = NULL;

  g_return_val_if_fail (G_IS_FILE (directory), NULL);

  self = g_object_new (DESKTOP_TYPE_MODEL, NULL);
  self->directory = g_object_ref (directory);
  self->store = store;

  if (!g_file_make_directory_with_parents (directory, NULL, &error) &&
      !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS))
    g_warning ("Cannot create %s: %s", g_file_peek_path (directory), error->message);
  g_clear_error (&error);

  self->monitor = g_file_monitor_directory (directory, G_FILE_MONITOR_WATCH_MOVES, NULL, &error);
  if (self->monitor)
    g_signal_connect (self->monitor, "changed", G_CALLBACK (on_monitor_changed), self);
  else
    g_warning ("Cannot monitor %s: %s", g_file_peek_path (directory), error->message);

  return self;
}
