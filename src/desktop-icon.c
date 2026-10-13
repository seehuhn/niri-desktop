/* SPDX-FileCopyrightText: 2026 Jochen Voss <voss@seehuhn.de>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "desktop-icon.h"

#include "settings.h"
#include "thumbnails.h"

#include <string.h>

#define DEFAULT_ICON_SIZE 64

struct _DesktopIcon
{
  GtkWidget     parent_instance;

  DesktopItem  *item;
  int           icon_size;

  GtkWidget    *box;
  GtkWidget    *image;
  GtkWidget    *label;
  GtkWidget    *entry;

  GCancellable *thumb_cancellable;

  GList        *(*drag_files_func) (DesktopIcon *, gpointer);
  gpointer      drag_files_data;

  gboolean      rename_pending;  /* label press on a selected item */
  guint         rename_timeout;  /* delayed rename after such a click */
  gboolean      drag_started;    /* drag-begin seen since the last press */
  gboolean      renaming;
};

G_DEFINE_FINAL_TYPE (DesktopIcon, desktop_icon, GTK_TYPE_WIDGET)

enum { PROP_ICON_SIZE = 1, N_PROPS };
static GParamSpec *props[N_PROPS];

enum {
  SIGNAL_CLICKED,
  SIGNAL_RENAME_REQUESTED,
  SIGNAL_DRAG_PREPARED,
  SIGNAL_DRAG_ENDED,
  N_SIGNALS
};
static guint signals[N_SIGNALS];

static gboolean
item_is_selected (DesktopItem *item)
{
  gboolean selected = FALSE;

  g_object_get (item, "selected", &selected, NULL);
  return selected;
}

static void
cancel_rename_timeout (DesktopIcon *self)
{
  g_clear_handle_id (&self->rename_timeout, g_source_remove);
}

/* ---- thumbnails ---- */

static void
cancel_thumbnail (DesktopIcon *self)
{
  if (self->thumb_cancellable != NULL)
    {
      g_cancellable_cancel (self->thumb_cancellable);
      g_clear_object (&self->thumb_cancellable);
    }
}

static void
on_thumbnail_loaded (GObject      *source,
                     GAsyncResult *res,
                     gpointer      user_data)
{
  g_autoptr (DesktopIcon) self = user_data;
  g_autoptr (GError) error = NULL;
  g_autoptr (GdkPaintable) paintable = NULL;

  (void) source;

  paintable = thumbnails_load_finish (res, &error);
  if (paintable == NULL)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_debug ("thumbnail: %s", error->message);
      return;
    }
  gtk_image_set_from_paintable (GTK_IMAGE (self->image), paintable);
}

static void
request_thumbnail (DesktopIcon *self)
{
  const char *content_type;

  cancel_thumbnail (self);
  content_type = desktop_item_get_content_type (self->item);
  if (content_type == NULL || !thumbnails_can_thumbnail (content_type))
    return;

  self->thumb_cancellable = g_cancellable_new ();
  thumbnails_load_async (desktop_item_get_file (self->item), content_type,
                         self->thumb_cancellable, on_thumbnail_loaded,
                         g_object_ref (self));
}

static void
on_map (GtkWidget *widget)
{
  request_thumbnail (DESKTOP_ICON (widget));
}

static void
on_unmap (GtkWidget *widget)
{
  cancel_thumbnail (DESKTOP_ICON (widget));
  cancel_rename_timeout (DESKTOP_ICON (widget));
}

/* ---- item binding ---- */

static void
update_from_info (DesktopIcon *self)
{
  GIcon *icon = desktop_item_get_icon (self->item);
  const char *text = desktop_item_get_display_name (self->item);

  if (icon != NULL)
    gtk_image_set_from_gicon (GTK_IMAGE (self->image), icon);
  else
    gtk_image_set_from_icon_name (GTK_IMAGE (self->image), "text-x-generic");
  gtk_label_set_text (GTK_LABEL (self->label),
                      text != NULL ? text : desktop_item_get_name (self->item));

  if (gtk_widget_get_mapped (GTK_WIDGET (self)))
    request_thumbnail (self);
}

