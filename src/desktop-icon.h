/* SPDX-FileCopyrightText: 2026 Jochen Voss <voss@seehuhn.de>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

#include "desktop-item.h"

G_BEGIN_DECLS

#define DESKTOP_TYPE_ICON (desktop_icon_get_type ())
G_DECLARE_FINAL_TYPE (DesktopIcon, desktop_icon, DESKTOP, ICON, GtkWidget)

/* Signals (besides clicked and rename-requested):
 *   drag-prepared (double x, double y): a drag starts, pointer at (x, y)
 *     in icon coordinates.
 *   drag-ended (void): the drag started from this icon has ended. */

GtkWidget   *desktop_icon_new (DesktopItem *item, int icon_size);
DesktopItem *desktop_icon_get_item (DesktopIcon *self);
void         desktop_icon_start_rename (DesktopIcon *self);
/* The canvas supplies the selected files.  The returned list holds
 * GFile pointers (transfer none); the icon frees the list itself. */
void         desktop_icon_set_drag_files_func (DesktopIcon *self,
                                               GList *(*func) (DesktopIcon *, gpointer),
                                               gpointer data);

G_END_DECLS
