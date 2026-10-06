lib_sources = files('position-store.c')

executable('lnxdesktop',
  lib_sources + files('main.c') + [lnxdesktop_resources],
  dependencies: lnxdesktop_deps,
  include_directories: lnxdesktop_inc,
  install: true)
