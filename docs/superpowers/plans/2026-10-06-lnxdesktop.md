# lnxDesktop Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A GTK4 window that shows `~/Desktop/` as freely arranged, persistent icons with open, open-with, drag-and-drop, clipboard, rename, trash and info.

**Architecture:** A `DesktopModel` (GListStore of `DesktopItem`, fed by a `GFileMonitor`, positions in a key file) is displayed by a `DesktopCanvas` widget whose custom layout manager calls pure `layout.c` to fit or park icons. `actions.c` wraps GIO and GTK launchers; `thumbnails.c` serves image previews. Nothing below the canvas and icon widgets touches GTK widgets.

**Tech Stack:** C11, GTK 4.18, GLib/GIO 2.84, GdkPixbuf, Meson/Ninja, GLib `g_test`.

**Spec:** `docs/superpowers/specs/2026-10-06-lnxdesktop-design.md`

## Global Constraints

- Debian 13 packages only: `gtk4`, `gio-unix-2.0`, `gdk-pixbuf-2.0` via pkg-config. No libadwaita, no gnome-desktop.
- All geometry in logical pixels. Grid pitch 112 × 128; picture 96 × 96; two-line label.
- App id `org.seehuhn.lnxdesktop`. Positions file `~/.config/lnxdesktop/positions.ini`.
- Stored positions are only ever changed by a user drag, a drop, a rename (key moves) or a delete (key removed). The layout pass never writes.
- Every GIO failure that the user triggered shows a `GtkAlertDialog` with the GError message.
- `GtkAppChooserDialog` is deprecated in GTK 4.10 but present in 4.18; compile with `-DGDK_DISABLE_DEPRECATION_WARNINGS`.
- Commit after every task. No `Claude-Session:` lines in commit messages.

## Review Focus

1. File names containing `[`, `]`, spaces or non-ASCII: the key-file group name must survive a round trip. Test in Task 2.
2. First allocation with a tiny or zero viewport (width below one pitch, height 0): layout must not divide by zero or loop. Test in Task 3.
3. Two files stored on the same spot: both stay where stored, never silently separated. Test in Task 3.
4. External rename of a file that has a position: the position follows the new name; rename of one without a position does not create one. Test in Task 5.
5. Drop or paste of a file whose name already exists on the Desktop (including copying a Desktop file onto the Desktop): a " (2)" name, never an overwrite. Test in Task 10.

---

### Task 1: Project skeleton and empty window

**Files:**
- Create: `meson.build`, `src/meson.build`, `tests/meson.build`, `src/main.c`, `data/style.css`, `data/lnxdesktop.gresource.xml`, `data/org.seehuhn.lnxdesktop.desktop`, `data/org.seehuhn.lnxdesktop.svg`, `data/meson.build`, `README.md`
- Test: `tests/test-smoke.c`

**Interfaces:**
- Produces: `meson.build` declares `lnxdesktop_deps` (gtk4, gio-unix-2.0, gdk-pixbuf-2.0) and `lnxdesktop_inc` (`src/`); `src/meson.build` defines `lib_sources` (every source except `main.c`) and builds the executable from `lib_sources + ['main.c']`; each later task appends its source to `lib_sources` and adds a test executable in `tests/meson.build` via the pattern `test('name', executable('test-name', 'test-name.c', lib_sources, dependencies: lnxdesktop_deps, include_directories: lnxdesktop_inc))`.

- [ ] **Step 1: Write the smoke test** — `tests/test-smoke.c`: `g_test_init`, one test `/smoke/glib` asserting `glib_check_version (2, 80, 0) == NULL`.
- [ ] **Step 2: Write `meson.build`** — project `lnxdesktop`, `c`, `version: '0.1'`, `default_options: ['c_std=c11', 'warning_level=2']`; `add_project_arguments('-DGDK_DISABLE_DEPRECATION_WARNINGS', language: 'c')`; subdirs `data`, `src`, `tests`.
- [ ] **Step 3: Write `data/`** — `style.css` (empty `.desktop-canvas { }` rule); gresource XML with prefix `/org/seehuhn/lnxdesktop` containing `style.css`; a `.desktop` file (Name=lnxDesktop, Exec=lnxdesktop, Icon=org.seehuhn.lnxdesktop, Type=Application, Categories=Utility;); a simple SVG icon; `data/meson.build` compiling the resource with `gnome.compile_resources('lnxdesktop-resources', ...)` and installing the desktop file to `datadir/applications` and the icon to `datadir/icons/hicolor/scalable/apps`.
- [ ] **Step 4: Write `src/main.c`** — `GtkApplication` with id `org.seehuhn.lnxdesktop`; on `activate` create a 640×480 `GtkApplicationWindow` titled "Desktop", load the CSS from the resource with `gtk_style_context_add_provider_for_display`, present it. `src/meson.build` builds the `lnxdesktop` executable with `install: true`.
- [ ] **Step 5: Verify** — Run: `meson setup build && meson compile -C build && meson test -C build`. Expected: build succeeds, `1/1 smoke OK`. Run `./build/src/lnxdesktop` briefly to see the empty window.
- [ ] **Step 6: Write `README.md`** — build and install commands (`meson setup build`, `meson install -C build`, `-Dprefix=$HOME/.local`), niri `window-rule { match app-id="org.seehuhn.lnxdesktop" ... }` example and `spawn-at-startup "lnxdesktop"`.
- [ ] **Step 7: Commit** — `git add -A && git commit -m "Add meson skeleton and empty window"`.

