/* SPDX-FileCopyrightText: 2026 Jochen Voss <voss@seehuhn.de>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "desktop-item.h"

#define QUERY_ATTRIBUTES \
  "standard::display-name,standard::content-type,standard::icon," \
  "standard::size,standard::type,time::modified"

struct _DesktopItem
{
  GObject    parent_instance;

  GFile     *file;
  char      *name;
  GFileInfo *info;
  GDateTime *modified;
  gboolean   has_position;
  int        x, y;
  gboolean   selected;
};

G_DEFINE_FINAL_TYPE (DesktopItem, desktop_item, G_TYPE_OBJECT)

enum { PROP_SELECTED = 1, PROP_INFO, PROP_POSITION, N_PROPS };
static GParamSpec *props[N_PROPS];

static void
desktop_item_get_property (GObject *object, guint id, GValue *value, GParamSpec *pspec)
{
  DesktopItem *self = DESKTOP_ITEM (object);

  switch (id)
    {
    case PROP_SELECTED:
      g_value_set_boolean (value, self->selected);
      break;
    case PROP_INFO:
      g_value_set_object (value, self->info);
      break;
    case PROP_POSITION:
      g_value_set_boolean (value, self->has_position);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, id, pspec);
    }
}

static void
desktop_item_set_property (GObject *object, guint id, const GValue *value, GParamSpec *pspec)
{
  DesktopItem *self = DESKTOP_ITEM (object);

  switch (id)
    {
    case PROP_SELECTED:
      {
        gboolean selected = g_value_get_boolean (value);
        if (self->selected != selected)
          {
            self->selected = selected;
            g_object_notify_by_pspec (object, props[PROP_SELECTED]);
          }
        break;
      }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, id, pspec);
    }
}

static void
desktop_item_finalize (GObject *object)
{
  DesktopItem *self = DESKTOP_ITEM (object);

  g_clear_object (&self->file);
  g_clear_object (&self->info);
  g_clear_pointer (&self->modified, g_date_time_unref);
  g_free (self->name);
  G_OBJECT_CLASS (desktop_item_parent_class)->finalize (object);
}

static void
desktop_item_class_init (DesktopItemClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);

  oc->get_property = desktop_item_get_property;
  oc->set_property = desktop_item_set_property;
  oc->finalize = desktop_item_finalize;

  props[PROP_SELECTED] = g_param_spec_boolean ("selected", NULL, NULL, FALSE,
                                               G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  props[PROP_INFO] = g_param_spec_object ("info", NULL, NULL, G_TYPE_FILE_INFO,
                                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  props[PROP_POSITION] = g_param_spec_boolean ("position", NULL, NULL, FALSE,
                                               G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (oc, N_PROPS, props);
}

static void
desktop_item_init (DesktopItem *self G_GNUC_UNUSED)
{
}

DesktopItem *
desktop_item_new (GFile *file)
{
  DesktopItem *self = g_object_new (DESKTOP_TYPE_ITEM, NULL);

  desktop_item_set_file (self, file);
  return self;
}

GFile *
desktop_item_get_file (DesktopItem *self)
{
  g_return_val_if_fail (DESKTOP_IS_ITEM (self), NULL);
  return self->file;
}

void
desktop_item_set_file (DesktopItem *self, GFile *file)
{
  g_return_if_fail (DESKTOP_IS_ITEM (self));
  g_return_if_fail (G_IS_FILE (file));

  g_object_ref (file);
  g_clear_object (&self->file);
  self->file = file;
  g_free (self->name);
  self->name = g_file_get_basename (file);
}

const char *
desktop_item_get_name (DesktopItem *self)
{
  g_return_val_if_fail (DESKTOP_IS_ITEM (self), NULL);
  return self->name;
}

const char *
desktop_item_get_display_name (DesktopItem *self)
{
  g_return_val_if_fail (DESKTOP_IS_ITEM (self), NULL);
  return self->info ? g_file_info_get_display_name (self->info) : NULL;
}

const char *
desktop_item_get_content_type (DesktopItem *self)
{
  g_return_val_if_fail (DESKTOP_IS_ITEM (self), NULL);
  return self->info ? g_file_info_get_content_type (self->info) : NULL;
}

goffset
desktop_item_get_size (DesktopItem *self)
{
  g_return_val_if_fail (DESKTOP_IS_ITEM (self), 0);
  return self->info ? g_file_info_get_size (self->info) : 0;
}

GDateTime *
desktop_item_get_modified (DesktopItem *self)
{
  g_return_val_if_fail (DESKTOP_IS_ITEM (self), NULL);
  return self->modified;
}

GIcon *
desktop_item_get_icon (DesktopItem *self)
{
  g_return_val_if_fail (DESKTOP_IS_ITEM (self), NULL);
  return self->info ? g_file_info_get_icon (self->info) : NULL;
}

gboolean
desktop_item_is_directory (DesktopItem *self)
{
  g_return_val_if_fail (DESKTOP_IS_ITEM (self), FALSE);
  return self->info && g_file_info_get_file_type (self->info) == G_FILE_TYPE_DIRECTORY;
}

gboolean
desktop_item_get_position (DesktopItem *self, int *x, int *y)
{
  g_return_val_if_fail (DESKTOP_IS_ITEM (self), FALSE);
  if (!self->has_position)
    return FALSE;
  if (x)
    *x = self->x;
  if (y)
    *y = self->y;
  return TRUE;
}

void
desktop_item_set_position (DesktopItem *self, int x, int y)
{
  g_return_if_fail (DESKTOP_IS_ITEM (self));
  self->has_position = TRUE;
  self->x = x;
  self->y = y;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_POSITION]);
}

void
desktop_item_clear_position (DesktopItem *self)
{
  g_return_if_fail (DESKTOP_IS_ITEM (self));
  self->has_position = FALSE;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_POSITION]);
}

static void
query_done (GObject *source, GAsyncResult *res, gpointer data)
{
  g_autoptr (GTask) task = data;
  DesktopItem *self = g_task_get_source_object (task);
  GError *error = NULL;
  GFileInfo *info = g_file_query_info_finish (G_FILE (source), res, &error);

  if (!info)
    {
      g_task_return_error (task, error);
      return;
    }
  g_clear_object (&self->info);
  self->info = info;
  g_clear_pointer (&self->modified, g_date_time_unref);
  self->modified = g_file_info_get_modification_date_time (info);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_INFO]);
  g_task_return_boolean (task, TRUE);
}

void
desktop_item_reload_async (DesktopItem *self, GCancellable *cancellable,
                           GAsyncReadyCallback callback, gpointer user_data)
{
  g_return_if_fail (DESKTOP_IS_ITEM (self));

  GTask *task = g_task_new (self, cancellable, callback, user_data);
  g_file_query_info_async (self->file, QUERY_ATTRIBUTES, G_FILE_QUERY_INFO_NONE,
                           G_PRIORITY_DEFAULT, cancellable, query_done, task);
}

gboolean
desktop_item_reload_finish (DesktopItem *self, GAsyncResult *res, GError **error)
{
  g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
  return g_task_propagate_boolean (G_TASK (res), error);
}