static void
on_item_info (GObject    *object,
              GParamSpec *pspec,
              gpointer    user_data)
{
  (void) object;
  (void) pspec;
  update_from_info (DESKTOP_ICON (user_data));
}

static void
update_selected (DesktopIcon *self)
{
  if (item_is_selected (self->item))
    gtk_widget_set_state_flags (GTK_WIDGET (self), GTK_STATE_FLAG_SELECTED, FALSE);
  else
    {
      gtk_widget_unset_state_flags (GTK_WIDGET (self), GTK_STATE_FLAG_SELECTED);
      cancel_rename_timeout (self);
    }
}

static void
on_item_selected (GObject    *object,
                  GParamSpec *pspec,
                  gpointer    user_data)
{
  (void) object;
  (void) pspec;
  update_selected (DESKTOP_ICON (user_data));
}

/* ---- rename ---- */

static void
end_rename (DesktopIcon *self)
{
  if (!self->renaming)
    return;
  GtkRoot *root = gtk_widget_get_root (GTK_WIDGET (self));
  GtkWidget *focus = root != NULL ? gtk_root_get_focus (root) : NULL;
  GtkWidget *parent = gtk_widget_get_parent (GTK_WIDGET (self));
  gboolean entry_focused = focus != NULL &&
                           (focus == self->entry || gtk_widget_is_ancestor (focus, self->entry));

  self->renaming = FALSE;
  gtk_widget_set_visible (self->entry, FALSE);
  gtk_widget_set_visible (self->label, TRUE);
  /* Hand keyboard focus back to the parent (the canvas), unless it has
   * already moved somewhere else. */
  if (entry_focused && parent != NULL)
    gtk_widget_grab_focus (parent);
}

void
desktop_icon_start_rename (DesktopIcon *self)
{
  const char *name;
  const char *dot;
  int end = -1;

  g_return_if_fail (DESKTOP_IS_ICON (self));

  name = desktop_item_get_name (self->item);
  gtk_editable_set_text (GTK_EDITABLE (self->entry), name);
  dot = desktop_item_is_directory (self->item) ? NULL : strrchr (name, '.');
  if (dot != NULL && dot != name)
    end = (int) g_utf8_pointer_to_offset (name, dot);

  self->renaming = TRUE;
  gtk_widget_set_visible (self->label, FALSE);
  gtk_widget_set_visible (self->entry, TRUE);
  gtk_widget_grab_focus (self->entry);
  gtk_editable_select_region (GTK_EDITABLE (self->entry), 0, end);
}

static void
on_entry_activate (GtkEntry *entry,
                   gpointer  user_data)
{
  DesktopIcon *self = DESKTOP_ICON (user_data);
  const char *text = gtk_editable_get_text (GTK_EDITABLE (entry));
  g_autofree char *new_name = NULL;
  gboolean handled = FALSE;

  if (text[0] == '\0' || g_strcmp0 (text, desktop_item_get_name (self->item)) == 0)
    {
      end_rename (self);
      return;
    }

  new_name = g_strdup (text);
  g_object_ref (self);
  g_signal_emit (self, signals[SIGNAL_RENAME_REQUESTED], 0, new_name, &handled);
  if (handled)
    end_rename (self);
  else if (self->renaming)
    gtk_widget_grab_focus (self->entry);
  g_object_unref (self);
}

static gboolean
on_entry_key_pressed (GtkEventControllerKey *controller,
                      guint                  keyval,
                      guint                  keycode,
                      GdkModifierType        state,
                      gpointer               user_data)
{
  (void) controller;
  (void) keycode;
  (void) state;

  if (keyval == GDK_KEY_F2)
    return GDK_EVENT_STOP;        /* do not restart the rename */
  if (keyval != GDK_KEY_Escape)
    return GDK_EVENT_PROPAGATE;
  end_rename (DESKTOP_ICON (user_data));
  return GDK_EVENT_STOP;
}

static void
on_entry_focus_leave (GtkEventControllerFocus *controller,
                      gpointer                 user_data)
{
  DesktopIcon *self = DESKTOP_ICON (user_data);
  GtkRoot *root = gtk_widget_get_root (GTK_WIDGET (self));

  (void) controller;

  /* Only cancel when focus moved elsewhere inside our window.  If the
   * window itself lost focus (e.g. an error dialog after a failed
   * rename), keep the entry open. */
  if (GTK_IS_WINDOW (root) && gtk_window_is_active (GTK_WINDOW (root)))
    end_rename (self);
}