---

### Task 2: Position store

**Files:**
- Create: `src/position-store.h`, `src/position-store.c`
- Test: `tests/test-position-store.c`

**Interfaces:**
- Produces:
  ```c
  typedef struct _PositionStore PositionStore;
  PositionStore *position_store_new (const char *path);        /* loads file if present */
  void           position_store_free (PositionStore *store);   /* flushes pending writes */
  gboolean       position_store_lookup (PositionStore *store, const char *name, int *x, int *y);
  void           position_store_set    (PositionStore *store, const char *name, int x, int y);
  void           position_store_remove (PositionStore *store, const char *name);
  void           position_store_rename (PositionStore *store, const char *old_name, const char *new_name);
  void           position_store_flush  (PositionStore *store); /* write now, atomically */
  ```
  Group name is `g_uri_escape_string (name, NULL, FALSE)` so any file name is a valid key-file group. Keys: `x`, `y` (int), `anchor` (string, always `"left"`). `set`/`remove`/`rename` schedule a flush on a 500 ms `g_timeout_add_once`; `free` and `flush` write immediately via `g_file_set_contents`. A file that fails `g_key_file_load_from_file` is renamed to `<path>.bak` and the store starts empty. A missing parent directory is created on flush.

- [ ] **Step 1: Write the failing tests** — `tests/test-position-store.c`, each test using a fresh `g_dir_make_tmp` directory:
  - `/store/missing-file-is-empty`: `lookup ("a.txt")` returns FALSE.
  - `/store/round-trip`: set `("a.txt", 10, 20)`, flush, free, new store on same path, lookup gives 10, 20.
  - `/store/weird-names`: set `("weird [1] ü.txt", 3, 4)` and `("a=b", 5, 6)`, flush, reopen, both found; the file contents contain no raw `[1]` inside a group header.
  - `/store/rename`: set a, rename a→b, lookup b TRUE, lookup a FALSE; rename of an unknown name is a no-op.
  - `/store/remove`: set then remove, lookup FALSE after reopen.
  - `/store/corrupt-renamed-to-bak`: write `"not a keyfile\n[\n"` to the path, open store, lookup anything FALSE, `<path>.bak` exists with that content.
  - `/store/free-flushes`: set without flush, free, reopen, lookup TRUE.
  - `/store/anchor-key-written`: after flush, file contains `anchor=left`.
- [ ] **Step 2: Run** `meson test -C build test-position-store` — Expected: compile failure, header missing.
- [ ] **Step 3: Implement** `position-store.c` per the interface above.
- [ ] **Step 4: Run** `meson test -C build test-position-store -v` — Expected: all 8 OK.
- [ ] **Step 5: Commit** — `git add src/position-store.* tests/ && git commit -m "Add position store"`.

---

### Task 3: Layout

**Files:**
- Create: `src/layout.h`, `src/layout.c`
- Test: `tests/test-layout.c`

**Interfaces:**
- Produces:
  ```c
  #define LAYOUT_PITCH_W 112
  #define LAYOUT_PITCH_H 128
  typedef struct {
    gboolean has_position;   /* in: stored position present */
    int      x, y;           /* in: stored position */
    int      width, height;  /* in: icon size */
    int      out_x, out_y;   /* out: display position */
    gboolean parked;         /* out: TRUE if not displayed at stored position */
  } LayoutItem;
  /* Items are processed in array order; returns content height (>= viewport_height). */
  int layout_arrange (LayoutItem *items, guint n_items, int viewport_width, int viewport_height);
  ```
  Algorithm: pass 1 places every item with `has_position` whose rect `(x, y, width, height)` lies inside `(0, 0, viewport_width, viewport_height)`; `parked = FALSE`. Pass 2, in array order, parks the rest: `cols = MAX (1, viewport_width / LAYOUT_PITCH_W)`, `rows = MAX (1, viewport_height / LAYOUT_PITCH_H)`; cell `(c, r)` has origin `(MAX (0, viewport_width - (c + 1) * LAYOUT_PITCH_W), r * LAYOUT_PITCH_H)`; scan `c = 0..cols-1` outer, `r = 0..rows-1` inner; a cell is free if the pitch-sized cell rect intersects no already placed item's rect. If none is free, `rows += 1` and scan only the new row across all columns, repeating until a cell is found. Return `MAX (viewport_height, max bottom of any item)`.

