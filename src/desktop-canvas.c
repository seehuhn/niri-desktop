/* SPDX-FileCopyrightText: 2026 Jochen Voss <voss@seehuhn.de>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "desktop-canvas.h"

#include <math.h>

#include "actions.h"
#include "desktop-layout.h"
#include "layout.h"
#include "settings.h"

struct _DesktopCanvas
{
  GtkWidget parent_instance;

  DesktopModel *model;
  GHashTable   *icons;   /* DesktopItem* -> DesktopIcon* (both unowned) */
  GtkAdjustment *vadj;   /* parent's vertical adjustment while rooted */
  guint         page_tick;  /* pending resize after a page-size change */
  guint         place_tick; /* pending save of new icons' positions */
  guint         selection_idle;  /* pending selection-changed emission */

  double        press_x, press_y;  /* last press, canvas coordinates */
  DesktopItem  *focus_item;        /* most recently selected, unowned */
  DesktopItem  *pending_single;    /* select only this on release, unowned */

  gboolean      band_active;
  gboolean      band_moved;        /* the band left its origin */
  gboolean      band_add;          /* Ctrl held: the band only adds */
  double        band_x, band_y;    /* origin */
  graphene_rect_t band;

  /* The drag started from one of our icons, or drag_source_item NULL. */
  DesktopItem  *drag_source_item;  /* owned */
  double        drag_hot_x, drag_hot_y;  /* pointer inside the dragged icon */
  GArray       *drag_offsets;      /* DragOffset, one per dragged item */
};

typedef struct
{
  DesktopItem *item;  /* owned */
  double       dx, dy;  /* allocation origin minus the dragged icon's */
} DragOffset;

G_DEFINE_FINAL_TYPE (DesktopCanvas, desktop_canvas, GTK_TYPE_WIDGET)

