gnome = import('gnome')

lnxdesktop_resources = gnome.compile_resources('lnxdesktop-resources',
  'lnxdesktop.gresource.xml',
  source_dir: meson.current_source_dir())

install_data('org.seehuhn.lnxdesktop.desktop',
  install_dir: get_option('datadir') / 'applications')
install_data('org.seehuhn.lnxdesktop.svg',
  install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps')