/* ---- clicks ---- */

static gboolean
on_rename_timeout (gpointer user_data)
{
  DesktopIcon *self = DESKTOP_ICON (user_data);

  self->rename_timeout = 0;
  desktop_icon_start_rename (self);
  return G_SOURCE_REMOVE;
}

static gboolean
point_in_label (DesktopIcon *self,
                double       x,
                double       y)
{
  graphene_point_t p = GRAPHENE_POINT_INIT ((float) x, (float) y);
  graphene_point_t q;

  if (!gtk_widget_get_visible (self->label))
    return FALSE;
  if (!gtk_widget_compute_point (GTK_WIDGET (self), self->label, &p, &q))
    return FALSE;
  return gtk_widget_contains (self->label, q.x, q.y);
}

static void
on_click_pressed (GtkGestureClick *gesture,
                  int              n_press,
                  double           x,
                  double           y,
                  gpointer         user_data)
{
  DesktopIcon *self = DESKTOP_ICON (user_data);
  guint button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture));
  GdkModifierType state =
    gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (gesture));

  cancel_rename_timeout (self);
  self->drag_started = FALSE;
  self->rename_pending = FALSE;

  if (button == GDK_BUTTON_PRIMARY && n_press == 1
      && (state & gtk_accelerator_get_default_mod_mask ()) == 0
      && !self->renaming
      && item_is_selected (self->item)
      && point_in_label (self, x, y))
    {
      /* The canvas narrows a multi-selection to this item on release,
       * so the rename starts on a single selection. */
      self->rename_pending = TRUE;
      g_signal_emit (self, signals[SIGNAL_CLICKED], 0, (int) button, n_press, state);
      return;
    }

  g_signal_emit (self, signals[SIGNAL_CLICKED], 0, (int) button, n_press, state);
}

static void
on_click_released (GtkGestureClick *gesture,
                   int              n_press,
                   double           x,
                   double           y,
                   gpointer         user_data)
{
  DesktopIcon *self = DESKTOP_ICON (user_data);
  gboolean start = self->rename_pending && !self->drag_started;

  (void) gesture;
  (void) n_press;
  (void) x;
  (void) y;

  self->rename_pending = FALSE;
  if (start)
    {
      /* Wait for the double-click time so that a double click on the
       * label activates the item instead of starting a rename. */
      int ms = 400;

      g_object_get (gtk_widget_get_settings (GTK_WIDGET (self)),
                    "gtk-double-click-time", &ms, NULL);
      cancel_rename_timeout (self);
      self->rename_timeout = g_timeout_add (ms, on_rename_timeout, self);
    }
}

static void
on_click_stopped (GtkGestureClick *gesture,
                  gpointer         user_data)
{
  DesktopIcon *self = DESKTOP_ICON (user_data);

  /* Do not cancel the rename timeout here: the gesture stops when its
   * own double-click timer expires, which is no later than ours. */
  (void) gesture;
  self->rename_pending = FALSE;
}

/* ---- drag source ---- */

static GdkContentProvider *
on_drag_prepare (GtkDragSource *source,
                 double         x,
                 double         y,
                 gpointer       user_data)
{
  DesktopIcon *self = DESKTOP_ICON (user_data);
  GSList *files = NULL;
  GdkFileList *file_list;
  GdkContentProvider *provider;
  g_autoptr (GdkPaintable) paintable = NULL;

  if (self->renaming)
    return NULL;

  if (self->drag_files_func != NULL)
    {
      GList *list = self->drag_files_func (self, self->drag_files_data);

      for (GList *l = g_list_last (list); l != NULL; l = l->prev)
        files = g_slist_prepend (files, l->data);
      g_list_free (list);
    }
  if (files == NULL)
    files = g_slist_prepend (files, desktop_item_get_file (self->item));

  file_list = gdk_file_list_new_from_list (files);
  g_slist_free (files);
  provider = gdk_content_provider_new_typed (GDK_TYPE_FILE_LIST, file_list);
  g_boxed_free (GDK_TYPE_FILE_LIST, file_list);

  paintable = gtk_widget_paintable_new (GTK_WIDGET (self));
  gtk_drag_source_set_icon (source, paintable, (int) x, (int) y);

  g_signal_emit (self, signals[SIGNAL_DRAG_PREPARED], 0, x, y);
  return provider;
}