enum {
  SIGNAL_ACTIVATE_SELECTION,
  SIGNAL_CONTEXT_MENU_REQUESTED,
  SIGNAL_SELECTION_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

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

/* Saves where the layout put each icon without a stored position, so that
 * later arrivals cannot push it to another cell.  Icons outside the visible
 * area stay unplaced, as a stored position there would not be honoured. */
static gboolean
place_tick (GtkWidget     *widget,
            GdkFrameClock *clock G_GNUC_UNUSED,
            gpointer       data G_GNUC_UNUSED)
{
  DesktopCanvas *self = DESKTOP_CANVAS (widget);
  int vp_w = gtk_widget_get_width (widget);
  int vp_h = self->vadj != NULL ? (int) gtk_adjustment_get_page_size (self->vadj)
                                : gtk_widget_get_height (widget);
  GtkWidget *child;

  self->place_tick = 0;
  if (vp_w <= 0 || vp_h <= 0)
    return G_SOURCE_REMOVE;

  for (child = gtk_widget_get_first_child (widget);
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      DesktopItem *item;
      graphene_rect_t b;

      if (!DESKTOP_IS_ICON (child))
        continue;
      item = desktop_icon_get_item (DESKTOP_ICON (child));
      if (desktop_item_get_position (item, NULL, NULL)
          || !gtk_widget_compute_bounds (child, widget, &b))
        continue;
      if (b.origin.x >= 0 && b.origin.y >= 0 &&
          b.origin.x + b.size.width <= vp_w && b.origin.y + b.size.height <= vp_h)
        desktop_model_set_position (self->model, item, (int) b.origin.x, (int) b.origin.y);
    }
  return G_SOURCE_REMOVE;
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

/* ---- selection ---- */

static void
emit_selection_changed (gpointer data)
{
  DesktopCanvas *self = data;

  self->selection_idle = 0;
  g_signal_emit (self, signals[SIGNAL_SELECTION_CHANGED], 0);
}

/* Coalesces the per-item notifications into one emission per main-loop
 * iteration. */
static void
queue_selection_changed (DesktopCanvas *self)
{
  if (self->selection_idle == 0)
    self->selection_idle = g_idle_add_once (emit_selection_changed, self);
}

static gboolean
is_selected (DesktopItem *item)
{
  gboolean selected = FALSE;

  g_object_get (item, "selected", &selected, NULL);
  return selected;
}

static void
set_selected (DesktopItem *item,
              gboolean     selected)
{
  g_object_set (item, "selected", selected, NULL);
}

static void
set_all_selected (DesktopCanvas *self,
                  gboolean       selected)
{
  GListModel *items = desktop_model_get_items (self->model);
  guint i, n = g_list_model_get_n_items (items);

  for (i = 0; i < n; i++)
    {
      g_autoptr (DesktopItem) item = g_list_model_get_item (items, i);

      set_selected (item, selected);
    }
}

static void
select_only (DesktopCanvas *self,
             DesktopItem   *item)
{
  GListModel *items = desktop_model_get_items (self->model);
  guint i, n = g_list_model_get_n_items (items);

  for (i = 0; i < n; i++)
    {
      g_autoptr (DesktopItem) other = g_list_model_get_item (items, i);

      set_selected (other, other == item);
    }
  self->focus_item = item;
}

static guint
count_selected (DesktopCanvas *self)
{
  GListModel *items = desktop_model_get_items (self->model);
  guint i, n = g_list_model_get_n_items (items), count = 0;

  for (i = 0; i < n; i++)
    {
      g_autoptr (DesktopItem) item = g_list_model_get_item (items, i);

      if (is_selected (item))
        count++;
    }
  return count;
}

/* Whether the widget at (x, y) in canvas coordinates lies inside an icon. */
static gboolean
pick_is_icon (DesktopCanvas *self,
              double         x,
              double         y)
{
  GtkWidget *w = gtk_widget_pick (GTK_WIDGET (self), x, y, GTK_PICK_DEFAULT);

  for (; w != NULL && w != GTK_WIDGET (self); w = gtk_widget_get_parent (w))
    if (DESKTOP_IS_ICON (w))
      return TRUE;
  return FALSE;
}

static void
on_icon_clicked (DesktopIcon     *icon,
                 int              button,
                 int              n_press,
                 GdkModifierType  state,
                 DesktopCanvas   *self)
{
  DesktopItem *item = desktop_icon_get_item (icon);
  GdkModifierType mods = state & gtk_accelerator_get_default_mod_mask ();

  if (button == GDK_BUTTON_PRIMARY && n_press == 1)
    {
      if (mods & GDK_CONTROL_MASK)
        {
          gboolean selected = !is_selected (item);

          set_selected (item, selected);
          if (selected)
            self->focus_item = item;
        }
      else if (mods & GDK_SHIFT_MASK)
        {
          set_selected (item, TRUE);
          self->focus_item = item;
        }
      else if (is_selected (item) && count_selected (self) > 1)
        {
          /* Keep the selection for a drag; a plain click narrows it
           * on release. */
          self->pending_single = item;
          self->focus_item = item;
        }
      else
        select_only (self, item);
    }
  else if (button == GDK_BUTTON_PRIMARY && n_press == 2)
    {
      g_signal_emit (self, signals[SIGNAL_ACTIVATE_SELECTION], 0);
    }
  else if (button == GDK_BUTTON_SECONDARY && n_press == 1)
    {
      if (!is_selected (item))
        select_only (self, item);
      g_signal_emit (self, signals[SIGNAL_CONTEXT_MENU_REQUESTED], 0,
                     item, self->press_x, self->press_y);
    }
}

/* ---- capture-phase press tracking ---- */

static gboolean
pick_is_editable (DesktopCanvas *self,
                  double         x,
                  double         y)
{
  GtkWidget *w = gtk_widget_pick (GTK_WIDGET (self), x, y, GTK_PICK_DEFAULT);

  for (; w != NULL && w != GTK_WIDGET (self); w = gtk_widget_get_parent (w))
    if (GTK_IS_EDITABLE (w))
      return TRUE;
  return FALSE;
}

static void
on_capture_pressed (GtkGestureClick *gesture G_GNUC_UNUSED,
                    int              n_press G_GNUC_UNUSED,
                    double           x,
                    double           y,
                    DesktopCanvas   *self)
{
  self->press_x = x;
  self->press_y = y;
  self->pending_single = NULL;
  /* Leave the focus in a rename entry that is being clicked into. */
  if (!pick_is_editable (self, x, y))
    gtk_widget_grab_focus (GTK_WIDGET (self));
}

static void
on_capture_released (GtkGestureClick *gesture G_GNUC_UNUSED,
                     int              n_press G_GNUC_UNUSED,
                     double           x G_GNUC_UNUSED,
                     double           y G_GNUC_UNUSED,
                     DesktopCanvas   *self)
{
  if (self->pending_single != NULL)
    select_only (self, self->pending_single);
  self->pending_single = NULL;
}

static void
on_capture_stopped (GtkGestureClick *gesture G_GNUC_UNUSED,
                    DesktopCanvas   *self)
{
  /* Motion beyond the drag threshold or a claimed sequence (an icon
   * drag) stops the gesture without a release. */
  self->pending_single = NULL;
}

/* ---- clicks on the empty canvas ---- */

static void
on_click_pressed (GtkGestureClick *gesture,
                  int              n_press,
                  double           x,
                  double           y,
                  DesktopCanvas   *self)
{
  guint button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture));

