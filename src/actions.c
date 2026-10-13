/* SPDX-FileCopyrightText: 2026 Jochen Voss <voss@seehuhn.de>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "actions.h"
#include "settings.h"
#include <string.h>

/* A dangling symlink counts as existing: only NOT_FOUND means free. */
static gboolean
name_taken (GFile *dir, const char *name, GHashTable *reserved)
{
  if (reserved != NULL && g_hash_table_contains (reserved, name))
    return TRUE;
  g_autoptr (GFile) f = g_file_get_child (dir, name);
  g_autoptr (GError) error = NULL;
  g_autoptr (GFileInfo) info = g_file_query_info (f, G_FILE_ATTRIBUTE_STANDARD_NAME,
                                                  G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, &error);
  return info != NULL || !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
}

char *
actions_unique_name_with_reserved (GFile *dir, const char *name, GHashTable *reserved)
{
  if (!name_taken (dir, name, reserved))
    return g_strdup (name);

  const char *dot = strrchr (name, '.');
  gsize stem_len = (dot != NULL && dot != name) ? (gsize) (dot - name) : strlen (name);
  const char *ext = (dot != NULL && dot != name) ? dot : "";

  for (int n = 2;; n++)
    {
      g_autofree char *candidate = g_strdup_printf ("%.*s (%d)%s", (int) stem_len, name, n, ext);
      if (!name_taken (dir, candidate, reserved))
        return g_steal_pointer (&candidate);
    }
}

char *
actions_unique_name (GFile *dir, const char *name)
{
  return actions_unique_name_with_reserved (dir, name, NULL);
}

void
actions_report_error (GtkWindow *parent, GError *error)
{
  g_autoptr (GtkAlertDialog) dialog = gtk_alert_dialog_new ("%s", error->message);
  gtk_alert_dialog_show (dialog, parent);
}

static gboolean
is_dismissal (GError *error)
{
  return g_error_matches (error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)
         || g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
}

/* Async callbacks carry a strong ref to the parent window (may be NULL). */
static gpointer
parent_ref (GtkWindow *parent)
{
  return parent ? g_object_ref (parent) : NULL;
}

static void
parent_unref (gpointer parent)
{
  g_clear_object (&parent);
}

static void
launch_done (GObject *source, GAsyncResult *res, gpointer data)
{
  GtkWindow *parent = data;
  g_autoptr (GError) error = NULL;
  if (!gtk_file_launcher_launch_finish (GTK_FILE_LAUNCHER (source), res, &error)
      && !is_dismissal (error))
    actions_report_error (parent, error);
  parent_unref (parent);
}

void
actions_open (GtkWindow *parent, GList *files)
{
  for (GList *l = files; l; l = l->next)
    {
      g_autoptr (GtkFileLauncher) launcher = gtk_file_launcher_new (l->data);
      gtk_file_launcher_launch (launcher, parent, NULL, launch_done, parent_ref (parent));
    }
}

static void
chooser_response (GtkDialog *dialog, int response, gpointer data)
{
  GFile *file = data;
  if (response == GTK_RESPONSE_OK)
    {
      GAppInfo *app = gtk_app_chooser_get_app_info (GTK_APP_CHOOSER (dialog));
      if (app != NULL)
        {
          g_autoptr (GError) error = NULL;
          GList *list = g_list_prepend (NULL, file);
          g_autoptr (GdkAppLaunchContext) context =
            gdk_display_get_app_launch_context (gtk_widget_get_display (GTK_WIDGET (dialog)));
          if (!g_app_info_launch (app, list, G_APP_LAUNCH_CONTEXT (context), &error))
            actions_report_error (gtk_window_get_transient_for (GTK_WINDOW (dialog)), error);
          g_list_free (list);
          g_object_unref (app);
        }
    }
  gtk_window_destroy (GTK_WINDOW (dialog));
}

void
actions_open_with (GtkWindow *parent, GFile *file)
{
  GtkWidget *dialog = gtk_app_chooser_dialog_new (parent, GTK_DIALOG_MODAL, file);
  g_object_set_data_full (G_OBJECT (dialog), "niri-desktop-file", g_object_ref (file), g_object_unref);
  g_signal_connect (dialog, "response", G_CALLBACK (chooser_response), file);
  gtk_window_present (GTK_WINDOW (dialog));
}

