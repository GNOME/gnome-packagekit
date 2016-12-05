# Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
#
# Licensed under the GNU General Public License Version 2 or later

subdir('appdata')
subdir('icons')

compiled = gnome.compile_schemas()
install_data('org.gnome.packagekit.gschema.xml',
  install_dir : 'share/glib-2.0/schemas'
)

install_data('org.gnome.packagekit.gschema.migrate',
  install_dir : 'share/GConf/gsettings'
)

i18n.merge_file(
  input: 'gpk-install-local-file.desktop.in',
  output: 'gpk-install-local-file.desktop',
  type: 'desktop',
  po_dir: join_paths(meson.source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'applications')
)

i18n.merge_file(
  input: 'gpk-log.desktop.in',
  output: 'gpk-log.desktop',
  type: 'desktop',
  po_dir: join_paths(meson.source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'applications')
)

i18n.merge_file(
  input: 'gpk-prefs.desktop.in',
  output: 'gpk-prefs.desktop',
  type: 'desktop',
  po_dir: join_paths(meson.source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'applications')
)

i18n.merge_file(
  input: 'org.gnome.Packages.desktop.in',
  output: 'org.gnome.Packages.desktop',
  type: 'desktop',
  po_dir: join_paths(meson.source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'applications')
)

i18n.merge_file(
  input: 'org.gnome.PackageUpdater.desktop.in',
  output: 'org.gnome.PackageUpdater.desktop',
  type: 'desktop',
  po_dir: join_paths(meson.source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'applications')
)

