/* SPDX-FileCopyrightText: 2026 Jochen Voss <voss@seehuhn.de>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gio/gio.h>
#include <gdk/gdk.h>

gboolean      thumbnails_is_image   (const char *content_type);
gboolean      thumbnails_can_thumbnail (const char *content_type);
char         *thumbnails_find_exec  (const char *content_type);
char        **thumbnails_expand_exec (const char *template,
                                      const char *uri,
                                      const char *path,
                                      const char *output,
                                      int         size,
                                      GError    **error);
char         *thumbnails_cache_path (GFile *file);
void          thumbnails_load_async (GFile               *file,
                                     const char          *content_type,
                                     GCancellable        *cancellable,
                                     GAsyncReadyCallback  callback,
                                     gpointer             user_data);
GdkPaintable *thumbnails_load_finish (GAsyncResult *res, GError **error);
