/* SPDX-FileCopyrightText: 2026 Jochen Voss <voss@seehuhn.de>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define DESKTOP_TYPE_BACKGROUND (desktop_background_get_type ())
G_DECLARE_FINAL_TYPE (DesktopBackground, desktop_background, DESKTOP, BACKGROUND, GtkWidget)

GtkWidget *desktop_background_new (void);
/* NULL clears the image.  Takes its own reference. */
void       desktop_background_set_texture (DesktopBackground *self, GdkTexture *texture_or_null);
gboolean   desktop_background_has_texture (DesktopBackground *self);
/* Mean relative luminance (0 = black, 1 = white) of the image under
 * `rect`, in widget coordinates.  FALSE without an image.  The "changed"
 * signal is emitted whenever the image changes. */
gboolean   desktop_background_get_luminance (DesktopBackground *self,
                                             const graphene_rect_t *rect,
                                             double *luminance);

/* Loads an image file in a worker thread, downscaled to at most 3840x3840
 * and rotated per its EXIF orientation. */
void        desktop_background_load_async (const char *path, GCancellable *cancellable,
                                           GAsyncReadyCallback callback, gpointer user_data);
GdkTexture *desktop_background_load_finish (GAsyncResult *result, GError **error);

G_END_DECLS
