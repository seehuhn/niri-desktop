#pragma once

#include <gtk/gtk.h>

#include "desktop-icon.h"
#include "desktop-model.h"

G_BEGIN_DECLS

#define DESKTOP_TYPE_CANVAS (desktop_canvas_get_type ())
G_DECLARE_FINAL_TYPE (DesktopCanvas, desktop_canvas, DESKTOP, CANVAS, GtkWidget)

GtkWidget    *desktop_canvas_new            (DesktopModel *model);
DesktopModel *desktop_canvas_get_model      (DesktopCanvas *self);
/* transfer none, or NULL */
DesktopIcon  *desktop_canvas_icon_for_item  (DesktopCanvas *self, DesktopItem *item);

/* Selected items in model order: transfer container, DesktopItem* */
GList        *desktop_canvas_get_selected_items (DesktopCanvas *self);
/* Selected files in model order: transfer container, GFile* (unowned) */
GList        *desktop_canvas_get_selected_files (DesktopCanvas *self);
void          desktop_canvas_select_all         (DesktopCanvas *self);
void          desktop_canvas_clear_selection    (DesktopCanvas *self);

G_END_DECLS
