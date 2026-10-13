# niri-desktop

A GTK4 application that shows the contents of `~/Desktop` as icons.

I love niri, but I was missing a desktop: a place where I can keep a
working set of files in a form that is visually easy to take in, and where
my browser can save its downloads.  niri-desktop is my solution to this
problem.  It shows a piece of desktop where file icons can live, I can open
the files by clicking on them, and since it is an ordinary window in a niri
column, it is never hidden behind other windows.

## Features

niri-desktop keeps the feature set small and leaves everything else to a
proper file manager.  It provides the following:

- Icons for all files in `~/Desktop`, updated automatically when files are
  added, removed or renamed by other programs.
- Free placement of icons.  Each icon stays where it was dropped, also
  across restarts.
- Thumbnails for images and, where a thumbnailer is installed, for other
  file types.
- Double-click to open a file, and a context menu with Open With…, Info,
  Rename and Move to Trash.
- Drag and drop and copy and paste, in both directions between
  niri-desktop and other applications.
- Selection of several files at once, using a rubber band or Ctrl+A.
- An optional background image and a configurable icon size.

## Build and Install

niri-desktop is built with Meson.  To build, test and install it, run:

    meson setup build
    meson compile -C build
    meson test -C build
    meson install -C build

To install into your home directory, configure with
`meson setup build -Dprefix=$HOME/.local`.

## Using niri-desktop with niri

Example `~/.config/niri/config.kdl` entries:

    window-rule {
        match app-id="de.seehuhn.NiriDesktop"
        open-maximized true
    }

    spawn-at-startup "niri-desktop"

## Configuration

Optional settings live in `~/.config/niri-desktop/config.ini`:

    [desktop]
    icon-size=64
    background=/home/you/Pictures/photo.jpg

`icon-size` is the picture size in logical pixels.  The default is 64 and
values are clamped to the range 32 to 128.  The grid pitch follows from it
(width + 16, height + 48).  The file is read once at startup.

`background` is the path of an image shown behind the icons.  It is scaled
proportionally until it covers the whole window and centred, so the edges
are cropped.  Normally you set it from the program: right-click empty space
and choose "Change Background…"; "Remove Background" (shown while an image is
set) deletes the key.  The choice is saved in `config.ini`, other keys and
comments are kept.  Large images are downscaled to at most 3840 pixels on
load; smaller ones are used at their native size.  If the file cannot be loaded at startup a warning is logged and the
plain background is used.

For development, the environment variable `NIRI_DESKTOP_DIR` replaces the Desktop directory.

## Licence

Copyright 2026 Jochen Voss <voss@seehuhn.de>

niri-desktop is free software: you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option)
any later version.  See [LICENSE](LICENSE) for the full text.
