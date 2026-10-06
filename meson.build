project('lnxdesktop', 'c',
  version: '0.1',
  default_options: ['c_std=c11', 'warning_level=2'])

add_project_arguments('-DGDK_DISABLE_DEPRECATION_WARNINGS', language: 'c')

lnxdesktop_deps = [
  dependency('gtk4'),
  dependency('gio-unix-2.0'),
  dependency('gdk-pixbuf-2.0'),
]
lnxdesktop_inc = include_directories('src')

subdir('data')
subdir('src')
subdir('tests')
