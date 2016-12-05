# Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
#
# Licensed under the GNU General Public License Version 2 or later

i18n.merge_file(
  input: 'org.gnome.Packages.appdata.xml.in',
  output: 'org.gnome.Packages.appdata.xml',
  type: 'xml',
  po_dir: join_paths(meson.source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'appdata')
)

i18n.merge_file(
  input: 'org.gnome.PackageUpdater.appdata.xml.in',
  output: 'org.gnome.PackageUpdater.appdata.xml',
  type: 'xml',
  po_dir: join_paths(meson.source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'appdata')
)