  if (pick_is_icon (self, x, y))
    {
      gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_DENIED);
      return;
    }
  if (button == GDK_BUTTON_SECONDARY && n_press == 1)
    g_signal_emit (self, signals[SIGNAL_CONTEXT_MENU_REQUESTED], 0, NULL, x, y);
}

static void
on_click_released (GtkGestureClick *gesture,
                   int              n_press G_GNUC_UNUSED,
                   double           x G_GNUC_UNUSED,
                   double           y G_GNUC_UNUSED,
                   DesktopCanvas   *self)
{
  guint button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture));
  GdkModifierType state =
    gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (gesture));

  if (button != GDK_BUTTON_PRIMARY || self->band_moved)
    return;
  if ((state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) != 0)
    return;
  set_all_selected (self, FALSE);
}

/* ---- rubber band ---- */

static void
on_band_begin (GtkGestureDrag *gesture,
               double          x,
               double          y,
               DesktopCanvas  *self)
{
  GdkModifierType state =
    gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (gesture));

  self->band_moved = FALSE;
  if (pick_is_icon (self, x, y))
    {
      gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_DENIED);
      return;
    }

  /* Ctrl or Shift: the band adds to the selection. */
  self->band_add = (state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) != 0;
  if (!self->band_add)
    set_all_selected (self, FALSE);
  self->band_active = TRUE;
  self->band_x = x;
  self->band_y = y;
  graphene_rect_init (&self->band, x, y, 0, 0);
}

