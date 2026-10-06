#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define DESKTOP_TYPE_LAYOUT (desktop_layout_get_type ())
G_DECLARE_FINAL_TYPE (DesktopLayout, desktop_layout, DESKTOP, LAYOUT, GtkLayoutManager)

/* Children are DesktopIcon, in model order; positions come from
 * desktop_item_get_position of each child's item. */
GtkLayoutManager *desktop_layout_new (void);

G_END_DECLS
