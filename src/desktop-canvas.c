#include "desktop-canvas.h"

#include "desktop-layout.h"
#include "settings.h"

struct _DesktopCanvas
{
  GtkWidget parent_instance;

  DesktopModel *model;
  GHashTable   *icons;   /* DesktopItem* -> DesktopIcon* (both unowned) */
  GtkAdjustment *vadj;   /* parent's vertical adjustment while rooted */
  guint         page_tick;  /* pending resize after a page-size change */
};

G_DEFINE_FINAL_TYPE (DesktopCanvas, desktop_canvas, GTK_TYPE_WIDGET)

static gboolean
page_size_tick (GtkWidget     *widget,
                GdkFrameClock *clock G_GNUC_UNUSED,
                gpointer       data G_GNUC_UNUSED)
{
  DESKTOP_CANVAS (widget)->page_tick = 0;
  gtk_widget_queue_resize (widget);
  return G_SOURCE_REMOVE;
}

/* The viewport updates its page size while it allocates the canvas, after
 * measuring it, and a resize queued at that point is dropped when the
 * allocation finishes.  Queue the resize on the next frame instead. */
static void
on_page_size (DesktopCanvas *self)
{
  if (self->page_tick == 0)
    self->page_tick = gtk_widget_add_tick_callback (GTK_WIDGET (self), page_size_tick,
                                                    NULL, NULL);
}

/* A position change can change the content height, hence a full resize. */
static void
queue_resize (DesktopCanvas *self)
{
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

/* The n-th DesktopIcon child (other children are skipped), or NULL. */
static GtkWidget *
nth_icon (DesktopCanvas *self,
          guint          n)
{
  GtkWidget *child;

  for (child = gtk_widget_get_first_child (GTK_WIDGET (self));
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      if (!DESKTOP_IS_ICON (child))
        continue;
      if (n-- == 0)
        return child;
    }
  return NULL;
}

static void
remove_icon (DesktopCanvas *self,
             GtkWidget     *icon)
{
  DesktopItem *item = desktop_icon_get_item (DESKTOP_ICON (icon));

  g_signal_handlers_disconnect_by_func (item, queue_resize, self);
  g_hash_table_remove (self->icons, item);
  gtk_widget_unparent (icon);
}

static void
on_items_changed (GListModel    *list,
                  guint          position,
                  guint          removed,
                  guint          added,
                  DesktopCanvas *self)
{
  int icon_size = settings_get_icon_size ();
  GtkWidget *prev;
  guint i;

  g_return_if_fail (removed == 0 || nth_icon (self, position + removed - 1) != NULL);
  for (i = 0; i < removed; i++)
    remove_icon (self, nth_icon (self, position));

  prev = position > 0 ? nth_icon (self, position - 1) : NULL;
  for (i = 0; i < added; i++)
    {
      g_autoptr (DesktopItem) item = g_list_model_get_item (list, position + i);
      GtkWidget *icon = desktop_icon_new (item, icon_size);

      /* prev == NULL inserts as the first child */
      gtk_widget_insert_after (icon, GTK_WIDGET (self), prev);
      g_hash_table_insert (self->icons, item, icon);
      g_signal_connect_swapped (item, "notify::position", G_CALLBACK (queue_resize), self);
      prev = icon;
    }

  if (removed > 0 || added > 0)
    gtk_widget_queue_resize (GTK_WIDGET (self));
}

static void
desktop_canvas_root (GtkWidget *widget)
{
  DesktopCanvas *self = DESKTOP_CANVAS (widget);
  GtkWidget *parent;

  GTK_WIDGET_CLASS (desktop_canvas_parent_class)->root (widget);

  parent = gtk_widget_get_parent (widget);
  if (GTK_IS_SCROLLABLE (parent))
    {
      GtkAdjustment *vadj = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (parent));

      if (vadj != NULL)
        {
          self->vadj = g_object_ref (vadj);
          g_signal_connect_swapped (vadj, "notify::page-size", G_CALLBACK (on_page_size), self);
        }
    }
}

static void
disconnect_vadj (DesktopCanvas *self)
{
  if (self->vadj == NULL)
    return;
  g_signal_handlers_disconnect_by_func (self->vadj, on_page_size, self);
  g_clear_object (&self->vadj);
  if (self->page_tick != 0)
    {
      gtk_widget_remove_tick_callback (GTK_WIDGET (self), self->page_tick);
      self->page_tick = 0;
    }
}

static void
desktop_canvas_unroot (GtkWidget *widget)
{
  disconnect_vadj (DESKTOP_CANVAS (widget));
  GTK_WIDGET_CLASS (desktop_canvas_parent_class)->unroot (widget);
}

static void
desktop_canvas_dispose (GObject *object)
{
  DesktopCanvas *self = DESKTOP_CANVAS (object);
  GtkWidget *child;

  if (self->model != NULL)
    g_signal_handlers_disconnect_by_func (desktop_model_get_items (self->model),
                                          on_items_changed, self);
  disconnect_vadj (self);
  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self))) != NULL)
    remove_icon (self, child);
  g_clear_object (&self->model);
  g_clear_pointer (&self->icons, g_hash_table_unref);

  G_OBJECT_CLASS (desktop_canvas_parent_class)->dispose (object);
}

static void
desktop_canvas_class_init (DesktopCanvasClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = desktop_canvas_dispose;
  widget_class->root = desktop_canvas_root;
  widget_class->unroot = desktop_canvas_unroot;
  gtk_widget_class_set_layout_manager_type (widget_class, DESKTOP_TYPE_LAYOUT);
  gtk_widget_class_set_css_name (widget_class, "desktopcanvas");
}

static void
desktop_canvas_init (DesktopCanvas *self)
{
  GtkWidget *widget = GTK_WIDGET (self);

  self->icons = g_hash_table_new (NULL, NULL);
  gtk_widget_set_hexpand (widget, TRUE);
  gtk_widget_set_vexpand (widget, TRUE);
  gtk_widget_set_focusable (widget, TRUE);
  gtk_widget_add_css_class (widget, "desktop-canvas");
}

GtkWidget *
desktop_canvas_new (DesktopModel *model)
{
  DesktopCanvas *self;
  GListModel *items;

  g_return_val_if_fail (DESKTOP_IS_MODEL (model), NULL);

  self = g_object_new (DESKTOP_TYPE_CANVAS, NULL);
  self->model = g_object_ref (model);
  items = desktop_model_get_items (model);
  g_signal_connect (items, "items-changed", G_CALLBACK (on_items_changed), self);
  on_items_changed (items, 0, 0, g_list_model_get_n_items (items), self);

  return GTK_WIDGET (self);
}

DesktopModel *
desktop_canvas_get_model (DesktopCanvas *self)
{
  g_return_val_if_fail (DESKTOP_IS_CANVAS (self), NULL);
  return self->model;
}

DesktopIcon *
desktop_canvas_icon_for_item (DesktopCanvas *self,
                              DesktopItem   *item)
{
  g_return_val_if_fail (DESKTOP_IS_CANVAS (self), NULL);
  return g_hash_table_lookup (self->icons, item);
}
