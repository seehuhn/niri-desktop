#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define DESKTOP_TYPE_ITEM (desktop_item_get_type ())
G_DECLARE_FINAL_TYPE (DesktopItem, desktop_item, DESKTOP, ITEM, GObject)

DesktopItem *desktop_item_new (GFile *file);
GFile       *desktop_item_get_file (DesktopItem *self);
void         desktop_item_set_file (DesktopItem *self, GFile *file);     /* after rename */
const char  *desktop_item_get_name (DesktopItem *self);                 /* g_file_get_basename, the store key */
const char  *desktop_item_get_display_name (DesktopItem *self);
const char  *desktop_item_get_content_type (DesktopItem *self);
goffset      desktop_item_get_size (DesktopItem *self);
GDateTime   *desktop_item_get_modified (DesktopItem *self);             /* transfer none, may be NULL */
GIcon       *desktop_item_get_icon (DesktopItem *self);                 /* transfer none, may be NULL */
gboolean     desktop_item_is_directory (DesktopItem *self);
gboolean     desktop_item_get_position (DesktopItem *self, int *x, int *y);
void         desktop_item_set_position (DesktopItem *self, int x, int y);
void         desktop_item_clear_position (DesktopItem *self);
void         desktop_item_reload_async (DesktopItem *self, GCancellable *cancellable,
                                        GAsyncReadyCallback callback, gpointer user_data);
gboolean     desktop_item_reload_finish (DesktopItem *self, GAsyncResult *res, GError **error);

G_END_DECLS
