lib_sources = files('position-store.c', 'layout.c', 'settings.c', 'desktop-background.c', 'desktop-item.c', 'desktop-model.c', 'thumbnails.c', 'desktop-icon.c', 'desktop-layout.c', 'desktop-canvas.c', 'actions.c')

executable('lnxdesktop',
  lib_sources + files('main.c') + [lnxdesktop_resources],
  dependencies: lnxdesktop_deps,
  include_directories: lnxdesktop_inc,
  install: true)
