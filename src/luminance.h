/* SPDX-FileCopyrightText: 2026 Jochen Voss <voss@seehuhn.de>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib.h>

/* Mean relative luminance (linear light, 0 = black, 1 = white) over
 * rectangles of an image, from a summed-area table of a downscaled copy. */
typedef struct _LuminanceMap LuminanceMap;

/* Builds the map from width x height pixels of 8-bit sRGB in the byte
 * order R, G, B, A (alpha ignored), `stride` bytes per row. */
LuminanceMap *luminance_map_new  (const guint8 *pixels, int width, int height, gsize stride);
void          luminance_map_free (LuminanceMap *map);

/* Mean luminance of the rectangle (x, y, w, h) in image pixels, clipped to
 * the image.  Returns FALSE if nothing of it lies inside the image. */
gboolean      luminance_map_mean (const LuminanceMap *map, double x, double y,
                                  double w, double h, double *mean);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LuminanceMap, luminance_map_free)
