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