static void
trash_done (GObject *source, GAsyncResult *res, gpointer data)
{
  GtkWindow *parent = data;
  g_autoptr (GError) error = NULL;
  if (!g_file_trash_finish (G_FILE (source), res, &error))
    actions_report_error (parent, error);
  parent_unref (parent);
}

void
actions_trash (GtkWindow *parent, GList *files)
{
  for (GList *l = files; l; l = l->next)
    g_file_trash_async (l->data, G_PRIORITY_DEFAULT, NULL, trash_done, parent_ref (parent));
}

gboolean
actions_rename (GtkWindow *parent, GFile *file, const char *new_name, GError **error)
{
  g_autoptr (GError) local = NULL;
  GFile *renamed = g_file_set_display_name (file, new_name, NULL, &local);
  if (renamed == NULL)
    {
      actions_report_error (parent, local);
      g_propagate_error (error, g_steal_pointer (&local));
      return FALSE;
    }
  g_object_unref (renamed);
  return TRUE;
}

static GSList *
list_to_slist (GList *files)
{
  GSList *out = NULL;
  for (GList *l = files; l; l = l->next)
    out = g_slist_prepend (out, l->data);
  return g_slist_reverse (out);
}

void
actions_copy_to_clipboard (GtkWidget *widget, GList *files)
{
  GSList *slist = list_to_slist (files);
  GdkFileList *list = gdk_file_list_new_from_list (slist);
  g_slist_free (slist);
  gdk_clipboard_set (gtk_widget_get_clipboard (widget), GDK_TYPE_FILE_LIST, list);
  g_boxed_free (GDK_TYPE_FILE_LIST, list);
}

typedef struct
{
  GtkWindow    *parent;
  DesktopModel *model;
} PasteData;

static void
paste_done (GObject *source, GAsyncResult *res, gpointer user_data)
{
  PasteData *pd = user_data;
  g_autoptr (GError) error = NULL;
  const GValue *value = gdk_clipboard_read_value_finish (GDK_CLIPBOARD (source), res, &error);

  if (value == NULL)
    {
      /* Clipboard holds no file list: nothing to paste. */
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED))
        actions_report_error (pd->parent, error);
    }
  else
    {
      GdkFileList *fl = g_value_get_boxed (value);
      GSList *slist = fl ? gdk_file_list_get_files (fl) : NULL;
      GList *files = NULL;
      for (GSList *s = slist; s; s = s->next)
        files = g_list_prepend (files, s->data);
      files = g_list_reverse (files);
      if (files != NULL)
        actions_import (pd->parent, pd->model, files, FALSE, FALSE, 0, 0);
      g_list_free (files);
      g_slist_free (slist);
    }

  g_clear_object (&pd->parent);
  g_object_unref (pd->model);
  g_free (pd);
}

void
actions_paste (GtkWindow *parent, GtkWidget *widget, DesktopModel *model)
{
  PasteData *pd = g_new0 (PasteData, 1);
  pd->parent = parent_ref (parent);
  pd->model = g_object_ref (model);
  gdk_clipboard_read_value_async (gtk_widget_get_clipboard (widget), GDK_TYPE_FILE_LIST,
                                  G_PRIORITY_DEFAULT, NULL, paste_done, pd);
}

#define TREE_ATTRS G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE

gboolean
actions_copy_recursive (GFile *src, GFile *dest, GCancellable *cancellable, GError **error)
{
  g_autoptr (GFileEnumerator) children = NULL;

  if (!g_file_make_directory (dest, cancellable, error))
    return FALSE;
  children = g_file_enumerate_children (src, TREE_ATTRS, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                        cancellable, error);
  if (children == NULL)
    return FALSE;

  for (;;)
    {
      GFileInfo *info;
      GFile *child;

      if (!g_file_enumerator_iterate (children, &info, &child, cancellable, error))
        return FALSE;
      if (info == NULL)
        return TRUE;

      g_autoptr (GFile) target = g_file_get_child (dest, g_file_info_get_name (info));
      gboolean ok = g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY
                    ? actions_copy_recursive (child, target, cancellable, error)
                    : g_file_copy (child, target, G_FILE_COPY_NOFOLLOW_SYMLINKS, cancellable,
                                   NULL, NULL, error);
      if (!ok)
        return FALSE;
    }
}

