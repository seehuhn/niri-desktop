# lnxDesktop — idea

## What I want

A small, minimal window that shows the files in `~/Desktop/` as icons — a
"desktop" for a compositor that doesn't have one.

The desktop here is **niri** (scrollable-tiling Wayland). niri draws no desktop
and no icons, so there is nowhere to put files and drag them from. The plan is
to keep this app open as an ordinary window in a niri column next to other
apps, rather than drawing on the background (in a tiling layout the background
would be hidden behind windows most of the time anyway).

It should:

- show an icon (and name) for each file in `~/Desktop/`, and keep up when files
  appear, disappear or are renamed by something else;
- let me **arrange the icons freely** — drop an icon where I want it and have it
  stay there, also across restarts;
- **open a file in an appropriate app on double-click**;
- let me **drag files into other apps**, and **copy & paste** them into other
  apps;
- on **right-click**, show a context menu for the file with at least
  **"Open With…"**, and probably **"Info"** (type, size, date, …).

## What I don't want

Minimal is the point. In particular **no "Open Terminal Here"**, and no
growing into a general file manager — no sidebar, path bar, tabs, or
browsing other folders. If something ends up useful beyond the list above, it
should earn its place.

## Technology

**GTK4, written in C.** GTK is a C library, so this is its native language:
no binding layer, nothing to compile besides the app itself, and the whole GTK
and GIO API is directly at hand (file monitoring, icons, launching apps,
drag-and-drop and clipboard all have direct support there).

## Environment

- Debian 13 "trixie" (GTK 4.18), Wayland only, niri compositor.
- Dependencies should come from Debian packages only — apt keeps those patched,
  while anything built by hand has to be updated by hand.
- Mixed-DPI setup: a Retina panel at 2× and an external monitor at 1×, so the
  window should look right at either scale.

## Open questions for the design

- Where icon positions are stored.
- What happens with new files that have no position yet, and when the window is
  resized smaller than the arrangement.
- Whether dropping files *onto* the window (to copy/move them into
  `~/Desktop/`), rename, delete-to-trash, or multi-selection are wanted — not
  asked for yet, but they tend to be missed once the app is in use.
- Thumbnails for images vs. plain type icons.
- How the app is built and installed.
