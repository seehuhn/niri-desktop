gnome = import('gnome')

niri_desktop_resources = gnome.compile_resources('niri-desktop-resources',
  'niri-desktop.gresource.xml',
  source_dir: meson.current_source_dir())

install_data('de.seehuhn.NiriDesktop.desktop',
  install_dir: get_option('datadir') / 'applications')
install_data('de.seehuhn.NiriDesktop.svg',
  install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps')