gboolean
actions_delete_recursive (GFile *file, GCancellable *cancellable, GError **error)
{
  g_autoptr (GFileInfo) info = g_file_query_info (file, TREE_ATTRS,
                                                  G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                                  cancellable, error);
  if (info == NULL)
    return FALSE;

  if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY)
    {
      g_autoptr (GFileEnumerator) children =
        g_file_enumerate_children (file, TREE_ATTRS, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                   cancellable, error);
      if (children == NULL)
        return FALSE;
      for (;;)
        {
          GFileInfo *child_info;
          GFile *child;

          if (!g_file_enumerator_iterate (children, &child_info, &child, cancellable, error))
            return FALSE;
          if (child_info == NULL)
            break;
          if (!actions_delete_recursive (child, cancellable, error))
            return FALSE;
        }
    }
  return g_file_delete (file, cancellable, error);
}

typedef struct
{
  GtkWindow *parent;
  GFile     *src;
  GFile     *dest;
  gboolean   move;
} TransferData;

static void
transfer_data_free (TransferData *td)
{
  parent_unref (td->parent);
  g_object_unref (td->src);
  g_object_unref (td->dest);
  g_free (td);
}

/* Copies a directory tree, and for a move then deletes the source. */
static void
tree_thread (GTask        *task,
             gpointer      source G_GNUC_UNUSED,
             gpointer      data,
             GCancellable *cancellable)
{
  TransferData *td = data;
  GError *error = NULL;

  if (actions_copy_recursive (td->src, td->dest, cancellable, &error) &&
      (!td->move || actions_delete_recursive (td->src, cancellable, &error)))
    g_task_return_boolean (task, TRUE);
  else
    g_task_return_error (task, error);
}

static void
tree_done (GObject *source G_GNUC_UNUSED, GAsyncResult *res, gpointer user_data)
{
  TransferData *td = user_data;
  g_autoptr (GError) error = NULL;

  if (!g_task_propagate_boolean (G_TASK (res), &error))
    actions_report_error (td->parent, error);
  transfer_data_free (td);
}

static void
transfer_done (GObject *source, GAsyncResult *res, gpointer user_data)
{
  TransferData *td = user_data;
  g_autoptr (GError) error = NULL;
  gboolean ok = td->move ? g_file_move_finish (G_FILE (source), res, &error)
                         : g_file_copy_finish (G_FILE (source), res, &error);

  /* GIO copies only single files; a directory needs a tree copy (and,
   * for a move across file systems, a tree delete afterwards). */
  if (!ok && g_error_matches (error, G_IO_ERROR, G_IO_ERROR_WOULD_RECURSE))
    {
      g_autoptr (GTask) task = g_task_new (NULL, NULL, tree_done, td);

      g_task_set_task_data (task, td, NULL);
      g_task_run_in_thread (task, tree_thread);
      return;
    }
  if (!ok)
    actions_report_error (td->parent, error);
  transfer_data_free (td);
}

void
actions_import (GtkWindow *parent, DesktopModel *model, GList *files,
                gboolean move, gboolean has_point, int x, int y)
{
  GFile *dir = desktop_model_get_directory (model);
  int pitch_h = settings_pitch_height (settings_get_icon_size ());
  int k = 0;
  g_autoptr (GHashTable) reserved = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  for (GList *l = files; l; l = l->next, k++)
    {
      GFile *src = l->data;
      int yk = y + k * pitch_h;
      g_autoptr (GFile) src_parent = g_file_get_parent (src);

      gboolean same_dir = src_parent != NULL && g_file_equal (src_parent, dir);

      if (same_dir && has_point)
        {
          g_autofree char *name = g_file_get_basename (src);
          DesktopItem *item = desktop_model_lookup (model, name);
          if (item != NULL)
            desktop_model_set_position (model, item, x, yk);
          continue;
        }
      /* Moving a file onto its own directory without a target is a no-op;
         a copy falls through and creates a uniquely named duplicate. */
      if (same_dir && move)
        continue;

      g_autofree char *base = g_file_get_basename (src);
      g_autofree char *unique = actions_unique_name_with_reserved (dir, base, reserved);
      g_hash_table_add (reserved, g_strdup (unique));
      g_autoptr (GFile) dest = g_file_get_child (dir, unique);
      if (has_point)
        desktop_model_preseed_position (model, unique, x, yk);

      TransferData *td = g_new0 (TransferData, 1);
      td->parent = parent_ref (parent);
      td->src = g_object_ref (src);
      td->dest = g_object_ref (dest);
      td->move = move;
      if (move)
        g_file_move_async (src, dest, G_FILE_COPY_NONE, G_PRIORITY_DEFAULT, NULL, NULL, NULL,
                           transfer_done, td);
      else
        g_file_copy_async (src, dest, G_FILE_COPY_NONE, G_PRIORITY_DEFAULT, NULL, NULL, NULL,
                           transfer_done, td);
    }
}

