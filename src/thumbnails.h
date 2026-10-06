#pragma once

#include <gio/gio.h>
#include <gdk/gdk.h>

gboolean      thumbnails_is_image   (const char *content_type);
char         *thumbnails_cache_path (GFile *file);
void          thumbnails_load_async (GFile               *file,
                                     const char          *content_type,
                                     GCancellable        *cancellable,
                                     GAsyncReadyCallback  callback,
                                     gpointer             user_data);
GdkPaintable *thumbnails_load_finish (GAsyncResult *res, GError **error);