- [ ] **Step 1: Write the failing tests** — `tests/test-layout.c`, all icons 96×112 unless noted, viewport 500×400 unless noted:
  - `/layout/stored-fits-stays`: item at (10, 10) → out (10, 10), not parked.
  - `/layout/stored-overflow-parks`: item at (450, 10) (right edge 546 > 500) → parked, out_x = 500 − 112 = 388, out_y = 0.
  - `/layout/new-fill-top-right-then-down`: three unpositioned → (388, 0), (388, 128), (388, 256); a fourth → (276, 0) (rows = 3).
  - `/layout/park-skips-occupied`: stored item at (390, 0) then one unpositioned → unpositioned at (388, 128).
  - `/layout/grows-downward`: viewport 120×140 (1 col, 1 row), three unpositioned → y = 0, 128, 256; return value 256 + 112 = 368.
  - `/layout/tiny-viewport`: viewport 0×0, two unpositioned → both placed at x = 0, y = 0 and 128, no hang; viewport 50×50 → x = 0.
  - `/layout/overlapping-stored-kept`: two stored at (10, 10) → both out (10, 10).
  - `/layout/round-trip-restores`: stored at (450, 10); arrange at 500×400 → parked; arrange again at 800×400 → out (450, 10), not parked.
  - `/layout/deterministic`: same input twice yields identical outputs.
- [ ] **Step 2: Run** `meson test -C build test-layout` — Expected: compile failure.
- [ ] **Step 3: Implement** `layout.c` per the algorithm above.
- [ ] **Step 4: Run** `meson test -C build test-layout -v` — Expected: all 9 OK.
- [ ] **Step 5: Commit** — `git commit -m "Add layout algorithm"`.

---

### Task 4: DesktopItem

**Files:**
- Create: `src/desktop-item.h`, `src/desktop-item.c`
- Test: `tests/test-desktop-item.c`

**Interfaces:**
- Produces:
  ```c
  G_DECLARE_FINAL_TYPE (DesktopItem, desktop_item, DESKTOP, ITEM, GObject)
  DesktopItem *desktop_item_new (GFile *file);
  GFile       *desktop_item_get_file (DesktopItem *self);
  void         desktop_item_set_file (DesktopItem *self, GFile *file);     /* after rename */
  const char  *desktop_item_get_name (DesktopItem *self);                 /* g_file_get_basename, the store key */
  const char  *desktop_item_get_display_name (DesktopItem *self);
  const char  *desktop_item_get_content_type (DesktopItem *self);
  goffset      desktop_item_get_size (DesktopItem *self);
  GDateTime   *desktop_item_get_modified (DesktopItem *self);             /* transfer none, may be NULL */
  GIcon       *desktop_item_get_icon (DesktopItem *self);                 /* transfer none, may be NULL */
  gboolean     desktop_item_is_directory (DesktopItem *self);
  gboolean     desktop_item_get_position (DesktopItem *self, int *x, int *y);
  void         desktop_item_set_position (DesktopItem *self, int x, int y);
  void         desktop_item_clear_position (DesktopItem *self);
  void         desktop_item_reload_async (DesktopItem *self, GCancellable *c, GAsyncReadyCallback cb, gpointer data);
  gboolean     desktop_item_reload_finish (DesktopItem *self, GAsyncResult *res, GError **error);
  ```
  Properties: `selected` (boolean, read-write), `info` (notify emitted after a successful reload), `position` (notify on set/clear). Reload queries attributes `standard::display-name,standard::content-type,standard::icon,standard::size,standard::type,time::modified` with `g_file_query_info_async`.

- [ ] **Step 1: Write the failing tests** — `tests/test-desktop-item.c` with a temp dir and a `GMainLoop` helper `run_until (gboolean *done)` with a 5 s timeout:
  - `/item/name-before-reload`: `get_name` is the basename even before reload; `get_display_name` is NULL.
  - `/item/reload-fills-info`: write "hello" to `note.txt`, reload, display name "note.txt", content type `text/plain`, size 5, modified non-NULL, icon non-NULL, not a directory; `notify::info` fired once.
  - `/item/directory`: `is_directory` TRUE for a created subdir.
  - `/item/position`: initially FALSE; set (1, 2) → TRUE, 1, 2; clear → FALSE; `notify::position` fired twice.
  - `/item/reload-missing-file-errors`: reload of a nonexistent file returns FALSE with `G_IO_ERROR_NOT_FOUND`.