static GtkWidget *
info_label (const char *text, gboolean key)
{
  GtkWidget *label = gtk_label_new (NULL);
  if (key)
    {
      g_autofree char *esc = g_markup_escape_text (text, -1);
      g_autofree char *markup = g_strdup_printf ("<b>%s</b>", esc);
      gtk_label_set_markup (GTK_LABEL (label), markup);
      gtk_label_set_xalign (GTK_LABEL (label), 1.0);
    }
  else
    {
      gtk_label_set_text (GTK_LABEL (label), text);
      gtk_label_set_xalign (GTK_LABEL (label), 0.0);
      gtk_label_set_selectable (GTK_LABEL (label), TRUE);
      gtk_label_set_width_chars (GTK_LABEL (label), 30);
      gtk_label_set_max_width_chars (GTK_LABEL (label), 60);
      gtk_label_set_wrap (GTK_LABEL (label), TRUE);
      gtk_label_set_wrap_mode (GTK_LABEL (label), PANGO_WRAP_WORD_CHAR);
      gtk_widget_set_hexpand (label, TRUE);
    }
  gtk_widget_set_valign (label, GTK_ALIGN_START);
  return label;
}

static gboolean
info_close (GtkWidget *widget, GVariant *args, gpointer data)
{
  (void) args;
  (void) data;
  gtk_window_destroy (GTK_WINDOW (widget));
  return TRUE;
}

void
actions_show_info (GtkWindow *parent, DesktopItem *item)
{
  GtkWidget *win = gtk_window_new ();
  gtk_window_set_transient_for (GTK_WINDOW (win), parent);
  gtk_window_set_title (GTK_WINDOW (win), "Info");
  gtk_window_set_default_size (GTK_WINDOW (win), 400, -1);

  GtkWidget *grid = gtk_grid_new ();
  gtk_grid_set_row_spacing (GTK_GRID (grid), 12);
  gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
  gtk_widget_set_margin_top (grid, 12);
  gtk_widget_set_margin_bottom (grid, 12);
  gtk_widget_set_margin_start (grid, 12);
  gtk_widget_set_margin_end (grid, 12);
  gtk_window_set_child (GTK_WINDOW (win), grid);

  const char *ctype = desktop_item_get_content_type (item);
  g_autofree char *desc = ctype ? g_content_type_get_description (ctype) : NULL;
  g_autofree char *type = ctype ? g_strdup_printf ("%s (%s)", desc ? desc : ctype, ctype)
                                : g_strdup ("Unknown");
  g_autofree char *size = g_format_size (desktop_item_get_size (item));
  GDateTime *mtime = desktop_item_get_modified (item);
  g_autofree char *modified = mtime ? g_date_time_format (mtime, "%c") : g_strdup ("Unknown");
  g_autofree char *path = g_file_get_path (desktop_item_get_file (item));
  if (path == NULL)
    path = g_file_get_uri (desktop_item_get_file (item));

  const char *keys[] = { "Name", "Type", "Size", "Modified", "Path" };
  const char *values[] = { desktop_item_get_display_name (item), type, size, modified, path };
  for (int i = 0; i < 5; i++)
    {
      gtk_grid_attach (GTK_GRID (grid), info_label (keys[i], TRUE), 0, i, 1, 1);
      gtk_grid_attach (GTK_GRID (grid), info_label (values[i] ? values[i] : "", FALSE), 1, i, 1, 1);
    }

  GtkEventController *sc = gtk_shortcut_controller_new ();
  gtk_shortcut_controller_add_shortcut (GTK_SHORTCUT_CONTROLLER (sc),
    gtk_shortcut_new (gtk_shortcut_trigger_parse_string ("Escape"),
                      gtk_callback_action_new (info_close, NULL, NULL)));
  gtk_widget_add_controller (win, sc);

  gtk_window_present (GTK_WINDOW (win));
}
