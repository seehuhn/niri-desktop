#include <gtk/gtk.h>

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

static void
on_activate (GApplication *app)
{
  GtkWidget *window;

  load_css ();
  window = gtk_application_window_new (GTK_APPLICATION (app));
  gtk_window_set_title (GTK_WINDOW (window), "Desktop");
  gtk_window_set_default_size (GTK_WINDOW (window), 640, 480);
  gtk_window_present (GTK_WINDOW (window));
}

int
main (int argc, char **argv)
{
  g_autoptr (GtkApplication) app =
    gtk_application_new ("org.seehuhn.lnxdesktop", G_APPLICATION_DEFAULT_FLAGS);

  g_signal_connect (app, "activate", G_CALLBACK (on_activate), NULL);
  return g_application_run (G_APPLICATION (app), argc, argv);
}
