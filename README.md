# lnxDesktop

A GTK4 application that shows the contents of `~/Desktop` as icons.

## Build and install

    meson setup build
    meson compile -C build
    meson test -C build
    meson install -C build

To install into your home directory, configure with
`meson setup build -Dprefix=$HOME/.local`.

## niri

Example `~/.config/niri/config.kdl` entries:

    window-rule {
        match app-id="org.seehuhn.lnxdesktop"
        open-maximized true
    }

    spawn-at-startup "lnxdesktop"

## Configuration

Optional settings live in `~/.config/lnxdesktop/config.ini`:

    [desktop]
    icon-size=64

`icon-size` is the picture size in logical pixels. The default is 64 and
values are clamped to the range 32 to 128. The grid pitch follows from it
(width + 16, height + 48). The file is read once at startup.

For development, the environment variable `LNXDESKTOP_DIR` replaces the Desktop directory.

## Status

The features in the spec are implemented and the unit and model tests
pass. Interactive behaviour (drag and drop, keyboard navigation, the
context menu and file operations) has not yet been tried by hand: the
manual release checklist below needs a human pass (it requires real input
devices and a second screen):

- [ ] Drag an icon to a text editor in another column; the path arrives. (needs a human pass)
- [ ] Drop a file from a file manager onto the window; it appears at the drop point. (needs a human pass)
- [ ] Toggle the column between full and half width; icons park and return. (needs a human pass)
- [ ] Move the window between the 2x and 1x screens; icons stay crisp and the arrangement is unchanged when moved back. (needs a human pass)
- [ ] Rename, trash, copy and paste, Open With..., Info. (needs a human pass)
