/* SPDX-FileCopyrightText: 2026 Jochen Voss <voss@seehuhn.de>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>
#include "desktop-model.h"
#include "desktop-item.h"

G_BEGIN_DECLS

/* "a.txt" -> "a.txt" if free, else "a (2).txt", "a (3).txt", ...; "Makefile" -> "Makefile (2)". */
char    *actions_unique_name (GFile *dir, const char *name);
/* As above, also avoiding names in `reserved` (a set of strings; may be NULL). */
char    *actions_unique_name_with_reserved (GFile *dir, const char *name, GHashTable *reserved);
void     actions_report_error (GtkWindow *parent, GError *error);
void     actions_open (GtkWindow *parent, GList *files);
void     actions_open_with (GtkWindow *parent, GFile *file);
void     actions_trash (GtkWindow *parent, GList *files);
gboolean actions_rename (GtkWindow *parent, GFile *file, const char *new_name, GError **error);
void     actions_copy_to_clipboard (GtkWidget *widget, GList *files);
void     actions_paste (GtkWindow *parent, GtkWidget *widget, DesktopModel *model);
void     actions_import (GtkWindow *parent, DesktopModel *model, GList *files,
                         gboolean move, gboolean has_point, int x, int y);
/* Whether a and b are local files on the same file system. */
gboolean actions_same_filesystem (GFile *a, GFile *b);
/* Copies or moves files into dir, renaming them where a name is taken.
 * Skips dir itself, its ancestors, and (for a move) files already in dir. */
void     actions_transfer (GtkWindow *parent, GList *files, GFile *dir, gboolean move);
/* Synchronous helpers for the import thread.  copy_recursive creates
 * dest as a new directory and copies src's contents into it (symlinks
 * inside are copied as links); delete_recursive deletes children first.
 * Both stop at, and return, the first error. */
gboolean actions_copy_recursive (GFile *src, GFile *dest, GCancellable *cancellable,
                                 GError **error);
gboolean actions_delete_recursive (GFile *file, GCancellable *cancellable, GError **error);
void     actions_show_info (GtkWindow *parent, DesktopItem *item);

G_END_DECLS
