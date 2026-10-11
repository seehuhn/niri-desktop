#include "settings.h"

#include <errno.h>
#include <glib/gstdio.h>

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

  gint64 v = g_key_file_get_int64 (kf, "desktop", "icon-size", &err);
  if (err != NULL)
    return ICON_SIZE_DEFAULT;
  return (int) CLAMP (v, ICON_SIZE_MIN, ICON_SIZE_MAX);
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
  return icon_size + 48;
}

char *
settings_load_background_from (const char *config_path)
{
  g_autoptr (GKeyFile) kf = g_key_file_new ();
  char *value;

  if (!g_key_file_load_from_file (kf, config_path, G_KEY_FILE_NONE, NULL))
    return NULL;
  value = g_key_file_get_string (kf, "desktop", "background", NULL);
  if (value != NULL && *value == '\0')
    g_clear_pointer (&value, g_free);
  return value;
}

gboolean
settings_save_background_to (const char *config_path,
                             const char *path_or_null,
                             GError    **error)
{
  g_autoptr (GKeyFile) kf = g_key_file_new ();
  g_autofree char *dir = g_path_get_dirname (config_path);

  /* Only a missing file is fine; never overwrite one we cannot parse. */
  g_autoptr (GError) load_error = NULL;
  if (!g_key_file_load_from_file (kf, config_path, G_KEY_FILE_KEEP_COMMENTS, &load_error)
      && !g_error_matches (load_error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
    {
      g_propagate_error (error, g_steal_pointer (&load_error));
      return FALSE;
    }

  if (path_or_null != NULL)
    g_key_file_set_string (kf, "desktop", "background", path_or_null);
  else
    g_key_file_remove_key (kf, "desktop", "background", NULL);

  if (g_mkdir_with_parents (dir, 0700) != 0)
    {
      int e = errno;
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (e),
                   "Cannot create %s: %s", dir, g_strerror (e));
      return FALSE;
    }
  return g_key_file_save_to_file (kf, config_path, error);
}

static char *
config_path (void)
{
  return g_build_filename (g_get_user_config_dir (), "lnxdesktop", "config.ini", NULL);
}

char *
settings_get_background (void)
{
  g_autofree char *path = config_path ();

  return settings_load_background_from (path);
}

gboolean
settings_set_background (const char *path_or_null,
                         GError    **error)
{
  g_autofree char *path = config_path ();

  return settings_save_background_to (path, path_or_null, error);
}
