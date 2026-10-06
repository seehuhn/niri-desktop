#pragma once

#include <gtk/gtk.h>

#include "desktop-item.h"

G_BEGIN_DECLS

#define DESKTOP_TYPE_ICON (desktop_icon_get_type ())
G_DECLARE_FINAL_TYPE (DesktopIcon, desktop_icon, DESKTOP, ICON, GtkWidget)

GtkWidget   *desktop_icon_new (DesktopItem *item, int icon_size);
DesktopItem *desktop_icon_get_item (DesktopIcon *self);
void         desktop_icon_start_rename (DesktopIcon *self);
/* The canvas supplies the selected files.  The returned list holds
 * GFile pointers (transfer none); the icon frees the list itself. */
void         desktop_icon_set_drag_files_func (DesktopIcon *self,
                                               GList *(*func) (DesktopIcon *, gpointer),
                                               gpointer data);

G_END_DECLS