- [ ] **Step 2: Run** the test — Expected: compile failure.
- [ ] **Step 3: Implement** `desktop-item.c`.
- [ ] **Step 4: Run** `meson test -C build test-desktop-item -v` — Expected: all 5 OK.
- [ ] **Step 5: Commit** — `git commit -m "Add DesktopItem"`.

---

### Task 5: DesktopModel

**Files:**
- Create: `src/desktop-model.h`, `src/desktop-model.c`
- Test: `tests/test-desktop-model.c`

**Interfaces:**
- Consumes: `PositionStore` (Task 2), `DesktopItem` (Task 4).
- Produces:
  ```c
  G_DECLARE_FINAL_TYPE (DesktopModel, desktop_model, DESKTOP, MODEL, GObject)
  DesktopModel *desktop_model_new (GFile *directory, PositionStore *store);   /* takes ownership of store; creates directory if missing */
  GListModel   *desktop_model_get_items (DesktopModel *self);               /* GListStore of DesktopItem, sorted by g_utf8_collate_key_for_filename of name */
  GFile        *desktop_model_get_directory (DesktopModel *self);
  gboolean      desktop_model_is_monitoring (DesktopModel *self);           /* FALSE if the GFileMonitor could not be created */
  void          desktop_model_load_async (DesktopModel *self, GCancellable *c, GAsyncReadyCallback cb, gpointer data);
  gboolean      desktop_model_load_finish (DesktopModel *self, GAsyncResult *res, GError **error);
  DesktopItem  *desktop_model_lookup (DesktopModel *self, const char *name);  /* transfer none or NULL */
  void          desktop_model_set_position (DesktopModel *self, DesktopItem *item, int x, int y);   /* item + store */
  void          desktop_model_preseed_position (DesktopModel *self, const char *name, int x, int y); /* store only, for drops */
  ```
  Behaviour: `load_async` enumerates with `g_file_enumerate_children_async`, skipping names starting with `.`, creating an item per child, reloading each, applying `position_store_lookup`, and inserting sorted. The monitor is created with `G_FILE_MONITOR_WATCH_MOVES`. Events: `CREATED` → add item (ignore if name exists); `DELETED` → remove item, `position_store_remove`; `RENAMED` → `position_store_rename`, `desktop_item_set_file`, remove and re-insert sorted; `MOVED_IN` like CREATED, `MOVED_OUT` like DELETED; `CHANGES_DONE_HINT` → reload item. Hidden files are ignored in all events.

- [ ] **Step 1: Write the failing tests** — `tests/test-desktop-model.c`, temp dir per test, a `wait_for_count (GListModel *, guint n)` helper that spins the main loop until `g_list_model_get_n_items` equals `n` or 5 s elapse (assert on timeout):
  - `/model/load-sorted-skips-hidden`: files `b.txt`, `a.txt`, `.hidden`; load; 2 items in order a, b; each item's content type set.
  - `/model/positions-applied-on-load`: store has `a.txt` → (7, 8); after load, item a has position (7, 8); item b has none.
  - `/model/create-adds`: after load, create `c.txt` → count 3, lookup "c.txt" non-NULL.
  - `/model/delete-removes-and-forgets-position`: set position of a via `desktop_model_set_position`, delete file → count 1 and `position_store_lookup ("a.txt")` FALSE.
  - `/model/rename-keeps-position`: set position of a (7, 8), `g_file_set_display_name` to `z.txt` → lookup "z.txt" has (7, 8), "a.txt" NULL, item is last in order; renaming b (no position) creates no store entry.
  - `/model/change-reloads`: append to `a.txt`; wait until `desktop_item_get_size` is updated.
  - `/model/creates-missing-directory`: `desktop_model_new` on a nonexistent path creates it, `is_monitoring` TRUE.
  - `/model/preseed-position`: preseed "new.txt" → (30, 40), then create `new.txt` → item has (30, 40).
- [ ] **Step 2: Run** — Expected: compile failure.
- [ ] **Step 3: Implement** `desktop-model.c`.
- [ ] **Step 4: Run** `meson test -C build test-desktop-model -v` — Expected: all 8 OK.
- [ ] **Step 5: Commit** — `git commit -m "Add DesktopModel with file monitoring"`.

---

### Task 6: Thumbnails

**Files:**
- Create: `src/thumbnails.h`, `src/thumbnails.c`
- Test: `tests/test-thumbnails.c`

