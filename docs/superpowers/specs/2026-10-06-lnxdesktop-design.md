# lnxDesktop — design

Date: 2026-10-06

## Purpose

lnxDesktop shows the files in `~/Desktop/` as freely arranged icons, the way
the macOS desktop does. Only the file icons are replicated: there is no Dock,
menu bar or app launcher, and applications run as separate niri windows, not
inside this program. It exists because niri (a scrollable-tiling Wayland compositor) draws no desktop,
so there is nowhere to put files and drag them from. The app runs as an
ordinary niri window in its own column; other apps run in neighbouring
columns.

Success looks like this: a download or screenshot lands in `~/Desktop/` and
appears as an icon; the user shuffles it where they like; double-click opens
it; it can be dragged into a mail client or editor in another column, or
copied and pasted there; right-click offers "Open With…" and "Info"; the
arrangement survives restarts and window resizes.

## Scope

### Included

- One icon (thumbnail for images, type icon otherwise) with name per
  non-hidden file or folder in `~/Desktop/`. External changes (create, delete,
  rename, modify) are tracked live.
- Free pixel placement of icons, persisted across restarts.
- Sensible behaviour when the window changes size or moves between screens:
  stored positions are never changed by the app, only by the user.
- Open on double-click or Enter in the default application for the file
  type; "Open With…" to pick any other installed application.
- Drag out to other apps; drop in from other apps (copy, or move when the drag
  action is move).
- Copy and paste of files via the clipboard.
- Multi-selection: click, ctrl-click, shift-click, rubber band, Ctrl+A.
- Rename in place.
- Move to Trash.
- Context menu: Open, Open With…, Rename, Move to Trash, Copy, Info.
- Info dialog: name, type, size, modified date, path.

### Excluded

- No "Open Terminal Here".
- No file-manager growth: no sidebar, path bar, tabs, or browsing of other
  folders. Double-clicking a folder opens it in the default file manager.
- No snap-to-grid, no auto-arrange.
- No undo.

## Constraints

- GTK 4 in C. No binding layer.
- Debian 13 "trixie" packages only: GTK 4.18, GLib/GIO, GdkPixbuf. No
  libadwaita, no gnome-desktop.
- Wayland only, niri compositor.
- Mixed DPI: a 2× panel and a 1× external monitor. All sizes in logical
  pixels.

## Architecture

A single process with one window. Modules are `.c`/`.h` pairs with one job
each. Dependencies point strictly downward:

```
main → desktop-canvas → { desktop-model, layout, actions, desktop-icon }
desktop-model → { desktop-item, position-store }
desktop-icon  → { desktop-item, thumbnails }
```

Nothing below `desktop-canvas` and `desktop-icon` knows about widgets.

### Modules

**`desktop-item.c`** — `DesktopItem`, a GObject holding: the `GFile`; the
cached `GFileInfo` (display name, content type, size, mtime); the icon
`GdkPaintable`; the stored position, or "none"; selection state. Provides an
async `reload()` that re-queries info and icon.

**`desktop-model.c`** — `DesktopModel`: owns a `GListStore` of `DesktopItem`
for `~/Desktop/`, a `GFileMonitor` on that directory, and the position store.
Enumerates on start, applies monitor events to the store (created, deleted,
renamed, changed), skips hidden files, and creates `~/Desktop/` if missing.
Never touches widgets.

**`position-store.c`** — reads and writes
`~/.config/lnxdesktop/positions.ini` with `GKeyFile`. One group per file
name, keys `x`, `y` (logical pixels from the top-left) and `anchor`
(reserved, always `left` for now; allows edge anchoring later without
migration). Writes are coalesced by a short idle timeout and done atomically
with `g_file_set_contents`. Renames move the group. A missing file is empty;
a corrupt file is renamed to `positions.ini.bak` and treated as empty.

**`layout.c`** — pure functions with no widget dependency. Input: the items
with their stored positions and sizes, and the viewport size. Output: a
display position per item. See "Layout" below. Unit-tested.

**`desktop-icon.c`** — `DesktopIcon`, a `GtkWidget` showing one item: the
picture above a label that wraps to two lines and then ellipsizes in the
middle. Swaps in a `GtkEntry` for rename. Owns its `GtkDragSource`, click
gestures and the `selected` CSS state.

**`desktop-canvas.c`** — `DesktopCanvas`, a `GtkFixed` subclass placed inside
a `GtkScrolledWindow` (vertical scrolling only). Binds to the model, creates
and removes icons on `items-changed`, runs the layout pass on size change,
draws the rubber band in `snapshot()`, owns the `GtkDropTarget`, the context
menu, keyboard shortcuts and selection.

**`actions.c`** — open, open-with, info dialog, trash, copy to clipboard,
paste, rename. Thin wrappers over GIO, `GtkFileLauncher`,
`GtkAppChooserDialog` and `GdkClipboard`.

**`thumbnails.c`** — for image mime types, looks up or generates freedesktop
thumbnails in `~/.cache/thumbnails/` (large size, 256 px) using GdkPixbuf,
asynchronously; falls back to the theme icon for the content type. Requests
are served in order of visibility.

**`main.c`** — `GtkApplication` with app-id `org.seehuhn.lnxdesktop`, one
window, loads `style.css` from GResource.

## Layout

Stored positions are absolute logical pixels from the top-left of the canvas,
exactly where the user dropped the icon. No snapping. Overlaps are allowed.

The layout pass runs on size change, on item add or remove, and when an icon
first learns its size. It produces a *display layout* and writes nothing to
disk:

1. Order items: those with a stored position first, in stored order; then
   unpositioned ones by name.
2. An item *fits* if its rectangle lies inside the viewport. Fitting items
   are displayed at their stored position.
