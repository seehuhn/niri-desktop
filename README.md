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