**Interfaces:**
- Produces:
  ```c
  gboolean      thumbnails_is_image (const char *content_type);              /* gdk_pixbuf_get_formats mime match */
  char         *thumbnails_cache_path (GFile *file);                          /* <cache dir>/thumbnails/large/<md5 of uri>.png */
  void          thumbnails_load_async (GFile *file, const char *content_type, GCancellable *c, GAsyncReadyCallback cb, gpointer data);
  GdkPaintable *thumbnails_load_finish (GAsyncResult *res, GError **error);   /* NULL + error if not an image or failed */
  ```
  `load_async` runs in a `GTask` thread: if the cached PNG exists and its `tEXt::Thumb::MTime` equals the file's mtime, load it; otherwise `gdk_pixbuf_new_from_file_at_scale (path, 256, 256, TRUE)`, apply `gdk_pixbuf_apply_embedded_orientation`, save as PNG with `Thumb::URI` and `Thumb::MTime` options to the cache path (creating directories, ignoring save errors), and return `gdk_texture_new_for_pixbuf`. The cache dir honours `XDG_CACHE_HOME` via `g_get_user_cache_dir`.

- [ ] **Step 1: Write the failing tests** — `tests/test-thumbnails.c`, setting `XDG_CACHE_HOME` to a temp dir via `g_setenv` before `g_test_init`; a 300×200 red PNG is generated in the test with GdkPixbuf:
  - `/thumb/is-image`: TRUE for `image/png`, `image/jpeg`; FALSE for `text/plain`, `application/pdf`.
  - `/thumb/cache-path`: for `file:///tmp/x.png` the path ends with `/thumbnails/large/` + md5 of that URI + `.png`.
  - `/thumb/generates-and-caches`: load → paintable 256×171 (width 256); cache file now exists.
  - `/thumb/uses-cache`: delete the source PNG, load again → still succeeds (served from cache) because the stale check is only done when the source exists. (Decision: a missing source means the item is being removed anyway.)
  - `/thumb/non-image-errors`: load with `text/plain` → NULL, error set.
- [ ] **Step 2: Run** — Expected: compile failure.
- [ ] **Step 3: Implement** `thumbnails.c`.
- [ ] **Step 4: Run** `meson test -C build test-thumbnails -v` — Expected: all 5 OK.
- [ ] **Step 5: Commit** — `git commit -m "Add thumbnail loader"`.

---

### Task 7: DesktopIcon widget

**Files:**
- Create: `src/desktop-icon.h`, `src/desktop-icon.c`
- Modify: `data/style.css`

**Interfaces:**
- Consumes: `DesktopItem` (Task 4), `thumbnails_*` (Task 6).
- Produces:
  ```c
  G_DECLARE_FINAL_TYPE (DesktopIcon, desktop_icon, DESKTOP, ICON, GtkWidget)
  GtkWidget   *desktop_icon_new (DesktopItem *item);
  DesktopItem *desktop_icon_get_item (DesktopIcon *self);
  void         desktop_icon_start_rename (DesktopIcon *self);
  void         desktop_icon_set_drag_files_func (DesktopIcon *self, GList *(*func) (DesktopIcon *, gpointer), gpointer data); /* canvas supplies the selected files */
  ```
  Signals:
  - `clicked (int button, int n_press, GdkModifierType state)` — emitted on press from a `GtkGestureClick` with button 0 (any), after the icon has decided whether this is a label click that starts a rename.
  - `rename-requested (const char *new_name) → gboolean` (`g_signal_accumulator_true_handled`): emitted on Enter in the entry; if no handler returns TRUE the entry stays open.
  - `drag-prepared (double x, double y)` — emitted from the `GtkDragSource::prepare` handler with the pointer offset inside the icon, so the canvas can record the hot spot.

  Structure: vertical `GtkBox` child containing a `GtkImage` (pixel size 96) and a `GtkLabel` (wrap, `PANGO_WRAP_WORD_CHAR`, `lines = 2`, ellipsize middle, justify centre, max-width-chars 12) and a hidden `GtkEntry` that replaces the label during rename. Fixed request: width 112, natural height 128 (box margin so the pitch matches). CSS classes `desktop-icon`, `:selected` state via `gtk_widget_set_state_flags (GTK_STATE_FLAG_SELECTED)` bound to the item's `selected` property. On `notify::info` the image is set from `desktop_item_get_icon`; if `thumbnails_is_image` and the widget is mapped, `thumbnails_load_async` replaces it with the paintable. A `GtkDragSource` with actions COPY | MOVE; `prepare` returns `gdk_content_provider_new_typed (GDK_TYPE_FILE_LIST, gdk_file_list_new_from_list (files))` where files come from the drag-files func (fallback: this item's file), and sets the drag icon to a `GtkWidgetPaintable` of the image. Rename: a click (button 1, n_press 1, no modifier) on the label while the item was already selected before the press starts rename on release instead of emitting `clicked`. Entry text initially selects the name without extension. Escape cancels.