static void
on_drag_begin (GtkDragSource *source,
               GdkDrag       *drag,
               gpointer       user_data)
{
  DesktopIcon *self = DESKTOP_ICON (user_data);

  (void) source;
  (void) drag;
  self->drag_started = TRUE;
  self->rename_pending = FALSE;
  cancel_rename_timeout (self);
}

static void
on_drag_end (GtkDragSource *source G_GNUC_UNUSED,
             GdkDrag       *drag G_GNUC_UNUSED,
             gboolean       delete_data G_GNUC_UNUSED,
             gpointer       user_data)
{
  g_signal_emit (user_data, signals[SIGNAL_DRAG_ENDED], 0);
}

/* ---- GObject ---- */

static void
desktop_icon_dispose (GObject *object)
{
  DesktopIcon *self = DESKTOP_ICON (object);

  cancel_thumbnail (self);
  cancel_rename_timeout (self);
  if (self->item != NULL)
    {
      g_signal_handlers_disconnect_by_data (self->item, self);
      g_clear_object (&self->item);
    }
  g_clear_pointer (&self->box, gtk_widget_unparent);
  self->image = NULL;
  self->label = NULL;
  self->entry = NULL;

  G_OBJECT_CLASS (desktop_icon_parent_class)->dispose (object);
}

