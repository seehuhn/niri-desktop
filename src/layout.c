#include "layout.h"

typedef struct {
  int x, y, w, h;
} Rect;

static gboolean
rect_intersects (const Rect *a, const Rect *b)
{
  return a->x < b->x + b->w && b->x < a->x + a->w &&
         a->y < b->y + b->h && b->y < a->y + a->h;
}

static gboolean
cell_is_free (const LayoutItem *items, const gboolean *placed, guint n_items,
              const Rect *cell)
{
  for (guint i = 0; i < n_items; i++)
    {
      if (!placed[i])
        continue;
      Rect r = { items[i].out_x, items[i].out_y, items[i].width, items[i].height };
      if (rect_intersects (cell, &r))
        return FALSE;
    }
  return TRUE;
}

static Rect
cell_rect (int viewport_width, int pitch_w, int pitch_h, int c, int r)
{
  Rect cell = { MAX (0, viewport_width - (c + 1) * pitch_w),
                r * pitch_h, pitch_w, pitch_h };
  return cell;
}

int
layout_arrange (LayoutItem *items, guint n_items, int viewport_width, int viewport_height,
                int pitch_w, int pitch_h)
{
  g_autofree gboolean *placed = g_new0 (gboolean, MAX (n_items, 1));

  /* Pass 1: stored positions that fit the viewport stay. */
  for (guint i = 0; i < n_items; i++)
    {
      LayoutItem *it = &items[i];
      if (it->has_position && it->x >= 0 && it->y >= 0 &&
          it->x + it->width <= viewport_width &&
          it->y + it->height <= viewport_height)
        {
          it->out_x = it->x;
          it->out_y = it->y;
          it->parked = FALSE;
          placed[i] = TRUE;
        }
    }

  /* Pass 2: park the rest in free cells, scanning from the top right. */
  int cols = MAX (1, viewport_width / pitch_w);
  int rows = MAX (1, viewport_height / pitch_h);

  for (guint i = 0; i < n_items; i++)
    {
      if (placed[i])
        continue;

      gboolean found = FALSE;
      Rect cell = { 0 };

      for (int c = 0; c < cols && !found; c++)
        for (int r = 0; r < rows && !found; r++)
          {
            cell = cell_rect (viewport_width, pitch_w, pitch_h, c, r);
            found = cell_is_free (items, placed, n_items, &cell);
          }

      while (!found)
        {
          rows++;
          for (int c = 0; c < cols && !found; c++)
            {
              cell = cell_rect (viewport_width, pitch_w, pitch_h, c, rows - 1);
              found = cell_is_free (items, placed, n_items, &cell);
            }
        }

      items[i].out_x = cell.x;
      items[i].out_y = cell.y;
      items[i].parked = TRUE;
      placed[i] = TRUE;
    }

  int content = viewport_height;
  for (guint i = 0; i < n_items; i++)
    content = MAX (content, items[i].out_y + items[i].height);
  return content;
}