- [ ] **Step 1: Write `desktop-icon.c`** per the above, plus CSS in `style.css`: `.desktop-icon { padding: 4px; border-radius: 8px; } .desktop-icon:selected { background: alpha(@accent_bg_color, 0.35); } .desktop-icon label { font-size: 0.9em; }`.
- [ ] **Step 2: Add a throwaway preview** — temporarily in `main.c` show one `DesktopIcon` for `~/Desktop/` itself; run `./build/src/lnxdesktop`, check icon, label and selection highlight (set selected via a key press). Remove the preview before commit.
- [ ] **Step 3: Verify** `meson compile -C build` has no warnings from `desktop-icon.c`.
- [ ] **Step 4: Commit** — `git commit -m "Add DesktopIcon widget"`.

---

### Task 8: Canvas with layout manager

**Files:**
- Create: `src/desktop-layout.h`, `src/desktop-layout.c`, `src/desktop-canvas.h`, `src/desktop-canvas.c`
- Modify: `src/main.c`

**Interfaces:**
- Consumes: `layout_arrange` (Task 3), `DesktopModel` (Task 5), `DesktopIcon` (Task 7).
- Produces:
  ```c
  G_DECLARE_FINAL_TYPE (DesktopLayout, desktop_layout, DESKTOP, LAYOUT, GtkLayoutManager)
  /* children are DesktopIcon; reads desktop_item_get_position of each child's item */

  G_DECLARE_FINAL_TYPE (DesktopCanvas, desktop_canvas, DESKTOP, CANVAS, GtkWidget)
  GtkWidget    *desktop_canvas_new (DesktopModel *model);
  DesktopModel *desktop_canvas_get_model (DesktopCanvas *self);
  GList        *desktop_canvas_get_selected_items (DesktopCanvas *self);   /* transfer container, DesktopItem* */
  GList        *desktop_canvas_get_selected_files (DesktopCanvas *self);   /* transfer container, GFile* */
  void          desktop_canvas_select_all (DesktopCanvas *self);
  void          desktop_canvas_clear_selection (DesktopCanvas *self);
  DesktopIcon  *desktop_canvas_icon_for_item (DesktopCanvas *self, DesktopItem *item);
  ```
  `DesktopLayout::measure`: horizontal → min 0, nat 0 (fill parent); vertical → content height from a trial `layout_arrange` using the parent `GtkViewport`'s current height (or `for_size * 3 / 4` when the parent has no height yet). `DesktopLayout::allocate`: build the `LayoutItem` array from children in child order (the model's sorted order; the canvas inserts icons in model order), call `layout_arrange (items, n, width, viewport_height)`, `gtk_widget_size_allocate` each child at `(out_x, out_y, 112, 128)`.
  Canvas: `vexpand`, `hexpand`, `focusable`, CSS class `desktop-canvas`; connects to `items-changed` on `desktop_model_get_items` and keeps `GHashTable item → icon`, inserting icons at the matching child position with `gtk_widget_insert_before`. Any `notify::position` on an item calls `gtk_widget_queue_allocate`. `main.c` builds: model from `g_get_user_special_dir (G_USER_DIRECTORY_DESKTOP)` (fallback `~/Desktop`) with a store at `g_get_user_config_dir()/lnxdesktop/positions.ini`, a `GtkScrolledWindow` (hscroll NEVER, vscroll AUTOMATIC) containing the canvas, `desktop_model_load_async` started after the window is presented. If `desktop_model_is_monitoring` is FALSE, a `GtkLabel` "External changes to the Desktop folder are not tracked." with CSS class `warning` is packed above the scrolled window.

- [ ] **Step 1: Write `desktop-layout.c` and `desktop-canvas.c`** per the above; wire `main.c`.
- [ ] **Step 2: Verify manually** — put six files in `~/Desktop/`, run; icons fill from the top right downward. Resize the window narrower than the arrangement and back; nothing in `positions.ini` changes (the file should not exist yet).
- [ ] **Step 3: Commit** — `git commit -m "Add canvas with parking layout"`.

---

### Task 9: Selection, rubber band and keyboard navigation

**Files:**
- Modify: `src/desktop-canvas.c`, `data/style.css`

**Interfaces:**
- Consumes: `DesktopIcon::clicked` (Task 7).
- Produces: signal on `DesktopCanvas`: `activate-selection ()` (double-click or Enter) and `context-menu-requested (DesktopItem *item_or_null, double x, double y)`. Selection state lives in each item's `selected` property.

