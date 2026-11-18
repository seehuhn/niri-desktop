/* SPDX-FileCopyrightText: 2026 Jochen Voss <voss@seehuhn.de>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "luminance.h"

#include <math.h>

/* Cells along the longer side of the image. */
#define MAP_CELLS 256

struct _LuminanceMap
{
  int     width, height;  /* of the image */
  int     cell;           /* image pixels per cell side */
  int     cols, rows;
  double *sums;           /* (cols + 1) x (rows + 1) summed-area table */
};

static double
srgb_to_linear (double c)
{
  return c <= 0.04045 ? c / 12.92 : pow ((c + 0.055) / 1.055, 2.4);
}

LuminanceMap *
luminance_map_new (const guint8 *pixels,
                   int           width,
                   int           height,
                   gsize         stride)
{
  LuminanceMap *map;
  double lut[256];
  g_autofree double *cells = NULL;
  g_autofree int *counts = NULL;
  int i, x, y, n1;

  g_return_val_if_fail (pixels != NULL && width > 0 && height > 0, NULL);

  for (i = 0; i < 256; i++)
    lut[i] = srgb_to_linear (i / 255.0);

  map = g_new0 (LuminanceMap, 1);
  map->width = width;
  map->height = height;
  map->cell = (MAX (width, height) + MAP_CELLS - 1) / MAP_CELLS;
  map->cols = (width + map->cell - 1) / map->cell;
  map->rows = (height + map->cell - 1) / map->cell;

  cells = g_new0 (double, (gsize) map->cols * map->rows);
  counts = g_new0 (int, (gsize) map->cols * map->rows);
  for (y = 0; y < height; y++)
    {
      const guint8 *p = pixels + y * stride;
      int row = (y / map->cell) * map->cols;

      for (x = 0; x < width; x++, p += 4)
        {
          int k = row + x / map->cell;

          cells[k] += 0.2126 * lut[p[0]] + 0.7152 * lut[p[1]] + 0.0722 * lut[p[2]];
          counts[k]++;
        }
    }

  n1 = map->cols + 1;
  map->sums = g_new0 (double, (gsize) n1 * (map->rows + 1));
  for (y = 0; y < map->rows; y++)
    for (x = 0; x < map->cols; x++)
      map->sums[(y + 1) * n1 + x + 1] = cells[y * map->cols + x] / counts[y * map->cols + x]
                                        + map->sums[y * n1 + x + 1]
                                        + map->sums[(y + 1) * n1 + x]
                                        - map->sums[y * n1 + x];
  return map;
}

void
luminance_map_free (LuminanceMap *map)
{
  if (map == NULL)
    return;
  g_free (map->sums);
  g_free (map);
}

gboolean
luminance_map_mean (const LuminanceMap *map,
                    double              x,
                    double              y,
                    double              w,
                    double              h,
                    double             *mean)
{
  int x0, y0, x1, y1, n1;
  double sum;

  g_return_val_if_fail (map != NULL && mean != NULL, FALSE);

  /* Whole cells touched by the rectangle, clipped to the image. */
  x0 = (int) floor (MAX (x, 0) / map->cell);
  y0 = (int) floor (MAX (y, 0) / map->cell);
  x1 = (int) ceil (MIN (x + w, map->width) / map->cell);
  y1 = (int) ceil (MIN (y + h, map->height) / map->cell);
  if (x0 >= x1 || y0 >= y1)
    return FALSE;

  n1 = map->cols + 1;
  sum = map->sums[y1 * n1 + x1] - map->sums[y0 * n1 + x1]
        - map->sums[y1 * n1 + x0] + map->sums[y0 * n1 + x0];
  *mean = sum / ((double) (x1 - x0) * (y1 - y0));
  return TRUE;
}