static void
desktop_icon_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  DesktopIcon *self = DESKTOP_ICON (object);

  switch (prop_id)
    {
    case PROP_ICON_SIZE:
      g_value_set_int (value, self->icon_size);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
desktop_icon_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  DesktopIcon *self = DESKTOP_ICON (object);

  switch (prop_id)
    {
    case PROP_ICON_SIZE:
      self->icon_size = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
desktop_icon_constructed (GObject *object)
{
  DesktopIcon *self = DESKTOP_ICON (object);

  G_OBJECT_CLASS (desktop_icon_parent_class)->constructed (object);

  gtk_image_set_pixel_size (GTK_IMAGE (self->image), self->icon_size);
  gtk_widget_set_size_request (GTK_WIDGET (self),
                               settings_pitch_width (self->icon_size),
                               settings_pitch_height (self->icon_size));
}

static void
desktop_icon_class_init (DesktopIconClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = desktop_icon_get_property;
  object_class->set_property = desktop_icon_set_property;
  object_class->constructed = desktop_icon_constructed;
  object_class->dispose = desktop_icon_dispose;

  props[PROP_ICON_SIZE] =
    g_param_spec_int ("icon-size", NULL, NULL, 16, 512, DEFAULT_ICON_SIZE,
                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, props);

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_set_css_name (widget_class, "desktopicon");

  signals[SIGNAL_CLICKED] =
    g_signal_new ("clicked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 3, G_TYPE_INT, G_TYPE_INT, GDK_TYPE_MODIFIER_TYPE);
  signals[SIGNAL_RENAME_REQUESTED] =
    g_signal_new ("rename-requested", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, g_signal_accumulator_true_handled, NULL, NULL,
                  G_TYPE_BOOLEAN, 1, G_TYPE_STRING);
  signals[SIGNAL_DRAG_PREPARED] =
    g_signal_new ("drag-prepared", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 2, G_TYPE_DOUBLE, G_TYPE_DOUBLE);
  signals[SIGNAL_DRAG_ENDED] =
    g_signal_new ("drag-ended", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
desktop_icon_init (DesktopIcon *self)
{
  GtkWidget *widget = GTK_WIDGET (self);
  GtkGesture *click;
  GtkDragSource *drag;
  GtkEventController *key;
  GtkEventController *focus;

  gtk_widget_add_css_class (widget, "desktop-icon");

  self->box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_parent (self->box, widget);

  self->image = gtk_image_new ();
  gtk_box_append (GTK_BOX (self->box), self->image);

  self->label = gtk_label_new (NULL);
  gtk_label_set_wrap (GTK_LABEL (self->label), TRUE);
  gtk_label_set_wrap_mode (GTK_LABEL (self->label), PANGO_WRAP_WORD_CHAR);
  gtk_label_set_lines (GTK_LABEL (self->label), 2);
  gtk_label_set_ellipsize (GTK_LABEL (self->label), PANGO_ELLIPSIZE_MIDDLE);
  gtk_label_set_justify (GTK_LABEL (self->label), GTK_JUSTIFY_CENTER);
  /* Let the allocated width decide; wrap and ellipsize handle it. */
  gtk_label_set_max_width_chars (GTK_LABEL (self->label), 1);
  gtk_widget_set_valign (self->label, GTK_ALIGN_START);
  gtk_box_append (GTK_BOX (self->box), self->label);

  self->entry = gtk_entry_new ();
  gtk_editable_set_width_chars (GTK_EDITABLE (self->entry), 1);
  gtk_editable_set_max_width_chars (GTK_EDITABLE (self->entry), 1);
  gtk_widget_set_visible (self->entry, FALSE);
  gtk_box_append (GTK_BOX (self->box), self->entry);
  g_signal_connect (self->entry, "activate", G_CALLBACK (on_entry_activate), self);

  key = gtk_event_controller_key_new ();
  g_signal_connect (key, "key-pressed", G_CALLBACK (on_entry_key_pressed), self);
  gtk_widget_add_controller (self->entry, key);

  /* GtkEntry delegates focus to its internal GtkText, so its own
   * has-focus never becomes TRUE; a focus controller sees focus
   * leaving the entry including its children. */
  focus = gtk_event_controller_focus_new ();
  g_signal_connect (focus, "leave", G_CALLBACK (on_entry_focus_leave), self);
  gtk_widget_add_controller (self->entry, focus);

  click = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click), 0);
  g_signal_connect (click, "pressed", G_CALLBACK (on_click_pressed), self);
  g_signal_connect (click, "released", G_CALLBACK (on_click_released), self);
  g_signal_connect (click, "stopped", G_CALLBACK (on_click_stopped), self);
  gtk_widget_add_controller (widget, GTK_EVENT_CONTROLLER (click));

  drag = gtk_drag_source_new ();
  gtk_drag_source_set_actions (drag, GDK_ACTION_COPY | GDK_ACTION_MOVE);
  g_signal_connect (drag, "prepare", G_CALLBACK (on_drag_prepare), self);
  g_signal_connect (drag, "drag-begin", G_CALLBACK (on_drag_begin), self);
  g_signal_connect (drag, "drag-end", G_CALLBACK (on_drag_end), self);
  gtk_widget_add_controller (widget, GTK_EVENT_CONTROLLER (drag));

  g_signal_connect (widget, "map", G_CALLBACK (on_map), NULL);
  g_signal_connect (widget, "unmap", G_CALLBACK (on_unmap), NULL);
}

GtkWidget *
desktop_icon_new (DesktopItem *item,
                  int          icon_size)
{
  DesktopIcon *self;

  g_return_val_if_fail (DESKTOP_IS_ITEM (item), NULL);

  self = g_object_new (DESKTOP_TYPE_ICON, "icon-size", icon_size, NULL);
  self->item = g_object_ref (item);
  g_signal_connect (item, "notify::info", G_CALLBACK (on_item_info), self);
  g_signal_connect (item, "notify::selected", G_CALLBACK (on_item_selected), self);
  update_from_info (self);
  update_selected (self);

  return GTK_WIDGET (self);
}

DesktopItem *
desktop_icon_get_item (DesktopIcon *self)
{
  g_return_val_if_fail (DESKTOP_IS_ICON (self), NULL);
  return self->item;
}

void
desktop_icon_set_drag_files_func (DesktopIcon *self,
                                  GList       *(*func) (DesktopIcon *, gpointer),
                                  gpointer     data)
{
  g_return_if_fail (DESKTOP_IS_ICON (self));
  self->drag_files_func = func;
  self->drag_files_data = data;
}