- [ ] **Step 1: Implement clicks** — on `clicked`: button 1 n_press 1: no modifier → select only this; Ctrl → toggle; Shift → add. n_press 2 → `activate-selection`. Button 3 → select this if not selected, then emit `context-menu-requested (item, x, y)` in canvas coordinates.
- [ ] **Step 2: Implement rubber band** — a `GtkGestureDrag` on the canvas; on `drag-begin` use `gtk_widget_pick` and deny the sequence unless the hit widget is the canvas itself; otherwise clear selection (unless Ctrl held), store origin, and on `drag-update` set `selected` on every icon whose allocation intersects the band; `snapshot` chains up then draws the band as a filled rect with `alpha(@accent_bg_color, 0.2)` and a 1 px `@accent_bg_color` border. Right-click on empty canvas emits `context-menu-requested (NULL, x, y)`; left-click on empty canvas with no drag clears the selection.
- [ ] **Step 3: Implement keys** — `GtkEventControllerKey` on the canvas: arrows pick the nearest icon in that direction from the focused item's centre (angle within 45° of the axis, smallest distance; the first icon when nothing is selected), select only it; Enter → `activate-selection`; Escape → clear selection; Ctrl+A → select all. Window actions `win.select-all` are added in Task 11; this task handles only the raw keys.
- [ ] **Step 4: Verify manually** — click, Ctrl-click, Shift-click, rubber band, arrows, Escape.
- [ ] **Step 5: Commit** — `git commit -m "Add selection, rubber band and keyboard navigation"`.

---

### Task 10: Actions: unique names, copy-in, open, trash, rename, clipboard, info

**Files:**
- Create: `src/actions.h`, `src/actions.c`
- Test: `tests/test-actions.c`

**Interfaces:**
- Consumes: `DesktopModel` (Task 5), `DesktopItem` (Task 4).
- Produces:
  ```c
  char    *actions_unique_name (GFile *dir, const char *name);   /* "a.txt" → "a.txt" if free, else "a (2).txt", "a (3).txt", … ; "Makefile" → "Makefile (2)" */
  void     actions_report_error (GtkWindow *parent, GError *error);  /* GtkAlertDialog with error->message */
  void     actions_open (GtkWindow *parent, GList *files);           /* GtkFileLauncher per file */
  void     actions_open_with (GtkWindow *parent, GFile *file);       /* GtkAppChooserDialog, launch chosen GAppInfo */
  void     actions_trash (GtkWindow *parent, GList *files);          /* g_file_trash_async each */
  gboolean actions_rename (GtkWindow *parent, GFile *file, const char *new_name, GError **error); /* g_file_set_display_name; reports and returns FALSE on error */
  void     actions_copy_to_clipboard (GtkWidget *widget, GList *files);  /* GdkFileList on gtk_widget_get_clipboard */
  void     actions_paste (GtkWindow *parent, GtkWidget *widget, DesktopModel *model);  /* reads GdkFileList, then actions_import */
  void     actions_import (GtkWindow *parent, DesktopModel *model, GList *files, gboolean move, gboolean has_point, int x, int y);
  void     actions_show_info (GtkWindow *parent, DesktopItem *item);
  ```
  `actions_import`: for each file, if its parent is the model directory and `has_point`, call `desktop_model_set_position`; otherwise compute `actions_unique_name`, `desktop_model_preseed_position` when `has_point` (offset by 0, +128 per further file), then `g_file_copy_async` or `g_file_move_async` with `G_FILE_COPY_NONE`, reporting errors. Info dialog: `GtkWindow` transient for parent, title "Info", a `GtkGrid` with rows Name, Type (`g_content_type_get_description` + mime in parentheses), Size (`g_format_size`), Modified (`g_date_time_format (…, "%c")`), Path; Escape closes.

- [ ] **Step 1: Write the failing tests** — `tests/test-actions.c` (no display needed for these):
  - `/actions/unique-name-free`: empty dir → "a.txt".
  - `/actions/unique-name-taken`: existing `a.txt` → "a (2).txt"; existing `a.txt` and `a (2).txt` → "a (3).txt".
  - `/actions/unique-name-no-extension`: existing `Makefile` → "Makefile (2)".
  - `/actions/unique-name-single-extension`: existing `archive.tar.gz` → "archive.tar (2).gz" (only the last extension is kept after the suffix).
- [ ] **Step 2: Run** — Expected: compile failure.
- [ ] **Step 3: Implement** `actions.c`.
- [ ] **Step 4: Run** `meson test -C build test-actions -v` — Expected: all 4 OK.
- [ ] **Step 5: Commit** — `git commit -m "Add file actions"`.

