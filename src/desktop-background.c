/* SPDX-FileCopyrightText: 2026 Jochen Voss <voss@seehuhn.de>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "desktop-background.h"

#include <gdk-pixbuf/gdk-pixbuf.h>

#include "layout.h"

#define LOAD_MAX_SIZE 3840

struct _DesktopBackground
{
  GtkWidget   parent_instance;
  GdkTexture *texture;
};

G_DEFINE_FINAL_TYPE (DesktopBackground, desktop_background, GTK_TYPE_WIDGET)

static void
desktop_background_snapshot (GtkWidget   *widget,
                             GtkSnapshot *snapshot)
{
  DesktopBackground *self = DESKTOP_BACKGROUND (widget);
  double w = gtk_widget_get_width (widget);
  double h = gtk_widget_get_height (widget);
  double x, y, dw, dh;

  if (self->texture == NULL || w <= 0 || h <= 0)
    return;

  layout_cover_rect (w, h, gdk_texture_get_width (self->texture),
                     gdk_texture_get_height (self->texture), &x, &y, &dw, &dh);
  gtk_snapshot_push_clip (snapshot, &GRAPHENE_RECT_INIT (0, 0, w, h));
  gtk_snapshot_append_scaled_texture (snapshot, self->texture, GSK_SCALING_FILTER_TRILINEAR,
                                      &GRAPHENE_RECT_INIT (x, y, dw, dh));
  gtk_snapshot_pop (snapshot);
}

static void
desktop_background_dispose (GObject *object)
{
  g_clear_object (&DESKTOP_BACKGROUND (object)->texture);
  G_OBJECT_CLASS (desktop_background_parent_class)->dispose (object);
}

static void
desktop_background_class_init (DesktopBackgroundClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = desktop_background_dispose;
  widget_class->snapshot = desktop_background_snapshot;
}

static void
desktop_background_init (DesktopBackground *self)
{
  /* Cropped drawing must not spill outside the widget. */
  gtk_widget_set_overflow (GTK_WIDGET (self), GTK_OVERFLOW_HIDDEN);
}

GtkWidget *
desktop_background_new (void)
{
  return g_object_new (DESKTOP_TYPE_BACKGROUND, NULL);
}

void
desktop_background_set_texture (DesktopBackground *self,
                                GdkTexture        *texture_or_null)
{
  g_return_if_fail (DESKTOP_IS_BACKGROUND (self));

  if (texture_or_null != NULL)
    g_object_ref (texture_or_null);
  g_clear_object (&self->texture);
  self->texture = texture_or_null;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

gboolean
desktop_background_has_texture (DesktopBackground *self)
{
  g_return_val_if_fail (DESKTOP_IS_BACKGROUND (self), FALSE);
  return self->texture != NULL;
}

static void
load_thread (GTask        *task,
             gpointer      source G_GNUC_UNUSED,
             gpointer      task_data,
             GCancellable *cancellable G_GNUC_UNUSED)
{
  const char *path = task_data;
  GError *error = NULL;
  int width = 0, height = 0;
  GdkPixbuf *pixbuf;
  GdkPixbuf *oriented;

  /* Only scale down; small images keep their native size. */
  if (gdk_pixbuf_get_file_info (path, &width, &height) != NULL &&
      (width > LOAD_MAX_SIZE || height > LOAD_MAX_SIZE))
    pixbuf = gdk_pixbuf_new_from_file_at_scale (path, LOAD_MAX_SIZE, LOAD_MAX_SIZE,
                                                TRUE, &error);
  else
    pixbuf = gdk_pixbuf_new_from_file (path, &error);

  if (pixbuf == NULL)
    {
      g_task_return_error (task, error);
      return;
    }
  oriented = gdk_pixbuf_apply_embedded_orientation (pixbuf);
  g_object_unref (pixbuf);
  if (oriented == NULL)
    {
      g_task_return_new_error (task, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                               "Cannot rotate the image");
      return;
    }
  g_task_return_pointer (task, gdk_texture_new_for_pixbuf (oriented), g_object_unref);
  g_object_unref (oriented);
}

void
desktop_background_load_async (const char         *path,
                               GCancellable       *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer            user_data)
{
  g_autoptr (GTask) task = g_task_new (NULL, cancellable, callback, user_data);

  g_task_set_source_tag (task, desktop_background_load_async);
  g_task_set_task_data (task, g_strdup (path), g_free);
  g_task_run_in_thread (task, load_thread);
}

GdkTexture *
desktop_background_load_finish (GAsyncResult *result,
                                GError      **error)
{
  g_return_val_if_fail (g_task_is_valid (result, NULL), NULL);
  return g_task_propagate_pointer (G_TASK (result), error);
}