3. All other items (unpositioned, or not fitting) are *parked*: scan cells of
   one grid pitch, starting top-right, downward then leftward, and take the
   first cell that does not intersect any already-placed item.
4. If parking runs out of cells, the canvas grows downward and the scrolled
   window scrolls vertically. There is never horizontal scrolling.

Because nothing is written, toggling a niri column between full and half
width, or moving the window to a screen of a different size, is reversible:
when the original size returns, every icon returns to its stored spot.

Only a user drag updates stored positions. Parked and unpositioned items keep
being parked on every pass until dragged, so new files always land in the
first gap at the top right and the positions file holds only deliberate
placements.

Grid pitch is 112 × 128 logical pixels; the picture is 96 × 96 with a two-line
label below.

## Interaction

**Selection.** Click selects one; ctrl-click toggles; shift-click adds; click
on empty canvas clears and starts a rubber band; Ctrl+A selects all; Escape
clears. Right-click on an unselected icon selects it first.

**Move within the window.** A `GtkGestureDrag` on the canvas moves the whole
selection live, keeping relative offsets. On release each moved icon's stored
position becomes its new top-left and the store is written.

**Drag out.** Each `DesktopIcon` has a `GtkDragSource` offering a
`GdkFileList` of all selected files, with actions copy and move, and the
icon's picture as drag icon. The canvas's drop target ignores drops whose
source is this window; those are handled as a move.

**Drop in.** The canvas `GtkDropTarget` accepts `GdkFileList`. Files already
in `~/Desktop/` are repositioned to the drop point. Others are copied with
`g_file_copy_async` (moved when the action is move); the position store is
pre-seeded with the drop point so the new icon appears where it was dropped.
Name collisions get a " (2)"-style suffix.

**Clipboard.** Copy puts a `GdkFileList` on the clipboard. Paste copies the
clipboard's files into `~/Desktop/` like a drop with no position.

**Open.** Double-click or Enter opens each selected file in the default
application for its file type, as `xdg-open` would: a PDF in the PDF viewer,
an image in the image viewer, and so on (implemented with `GtkFileLauncher`).
Folders open in the default file manager.

**Context menu.** Right-click or Menu key on an icon: Open, Open With…,
Rename, Move to Trash, Copy, Info. "Open With…" shows a chooser listing every
installed application that handles the file's type and opens the file in the
one picked (`GtkAppChooserDialog`). On empty space: Paste (when the clipboard
holds files) and Select All.

**Rename.** Clicking the name of an already-selected icon, or F2, swaps in a
`GtkEntry` with the base name selected. Enter calls
`g_file_set_display_name`; Escape cancels. The monitor's rename event moves
the position entry to the new name.

**Trash.** Delete key or menu entry calls `g_file_trash` for each selected
file, with no confirmation. The deleted file's position entry is removed.

**Keyboard.** Arrows move the selection to the nearest icon in that
direction; Enter opens; Delete trashes; F2 renames; Ctrl+C, Ctrl+V, Ctrl+A as
usual.

**Info dialog.** A plain `GtkWindow` with a grid: name, type (description and
mime type), size (human-readable), modified date, full path. Read-only.

**External changes.** The model handles monitor events; the canvas reacts
only to `items-changed`. A changed file reloads info and thumbnail.

## Error handling

- Every GIO failure (copy, move, trash, rename, launch) shows a
  `GtkAlertDialog` with the `GError` message. Nothing is retried silently.
- A failed thumbnail falls back to the type icon with no dialog.
- Corrupt positions file: renamed to `positions.ini.bak`, start empty.
- Missing `~/Desktop/`: created.
- Monitor creation failure: the app runs, and a one-line bar at the top says
  that external changes are not tracked.
- Rename to an existing name: dialog, entry stays open.
- File disappears mid-rename or mid-drag: icon vanishes, operation abandoned
  quietly.
- Positions file is written atomically so a crash never truncates it.

## Appearance

Background uses the theme's view colour. Selected icons get a rounded
translucent highlight behind picture and label. All styling is in
`style.css`, compiled into the binary as a GResource. Thumbnails are
requested at 256 px and scaled down so they are crisp on the 2× panel.

## Build and install

- Meson project `lnxdesktop`, C11, `warning_level=2`.
- Dependencies by pkg-config: `gtk4`, `gio-unix-2.0`, `gdk-pixbuf-2.0`.
- Layout: `src/`, `tests/`, `data/` (`lnxdesktop.desktop`, scalable SVG icon,
  `style.css`, GResource XML), `README.md`.
- `meson setup build && meson install -C build`. Default prefix
  `/usr/local`; `-Dprefix=$HOME/.local` for a user install.
- The README shows a niri `window-rule` for `app-id="org.seehuhn.lnxdesktop"`
  and an autostart line.

## Testing

Run with `meson test`.

- **Unit tests** (`g_test`) for `layout.c`: fit, park, top-right column-fill
  order, downward growth, identical output for identical input, reversibility
  after a size round trip. For `position-store.c`: round trip, rename,
  missing file, corrupt file renamed to `.bak`.
- **Model tests** with a temporary directory and a main loop, no display:
  create, delete, rename, modify files on disk and assert the `GListStore`
  follows within a timeout.
- **No widget tests.** Headless GTK widget tests are fragile and the canvas
  is thin glue over tested parts.

Manual checklist before each release:

1. Drag an icon to a text editor in another column; the path arrives.
2. Drop a file from a file manager onto the window; it appears at the drop
   point.
3. Toggle the column between full and half width; icons park and return.
4. Move the window between the 2× and 1× screens; icons stay crisp and the
   arrangement is unchanged when moved back.
5. Rename, trash, copy and paste, Open With…, Info.

Development of the unit- and model-tested modules follows TDD.