---

### Task 11: Window actions, context menu, open, rename, trash, info wiring

**Files:**
- Modify: `src/main.c`, `src/desktop-canvas.c`

**Interfaces:**
- Consumes: `actions_*` (Task 10), canvas signals (Task 9), `DesktopIcon::rename-requested`, `desktop_icon_start_rename` (Task 7).
- Produces: window `GSimpleAction`s `open`, `open-with`, `rename`, `trash`, `copy`, `paste`, `info`, `select-all`; accelerators `Return`→open, `F2`→rename, `Delete`→trash, `<Control>c`, `<Control>v`, `<Control>a`.

- [ ] **Step 1: Add actions in `main.c`** — each reads `desktop_canvas_get_selected_files`/`items` and calls the matching `actions_*`; `open-with` and `info` use the first selected item; `rename` calls `desktop_icon_start_rename` on the first selected icon; `paste` calls `actions_paste`. Enable/disable actions on selection change (`open-with`, `rename`, `info` need exactly one selected; `open`, `trash`, `copy` need at least one).
- [ ] **Step 2: Context menu** — in `main.c` connect `context-menu-requested`: build a `GMenu` with sections Open / Open With…, Rename, Move to Trash, Copy, Info for an item, or Paste / Select All for empty space (Paste only when `gdk_content_formats_contain_gtype (gdk_clipboard_get_formats (...), GDK_TYPE_FILE_LIST)`); show in a `GtkPopoverMenu` parented to the canvas, pointing to `(x, y)`.
- [ ] **Step 3: Rename wiring** — canvas connects `rename-requested` on every icon: `actions_rename (window, file, new_name, NULL)` returns TRUE → handler returns TRUE (entry closes); FALSE → entry stays.
- [ ] **Step 4: Activate** — `activate-selection` calls the `open` action.
- [ ] **Step 5: Verify manually** — double-click a PDF opens the viewer; right-click → Open With… lists viewers; F2 rename; Delete trashes; Info shows the five rows; Ctrl+C then paste in a file manager.
- [ ] **Step 6: Commit** — `git commit -m "Wire actions, context menu and shortcuts"`.

---

### Task 12: Drag and drop, internal moves, position persistence

**Files:**
- Modify: `src/desktop-canvas.c`, `src/desktop-icon.c`

**Interfaces:**
- Consumes: `desktop_icon_set_drag_files_func`, `drag-prepared` (Task 7); `actions_import` (Task 10); `desktop_model_set_position` (Task 5).

- [ ] **Step 1: Drag out** — the canvas sets each icon's drag-files func to return `desktop_canvas_get_selected_files` (selecting the dragged icon first if it was not selected). On `drag-prepared (x, y)` the canvas records `hot = (x, y)`, the dragged item, and for every selected item its offset from the dragged icon's allocation origin.
- [ ] **Step 2: Drop target** — `GtkDropTarget` on the canvas for `GDK_TYPE_FILE_LIST`, actions COPY | MOVE. In `drop`: if `gdk_drop_get_drag (drop) != NULL` and the recorded drag is set (local move): for each recorded item `desktop_model_set_position (model, item, x - hot.x + dx, y - hot.y + dy)` clamped to ≥ 0; return TRUE. Otherwise `actions_import (window, model, files, action == MOVE, TRUE, x - 48, y - 56)` (centre the first icon on the pointer). Clear the recorded drag on `drag-end`.
- [ ] **Step 3: Verify manually** — move an icon, restart the app, it is where it was dropped and `positions.ini` holds it; move a multi-selection, offsets preserved; drag a file to a text editor in another niri column; drop a file from a file manager onto the window; toggle niri column width full → half → full, every icon returns.
- [ ] **Step 4: Commit** — `git commit -m "Add drag and drop and persistent moves"`.

---

### Task 13: Polish and release checklist

**Files:**
- Modify: `README.md`, `data/style.css`, `src/desktop-icon.c`

- [ ] **Step 1: Thumbnail priority** — load thumbnails only once an icon is mapped (`GtkWidget::map`), so visible icons come first; cancel on unmap via a `GCancellable`.
- [ ] **Step 2: Shutdown flush** — `position_store_free` is called from the model's `dispose`; verify by setting a position and closing the window immediately, then checking the file.
- [ ] **Step 3: HiDPI check** — move the window between the 2× and 1× screens; icons stay crisp, arrangement unchanged when moved back.
- [ ] **Step 4: Run the full manual checklist from the spec** and record the result in `README.md` under "Status".
- [ ] **Step 5: Run** `meson test -C build` — Expected: all tests OK.
- [ ] **Step 6: Commit** — `git commit -m "Polish thumbnails, document status"`.
