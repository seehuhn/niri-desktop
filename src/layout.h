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
