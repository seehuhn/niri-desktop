#include <gtk/gtk.h>

#include "actions.h"
#include "desktop-canvas.h"
#include "desktop-model.h"
#include "position-store.h"

static void
load_css (void)
{
  g_autoptr (GtkCssProvider) provider = gtk_css_provider_new ();

  gtk_css_provider_load_from_resource (provider,
                                       "/org/seehuhn/lnxdesktop/style.css");
  gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                              GTK_STYLE_PROVIDER (provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

/* LNXDESKTOP_DIR (developer override), else the XDG Desktop directory,
 * else ~/Desktop. */
static GFile *
desktop_directory (void)
{
  const char *override = g_getenv ("LNXDESKTOP_DIR");
  const char *dir;

  if (override != NULL && *override != '\0')
    return g_file_new_for_path (override);

  dir = g_get_user_special_dir (G_USER_DIRECTORY_DESKTOP);
  if (dir == NULL || g_strcmp0 (dir, g_get_home_dir ()) == 0)
    {
      g_autofree char *fallback = g_build_filename (g_get_home_dir (), "Desktop", NULL);
      return g_file_new_for_path (fallback);
    }
  return g_file_new_for_path (dir);
}

/* Per-window state, owned by the window. */
typedef struct
{
  GtkWindow     *window;  /* unowned */
  DesktopCanvas *canvas;  /* unowned */
  DesktopModel  *model;
} App;

static void
app_free (App *app)
{
  g_clear_object (&app->model);
  g_free (app);
}

static void
on_load_finished (GObject      *source,
                  GAsyncResult *res,
                  gpointer      user_data)
{
  g_autoptr (GtkWindow) window = user_data;
  g_autoptr (GError) error = NULL;

  if (!desktop_model_load_finish (DESKTOP_MODEL (source), res, &error))
    actions_report_error (window, error);
}

/* ---- window actions ---- */

/* The first selected item, or NULL; transfer none. */
static DesktopItem *
first_selected_item (App *app)
{
  GList *items = desktop_canvas_get_selected_items (app->canvas);
  DesktopItem *item = items != NULL ? items->data : NULL;

  g_list_free (items);
  return item;
}

static void
action_open (GSimpleAction *action G_GNUC_UNUSED,
             GVariant      *parameter G_GNUC_UNUSED,
             gpointer       user_data)
{
  App *app = user_data;
  GList *files = desktop_canvas_get_selected_files (app->canvas);

  actions_open (app->window, files);
  g_list_free (files);
}

static void
action_open_with (GSimpleAction *action G_GNUC_UNUSED,
                  GVariant      *parameter G_GNUC_UNUSED,
                  gpointer       user_data)
{
  App *app = user_data;
  DesktopItem *item = first_selected_item (app);

  if (item != NULL)
    actions_open_with (app->window, desktop_item_get_file (item));
}

static void
action_rename (GSimpleAction *action G_GNUC_UNUSED,
               GVariant      *parameter G_GNUC_UNUSED,
               gpointer       user_data)
{
  App *app = user_data;
  DesktopItem *item = first_selected_item (app);
  DesktopIcon *icon = item != NULL ? desktop_canvas_icon_for_item (app->canvas, item) : NULL;

  if (icon != NULL)
    desktop_icon_start_rename (icon);
}

static void
action_trash (GSimpleAction *action G_GNUC_UNUSED,
              GVariant      *parameter G_GNUC_UNUSED,
              gpointer       user_data)
{
  App *app = user_data;
  GList *files = desktop_canvas_get_selected_files (app->canvas);
  /* Trashing changes the model, which owns the listed files. */
  GList *owned = g_list_copy_deep (files, (GCopyFunc) (void (*) (void)) g_object_ref, NULL);

  g_list_free (files);
  actions_trash (app->window, owned);
  g_list_free_full (owned, g_object_unref);
}

static void
action_copy (GSimpleAction *action G_GNUC_UNUSED,
             GVariant      *parameter G_GNUC_UNUSED,
             gpointer       user_data)
{
  App *app = user_data;
  GList *files = desktop_canvas_get_selected_files (app->canvas);

  actions_copy_to_clipboard (GTK_WIDGET (app->canvas), files);
  g_list_free (files);
}

static void
action_paste (GSimpleAction *action G_GNUC_UNUSED,
              GVariant      *parameter G_GNUC_UNUSED,
              gpointer       user_data)
{
  App *app = user_data;

  actions_paste (app->window, GTK_WIDGET (app->canvas), app->model);
}

static void
action_info (GSimpleAction *action G_GNUC_UNUSED,
             GVariant      *parameter G_GNUC_UNUSED,
             gpointer       user_data)
{
  App *app = user_data;
  DesktopItem *item = first_selected_item (app);

  if (item != NULL)
    actions_show_info (app->window, item);
}

static void
action_select_all (GSimpleAction *action G_GNUC_UNUSED,
                   GVariant      *parameter G_GNUC_UNUSED,
                   gpointer       user_data)
{
  App *app = user_data;

  desktop_canvas_select_all (app->canvas);
}

static const GActionEntry win_actions[] = {
  { .name = "open", .activate = action_open },
  { .name = "open-with", .activate = action_open_with },
  { .name = "rename", .activate = action_rename },
  { .name = "trash", .activate = action_trash },
  { .name = "copy", .activate = action_copy },
  { .name = "paste", .activate = action_paste },
  { .name = "info", .activate = action_info },
  { .name = "select-all", .activate = action_select_all },
};

/* Shortcuts live on the canvas, not on the application: application
 * accelerators run in the window's capture phase and would take Return,
 * Delete, Ctrl+A, Ctrl+C and Ctrl+V from an open rename entry.  A
 * bubble-phase controller on the canvas only sees keys the entry left
 * unhandled. */
static const struct
{
  const char *action;
  const char *accel;
} shortcuts[] = {
  { "win.open", "Return" },
  { "win.open", "KP_Enter" },
  { "win.rename", "F2" },
  { "win.trash", "Delete" },
  { "win.copy", "<Control>c" },
  { "win.paste", "<Control>v" },
  { "win.select-all", "<Control>a" },
  { "win.info", "<Control>i" },
};

static void
add_shortcuts (GtkWidget *canvas)
{
  GtkEventController *controller = gtk_shortcut_controller_new ();
  gsize i;

  gtk_shortcut_controller_set_scope (GTK_SHORTCUT_CONTROLLER (controller),
                                     GTK_SHORTCUT_SCOPE_LOCAL);
  for (i = 0; i < G_N_ELEMENTS (shortcuts); i++)
    gtk_shortcut_controller_add_shortcut (GTK_SHORTCUT_CONTROLLER (controller),
                                          gtk_shortcut_new (gtk_shortcut_trigger_parse_string (shortcuts[i].accel),
                                                            gtk_named_action_new (shortcuts[i].action)));
  gtk_widget_add_controller (canvas, controller);
}

static void
set_enabled (App        *app,
             const char *name,
             gboolean    enabled)
{
  GAction *action = g_action_map_lookup_action (G_ACTION_MAP (app->window), name);

  g_simple_action_set_enabled (G_SIMPLE_ACTION (action), enabled);
}

static void
update_actions (App *app)
{
  GList *items = desktop_canvas_get_selected_items (app->canvas);
  guint n = g_list_length (items);

  g_list_free (items);
  set_enabled (app, "open", n >= 1);
  set_enabled (app, "trash", n >= 1);
  set_enabled (app, "copy", n >= 1);
  set_enabled (app, "open-with", n == 1);
  set_enabled (app, "rename", n == 1);
  set_enabled (app, "info", n == 1);
}

/* ---- context menu ---- */

G_GNUC_NULL_TERMINATED static void
append_section (GMenu *menu,
                ...)
{
  g_autoptr (GMenu) section = g_menu_new ();
  const char *label;
  va_list args;

  va_start (args, menu);
  while ((label = va_arg (args, const char *)) != NULL)
    {
      const char *action = va_arg (args, const char *);
      const char *accel = va_arg (args, const char *);
      g_autoptr (GMenuItem) item = g_menu_item_new (label, action);

      if (accel != NULL)
        g_menu_item_set_attribute (item, "accel", "s", accel);
      g_menu_append_item (section, item);
    }
  va_end (args);
  g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
}

static void
unparent_popover (gpointer data)
{
  g_autoptr (GtkWidget) popover = data;

  if (gtk_widget_get_parent (popover) != NULL)
    gtk_widget_unparent (popover);
}

static void
on_popover_closed (GtkPopover *popover)
{
  /* Not during the signal: the menu may still be activating an item. */
  g_idle_add_once (unparent_popover, g_object_ref (popover));
}

static void
on_context_menu_requested (DesktopCanvas *canvas,
                           DesktopItem   *item,
                           double         x,
                           double         y,
                           App           *app)
{
  g_autoptr (GMenu) menu = g_menu_new ();
  GtkWidget *popover;
  GdkRectangle rect = { (int) x, (int) y, 1, 1 };

  /* The selection may have changed in this very event. */
  update_actions (app);

  if (item != NULL)
    {
      append_section (menu, "Open", "win.open", "Return",
                      "Open With…", "win.open-with", NULL, NULL);
      append_section (menu, "Rename", "win.rename", "F2", NULL);
      append_section (menu, "Copy", "win.copy", "<Control>c", NULL);
      append_section (menu, "Move to Trash", "win.trash", "Delete", NULL);
      append_section (menu, "Info", "win.info", "<Control>i", NULL);
    }
  else
    {
      GdkClipboard *clipboard = gtk_widget_get_clipboard (GTK_WIDGET (canvas));

      if (gdk_content_formats_contain_gtype (gdk_clipboard_get_formats (clipboard),
                                             GDK_TYPE_FILE_LIST))
        append_section (menu, "Paste", "win.paste", "<Control>v", NULL);
      append_section (menu, "Select All", "win.select-all", "<Control>a", NULL);
    }

  popover = gtk_popover_menu_new_from_model (G_MENU_MODEL (menu));
  gtk_widget_set_parent (popover, GTK_WIDGET (canvas));
  gtk_popover_set_pointing_to (GTK_POPOVER (popover), &rect);
  gtk_popover_set_has_arrow (GTK_POPOVER (popover), FALSE);
  gtk_widget_set_halign (popover, GTK_ALIGN_START);
  g_signal_connect (popover, "closed", G_CALLBACK (on_popover_closed), NULL);
  gtk_popover_popup (GTK_POPOVER (popover));
}

static void
on_activate_selection (DesktopCanvas *canvas,
                       App           *app G_GNUC_UNUSED)
{
  gtk_widget_activate_action (GTK_WIDGET (canvas), "win.open", NULL);
}

static void
on_activate (GApplication *app)
{
  g_autoptr (GFile) dir = NULL;
  g_autofree char *positions = NULL;
  g_autoptr (DesktopModel) model = NULL;
  App *state;
  GtkWindow *existing = gtk_application_get_active_window (GTK_APPLICATION (app));
  GtkWidget *window, *box, *scrolled, *canvas;

  if (existing != NULL)
    {
      gtk_window_present (existing);
      return;
    }

  dir = desktop_directory ();
  positions = g_build_filename (g_get_user_config_dir (), "lnxdesktop", "positions.ini", NULL);
  model = desktop_model_new (dir, position_store_new (positions));
  load_css ();
  window = gtk_application_window_new (GTK_APPLICATION (app));
  gtk_window_set_title (GTK_WINDOW (window), "Desktop");
  gtk_window_set_decorated (GTK_WINDOW (window), FALSE);
  gtk_window_set_default_size (GTK_WINDOW (window), 640, 480);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  if (!desktop_model_is_monitoring (model))
    {
      GtkWidget *label = gtk_label_new ("External changes to the Desktop folder are not tracked.");

      gtk_widget_add_css_class (label, "warning");
      gtk_box_append (GTK_BOX (box), label);
    }

  scrolled = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand (scrolled, TRUE);
  canvas = desktop_canvas_new (model);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), canvas);
  gtk_box_append (GTK_BOX (box), scrolled);
  gtk_window_set_child (GTK_WINDOW (window), box);

  state = g_new0 (App, 1);
  state->window = GTK_WINDOW (window);
  state->canvas = DESKTOP_CANVAS (canvas);
  state->model = g_object_ref (model);
  g_object_set_data_full (G_OBJECT (window), "lnxdesktop-app", state, (GDestroyNotify) app_free);
  g_action_map_add_action_entries (G_ACTION_MAP (window), win_actions,
                                   G_N_ELEMENTS (win_actions), state);
  add_shortcuts (canvas);
  update_actions (state);
  g_signal_connect_swapped (canvas, "selection-changed", G_CALLBACK (update_actions), state);
  g_signal_connect (canvas, "activate-selection", G_CALLBACK (on_activate_selection), state);
  g_signal_connect (canvas, "context-menu-requested",
                    G_CALLBACK (on_context_menu_requested), state);

  gtk_window_present (GTK_WINDOW (window));
  desktop_model_load_async (model, NULL, on_load_finished, g_object_ref (window));
}

int
main (int argc, char **argv)
{
  g_autoptr (GtkApplication) app =
    gtk_application_new ("org.seehuhn.lnxdesktop", G_APPLICATION_DEFAULT_FLAGS);

  g_signal_connect (app, "activate", G_CALLBACK (on_activate), NULL);
  return g_application_run (G_APPLICATION (app), argc, argv);
}
