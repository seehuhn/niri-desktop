#pragma once

#include <glib.h>

/* Icon size from config.ini, read once and cached; default 64, clamped 32..128. */
int settings_get_icon_size (void);
int settings_pitch_width (int icon_size);
int settings_pitch_height (int icon_size);

/* Uncached core: reads one file; default 64 on any problem, else clamped. */
int settings_load_icon_size_from (const char *path);
