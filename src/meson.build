lib_sources = files('position-store.c', 'layout.c', 'settings.c', 'desktop-background.c', 'desktop-item.c', 'desktop-model.c', 'thumbnails.c', 'desktop-icon.c', 'desktop-layout.c', 'desktop-canvas.c', 'actions.c')

executable('niri-desktop',
  lib_sources + files('main.c') + [niri_desktop_resources],
  dependencies: niri_desktop_deps,
  include_directories: niri_desktop_inc,
  install: true)
