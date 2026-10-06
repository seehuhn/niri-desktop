#pragma once

#include <gio/gio.h>
#include "desktop-item.h"
#include "position-store.h"

G_BEGIN_DECLS

#define DESKTOP_TYPE_MODEL (desktop_model_get_type ())
G_DECLARE_FINAL_TYPE (DesktopModel, desktop_model, DESKTOP, MODEL, GObject)

/* Takes ownership of store; creates the directory if missing. */
DesktopModel *desktop_model_new             (GFile *directory, PositionStore *store);
/* GListStore of DesktopItem, sorted by g_utf8_collate_key_for_filename of name. */
GListModel   *desktop_model_get_items       (DesktopModel *self);
GFile        *desktop_model_get_directory   (DesktopModel *self);
/* FALSE if the GFileMonitor could not be created. */
gboolean      desktop_model_is_monitoring   (DesktopModel *self);
void          desktop_model_load_async      (DesktopModel *self, GCancellable *cancellable,
                                             GAsyncReadyCallback callback, gpointer user_data);
gboolean      desktop_model_load_finish     (DesktopModel *self, GAsyncResult *res, GError **error);
/* transfer none, or NULL */
DesktopItem  *desktop_model_lookup          (DesktopModel *self, const char *name);
/* item + store */
void          desktop_model_set_position    (DesktopModel *self, DesktopItem *item, int x, int y);
/* store only, for drops */
void          desktop_model_preseed_position (DesktopModel *self, const char *name, int x, int y);

G_END_DECLS
