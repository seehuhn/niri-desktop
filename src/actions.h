#pragma once

#include <gtk/gtk.h>
#include "desktop-model.h"
#include "desktop-item.h"

G_BEGIN_DECLS

/* "a.txt" -> "a.txt" if free, else "a (2).txt", "a (3).txt", ...; "Makefile" -> "Makefile (2)". */
char    *actions_unique_name (GFile *dir, const char *name);
void     actions_report_error (GtkWindow *parent, GError *error);
void     actions_open (GtkWindow *parent, GList *files);
void     actions_open_with (GtkWindow *parent, GFile *file);
void     actions_trash (GtkWindow *parent, GList *files);
gboolean actions_rename (GtkWindow *parent, GFile *file, const char *new_name, GError **error);
void     actions_copy_to_clipboard (GtkWidget *widget, GList *files);
void     actions_paste (GtkWindow *parent, GtkWidget *widget, DesktopModel *model);
void     actions_import (GtkWindow *parent, DesktopModel *model, GList *files,
                         gboolean move, gboolean has_point, int x, int y);
void     actions_show_info (GtkWindow *parent, DesktopItem *item);

G_END_DECLS
