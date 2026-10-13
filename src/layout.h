/* SPDX-FileCopyrightText: 2026 Jochen Voss <voss@seehuhn.de>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib.h>

typedef struct {
  gboolean has_position;   /* in: stored position present */
  int      x, y;           /* in: stored position */
  int      width, height;  /* in: icon size */
  int      out_x, out_y;   /* out: display position */
  gboolean parked;         /* out: TRUE if not displayed at stored position */
} LayoutItem;

/* Items are processed in array order; returns content height (>= viewport_height). */
int layout_arrange (LayoutItem *items, guint n_items, int viewport_width, int viewport_height,
                    int pitch_w, int pitch_h);

/* Moves the top-left (x, y) of a group_w x group_h rectangle so that the
 * rectangle lies inside 0..viewport_w x 0..viewport_h.  Where the viewport
 * is smaller than the group, that coordinate is pinned to 0. */
void layout_clamp_group (int *x, int *y, int group_w, int group_h,
                         int viewport_w, int viewport_h);

/* "Cover" placement: scales an iw x ih image proportionally until it covers
 * a w x h area, centred.  Returns the drawn rectangle (x, y may be negative). */
void layout_cover_rect (double w, double h, double iw, double ih,
                        double *x, double *y, double *dw, double *dh);
