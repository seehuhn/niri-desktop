# SPDX-FileCopyrightText: 2026 Jochen Voss <voss@seehuhn.de>
# SPDX-License-Identifier: GPL-3.0-or-later

test('smoke', executable('test-smoke', 'test-smoke.c',
  dependencies: niri_desktop_deps))
test('position-store', executable('test-position-store', 'test-position-store.c',
  lib_sources, dependencies: niri_desktop_deps, include_directories: niri_desktop_inc))
test('layout', executable('test-layout', 'test-layout.c',
  lib_sources, dependencies: niri_desktop_deps, include_directories: niri_desktop_inc))
test('desktop-item', executable('test-desktop-item', 'test-desktop-item.c',
  lib_sources, dependencies: niri_desktop_deps, include_directories: niri_desktop_inc))
test('desktop-model', executable('test-desktop-model', 'test-desktop-model.c',
  lib_sources, dependencies: niri_desktop_deps, include_directories: niri_desktop_inc))
test('thumbnails', executable('test-thumbnails', 'test-thumbnails.c',
  lib_sources, dependencies: niri_desktop_deps, include_directories: niri_desktop_inc))
test('settings', executable('test-settings', 'test-settings.c',
  lib_sources, dependencies: niri_desktop_deps, include_directories: niri_desktop_inc))
test('actions', executable('test-actions', 'test-actions.c',
  lib_sources, dependencies: niri_desktop_deps, include_directories: niri_desktop_inc))
