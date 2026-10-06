#include "settings.h"

#define ICON_SIZE_DEFAULT 64
#define ICON_SIZE_MIN 32
#define ICON_SIZE_MAX 128

int
settings_load_icon_size_from (const char *path)
{
  g_autoptr (GKeyFile) kf = g_key_file_new ();
  g_autoptr (GError) err = NULL;

  if (!g_key_file_load_from_file (kf, path, G_KEY_FILE_NONE, &err))
    return ICON_SIZE_DEFAULT;

  int v = g_key_file_get_integer (kf, "desktop", "icon-size", &err);
  if (err != NULL)
    return ICON_SIZE_DEFAULT;
  return CLAMP (v, ICON_SIZE_MIN, ICON_SIZE_MAX);
}

int
settings_get_icon_size (void)
{
  static gsize once = 0;
  static int size = ICON_SIZE_DEFAULT;

  if (g_once_init_enter (&once))
    {
      g_autofree char *path =
        g_build_filename (g_get_user_config_dir (), "lnxdesktop", "config.ini", NULL);
      size = settings_load_icon_size_from (path);
      g_once_init_leave (&once, 1);
    }
  return size;
}

int
settings_pitch_width (int icon_size)
{
  return icon_size + 16;
}

int
settings_pitch_height (int icon_size)
{
  return icon_size + 32;
}
