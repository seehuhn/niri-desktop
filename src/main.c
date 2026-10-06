#include <gtk/gtk.h>

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

static void
on_load_finished (GObject      *source,
                  GAsyncResult *res,
                  gpointer      user_data G_GNUC_UNUSED)
{
  g_autoptr (GError) error = NULL;

  if (!desktop_model_load_finish (DESKTOP_MODEL (source), res, &error))
    g_warning ("Could not read the Desktop folder: %s", error->message);
}

static void
on_activate (GApplication *app)
{
  g_autoptr (GFile) dir = desktop_directory ();
  g_autofree char *positions = g_build_filename (g_get_user_config_dir (), "lnxdesktop",
                                                 "positions.ini", NULL);
  g_autoptr (DesktopModel) model = desktop_model_new (dir, position_store_new (positions));
  GtkWidget *window, *box, *scrolled, *canvas;

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

  gtk_window_present (GTK_WINDOW (window));
  desktop_model_load_async (model, NULL, on_load_finished, NULL);
}

int
main (int argc, char **argv)
{
  g_autoptr (GtkApplication) app =
    gtk_application_new ("org.seehuhn.lnxdesktop", G_APPLICATION_DEFAULT_FLAGS);

  g_signal_connect (app, "activate", G_CALLBACK (on_activate), NULL);
  return g_application_run (G_APPLICATION (app), argc, argv);
}
