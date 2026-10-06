#include "desktop-layout.h"

#include "desktop-icon.h"
#include "layout.h"
#include "settings.h"

struct _DesktopLayout
{
  GtkLayoutManager parent_instance;
};

G_DEFINE_FINAL_TYPE (DesktopLayout, desktop_layout, GTK_TYPE_LAYOUT_MANAGER)

/* Height of the visible area: the parent GtkViewport's height, or 0. */
static int
viewport_height (GtkWidget *widget)
{
  GtkWidget *parent = gtk_widget_get_parent (widget);

  if (GTK_IS_VIEWPORT (parent))
    return gtk_widget_get_height (parent);
  return 0;
}

/* Fills an array of LayoutItem from the children in child order. */
static LayoutItem *
collect_items (GtkWidget *widget,
               int        pitch_w,
               int        pitch_h,
               guint     *n_out)
{
  GArray *items = g_array_new (FALSE, TRUE, sizeof (LayoutItem));
  GtkWidget *child;

  for (child = gtk_widget_get_first_child (widget);
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      LayoutItem li = { 0 };
      int min = 0;

      if (!DESKTOP_IS_ICON (child))
        continue;
      li.has_position = desktop_item_get_position (desktop_icon_get_item (DESKTOP_ICON (child)),
                                                   &li.x, &li.y);
      gtk_widget_measure (child, GTK_ORIENTATION_VERTICAL, pitch_w, &min, NULL, NULL, NULL);
      li.width = pitch_w;
      li.height = MAX (pitch_h, min);
      g_array_append_val (items, li);
    }

  *n_out = items->len;
  return (LayoutItem *) g_array_free (items, FALSE);
}

static GtkSizeRequestMode
desktop_layout_get_request_mode (GtkLayoutManager *manager G_GNUC_UNUSED,
                                 GtkWidget        *widget G_GNUC_UNUSED)
{
  return GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}

static void
desktop_layout_measure (GtkLayoutManager *manager G_GNUC_UNUSED,
                        GtkWidget        *widget,
                        GtkOrientation    orientation,
                        int               for_size,
                        int              *minimum,
                        int              *natural,
                        int              *minimum_baseline G_GNUC_UNUSED,
                        int              *natural_baseline G_GNUC_UNUSED)
{
  int icon_size = settings_get_icon_size ();
  int pitch_w = settings_pitch_width (icon_size);
  int pitch_h = settings_pitch_height (icon_size);
  g_autofree LayoutItem *items = NULL;
  guint n;
  int width, height;

  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      *minimum = *natural = 0;
      return;
    }

  width = for_size;
  if (width < 0)
    width = gtk_widget_get_width (widget);
  if (width <= 0)
    width = pitch_w;
  height = viewport_height (widget);
  if (height <= 0)
    height = width * 3 / 4;

  items = collect_items (widget, pitch_w, pitch_h, &n);
  *minimum = *natural = layout_arrange (items, n, width, height, pitch_w, pitch_h);
}

static void
desktop_layout_allocate (GtkLayoutManager *manager G_GNUC_UNUSED,
                         GtkWidget        *widget,
                         int               width,
                         int               height,
                         int               baseline G_GNUC_UNUSED)
{
  int icon_size = settings_get_icon_size ();
  int pitch_w = settings_pitch_width (icon_size);
  int pitch_h = settings_pitch_height (icon_size);
  g_autofree LayoutItem *items = NULL;
  GtkWidget *child;
  guint n, i = 0;
  int vp_height = viewport_height (widget);

  if (vp_height <= 0)
    vp_height = height;

  items = collect_items (widget, pitch_w, pitch_h, &n);
  layout_arrange (items, n, width, vp_height, pitch_w, pitch_h);

  for (child = gtk_widget_get_first_child (widget);
       child != NULL && i < n;
       child = gtk_widget_get_next_sibling (child))
    {
      LayoutItem *li;

      if (!DESKTOP_IS_ICON (child))
        continue;
      li = &items[i++];
      gtk_widget_size_allocate (child,
                                &(GtkAllocation) { li->out_x, li->out_y, li->width, li->height },
                                -1);
    }
}

static void
desktop_layout_class_init (DesktopLayoutClass *klass)
{
  GtkLayoutManagerClass *manager_class = GTK_LAYOUT_MANAGER_CLASS (klass);

  manager_class->get_request_mode = desktop_layout_get_request_mode;
  manager_class->measure = desktop_layout_measure;
  manager_class->allocate = desktop_layout_allocate;
}

static void
desktop_layout_init (DesktopLayout *self G_GNUC_UNUSED)
{
}

GtkLayoutManager *
desktop_layout_new (void)
{
  return g_object_new (DESKTOP_TYPE_LAYOUT, NULL);
}
