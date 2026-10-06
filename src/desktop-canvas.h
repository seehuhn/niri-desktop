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

G_END_DECLS