static void
on_band_update (GtkGestureDrag *gesture G_GNUC_UNUSED,
                double          dx,
                double          dy,
                DesktopCanvas  *self)
{
  GtkWidget *child;

  if (!self->band_active)
    return;
  if (dx != 0 || dy != 0)
    self->band_moved = TRUE;

  graphene_rect_init (&self->band,
                      dx < 0 ? self->band_x + dx : self->band_x,
                      dy < 0 ? self->band_y + dy : self->band_y,
                      fabs (dx), fabs (dy));

  for (child = gtk_widget_get_first_child (GTK_WIDGET (self));
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      graphene_rect_t bounds;
      gboolean hit;

      if (!DESKTOP_IS_ICON (child)
          || !gtk_widget_compute_bounds (child, GTK_WIDGET (self), &bounds))
        continue;
      /* Compare edges by hand: graphene_rect_intersection reports no
       * intersection for a zero-width or zero-height band. */
      hit = self->band.origin.x <= bounds.origin.x + bounds.size.width
            && bounds.origin.x <= self->band.origin.x + self->band.size.width
            && self->band.origin.y <= bounds.origin.y + bounds.size.height
            && bounds.origin.y <= self->band.origin.y + self->band.size.height;
      if (hit)
        set_selected (desktop_icon_get_item (DESKTOP_ICON (child)), TRUE);
      else if (!self->band_add)
        set_selected (desktop_icon_get_item (DESKTOP_ICON (child)), FALSE);
    }

  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
on_band_end (GtkGestureDrag *gesture G_GNUC_UNUSED,
             double          dx G_GNUC_UNUSED,
             double          dy G_GNUC_UNUSED,
             DesktopCanvas  *self)
{
  if (!self->band_active)
    return;
  self->band_active = FALSE;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
desktop_canvas_snapshot (GtkWidget   *widget,
                         GtkSnapshot *snapshot)
{
  DesktopCanvas *self = DESKTOP_CANVAS (widget);
  GskRoundedRect outline;
  GdkRGBA border, fill;
  const float widths[4] = { 1, 1, 1, 1 };

  GTK_WIDGET_CLASS (desktop_canvas_parent_class)->snapshot (widget, snapshot);

  if (!self->band_active)
    return;

  /* The foreground colour stands in for the theme's selection colour,
   * which widget code cannot query without a CSS lookup. */
  gtk_widget_get_color (widget, &border);
  fill = border;
  fill.alpha = 0.2f;
  gtk_snapshot_append_color (snapshot, &fill, &self->band);
  gsk_rounded_rect_init_from_rect (&outline, &self->band, 0);
  gtk_snapshot_append_border (snapshot, &outline, widths,
                              (const GdkRGBA[4]) { border, border, border, border });
}

/* ---- keyboard ---- */

static void
icon_centre (DesktopCanvas   *self,
             GtkWidget       *icon,
             graphene_point_t *centre)
{
  graphene_rect_t bounds;

  if (!gtk_widget_compute_bounds (icon, GTK_WIDGET (self), &bounds))
    graphene_rect_init (&bounds, 0, 0, 0, 0);
  graphene_rect_get_center (&bounds, centre);
}

/* The icon nearest to from in the direction (ux, uy), within 45 degrees
 * of that axis, or NULL. */
static GtkWidget *
nearest_icon (DesktopCanvas *self,
              GtkWidget     *from,
              int            ux,
              int            uy)
{
  graphene_point_t origin;
  GtkWidget *child, *best = NULL;
  float best_dist = G_MAXFLOAT;

  icon_centre (self, from, &origin);
  for (child = gtk_widget_get_first_child (GTK_WIDGET (self));
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      graphene_point_t c;
      float dx, dy, along, across, dist;

      if (child == from || !DESKTOP_IS_ICON (child))
        continue;
      icon_centre (self, child, &c);
      dx = c.x - origin.x;
      dy = c.y - origin.y;
      along = ux != 0 ? ux * dx : uy * dy;
      across = ux != 0 ? fabsf (dy) : fabsf (dx);
      if (along <= across)
        continue;
      dist = dx * dx + dy * dy;
      if (dist < best_dist)
        {
          best_dist = dist;
          best = child;
        }
    }
  return best;
}

static GtkWidget *
first_selected_icon (DesktopCanvas *self)
{
  GtkWidget *child;

  for (child = gtk_widget_get_first_child (GTK_WIDGET (self));
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    if (DESKTOP_IS_ICON (child) && is_selected (desktop_icon_get_item (DESKTOP_ICON (child))))
      return child;
  return NULL;
}

static void
move_selection (DesktopCanvas *self,
                int            ux,
                int            uy)
{
  GtkWidget *from = NULL, *to;

  if (self->focus_item != NULL && is_selected (self->focus_item))
    from = g_hash_table_lookup (self->icons, self->focus_item);
  if (from == NULL)
    from = first_selected_icon (self);

  to = from != NULL ? nearest_icon (self, from, ux, uy) : nth_icon (self, 0);
  if (to == NULL)
    return;
  select_only (self, desktop_icon_get_item (DESKTOP_ICON (to)));
}

/* Requests the context menu for the focused (or first) selected icon at
 * its centre, or for the background at the canvas centre. */
static void
request_context_menu_from_keyboard (DesktopCanvas *self)
{
  GtkWidget *icon = NULL;
  graphene_point_t c;

  if (self->focus_item != NULL && is_selected (self->focus_item))
    icon = g_hash_table_lookup (self->icons, self->focus_item);
  if (icon == NULL)
    icon = first_selected_icon (self);

  if (icon != NULL)
    {
      icon_centre (self, icon, &c);
      g_signal_emit (self, signals[SIGNAL_CONTEXT_MENU_REQUESTED], 0,
                     desktop_icon_get_item (DESKTOP_ICON (icon)), (double) c.x, (double) c.y);
    }
  else
    {
      g_signal_emit (self, signals[SIGNAL_CONTEXT_MENU_REQUESTED], 0, NULL,
                     gtk_widget_get_width (GTK_WIDGET (self)) / 2.0,
                     gtk_widget_get_height (GTK_WIDGET (self)) / 2.0);
    }
}

static gboolean
on_key_pressed (GtkEventControllerKey *controller G_GNUC_UNUSED,
                guint                  keyval,
                guint                  keycode G_GNUC_UNUSED,
                GdkModifierType        state,
                DesktopCanvas         *self)
{
  GdkModifierType mods = state & gtk_accelerator_get_default_mod_mask ();

  if (keyval == GDK_KEY_Menu || (keyval == GDK_KEY_F10 && mods == GDK_SHIFT_MASK))
    {
      request_context_menu_from_keyboard (self);
      return GDK_EVENT_STOP;
    }

  switch (keyval)
    {
    case GDK_KEY_Left:
    case GDK_KEY_KP_Left:
      move_selection (self, -1, 0);
      return GDK_EVENT_STOP;
    case GDK_KEY_Right:
    case GDK_KEY_KP_Right:
      move_selection (self, 1, 0);
      return GDK_EVENT_STOP;
    case GDK_KEY_Up:
    case GDK_KEY_KP_Up:
      move_selection (self, 0, -1);
      return GDK_EVENT_STOP;
    case GDK_KEY_Down:
    case GDK_KEY_KP_Down:
      move_selection (self, 0, 1);
      return GDK_EVENT_STOP;
    case GDK_KEY_Escape:
      set_all_selected (self, FALSE);
      return GDK_EVENT_STOP;
    default:
      return GDK_EVENT_PROPAGATE;
    }
}

/* ---- drag and drop ---- */

static void
drag_offset_clear (gpointer data)
{
  DragOffset *off = data;

  g_clear_object (&off->item);
}

static void
clear_drag (DesktopCanvas *self)
{
  g_clear_object (&self->drag_source_item);
  if (self->drag_offsets != NULL)
    g_array_set_size (self->drag_offsets, 0);
}

static GList *
canvas_drag_files (DesktopIcon *icon,
                   gpointer     data)
{
  DesktopCanvas *self = data;
  DesktopItem *item = desktop_icon_get_item (icon);

  if (!is_selected (item))
    select_only (self, item);
  return desktop_canvas_get_selected_files (self);
}

static void
on_icon_drag_prepared (DesktopIcon   *icon,
                       double         x,
                       double         y,
                       DesktopCanvas *self)
{
  graphene_rect_t origin;
  GList *selected, *l;

  clear_drag (self);
  if (!gtk_widget_compute_bounds (GTK_WIDGET (icon), GTK_WIDGET (self), &origin))
    return;

  self->drag_source_item = g_object_ref (desktop_icon_get_item (icon));
  self->drag_hot_x = x;
  self->drag_hot_y = y;

  selected = desktop_canvas_get_selected_items (self);
  for (l = selected; l != NULL; l = l->next)
    {
      GtkWidget *other = g_hash_table_lookup (self->icons, l->data);
      graphene_rect_t b;
      DragOffset off;

      if (other == NULL || !gtk_widget_compute_bounds (other, GTK_WIDGET (self), &b))
        continue;
      off.item = g_object_ref (l->data);
      off.dx = b.origin.x - origin.origin.x;
      off.dy = b.origin.y - origin.origin.y;
      g_array_append_val (self->drag_offsets, off);
    }
  g_list_free (selected);
}

static void
on_icon_drag_ended (DesktopIcon   *icon G_GNUC_UNUSED,
                    DesktopCanvas *self)
{
  clear_drag (self);
}

/* Whether the drop comes from a drag that one of our icons started. */
static gboolean
is_local_drop (DesktopCanvas *self,
               GdkDrop       *drop)
{
  return drop != NULL && gdk_drop_get_drag (drop) != NULL
         && self->drag_source_item != NULL;
}

static GdkDragAction
on_drop_motion (GtkDropTarget *target,
                double         x G_GNUC_UNUSED,
                double         y G_GNUC_UNUSED,
                DesktopCanvas *self)
{
  GdkDrop *drop = gtk_drop_target_get_current_drop (target);

  if (is_local_drop (self, drop))
    return GDK_ACTION_MOVE;
  /* External drags copy, unless the source only offers a move. */
  if (drop != NULL && gdk_drop_get_actions (drop) == GDK_ACTION_MOVE)
    return GDK_ACTION_MOVE;
  return GDK_ACTION_COPY;
}

/* The area in which the layout keeps stored positions: the canvas width
 * by the viewport's page size (the canvas height without a viewport). */
static void
drop_area (DesktopCanvas *self,
           int           *vp_w,
           int           *vp_h)
{
  *vp_w = gtk_widget_get_width (GTK_WIDGET (self));
  *vp_h = self->vadj != NULL ? (int) gtk_adjustment_get_page_size (self->vadj) : 0;
  if (*vp_h <= 0)
    *vp_h = gtk_widget_get_height (GTK_WIDGET (self));
}

/* Moves every dragged item so that the dragged icon's hot spot lands on
 * (x, y), keeping the items' offsets, and stores the new positions.  The
 * group is shifted as a whole so that every item stays inside the drop
 * area; otherwise the layout would park it. */
static void
move_dragged_items (DesktopCanvas *self,
                    double         x,
                    double         y)
{
  int pitch_h = settings_pitch_height (settings_get_icon_size ());
  int pitch_w = settings_pitch_width (settings_get_icon_size ());
  int min_x = G_MAXINT, min_y = G_MAXINT, max_x = G_MININT, max_y = G_MININT;
  int vp_w, vp_h, gx, gy;
  guint i;

  for (i = 0; i < self->drag_offsets->len; i++)
    {
      DragOffset *off = &g_array_index (self->drag_offsets, DragOffset, i);
      GtkWidget *icon = g_hash_table_lookup (self->icons, off->item);
      int nx, ny;

      /* Skip items deleted during the drag; their key is gone. */
      if (icon == NULL)
        continue;
      nx = (int) (x - self->drag_hot_x + off->dx);
      ny = (int) (y - self->drag_hot_y + off->dy);
      min_x = MIN (min_x, nx);
      min_y = MIN (min_y, ny);
      max_x = MAX (max_x, nx + pitch_w);
      max_y = MAX (max_y, ny + MAX (pitch_h, gtk_widget_get_height (icon)));
    }

  if (min_x == G_MAXINT)
    {
      clear_drag (self);
      return;
    }

  drop_area (self, &vp_w, &vp_h);
  gx = min_x;
  gy = min_y;
  layout_clamp_group (&gx, &gy, max_x - min_x, max_y - min_y, vp_w, vp_h);

  for (i = 0; i < self->drag_offsets->len; i++)
    {
      DragOffset *off = &g_array_index (self->drag_offsets, DragOffset, i);
      int nx, ny;

      if (!g_hash_table_contains (self->icons, off->item))
        continue;
      nx = (int) (x - self->drag_hot_x + off->dx) - min_x + gx;
      ny = (int) (y - self->drag_hot_y + off->dy) - min_y + gy;
      desktop_model_set_position (self->model, off->item, MAX (0, nx), MAX (0, ny));
    }
  clear_drag (self);
}

static gboolean
on_drop (GtkDropTarget *target,
         const GValue  *value,
         double         x,
         double         y,
         DesktopCanvas *self)
{
  GdkDrop *drop = gtk_drop_target_get_current_drop (target);
  int icon_size = settings_get_icon_size ();
  int pitch_w = settings_pitch_width (icon_size);
  int pitch_h = settings_pitch_height (icon_size);
  GtkRoot *root;
  GSList *slist, *sl;
  GList *files = NULL;
  gboolean move;
  int ix, iy, vp_w, vp_h;

  if (is_local_drop (self, drop))
    {
      move_dragged_items (self, x, y);
      return TRUE;
    }

  if (!G_VALUE_HOLDS (value, GDK_TYPE_FILE_LIST))
    return FALSE;
  /* transfer container */
  slist = gdk_file_list_get_files (g_value_get_boxed (value));
  for (sl = slist; sl != NULL; sl = sl->next)
    files = g_list_prepend (files, sl->data);
  files = g_list_reverse (files);
  g_slist_free (slist);
  if (files == NULL)
    return FALSE;

  /* actions_import stacks the files one pitch apart below the point. */
  ix = (int) x - pitch_w / 2;
  iy = (int) y - pitch_h / 2;
  drop_area (self, &vp_w, &vp_h);
  layout_clamp_group (&ix, &iy, pitch_w, (int) g_list_length (files) * pitch_h, vp_w, vp_h);

  move = drop != NULL && gdk_drop_get_actions (drop) == GDK_ACTION_MOVE;
  root = gtk_widget_get_root (GTK_WIDGET (self));
  actions_import (GTK_IS_WINDOW (root) ? GTK_WINDOW (root) : NULL, self->model, files,
                  move, TRUE, ix, iy);
  g_list_free (files);
  return TRUE;
}

/* ---- children ---- */

static gboolean
on_rename_requested (DesktopIcon   *icon,
                     const char    *new_name,
                     DesktopCanvas *self)
{
  DesktopItem *item = desktop_icon_get_item (icon);
  GtkRoot *root = gtk_widget_get_root (GTK_WIDGET (self));

  /* actions_rename reports a failure itself. */
  return actions_rename (GTK_IS_WINDOW (root) ? GTK_WINDOW (root) : NULL,
                         desktop_item_get_file (item), new_name, NULL);
}

static void
remove_icon (DesktopCanvas *self,
             GtkWidget     *icon)
{
  DesktopItem *item = desktop_icon_get_item (DESKTOP_ICON (icon));

  if (self->focus_item == item)
    self->focus_item = NULL;
  if (self->pending_single == item)
    self->pending_single = NULL;
  g_signal_handlers_disconnect_by_func (item, queue_resize, self);
  g_signal_handlers_disconnect_by_func (item, queue_selection_changed, self);
  if (is_selected (item))
    queue_selection_changed (self);
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
      g_signal_connect_swapped (item, "notify::selected",
                                G_CALLBACK (queue_selection_changed), self);
      g_signal_connect (icon, "clicked", G_CALLBACK (on_icon_clicked), self);
      g_signal_connect (icon, "rename-requested", G_CALLBACK (on_rename_requested), self);
      g_signal_connect (icon, "drag-prepared", G_CALLBACK (on_icon_drag_prepared), self);
      g_signal_connect (icon, "drag-ended", G_CALLBACK (on_icon_drag_ended), self);
      desktop_icon_set_drag_files_func (DESKTOP_ICON (icon), canvas_drag_files, self);
      if (is_selected (item))
        queue_selection_changed (self);
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
  DesktopCanvas *self = DESKTOP_CANVAS (widget);

  disconnect_vadj (self);
  if (self->place_tick != 0)
    {
      gtk_widget_remove_tick_callback (widget, self->place_tick);
      self->place_tick = 0;
    }
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
    {
      if (DESKTOP_IS_ICON (child))
        remove_icon (self, child);
      else
        gtk_widget_unparent (child);  /* an open context menu */
    }
  g_clear_handle_id (&self->selection_idle, g_source_remove);
  clear_drag (self);
  g_clear_pointer (&self->drag_offsets, g_array_unref);
  g_clear_object (&self->model);
  g_clear_pointer (&self->icons, g_hash_table_unref);

  G_OBJECT_CLASS (desktop_canvas_parent_class)->dispose (object);
}

/* Popovers parented to the canvas (context menus) follow it. */
static void
desktop_canvas_size_allocate (GtkWidget *widget,
                              int        width,
                              int        height,
                              int        baseline)
{
  DesktopCanvas *self = DESKTOP_CANVAS (widget);
  GtkWidget *child;

  GTK_WIDGET_CLASS (desktop_canvas_parent_class)->size_allocate (widget, width, height, baseline);

  /* Setting positions here would queue a resize during allocation, which
   * GTK drops; save them on the next frame instead. */
  if (self->place_tick == 0)
    for (child = gtk_widget_get_first_child (widget);
         child != NULL;
         child = gtk_widget_get_next_sibling (child))
      if (DESKTOP_IS_ICON (child)
          && !desktop_item_get_position (desktop_icon_get_item (DESKTOP_ICON (child)), NULL, NULL))
        {
          self->place_tick = gtk_widget_add_tick_callback (widget, place_tick, NULL, NULL);
          break;
        }

  for (child = gtk_widget_get_first_child (widget);
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    if (GTK_IS_POPOVER (child))
      gtk_popover_present (GTK_POPOVER (child));
}

static void
desktop_canvas_class_init (DesktopCanvasClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = desktop_canvas_dispose;
  widget_class->root = desktop_canvas_root;
  widget_class->unroot = desktop_canvas_unroot;
  widget_class->snapshot = desktop_canvas_snapshot;
  widget_class->size_allocate = desktop_canvas_size_allocate;
  gtk_widget_class_set_layout_manager_type (widget_class, DESKTOP_TYPE_LAYOUT);
  gtk_widget_class_set_css_name (widget_class, "desktopcanvas");

  signals[SIGNAL_ACTIVATE_SELECTION] =
    g_signal_new ("activate-selection", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
  signals[SIGNAL_CONTEXT_MENU_REQUESTED] =
    g_signal_new ("context-menu-requested", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 3, DESKTOP_TYPE_ITEM, G_TYPE_DOUBLE, G_TYPE_DOUBLE);
  signals[SIGNAL_SELECTION_CHANGED] =
    g_signal_new ("selection-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
desktop_canvas_init (DesktopCanvas *self)
{
  GtkWidget *widget = GTK_WIDGET (self);
  GtkGesture *capture, *click, *band;
  GtkEventController *key;
  GtkDropTarget *drop;

  self->icons = g_hash_table_new (NULL, NULL);
  self->drag_offsets = g_array_new (FALSE, FALSE, sizeof (DragOffset));
  g_array_set_clear_func (self->drag_offsets, drag_offset_clear);

  capture = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (capture), 0);
  gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (capture), GTK_PHASE_CAPTURE);
  g_signal_connect (capture, "pressed", G_CALLBACK (on_capture_pressed), self);
  g_signal_connect (capture, "released", G_CALLBACK (on_capture_released), self);
  g_signal_connect (capture, "stopped", G_CALLBACK (on_capture_stopped), self);
  gtk_widget_add_controller (widget, GTK_EVENT_CONTROLLER (capture));

  click = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click), 0);
  g_signal_connect (click, "pressed", G_CALLBACK (on_click_pressed), self);
  g_signal_connect (click, "released", G_CALLBACK (on_click_released), self);
  gtk_widget_add_controller (widget, GTK_EVENT_CONTROLLER (click));

  band = gtk_gesture_drag_new ();
  g_signal_connect (band, "drag-begin", G_CALLBACK (on_band_begin), self);
  g_signal_connect (band, "drag-update", G_CALLBACK (on_band_update), self);
  g_signal_connect (band, "drag-end", G_CALLBACK (on_band_end), self);
  gtk_widget_add_controller (widget, GTK_EVENT_CONTROLLER (band));

  key = gtk_event_controller_key_new ();
  g_signal_connect (key, "key-pressed", G_CALLBACK (on_key_pressed), self);
  gtk_widget_add_controller (widget, key);

  drop = gtk_drop_target_new (GDK_TYPE_FILE_LIST, GDK_ACTION_COPY | GDK_ACTION_MOVE);
  g_signal_connect (drop, "enter", G_CALLBACK (on_drop_motion), self);
  g_signal_connect (drop, "motion", G_CALLBACK (on_drop_motion), self);
  g_signal_connect (drop, "drop", G_CALLBACK (on_drop), self);
  gtk_widget_add_controller (widget, GTK_EVENT_CONTROLLER (drop));

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

GList *
desktop_canvas_get_selected_items (DesktopCanvas *self)
{
  GListModel *items;
  GList *result = NULL;
  guint i, n;

  g_return_val_if_fail (DESKTOP_IS_CANVAS (self), NULL);

  items = desktop_model_get_items (self->model);
  n = g_list_model_get_n_items (items);
  for (i = n; i > 0; i--)
    {
      g_autoptr (DesktopItem) item = g_list_model_get_item (items, i - 1);

      /* the model holds the reference */
      if (is_selected (item))
        result = g_list_prepend (result, item);
    }
  return result;
}

GList *
desktop_canvas_get_selected_files (DesktopCanvas *self)
{
  GList *list, *l;

  g_return_val_if_fail (DESKTOP_IS_CANVAS (self), NULL);

  list = desktop_canvas_get_selected_items (self);
  for (l = list; l != NULL; l = l->next)
    l->data = desktop_item_get_file (l->data);
  return list;
}

void
desktop_canvas_select_all (DesktopCanvas *self)
{
  g_return_if_fail (DESKTOP_IS_CANVAS (self));
  set_all_selected (self, TRUE);
}

void
desktop_canvas_clear_selection (DesktopCanvas *self)
{
  g_return_if_fail (DESKTOP_IS_CANVAS (self));
  set_all_selected (self, FALSE);
}
